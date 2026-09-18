#include "runner/process.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <libproc.h>
#include <mach/mach.h>

#include <mutex>
#else
#include <sys/prctl.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "posix/descriptors.hpp"
#include "profile.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// This process's current resident set in kilobytes. Zero if it cannot be
// read, which makes every subsequent peak measurable rather than none -- the
// failure mode that reports too much rather than silently discarding every
// figure.
//
// Read rather than parsed with iostreams because this sits in front of every
// fork: one open/read/close is a rounding error beside the fork itself, a
// stream is not.
#ifdef __APPLE__
// macOS has no procfs, and a zero floor here is not a missing statistic but a
// wrong one: peak_rss_above_floor would go back to passing ru_maxrss through
// unqualified, promoting this process's own footprint into whichever tool
// happened to be spawned. mach_task_basic_info reports the same resident set
// directly, in bytes.
std::uint64_t self_resident_kb() {
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t info_count = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t status =
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &info_count);
    if (status != KERN_SUCCESS) {
        return 0;
    }
    return static_cast<std::uint64_t>(info.resident_size) / 1024;
}
#else
// The second field of /proc/self/statm, which is resident pages.
std::uint64_t self_resident_kb() {
    const int statm_fd = open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    if (statm_fd < 0) {
        return 0;
    }
    std::array<char, 128> buffer{};
    const ssize_t read_bytes = read(statm_fd, buffer.data(), buffer.size() - 1);
    close(statm_fd);
    if (read_bytes <= 0) {
        return 0;
    }
    buffer[static_cast<std::size_t>(read_bytes)] = '\0';
    char* cursor = nullptr;
    // Field one is the total program size, which is address space rather than
    // memory and enormous under ASAN; the resident count is the one after it.
    std::strtoull(buffer.data(), &cursor, 10);
    const auto resident_pages =
        static_cast<std::uint64_t>(std::strtoull(cursor, nullptr, 10));
    const auto page_kb =
        static_cast<std::int64_t>(sysconf(_SC_PAGESIZE) / 1024);
    if (page_kb <= 0) {
        return 0;
    }
    return resident_pages * static_cast<std::uint64_t>(page_kb);
}
#endif

// The floor has to bound this process's resident set at the instant of the
// fork, which no single read can hit. A sample taken before it is stale by
// whatever a sibling scoring thread allocated in the window, and understating
// the floor by even one page is not a small error: peak_rss_above_floor then
// passes the raw ru_maxrss through, promoting this process's entire footprint
// into some tool's peak. The larger of a read on each side errs the other way,
// discarding a tool whose peak happens to fall inside that window rather than
// mis-attributing one.
std::uint64_t widen_rss_floor(std::uint64_t floor_before_fork_kb) {
    return std::max(floor_before_fork_kb, self_resident_kb());
}

// The child's own peak, or zero where the fork floor swallowed it. See
// ProcessResult::m_peak_rss_floor_kb: ru_maxrss is
// max(parent RSS at fork, the tool's true peak), so it is the tool's figure
// exactly when it clears the floor and says nothing about the tool otherwise.
// Reporting it regardless would attribute this process's own footprint to
// whichever tool happened to be spawned.
std::uint64_t peak_rss_above_floor(std::uint64_t raw_maxrss_kb,
                                   std::uint64_t floor_kb) {
    return raw_maxrss_kb > floor_kb ? raw_maxrss_kb : 0;
}

// ru_maxrss is kilobytes on Linux and *bytes* on macOS, and non-negative for
// a reaped child. The difference is silent -- nothing fails, every
// tool/<name>/rss_* counter simply reads 1024x high -- so the unit lives in
// one place rather than at the call site.
std::uint64_t maxrss_kb(const struct rusage& usage) {
    const auto raw = static_cast<std::uint64_t>(usage.ru_maxrss);
#ifdef __APPLE__
    return raw / 1024;
#else
    return raw;
#endif
}

double rusage_cpu_seconds(const struct rusage& usage) {
    const double user_s = static_cast<double>(usage.ru_utime.tv_sec) +
                          (static_cast<double>(usage.ru_utime.tv_usec) / 1e6);
    const double sys_s = static_cast<double>(usage.ru_stime.tv_sec) +
                         (static_cast<double>(usage.ru_stime.tv_usec) / 1e6);
    return user_s + sys_s;
}

