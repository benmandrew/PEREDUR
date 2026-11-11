#include "runner/process.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <crt_externs.h>
#include <libproc.h>
#include <spawn.h>
#else
#include <pthread.h>
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
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "posix/descriptors.hpp"
#include "profile.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// The calling thread's ExecCancelScope signals, both null with none
// installed.
thread_local ExecCancelScope::Signal t_exec_cancel;

// How often a read under a cancel flag wakes to look at it. Only a cancelled
// speculative query waits this long past its cancellation, against the
// seconds it would otherwise run on.
constexpr int k_cancel_poll_ms = 20;

bool watched(const ExecCancelScope::Signal& signal) {
    return signal.m_now != nullptr || signal.m_after_grace != nullptr;
}

// Whether an exec started at `start` is to be killed now.
bool exec_cancelled(const ExecCancelScope::Signal& signal,
                    Clock::time_point start) {
    if (signal.m_now != nullptr &&
        signal.m_now->load(std::memory_order_acquire)) {
        return true;
    }
    return signal.m_after_grace != nullptr &&
           signal.m_after_grace->load(std::memory_order_acquire) &&
           Clock::now() - start >= std::chrono::milliseconds{signal.m_grace_ms};
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

// Waits until read_fd is readable, returning true, or until `deadline` passes
// or `signal` cancels the exec started at `start`, returning false. With
// neither, poll blocks indefinitely.
bool wait_readable(int read_fd,
                   const std::optional<Clock::time_point>& deadline,
                   const ExecCancelScope::Signal& signal,
                   Clock::time_point start) {
    const bool watching = watched(signal);
    while (true) {
        if (watching && exec_cancelled(signal, start)) {
            return false;
        }
        const std::optional<int> poll_ms = poll_timeout_ms(deadline);
        if (!poll_ms.has_value()) {
            return false;
        }
        // Under a cancel flag the wait is cut into slices, since nothing
        // wakes poll when the flag is set.
        const bool sliced =
            watching && (*poll_ms < 0 || *poll_ms > k_cancel_poll_ms);
        struct pollfd pfd{};
        pfd.fd = read_fd;
        pfd.events = POLLIN;
        const int poll_result =
            poll(&pfd, 1, sliced ? k_cancel_poll_ms : *poll_ms);
        if (poll_result > 0) {
            return true;
        }
        if (poll_result == 0) {
            if (sliced) {
                continue;
            }
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        assert(false);
        __builtin_unreachable();
    }
}

// Reads read_fd until the child closes it (EOF) or `deadline` passes, if there
// is one, or `signal` cancels the exec started at `start`. Returns the bytes
// read so far and whether it stopped short of EOF; the caller kills and reaps.
// With no deadline and no cancel signal, poll blocks indefinitely and this
// only returns on EOF.
std::pair<std::string, bool> read_until(
    int read_fd, const std::optional<Clock::time_point>& deadline,
    const ExecCancelScope::Signal& signal = {},
    Clock::time_point start = Clock::time_point{}) {
    // The child's own runtime: the parent sits in poll()/read() until the tool
    // closes its stdout. Wall greatly exceeding CPU here is expected and
    // healthy -- it means the parent is blocked, not spinning.
    PEREDUR_PROFILE_SCOPE("proc/read");
    std::string output;
    std::array<char, 4096> read_buf{};
    while (true) {
        if (!wait_readable(read_fd, deadline, signal, start)) {
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
//
// The reaper leaves PEREDUR's process group as soon as it is forked. A signal
// aimed at that group would otherwise kill it alongside PEREDUR, while every
// tool, being in a group of its own, never sees the signal and is left
// running. That is what GNU timeout does on expiry (it setpgid's itself and
// signals the group), what Ctrl-C does to the foreground group, and what a
// harness that kills by group does; a run under `timeout 2400` stranded two
// ltlsynt at 87% CPU for three hours that way. Leaving the group is the only
// defence against SIGKILL, which timeout -s KILL sends. The reaper also
// ignores the signals that ask a process to exit, since `pkill peredur` names
// it as readily as PEREDUR. Ignoring SIGHUP matters for a second reason: once
// PEREDUR is gone the reaper's group is orphaned, and if the reaper had been
// stopped the kernel sends it SIGHUP and then SIGCONT, which must resume it
// rather than kill it. It never needs to be told to exit, as end of file is
// its exit.
constexpr std::size_t k_max_tracked_groups = 4096;

// Set once, then only read. -1 means the reaper could not be started, in which
// case children skip registration and macOS containment degrades to none.
std::atomic<int> g_reaper_write_fd{-1};

// Everything below runs in a forked child of a multithreaded process, so it
// may touch nothing that another thread could have held mid-update at the
// fork: no allocation, no locks, no iostreams. A fixed array plus
// setpgid/sigaction/read/close/killpg/_exit is the whole vocabulary.
[[noreturn]] void reaper_main(int read_fd) {
    // The parent repeats this after the fork; see start_reaper_once.
    setpgid(0, 0);
    struct sigaction ignore{};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    for (const int signal_number : {SIGHUP, SIGINT, SIGQUIT, SIGTERM}) {
        sigaction(signal_number, &ignore, nullptr);
    }
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
        // As adopt_child_process_group does for a tool: whichever side runs
        // first, the reaper is out of PEREDUR's group before this returns and
        // before any tool can be registered with it.
        [[maybe_unused]] const int setpgid_result =
            setpgid(reaper_pid, reaper_pid);
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

// Writes "peredur: cannot exec <path>: errno <n>" to stderr. A failed exec
// otherwise surfaces only as exit status 127 and empty output, which every
// runner reads as unrecognised tool output rather than as a tool that never
// ran. Runs between fork and exec, so it formats the number by hand: write is
// async-signal-safe and snprintf is not.
void report_exec_failure(const char* path, int error_number) {
    constexpr std::string_view k_prefix = "peredur: cannot exec ";
    constexpr std::string_view k_errno = ": errno ";
    // E2BIG is named because it is the one a caller can cause: one argument
    // over MAX_ARG_STRLEN (128 KiB on Linux), which is why formulas travel on
    // stdin rather than in argv.
    constexpr std::string_view k_e2big = " (E2BIG, argument list too long)";
    std::array<char, 16> digits{};
    std::size_t start = digits.size();
    int remaining = error_number < 0 ? 0 : error_number;
    do {
        --start;
        digits[start] = static_cast<char>('0' + (remaining % 10));
        remaining /= 10;
    } while (remaining > 0 && start > 0);
    std::size_t path_length = 0;
    while (path[path_length] != '\0') {
        ++path_length;
    }
    write_all(STDERR_FILENO, k_prefix.data(), k_prefix.size());
    write_all(STDERR_FILENO, path, path_length);
    write_all(STDERR_FILENO, k_errno.data(), k_errno.size());
    write_all(STDERR_FILENO, &digits[start], digits.size() - start);
    if (error_number == E2BIG) {
        write_all(STDERR_FILENO, k_e2big.data(), k_e2big.size());
    }
    write_all(STDERR_FILENO, "\n", 1);
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
    report_exec_failure(argv[0], errno);
    _exit(127);
}

// One of the child's standard descriptors and the parent's descriptor it is
// copied from. A negative source leaves the target as inherited.
struct ChildDescriptor {
    int m_source = -1;
    int m_target = -1;
};

#ifdef __APPLE__
// macOS starts children without forking where it can. A fork from an ASan
// build costs a fixed 74 ms here, whatever the heap holds, against 1.2 ms for
// posix_spawn, and a debug test run is mostly tool calls.
//
// harden_child_after_fork's work moves into spawn attributes and the parent.
// POSIX_SPAWN_SETPGROUP gives the child its own group atomically, so there is
// no window for adopt_child_process_group to close. Under KillWithParentThread
// the child starts suspended, is registered with the reaper, and only then
// resumes, which closes the window the getppid check closes after a fork: if
// PEREDUR dies before the registration lands, the stopped child's group is
// orphaned and the kernel's SIGHUP kills it. SIGHUP is reset to its default
// for that, since a run under nohup would otherwise hand down SIG_IGN.
//
// Returns -1 when posix_spawn fails, and the caller forks instead, so a failed
// exec is still reported by report_exec_failure in the child's own output.
pid_t spawn_without_fork(const std::vector<char*>& argv,
                         ExecutableLookup lookup, ParentDeathPolicy policy,
                         std::initializer_list<ChildDescriptor> descriptors) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    for (const ChildDescriptor& descriptor : descriptors) {
        if (descriptor.m_source >= 0) {
            posix_spawn_file_actions_adddup2(&actions, descriptor.m_source,
                                             descriptor.m_target);
        }
    }
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    const bool contained = policy == ParentDeathPolicy::KillWithParentThread;
    int flags = POSIX_SPAWN_SETPGROUP;
    if (contained) {
        flags |= POSIX_SPAWN_START_SUSPENDED | POSIX_SPAWN_SETSIGDEF;
        sigset_t hangup;
        sigemptyset(&hangup);
        sigaddset(&hangup, SIGHUP);
        posix_spawnattr_setsigdefault(&attributes, &hangup);
    }
    posix_spawnattr_setflags(&attributes, static_cast<std::int16_t>(flags));
    posix_spawnattr_setpgroup(&attributes, 0);
    pid_t child_pid = -1;
    const int error =
        lookup == ExecutableLookup::SearchPath
            ? posix_spawnp(&child_pid, argv[0], &actions, &attributes,
                           argv.data(), *_NSGetEnviron())
            : posix_spawn(&child_pid, argv[0], &actions, &attributes,
                          argv.data(), *_NSGetEnviron());
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        return -1;
    }
    if (contained) {
        const int reaper_fd = g_reaper_write_fd.load(std::memory_order_acquire);
        if (reaper_fd >= 0) {
            [[maybe_unused]] const ssize_t written =
                write(reaper_fd, &child_pid, sizeof(child_pid));
        }
        kill(child_pid, SIGCONT);
    }
    return child_pid;
}
#else
// Linux always forks: PR_SET_PDEATHSIG has to be set in the child, and
// posix_spawn has no attribute for it.
pid_t spawn_without_fork(
    const std::vector<char*>& /*argv*/, ExecutableLookup /*lookup*/,
    ParentDeathPolicy /*policy*/,
    std::initializer_list<ChildDescriptor> /*descriptors*/) {
    return -1;
}
#endif

// A close-on-exec descriptor holding `content`, positioned at its start, for
// the child to take as its stdin. A file rather than a pipe, so the whole
// payload is in place before the fork: nothing is written while the child's
// output is being read, so a large payload can neither deadlock against a
// full stdout pipe nor raise SIGPIPE when a child exits without reading it,
// and the deadline and kill paths are unchanged.
//
// Linux uses memfd_create, which touches no filesystem and needs no cleanup.
// macOS has no memfd, so it takes an unlinked temporary file whose
// FD_CLOEXEC is set after the open, which is why the caller holds the
// SpawnGuard.
int input_file_holding(const std::string& content) {
#ifdef __APPLE__
    std::string name = "/tmp/peredur-input-XXXXXX";
    const int input_fd = mkstemp(name.data());
    if (input_fd >= 0) {
        unlink(name.c_str());
        if (fcntl(input_fd, F_SETFD, FD_CLOEXEC) != 0) {
            close(input_fd);
            throw std::runtime_error(
                "cannot mark the child's input file close-on-exec");
        }
    }
#else
    const int input_fd = memfd_create("peredur-input", MFD_CLOEXEC);
#endif
    if (input_fd < 0) {
        throw std::runtime_error("cannot create the child's input file");
    }
    if (!write_all(input_fd, content.data(), content.size()) ||
        lseek(input_fd, 0, SEEK_SET) != 0) {
        close(input_fd);
        throw std::runtime_error("cannot write the child's input file");
    }
    return input_fd;
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
         double& cpu_s_out, bool& timed_out) {
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
    // Read before the fork, since after it the child cannot tell its original
    // parent from any other pid. See harden_child_after_fork.
    const pid_t parent_pid = getpid();
    pid_t child_pid = spawn_without_fork(
        argv, lookup, policy,
        {{stdin_pipe[0], STDIN_FILENO}, {stdout_pipe[1], STDOUT_FILENO}});
    if (child_pid < 0) {
        child_pid = fork();
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
            // The dup2 copies clear O_CLOEXEC, which is what lets the child
            // keep exactly the two descriptors it needs across the exec.
            if (dup2(stdin_pipe[0], STDIN_FILENO) < 0 ||
                dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
                _exit(127);
            }
            close(stdin_pipe[0]);
            close(stdout_pipe[1]);
            exec_child(argv, lookup, policy, parent_pid);
        }
        adopt_child_process_group(child_pid);
    }
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    return {child_pid, stdin_pipe[1], stdout_pipe[0]};
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

double reap_with_grace(pid_t pid, std::chrono::milliseconds grace) {
    double cpu_s = 0.0;
    bool timed_out = false;
    reap(pid, Clock::now() + grace, cpu_s, timed_out);
    return cpu_s;
}

namespace {

// The tool server (see ToolServer in the header, and spot_forkserver.cpp for
// the protocol).
//
// A server is spawned through spawn_piped_child like any other child, so this
// file still holds every fork peredur makes; the per-query forks happen inside
// the server, a separate single-threaded process. It is spawned
// SurviveParentThread because it outlives the scoring thread that started it.
// Containment on peredur's death is the server's own: its stdin is the pipe
// whose write end lives here, the kernel closes it when this process dies,
// and the server watches for that hang-up while a tool runs.
struct ToolServerConfig {
    std::string m_path;
    std::vector<std::string> m_served;
};

// Swapped whole by set_tool_server. A config is never freed, so a call that
// loaded the previous one finishes against it; all_server_configs keeps every
// one reachable.
std::atomic<const ToolServerConfig*> g_tool_server{nullptr};

std::mutex& server_mutex() {
    static auto& mutex = *new std::mutex();
    return mutex;
}

std::vector<std::unique_ptr<ToolServerConfig>>& all_server_configs() {
    static auto& configs =
        *new std::vector<std::unique_ptr<ToolServerConfig>>();
    return configs;
}

struct ServerHandle {
    PipedChild m_child;
    const ToolServerConfig* m_config = nullptr;
};

std::vector<ServerHandle*>& idle_servers() {
    static auto& pool = *new std::vector<ServerHandle*>();
    return pool;
}

// Transport failures in a row after which the server is turned off, so a
// server binary that cannot start costs three wasted spawns, not one per call.
constexpr std::size_t k_max_consecutive_server_failures = 3;
std::atomic<std::size_t> g_consecutive_server_failures{0};

void discard_server(ServerHandle* server) {
    kill_process_tree(server->m_child.m_pid);
    close(server->m_child.m_write_fd);
    close(server->m_child.m_read_fd);
    int status = 0;
    while (waitpid(server->m_child.m_pid, &status, 0) < 0 && errno == EINTR) {
    }
    delete server;
}

const ToolServerConfig* server_for(const std::vector<std::string>& arguments,
                                   ExecutableLookup lookup) {
    if (lookup != ExecutableLookup::AbsolutePath) {
        return nullptr;
    }
    const ToolServerConfig* config =
        g_tool_server.load(std::memory_order_acquire);
    if (config == nullptr ||
        std::find(config->m_served.begin(), config->m_served.end(),
                  arguments[0]) == config->m_served.end()) {
        return nullptr;
    }
    // The protocol is line-framed.
    for (const std::string& argument : arguments) {
        if (argument.find('\n') != std::string::npos) {
            return nullptr;
        }
    }
    return config;
}

// How long a new server may take to build its BuDDy table and fork its zygote
// before it counts as failed. Not charged to the call: see take_server.
constexpr std::chrono::milliseconds k_server_ready_timeout{30'000};

// Owns a server for one call. One still held when the lease ends, by a
// return or an exception, is killed with its tool and reaped.
class ServerLease {
   public:
    ServerLease() = default;
    explicit ServerLease(ServerHandle* server) : m_server(server) {}
    ~ServerLease() { discard(); }
    ServerLease(const ServerLease&) = delete;
    ServerLease& operator=(const ServerLease&) = delete;
    ServerLease(ServerLease&& other) noexcept
        : m_server(std::exchange(other.m_server, nullptr)) {}
    ServerLease& operator=(ServerLease&& other) noexcept {
        if (this != &other) {
            discard();
            m_server = std::exchange(other.m_server, nullptr);
        }
        return *this;
    }

    [[nodiscard]] ServerHandle* get() const { return m_server; }
    [[nodiscard]] bool held() const { return m_server != nullptr; }

    void discard() {
        if (m_server != nullptr) {
            discard_server(std::exchange(m_server, nullptr));
        }
    }

    // Back to the pool, unless set_tool_server has replaced its setting.
    void give_back() {
        ServerHandle* server = std::exchange(m_server, nullptr);
        if (server == nullptr) {
            return;
        }
        if (g_tool_server.load(std::memory_order_acquire) == server->m_config) {
            const std::scoped_lock lock(server_mutex());
            idle_servers().push_back(server);
            return;
        }
        discard_server(server);
    }

   private:
    ServerHandle* m_server = nullptr;
};

enum class ReadyOutcome : std::uint8_t { Ready, Failed, Cancelled };

// Waits for a new server's "ready" line.
ReadyOutcome await_ready(int read_fd) {
    const Clock::time_point start = Clock::now();
    const std::optional<Clock::time_point> deadline =
        start + k_server_ready_timeout;
    std::string line;
    std::array<char, 16> chunk{};
    while (line.size() < 6) {
        if (!wait_readable(read_fd, deadline, t_exec_cancel, start)) {
            return watched(t_exec_cancel) &&
                           exec_cancelled(t_exec_cancel, start)
                       ? ReadyOutcome::Cancelled
                       : ReadyOutcome::Failed;
        }
        const ssize_t got = read(read_fd, chunk.data(), 6 - line.size());
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            return ReadyOutcome::Failed;
        }
        line.append(chunk.data(), static_cast<std::size_t>(got));
    }
    return line == "ready\n" ? ReadyOutcome::Ready : ReadyOutcome::Failed;
}

// A pooled server of `config`, or a new one once it has said it is ready, so
// that its start-up (an exec, BuDDy's table and the zygote's fork) is never
// charged to the call that happens to need it: a call's timeout would
// otherwise depend on whether an earlier call had discarded a server. An
// empty lease when a new server could not be started.
ServerLease take_server(const ToolServerConfig* config) {
    std::vector<ServerHandle*> stale;
    ServerHandle* pooled = nullptr;
    {
        const std::scoped_lock lock(server_mutex());
        std::vector<ServerHandle*>& pool = idle_servers();
        while (pooled == nullptr && !pool.empty()) {
            ServerHandle* candidate = pool.back();
            pool.pop_back();
            if (candidate->m_config == config) {
                pooled = candidate;
            } else {
                stale.push_back(candidate);
            }
        }
    }
    for (ServerHandle* old : stale) {
        discard_server(old);
    }
    if (pooled != nullptr) {
        return ServerLease(pooled);
    }
    auto fresh = std::make_unique<ServerHandle>();
    fresh->m_config = config;
    try {
        fresh->m_child = spawn_piped_child(
            {config->m_path}, ParentDeathPolicy::SurviveParentThread,
            ExecutableLookup::AbsolutePath);
    } catch (const std::exception&) {
        return {};
    }
    ServerLease lease(fresh.release());
    ++ToolServerStats::n_started;
    // Non-blocking, so a server that stops reading cannot hold a caller past
    // its deadline inside write.
    const int write_fd = lease.get()->m_child.m_write_fd;
    const int flags = fcntl(write_fd, F_GETFL);
    [[maybe_unused]] const int set_result =
        fcntl(write_fd, F_SETFL, flags | O_NONBLOCK);
#ifdef __APPLE__
    // A write to a dead server must fail with EPIPE, not raise SIGPIPE.
    fcntl(write_fd, F_SETNOSIGPIPE, 1);
#endif
    switch (await_ready(lease.get()->m_child.m_read_fd)) {
        case ReadyOutcome::Ready:
            return lease;
        case ReadyOutcome::Cancelled:
            lease.discard();
            throw ExecCancelled{};
        case ReadyOutcome::Failed:
            break;
    }
    lease.discard();
    return {};
}

enum class WriteOutcome : std::uint8_t { Written, Broken, Stopped };

// One write attempt, with SIGPIPE held back on Linux, where a write to a dead
// server would otherwise raise it and kill peredur. The pending signal is
// consumed before the mask is restored. macOS sets F_SETNOSIGPIPE instead.
ssize_t write_without_sigpipe(int write_fd, const char* data,
                              std::size_t size) {
#ifdef __APPLE__
    return write(write_fd, data, size);
#else
    sigset_t pipe_only;
    sigemptyset(&pipe_only);
    sigaddset(&pipe_only, SIGPIPE);
    sigset_t previous;
    pthread_sigmask(SIG_BLOCK, &pipe_only, &previous);
    const ssize_t written = write(write_fd, data, size);
    const int saved_errno = errno;
    if (written < 0 && saved_errno == EPIPE) {
        const struct timespec no_wait{0, 0};
        while (sigtimedwait(&pipe_only, nullptr, &no_wait) < 0 &&
               errno == EINTR) {
        }
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    errno = saved_errno;
    return written;
#endif
}

// Writes the whole request, waiting out a full pipe in slices so that a
// deadline or a cancellation on `signal` stops it; with neither, it waits
// until the server reads.
WriteOutcome write_request(int write_fd, const std::string& request,
                           const std::optional<Clock::time_point>& deadline,
                           Clock::time_point start,
                           const ExecCancelScope::Signal& signal) {
    std::size_t offset = 0;
    while (offset < request.size()) {
        const ssize_t written = write_without_sigpipe(
            write_fd, request.data() + offset, request.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const std::optional<int> poll_ms = poll_timeout_ms(deadline);
            if (!poll_ms.has_value() ||
                (watched(signal) && exec_cancelled(signal, start))) {
                return WriteOutcome::Stopped;
            }
            const int slice = *poll_ms < 0 || *poll_ms > k_cancel_poll_ms
                                  ? k_cancel_poll_ms
                                  : *poll_ms;
            struct pollfd pfd{};
            pfd.fd = write_fd;
            pfd.events = POLLOUT;
            poll(&pfd, 1, slice);
            continue;
        }
        return WriteOutcome::Broken;
    }
    return WriteOutcome::Written;
}

// Counts a failure of a server of `config`, and turns `config` off at the
// third in a row. Under the lock set_tool_server holds, and only if `config`
// is still the setting, so a failure of the old setting cannot turn off a new
// one installed meanwhile.
void record_server_failure(const ToolServerConfig* config) {
    ++ToolServerStats::n_failures;
    const std::scoped_lock lock(server_mutex());
    if (g_tool_server.load(std::memory_order_acquire) != config) {
        return;
    }
    if (++g_consecutive_server_failures == k_max_consecutive_server_failures) {
        g_tool_server.store(nullptr, std::memory_order_release);
        {
            // One line, once: it is the only sign in a run's log that its
            // tools went back to execs part-way through.
            const std::string warning =
                "peredur: " + config->m_path + " failed " +
                std::to_string(k_max_consecutive_server_failures) +
                " times in a row; running its tools as execs from now on\n";
            write_all(STDERR_FILENO, warning.data(), warning.size());
        }
    }
}

enum class ReplyOutcome : std::uint8_t { Answered, Broken, Stopped };

struct Reply {
    ReplyOutcome m_outcome = ReplyOutcome::Broken;
    // Whether a Stopped read had taken part of the reply, which leaves the
    // stream out of step for a later read.
    bool m_partial = false;
    int m_exit_code = -1;
    std::int64_t m_cpu_us = 0;
    std::string m_output;
};

// Parses "<exit_code> <cpu_us> <length>" into `reply`, returning the length,
// or nullopt for a header the server could not have written.
std::optional<std::size_t> parse_reply_header(const std::string& header,
                                              Reply& reply) {
    const char* cursor = header.c_str();
    char* end = nullptr;
    errno = 0;
    const std::int64_t exit_code = std::strtoll(cursor, &end, 10);
    if (end == cursor || *end != ' ') {
        return std::nullopt;
    }
    cursor = end;
    const std::int64_t cpu_us = std::strtoll(cursor, &end, 10);
    if (end == cursor || *end != ' ') {
        return std::nullopt;
    }
    cursor = end;
    const std::uint64_t length = std::strtoull(cursor, &end, 10);
    if (end == cursor || *end != '\0' || errno != 0) {
        return std::nullopt;
    }
    reply.m_exit_code = static_cast<int>(exit_code);
    reply.m_cpu_us = cpu_us;
    return static_cast<std::size_t>(length);
}

// Reads one reply, until the deadline or a cancellation on `signal` stops it.
Reply read_reply(int read_fd, const std::optional<Clock::time_point>& deadline,
                 Clock::time_point start,
                 const ExecCancelScope::Signal& signal) {
    Reply reply;
    std::string buffer;
    std::size_t header_end = std::string::npos;
    std::size_t need = 0;
    std::array<char, 65536> chunk{};
    while (header_end == std::string::npos || buffer.size() < need) {
        if (!wait_readable(read_fd, deadline, signal, start)) {
            reply.m_outcome = ReplyOutcome::Stopped;
            reply.m_partial = !buffer.empty();
            return reply;
        }
        const ssize_t got = read(read_fd, chunk.data(), chunk.size());
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            return reply;
        }
        buffer.append(chunk.data(), static_cast<std::size_t>(got));
        if (header_end != std::string::npos) {
            continue;
        }
        header_end = buffer.find('\n');
        if (header_end != std::string::npos) {
            const std::optional<std::size_t> length =
                parse_reply_header(buffer.substr(0, header_end), reply);
            if (!length.has_value()) {
                return reply;
            }
            need = header_end + 1 + *length;
        }
    }
    reply.m_outcome =
        buffer.size() == need ? ReplyOutcome::Answered : ReplyOutcome::Broken;
    // Moved, not copied: the buffer is the call's own.
    buffer.erase(0, header_end + 1);
    reply.m_output = std::move(buffer);
    return reply;
}

// How long a server has to kill its tool and reply once the client asks.
// Past it, the client kills the server's whole group instead. The tool's exit
// is a SIGKILL's, so this covers the kernel tearing down a large address space.
constexpr std::chrono::milliseconds k_server_kill_grace{2'000};

// Asks `server` to kill the tool running a request that has timed out or been
// cancelled, and reads the reply that follows, so that the server can serve
// again. nullopt when the reply did not come back within the grace, and the
// server must go. Cancellation does not cut this wait short: it is the kill.
std::optional<Reply> reclaim_server(const ServerHandle& server) {
    const ExecCancelScope::Signal unwatched{};
    const Clock::time_point start = Clock::now();
    const std::optional<Clock::time_point> deadline =
        start + k_server_kill_grace;
    if (write_request(server.m_child.m_write_fd, "k\n", deadline, start,
                      unwatched) != WriteOutcome::Written) {
        return std::nullopt;
    }
    Reply reply =
        read_reply(server.m_child.m_read_fd, deadline, start, unwatched);
    if (reply.m_outcome != ReplyOutcome::Answered) {
        return std::nullopt;
    }
    return reply;
}

std::string server_request(const std::vector<std::string>& arguments,
                           const std::string& input) {
    std::string request = std::to_string(arguments.size()) + "\n";
    for (const std::string& argument : arguments) {
        request += argument;
        request += '\n';
    }
    request += std::to_string(input.size()) + "\n";
    request += input;
    return request;
}

// Runs one call on a server. nullopt when the server failed before answering,
// in which case nothing was decided and the caller runs the exec.
// `charged_from` is set to when the call's clock started, once a server was
// ready.
std::optional<ProcessResult> run_on_server(
    const ToolServerConfig* config, const std::vector<std::string>& arguments,
    const std::string& input, std::chrono::milliseconds timeout,
    Clock::time_point& charged_from) {
    PEREDUR_PROFILE_SCOPE("proc/server");
    ServerLease server = take_server(config);
    if (!server.held()) {
        record_server_failure(config);
        return std::nullopt;
    }
    const Clock::time_point start = Clock::now();
    charged_from = start;
    const std::optional<Clock::time_point> deadline = deadline_after(timeout);
    const WriteOutcome written = write_request(server.get()->m_child.m_write_fd,
                                               server_request(arguments, input),
                                               deadline, start, t_exec_cancel);
    Reply reply;
    if (written == WriteOutcome::Written) {
        reply = read_reply(server.get()->m_child.m_read_fd, deadline, start,
                           t_exec_cancel);
    } else if (written == WriteOutcome::Stopped) {
        reply.m_outcome = ReplyOutcome::Stopped;
    }
    if (reply.m_outcome == ReplyOutcome::Broken) {
        server.discard();
        record_server_failure(config);
        return std::nullopt;
    }
    ++ToolServerStats::n_requests;
    ProcessResult result;
    if (reply.m_outcome == ReplyOutcome::Stopped) {
        const bool cancelled =
            watched(t_exec_cancel) && exec_cancelled(t_exec_cancel, start);
        // A request half written, or a reply half read, leaves the stream out
        // of step, so only a server stopped between the two can be kept.
        std::optional<Reply> killed;
        if (written == WriteOutcome::Written && !reply.m_partial) {
            killed = reclaim_server(*server.get());
        }
        result.m_timed_out = true;
        if (killed.has_value()) {
            // As an exec's kill and reap would give: what the tool printed
            // before it was killed, its exit status and its CPU. A tool that
            // finished just as the kill was sent keeps its own status.
            ++ToolServerStats::n_killed;
            result.m_exit_code = killed->m_exit_code;
            result.m_output = std::move(killed->m_output);
            result.m_cpu_s = static_cast<double>(killed->m_cpu_us) / 1e6;
            if (killed->m_cpu_us > 0) {
                ToolServerStats::n_cpu_ns +=
                    static_cast<std::uint64_t>(killed->m_cpu_us) * 1000U;
            }
            server.give_back();
        } else {
            // The server's group holds the server, its zygote and the tool,
            // so this kill is the exec path's kill of the tool's group. The
            // tool's output is in the server's memory file and goes with it,
            // so the call returns none, and the CPU of the tool is not
            // counted.
            server.discard();
            ++ToolServerStats::n_discarded;
            result.m_exit_code = 128 + SIGKILL;
        }
        if (cancelled) {
            throw ExecCancelled{};
        }
    } else {
        g_consecutive_server_failures.store(0, std::memory_order_relaxed);
        result.m_exit_code = reply.m_exit_code;
        result.m_output = std::move(reply.m_output);
        result.m_cpu_s = static_cast<double>(reply.m_cpu_us) / 1e6;
        if (reply.m_cpu_us > 0) {
            ToolServerStats::n_cpu_ns +=
                static_cast<std::uint64_t>(reply.m_cpu_us) * 1000U;
        }
        server.give_back();
    }
    result.m_wall_s =
        std::chrono::duration<double>(Clock::now() - start).count();
    return result;
}

// The call through the tool server, when one is set and serves it; nullopt
// sends it to the exec. `charged_from` is left alone unless a server took
// the call.
std::optional<ProcessResult> try_tool_server(
    const std::vector<std::string>& arguments, const std::string* input,
    std::chrono::milliseconds timeout, ExecutableLookup lookup,
    Clock::time_point& charged_from) {
    if (input == nullptr) {
        return std::nullopt;
    }
    const ToolServerConfig* config = server_for(arguments, lookup);
    if (config == nullptr) {
        return std::nullopt;
    }
    // As the exec path does: a flag already set starts nothing.
    if (watched(t_exec_cancel) && exec_cancelled(t_exec_cancel, Clock::now())) {
        throw ExecCancelled{};
    }
    return run_on_server(config, arguments, *input, timeout, charged_from);
}

// A server that broke part-way through a call has spent some of its budget,
// and the exec gets only what is left: callers size a budget as the most one
// call may take, and a fallback with the whole budget again could take twice
// that. Shortens `timeout` by what was spent since `charged_from`, or returns
// the timed-out result when nothing is left.
std::optional<ProcessResult> charge_server_time(
    std::chrono::milliseconds& timeout, Clock::time_point charged_from) {
    if (timeout <= std::chrono::milliseconds::zero()) {
        return std::nullopt;
    }
    const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - charged_from);
    if (spent < timeout) {
        timeout -= spent;
        return std::nullopt;
    }
    ProcessResult result;
    result.m_exit_code = 128 + SIGKILL;
    result.m_wall_s =
        std::chrono::duration<double>(Clock::now() - charged_from).count();
    result.m_timed_out = true;
    return result;
}

// execute_and_capture, with `input` as the child's stdin when it is non-null.
// Without one the child inherits this process's stdin, as it always has.
ProcessResult run_and_capture(const std::vector<std::string>& arguments,
                              const std::string* input,
                              std::chrono::milliseconds timeout,
                              ExecutableLookup lookup) {
    assert(!arguments.empty());
    Clock::time_point charged_from = Clock::now();
    if (std::optional<ProcessResult> served =
            try_tool_server(arguments, input, timeout, lookup, charged_from)) {
        return std::move(*served);
    }
    if (std::optional<ProcessResult> spent =
            charge_server_time(timeout, charged_from)) {
        return std::move(*spent);
    }
    const Clock::time_point start = Clock::now();
    if (watched(t_exec_cancel) && exec_cancelled(t_exec_cancel, start)) {
        throw ExecCancelled{};
    }
    const std::vector<char*> argv = exec_argv(arguments);
    std::array<int, 2> pipe_fds = {-1, -1};
    pid_t child_pid = -1;
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
        const int input_fd = input == nullptr ? -1 : input_file_holding(*input);
        [[maybe_unused]] const int pipe_result = make_cloexec_pipe(pipe_fds);
        assert(pipe_result == 0);
        // See the same capture in spawn_piped_child.
        const pid_t parent_pid = getpid();
        child_pid = spawn_without_fork(argv, lookup,
                                       ParentDeathPolicy::KillWithParentThread,
                                       {{pipe_fds[1], STDOUT_FILENO},
                                        {pipe_fds[1], STDERR_FILENO},
                                        {input_fd, STDIN_FILENO}});
        if (child_pid < 0) {
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
                    dup2(pipe_fds[1], STDERR_FILENO) < 0 ||
                    (input_fd >= 0 && dup2(input_fd, STDIN_FILENO) < 0)) {
                    _exit(127);
                }
                close(pipe_fds[1]);
                exec_child(argv, lookup,
                           ParentDeathPolicy::KillWithParentThread, parent_pid);
            }
            adopt_child_process_group(child_pid);
        }
        close(pipe_fds[1]);
        if (input_fd >= 0) {
            close(input_fd);
        }
    }
    const std::optional<Clock::time_point> deadline = deadline_after(timeout);
    auto [output, timed_out] =
        read_until(pipe_fds[0], deadline, t_exec_cancel, start);
    close(pipe_fds[0]);
    // Read once the read has stopped, so a flag set after EOF still leaves
    // the call its answer.
    const bool cancelled = timed_out && watched(t_exec_cancel) &&
                           exec_cancelled(t_exec_cancel, start);
    if (timed_out) {
        kill_process_tree(child_pid);
    }
    double cpu_s = 0.0;
    // Reap against the same deadline rather than blocking. EOF on the pipe
    // means the child closed stdout, not that it exited, so a tool that wedges
    // after writing its answer would otherwise outlive its timeout inside
    // wait4 — the one place the old per-runner wrappers could still hang on a
    // call that nominally had a budget.
    const int exit_code =
        reap(child_pid, timed_out ? std::nullopt : deadline, cpu_s, timed_out);
    if (cancelled) {
        throw ExecCancelled{};
    }
    const double wall_s =
        std::chrono::duration<double>(Clock::now() - start).count();
    return {exit_code, std::move(output), cpu_s, wall_s, timed_out};
}

}  // namespace

ExecCancelScope::ExecCancelScope(const std::atomic<bool>& flag,
                                 const std::atomic<bool>* after_grace,
                                 std::chrono::milliseconds grace)
    : m_previous(t_exec_cancel) {
    t_exec_cancel = Signal{&flag, after_grace, grace.count()};
}

ExecCancelScope::~ExecCancelScope() { t_exec_cancel = m_previous; }

ProcessResult execute_and_capture(const std::vector<std::string>& arguments,
                                  std::chrono::milliseconds timeout,
                                  ExecutableLookup lookup) {
    return run_and_capture(arguments, nullptr, timeout, lookup);
}

ProcessResult execute_and_capture_with_input(
    const std::vector<std::string>& arguments, const std::string& input,
    std::chrono::milliseconds timeout, ExecutableLookup lookup) {
    return run_and_capture(arguments, &input, timeout, lookup);
}

void set_tool_server(ToolServer server) {
    std::vector<ServerHandle*> idle;
    {
        const std::scoped_lock lock(server_mutex());
        const ToolServerConfig* next = nullptr;
        if (!server.m_path.empty()) {
            all_server_configs().push_back(
                std::make_unique<ToolServerConfig>(ToolServerConfig{
                    std::move(server.m_path), std::move(server.m_served)}));
            next = all_server_configs().back().get();
        }
        g_tool_server.store(next, std::memory_order_release);
        g_consecutive_server_failures.store(0, std::memory_order_relaxed);
        idle.swap(idle_servers());
    }
    for (ServerHandle* old : idle) {
        discard_server(old);
    }
}

bool tool_server_active() {
    return g_tool_server.load(std::memory_order_acquire) != nullptr;
}