// Milliseconds left until `deadline`, clamped to poll's int range. -1 (poll
// blocks forever) when there is no deadline, nullopt once it has passed.
std::optional<int> poll_timeout_ms(
    const std::optional<Clock::time_point>& deadline) {
    if (!deadline.has_value()) {
        return -1;
    }
    const auto now = Clock::now();
    if (now >= *deadline) {
        return std::nullopt;
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now)
            .count();
    return remaining > std::numeric_limits<int>::max()
               ? std::numeric_limits<int>::max()
               : static_cast<int>(remaining);
}

// Reads read_fd until the child closes it (EOF) or `deadline` passes, if there
// is one. Returns the bytes read so far and whether the deadline was hit; the
// caller kills and reaps. With no deadline, poll blocks indefinitely and this
// only returns on EOF.
std::pair<std::string, bool> read_until(
    int read_fd, const std::optional<Clock::time_point>& deadline) {
    // The child's own runtime: the parent sits in poll()/read() until the tool
    // closes its stdout. Wall greatly exceeding CPU here is expected and
    // healthy -- it means the parent is blocked, not spinning.
    PEREDUR_PROFILE_SCOPE("proc/read");
    std::string output;
    std::array<char, 4096> read_buf{};
    while (true) {
        const std::optional<int> poll_ms = poll_timeout_ms(deadline);
        if (!poll_ms.has_value()) {
            return {output, true};
        }
        struct pollfd pfd{};
        pfd.fd = read_fd;
        pfd.events = POLLIN;
        const int poll_result = poll(&pfd, 1, *poll_ms);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            assert(false);
            __builtin_unreachable();
        }
        if (poll_result == 0) {
            return {output, true};
        }
        const ssize_t bytes_read =
            read(read_fd, read_buf.data(), read_buf.size());
        if (bytes_read > 0) {
            output.append(read_buf.data(),
                          static_cast<std::size_t>(bytes_read));
            continue;
        }
        if (bytes_read == 0) {
            return {output, false};
        }
        if (errno == EINTR) {
            continue;
        }
        assert(false);
        __builtin_unreachable();
    }
}

// Records one tool invocation's peak resident set under the tool's own name,
// so the distribution can be read per tool rather than per run.
//
// Both a max and a total, because neither answers the question alone: these
// peaks are heavily tailed, so a mean over calls describes the common case and
// says nothing about the worst, while the max says nothing about how often.
// The threshold counters between them are a coarse histogram of the tail --
// enough to size a memory limit against, which is the whole purpose. A finer
// histogram would need a bucket array per tool and this needs no allocation
// at all.
//
// `peak_rss_kb` is zero for a call whose peak stayed under the fork floor, and
// such a call contributes to `calls` alone: rolling it in as a zero would drag
// every mean down, and rolling in the floor instead would report this process's
// own memory as the tool's. `rss_measured` is the denominator the max, total
// and threshold counters share, so a mean is rss_kb_total / rss_measured and
// calls - rss_measured is how much of the distribution went unseen.
void record_tool_peak_rss(const std::string& executable,
                          std::uint64_t peak_rss_kb) {
    if (!profile::enabled()) {
        return;
    }
    const std::size_t slash = executable.find_last_of('/');
    const std::string name =
        slash == std::string::npos ? executable : executable.substr(slash + 1);
    const std::string prefix = "tool/" + name + "/";
    profile::add_count((prefix + "calls").c_str());
    if (peak_rss_kb == 0) {
        return;
    }
    profile::add_count((prefix + "rss_measured").c_str());
    profile::record_max(prefix + "rss_kb_max", peak_rss_kb);
    profile::add_count((prefix + "rss_kb_total").c_str(), peak_rss_kb);
    // Kilobytes, so 256 MiB upwards. Chosen to straddle the range a limit
    // would plausibly be set in rather than to be evenly spaced.
    static constexpr std::array<std::pair<std::uint64_t, const char*>, 4>
        k_thresholds{{{262'144ULL, "rss_ge_256m"},
                      {1'048'576ULL, "rss_ge_1g"},
                      {4'194'304ULL, "rss_ge_4g"},
                      {16'777'216ULL, "rss_ge_16g"}}};
    for (const auto& [threshold_kb, counter_name] : k_thresholds) {
        if (peak_rss_kb >= threshold_kb) {
            profile::add_count((prefix + counter_name).c_str());
        }
    }
}

// fork and the creation of a close-on-exec pipe must not interleave. A fork on
// another scoring-pool thread inherits a pipe whose descriptors do not carry
// the flag yet and holds them past its own exec, so the reader waiting on that
// pipe never sees end of file (PR #53). Linux closes that window inside the
// kernel, with pipe2. macOS has no pipe2, and pipe followed by fcntl reopens
// exactly the race the comments at both fork sites say pipe2 is here to
// prevent -- so on macOS the window is closed out here instead, by making the
// two operations mutually exclusive.
//
// The critical section is the pipe and the fork, not the exec: the child is a
// separate process by the time it runs. Holding a mutex across fork is safe
// because the child touches nothing that could want it before exec.
//
// Defined only where it is taken. Guarding the *uses* alone and leaving the
// definition unconditional costs Linux -Wunused-function under -Werror, since
// nothing there calls it.
#ifdef __APPLE__
std::mutex& spawn_mutex() {
    static std::mutex mutex;
    return mutex;
}
#endif

// A hold on spawn_mutex for the platform that needs one, and nothing at all
// for the platform that does not: serialising forks on Linux would cost
// concurrency to close a window pipe2 has already closed.
class SpawnGuard {
   public:
    SpawnGuard() {
#ifdef __APPLE__
        spawn_mutex().lock();
#endif
    }
    ~SpawnGuard() {
#ifdef __APPLE__
        spawn_mutex().unlock();
#endif
    }
    SpawnGuard(const SpawnGuard&) = delete;
    SpawnGuard& operator=(const SpawnGuard&) = delete;
    SpawnGuard(SpawnGuard&&) = delete;
    SpawnGuard& operator=(SpawnGuard&&) = delete;
};

#ifdef __APPLE__
// Parent death, macOS side.
//
// A tool child must not outlive the PEREDUR process: a campaign harness
// enforcing a wall or RAM budget, the OOM killer and Ctrl-C all kill PEREDUR
// outright, and a stranded ltl2tgba then holds multiple gigabytes past the run
// that started it (PR #47). Linux hands that to the kernel -- PR_SET_PDEATHSIG
// survives the exec and needs nothing of ours running in the child.
//
// macOS has no mechanism that survives exec, so the enforcement moves into a
// process of our own. That is a **weaker guarantee** and should be read as
// one: it holds only while the reaper is alive and schedulable, where the
// Linux guarantee holds even with every thread of PEREDUR wedged.
//
// The reaper is forked once, on the first spawn that asks for containment, and
// holds the read end of a registration pipe whose write end lives here. Each
// child writes its own process group to that pipe after setpgid and before
// exec -- the same point prctl sits at on Linux, and it closes the same
// window, since a parent that dies before the registration lands has not let
// anything escape yet. When PEREDUR dies, every copy of the write end closes,
// the reaper reads end of file and kills every group still registered.
constexpr std::size_t k_max_tracked_groups = 4096;

// Set once, then only read. -1 means the reaper could not be started, in which
// case children skip registration and macOS containment degrades to none.
std::atomic<int> g_reaper_write_fd{-1};

// Everything below runs in a forked child of a multithreaded process, so it
// may touch nothing that another thread could have held mid-update at the
// fork: no allocation, no locks, no iostreams. A fixed array plus
// read/close/killpg/_exit is the whole vocabulary.
[[noreturn]] void reaper_main(int read_fd) {
    static std::array<pid_t, k_max_tracked_groups> tracked{};
    std::size_t tracked_count = 0;
    while (true) {
        pid_t message = 0;
        // The critical section clang-tidy sees is spawn_mutex, held by the
        // parent across the fork in start_reaper_once. It has no meaning on
        // this side of that fork: the child holds a copy no thread of its own
        // will ever take or release, and blocking here forever is precisely
        // the design -- the read returns when PEREDUR dies and not before.
        // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
        const ssize_t got = read(read_fd, &message, sizeof(message));
        if (got == 0) {
            // Every write end is closed: PEREDUR is gone.
            break;
        }
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (got != static_cast<ssize_t>(sizeof(message))) {
            continue;
        }
        if (message > 0) {
            // Overflowing the table drops the registration rather than a live
            // entry: the cost is one uncontained tool, not a missed kill on
            // 4096 others.
            if (tracked_count < tracked.size()) {
                tracked[tracked_count] = message;
                ++tracked_count;
            }
            continue;
        }
        const pid_t finished = -message;
        for (std::size_t idx = 0; idx < tracked_count; ++idx) {
            if (tracked[idx] == finished) {
                --tracked_count;
                tracked[idx] = tracked[tracked_count];
                break;
            }
        }
    }
    for (std::size_t idx = 0; idx < tracked_count; ++idx) {
        killpg(tracked[idx], SIGKILL);
    }
    _exit(0);
}

// The highest descriptor this process currently has open, or -1 if that
// cannot be determined.
//
// This exists to bound the loop below, and the obvious bound does not work:
// getdtablesize() reports the soft RLIMIT_NOFILE, which is 1048576 in the Nix
// dev shell, and a million close() calls is not a bound but a hang -- it shows
// up as a reaper pinned at 100% CPU while the run waits on it. proc_pidinfo
// reports the descriptors actually open, which is a handful.
//
// Called in the parent, before the fork, because it allocates. A descriptor
// another thread opens in the window between this and the fork is missed, and
// that is deliberate rather than merely tolerated: the caller holds the
// SpawnGuard, so no *pipe* can be created in that window, and a plain file the
// reaper holds open gates nobody's end of file. Pipes are the whole reason the
// loop exists.
int highest_open_descriptor() {
    const int sized = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (sized <= 0) {
        return -1;
    }
    std::vector<proc_fdinfo> entries(
        (static_cast<std::size_t>(sized) / sizeof(proc_fdinfo)) + 64);
    const int filled =
        proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, entries.data(),
                     static_cast<int>(entries.size() * sizeof(proc_fdinfo)));
    if (filled <= 0) {
        return -1;
    }
    const std::size_t count =
        static_cast<std::size_t>(filled) / sizeof(proc_fdinfo);
    int highest = -1;
    for (std::size_t idx = 0; idx < count; ++idx) {
        highest = std::max(highest, entries[idx].proc_fd);
    }
    return highest;
}

// The reaper never execs, so O_CLOEXEC does nothing for it. Without this it
// would hold a copy of every pipe open at the moment it was forked for the
// rest of the run, and the reader on the far side would never see end of file
// -- the exact failure O_CLOEXEC exists to prevent, arriving by the one route
// that flag cannot cover. Its own registration pipe's write end is in that
// set, and keeping that one would stop it ever seeing the end of file it
// waits on.
void close_every_descriptor_except(int keep_fd, int highest_fd) {
    for (int candidate_fd = 0; candidate_fd <= highest_fd; ++candidate_fd) {
        if (candidate_fd != keep_fd) {
            close(candidate_fd);
        }
    }
}

// Called with a SpawnGuard held, so make_cloexec_pipe below is safe and no
// other thread is between its own pipe and its own fork.
void start_reaper_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::array<int, 2> fds = {-1, -1};
        if (make_cloexec_pipe(fds) != 0) {
            return;
        }
        // Sampled before the fork: the child may not allocate.
        const int highest_fd = highest_open_descriptor();
        const pid_t reaper_pid = fork();
        if (reaper_pid < 0) {
            close(fds[0]);
            close(fds[1]);
            return;
        }
        if (reaper_pid == 0) {
            close_every_descriptor_except(fds[0], highest_fd);
            reaper_main(fds[0]);
        }
        close(fds[0]);
        g_reaper_write_fd.store(fds[1], std::memory_order_release);
    });
}

// Drops a reaped group from the table. A registration left behind would be
// killpg'd on PEREDUR's death, which is harmless against an exited group
// (ESRCH) right up until the kernel has recycled that pid, so this is not
// merely tidiness.
void unregister_from_reaper(pid_t group) {
    const int reaper_fd = g_reaper_write_fd.load(std::memory_order_acquire);
    if (reaper_fd < 0) {
        return;
    }
    const pid_t message = -group;
    [[maybe_unused]] const ssize_t written =
        write(reaper_fd, &message, sizeof(message));
}
#else
void start_reaper_once() {}
void unregister_from_reaper(pid_t /*group*/) {}
#endif

// The parent half of the containment policy, called immediately after fork():
// repeats the child's setpgid so the group exists whichever side the scheduler
// runs first -- otherwise a timeout firing in that window would killpg a group
// that does not exist yet. Whichever call loses fails harmlessly (EACCES once
// the child has exec'd, ESRCH once it has exited), so the result is
// deliberately ignored.
void adopt_child_process_group(pid_t child_pid) {
    [[maybe_unused]] const int setpgid_result = setpgid(child_pid, child_pid);
}

// SIGKILLs the process group led by `pid`, to catch grandchildren, then `pid`
// itself, in case setpgid never took effect and there is no such group. Call
// before the child is reaped, while the pid cannot have been reused.
void kill_process_tree(pid_t pid) {
    killpg(pid, SIGKILL);
    kill(pid, SIGKILL);
}

// Built before forking: heap allocation inside the child between fork() and
// exec() can deadlock if another thread held the allocator lock at the moment
// of the fork (e.g. under ASAN's allocator).
std::vector<char*> exec_argv(const std::vector<std::string>& arguments) {
    std::vector<char*> argv(arguments.size() + 1);
    for (std::size_t arg_idx = 0; arg_idx < arguments.size(); ++arg_idx) {
        argv[arg_idx] = const_cast<char*>(arguments[arg_idx].c_str());
    }
    argv[arguments.size()] = nullptr;
    return argv;
}

// The child's side of both forks once its descriptors are in place: the
// containment policy, then the exec, with 127 as the exit status of a failed
// one.
[[noreturn]] void exec_child(const std::vector<char*>& argv,
                             ExecutableLookup lookup, ParentDeathPolicy policy,
                             pid_t parent_pid) {
    harden_child_after_fork(policy, parent_pid);
    if (lookup == ExecutableLookup::SearchPath) {
        execvp(argv[0], argv.data());
    } else {
        execv(argv[0], argv.data());
    }
    _exit(127);
}

std::optional<Clock::time_point> deadline_after(
    std::chrono::milliseconds timeout) {
    if (timeout > std::chrono::milliseconds::zero()) {
        return Clock::now() + timeout;
    }
    return std::nullopt;
}

// Reaps `pid`, killing its process group and continuing to wait if `deadline`
// passes first. Sets `timed_out` if that kill was needed. Returns the exit
// status in ProcessResult's encoding.
int reap(pid_t pid, const std::optional<Clock::time_point>& deadline,
         std::uint64_t rss_floor_kb, double& cpu_s_out,
         std::uint64_t& peak_rss_kb_out, bool& timed_out) {
    // Only the reap, not the kill that may precede it: this is the wait for a
    // child that has already closed its stdout. On the common path the first
    // WNOHANG collects it and the wall time is near zero, so a large total
    // here means tools are lingering after their last write rather than
    // exiting -- which is the failure the deadline on this loop exists to
    // bound.
    PEREDUR_PROFILE_SCOPE("proc/wait");
    int wait_status = 0;
    struct rusage child_usage{};
    // Unwrapped once here rather than dereferenced in the loop: the polling
    // branch below is only reachable when there is a deadline, but that is not
    // a fact the optional itself carries.
    const bool bounded = deadline.has_value();
    const Clock::time_point deadline_point =
        deadline.value_or(Clock::time_point::max());
    bool killed = false;
    while (true) {
        // Once the group is killed there is nothing left to wait out, and
        // SIGKILL cannot be caught, so drop the polling and block.
        const int flags = (bounded && !killed) ? WNOHANG : 0;
        const pid_t waited = wait4(pid, &wait_status, flags, &child_usage);
        if (waited == pid) {
            break;
        }
        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }
            assert(false);
            __builtin_unreachable();
        }
        if (Clock::now() >= deadline_point) {
            kill_process_tree(pid);
            killed = true;
            timed_out = true;
            continue;
        }
        // There is no portable wait-with-timeout, so poll. The common path
        // never sleeps at all: the child is normally already dead by the time
        // its pipe reaches EOF, so the first WNOHANG reaps it. 2 ms is
        // negligible beside the solver calls this wraps.
        const struct timespec nap{0, 2'000'000};
        nanosleep(&nap, nullptr);
    }
    // Reaped, so the reaper has nothing left to enforce for it.
    unregister_from_reaper(pid);
    cpu_s_out = rusage_cpu_seconds(child_usage);
    peak_rss_kb_out =
        peak_rss_above_floor(maxrss_kb(child_usage), rss_floor_kb);
    if (WIFEXITED(wait_status)) {
        return WEXITSTATUS(wait_status);
    }
    if (WIFSIGNALED(wait_status)) {
        return 128 + WTERMSIG(wait_status);
    }
    return -1;
}

}  // namespace

PipedChild spawn_piped_child(const std::vector<std::string>& arguments,
                             ParentDeathPolicy policy,
                             ExecutableLookup lookup) {
    assert(!arguments.empty());
    PEREDUR_PROFILE_SCOPE("proc/fork+exec");
    const std::vector<char*> argv = exec_argv(arguments);
    std::array<int, 2> stdin_pipe = {-1, -1};
    std::array<int, 2> stdout_pipe = {-1, -1};
    // Close-on-exec on both, for the reason spelled out at the pipe in
    // execute_and_capture below: a concurrent fork on another scoring-pool
    // thread inherits these ends and holds them past its own exec, and this
    // call's reader then never sees end of file (PR #53). The guard is what
    // makes that true on a platform without pipe2; see SpawnGuard. It is taken
    // before the reaper starts so that fork is covered too, and released only
    // on return, the tail after the fork being a few syscalls.
    const SpawnGuard spawn_guard;
    if (policy == ParentDeathPolicy::KillWithParentThread) {
        start_reaper_once();
    }
    [[maybe_unused]] const int stdin_pipe_result =
        make_cloexec_pipe(stdin_pipe);
    assert(stdin_pipe_result == 0);
    [[maybe_unused]] const int stdout_pipe_result =
        make_cloexec_pipe(stdout_pipe);
    assert(stdout_pipe_result == 0);
    // As in execute_and_capture, and handed back on PipedChild because the reap
    // that needs it happens arbitrarily later, in another thread, by which time
    // this process has grown and can no longer measure its own floor.
    const std::uint64_t rss_floor_kb = self_resident_kb();
    // Read before the fork, since after it the child cannot tell its original
    // parent from any other pid. See harden_child_after_fork.
    const pid_t parent_pid = getpid();
    const pid_t child_pid = fork();
    if (child_pid < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        assert(false);
        __builtin_unreachable();
    }
    if (child_pid == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        // The dup2 copies clear O_CLOEXEC, which is what lets the child keep
        // exactly the two descriptors it needs across the exec.
        if (dup2(stdin_pipe[0], STDIN_FILENO) < 0 ||
            dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        exec_child(argv, lookup, policy, parent_pid);
    }
    adopt_child_process_group(child_pid);
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    return {child_pid, stdin_pipe[1], stdout_pipe[0],
            widen_rss_floor(rss_floor_kb)};
}

std::pair<std::string, bool> read_until_eof(int read_fd,
                                            std::chrono::milliseconds timeout) {
    return read_until(read_fd, deadline_after(timeout));
}

void harden_child_after_fork(ParentDeathPolicy policy, pid_t parent_pid) {
    // Own process group, so one killpg reaches this child *and anything it
    // spawns*. A bare kill(pid) hits only the direct child and strands every
    // grandchild as an orphan reparented to PID 1 — which is how multi-GB
    // ltl2tgba processes have survived past the run that started them.
    setpgid(0, 0);
    if (policy == ParentDeathPolicy::SurviveParentThread) {
        return;
    }
    // Never outlive the parent: if the PEREDUR process is killed (a campaign
    // harness enforcing a wall/RAM budget, the OOM killer, Ctrl-C) while this
    // subprocess is mid-run, deliver SIGKILL rather than leave the tool
    // running unattended.
    //
    // The kernel ties this to the forking *thread*, not the process, so it is
    // only correct where that thread waits for the child — every exec behind
    // execute_and_capture does. A child that outlives its spawning thread must
    // opt out (see ParentDeathPolicy).
#ifdef __APPLE__
    // The same request, made of our own reaper rather than the kernel, and
    // made from the same place for the same reason. One pid_t is far below
    // PIPE_BUF, so this write cannot interleave with a concurrent child's.
    // Registering the process group rather than this pid is what carries the
    // guarantee to grandchildren, as killpg does at every other call site.
    const int reaper_fd = g_reaper_write_fd.load(std::memory_order_acquire);
    if (reaper_fd >= 0) {
        const pid_t group = getpid();
        [[maybe_unused]] const ssize_t written =
            write(reaper_fd, &group, sizeof(group));
    }
#else
    prctl(PR_SET_PDEATHSIG, SIGKILL);
#endif
    // Closes the fork/registration race: if the parent died in the window
    // before the request was registered, nothing is coming to enforce it —
    // and nothing has escaped yet either, since the exec is still below.
    //
    // Compared against the pid the parent read before forking, never against
    // 1. Reparenting to init is the *symptom* of a dead parent rather than the
    // condition, and the two part company exactly when PEREDUR is itself PID 1
    // — which a container makes routine, ENTRYPOINT being exec'd as pid 1. Read
    // as "getppid() == 1", every tool subprocess in the image exited 127 before
    // its exec, and every query came back with empty output and no timeout.
    if (getppid() != parent_pid) {
        _exit(127);
    }
}

double reap_with_grace(pid_t pid, std::chrono::milliseconds grace,
                       const std::string& executable,
                       std::uint64_t rss_floor_kb) {
    double cpu_s = 0.0;
    std::uint64_t peak_rss_kb = 0;
    bool timed_out = false;
    reap(pid, Clock::now() + grace, rss_floor_kb, cpu_s, peak_rss_kb,
         timed_out);
    // A long-lived child, so this is its whole-run peak rather than one
    // request's — the same whole-run reading its CPU total already is.
    record_tool_peak_rss(executable, peak_rss_kb);
    return cpu_s;
}

ProcessResult execute_and_capture(const std::vector<std::string>& arguments,
                                  std::chrono::milliseconds timeout,
                                  ExecutableLookup lookup) {
    assert(!arguments.empty());
    const std::vector<char*> argv = exec_argv(arguments);
    std::array<int, 2> pipe_fds = {-1, -1};
    pid_t child_pid = -1;
    std::uint64_t rss_floor_kb = 0;
    {
        // Timed separately from the read and wait below: this scope is the
        // parent's share of process creation -- the pipe, the fork's page
        // table copy, and the group setup -- which is the part a cheaper spawn
        // primitive removes. It is work the parent does itself, so unlike
        // proc/read it should show CPU close to its wall time. The child's own
        // runtime is not in here: the parent leaves this scope as soon as fork
        // returns.
        PEREDUR_PROFILE_SCOPE("proc/fork+exec");
        // Close-on-exec, because these runners are called from many
        // scoring-pool threads at once. Without it a fork here inherits every
        // pipe another call has open, and holds the write end past its own
        // exec, so that call's reader never sees end of file. pipe2 sets the
        // flag atomically; where there is no pipe2, SpawnGuard is what stops
        // pipe followed by fcntl racing a concurrent fork. The dup2 onto
        // stdout and stderr below clears the flag on the copies, which is what
        // lets the child keep those two.
        const SpawnGuard spawn_guard;
        start_reaper_once();
        [[maybe_unused]] const int pipe_result = make_cloexec_pipe(pipe_fds);
        assert(pipe_result == 0);
        // The near side of the floor; widen_rss_floor takes the far side once
        // the fork has returned.
        rss_floor_kb = self_resident_kb();
        // See the same capture in spawn_piped_child.
        const pid_t parent_pid = getpid();
        child_pid = fork();
        if (child_pid < 0) {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            assert(false);
            __builtin_unreachable();
        }
        if (child_pid == 0) {
            close(pipe_fds[0]);
            if (dup2(pipe_fds[1], STDOUT_FILENO) < 0 ||
                dup2(pipe_fds[1], STDERR_FILENO) < 0) {
                _exit(127);
            }
            close(pipe_fds[1]);
            exec_child(argv, lookup, ParentDeathPolicy::KillWithParentThread,
                       parent_pid);
        }
        adopt_child_process_group(child_pid);
        close(pipe_fds[1]);
        rss_floor_kb = widen_rss_floor(rss_floor_kb);
    }
    const std::optional<Clock::time_point> deadline = deadline_after(timeout);
    auto [output, timed_out] = read_until(pipe_fds[0], deadline);
    close(pipe_fds[0]);
    if (timed_out) {
        kill_process_tree(child_pid);
    }
    double cpu_s = 0.0;
    std::uint64_t peak_rss_kb = 0;
    // Reap against the same deadline rather than blocking. EOF on the pipe
    // means the child closed stdout, not that it exited, so a tool that wedges
    // after writing its answer would otherwise outlive its timeout inside
    // wait4 — the one place the old per-runner wrappers could still hang on a
    // call that nominally had a budget.
    const int exit_code = reap(child_pid, timed_out ? std::nullopt : deadline,
                               rss_floor_kb, cpu_s, peak_rss_kb, timed_out);
    record_tool_peak_rss(arguments[0], peak_rss_kb);
    return {exit_code,   std::move(output), cpu_s,
            peak_rss_kb, rss_floor_kb,      timed_out};
}
