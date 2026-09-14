#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "runner/process.hpp"
#include "test_suite.hpp"
#include "test_support.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

// Every hung child below sleeps far longer than its budget, so a timeout that
// silently stopped firing would show up as a multi-second test rather than a
// wrong assertion. Anything over this means the deadline did not work.
constexpr milliseconds k_max_reasonable_elapsed{10'000};

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start)
        .count();
}

std::string scratch_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() /
            ("peredur_process_test_" + std::to_string(getpid()) + "_" + name))
        .string();
}

void test_captures_output_and_exit_code() {
    const ProcessResult result =
        execute_and_capture({"/bin/sh", "-c", "printf hello; exit 3"});
    expect(result.m_output == "hello",
           "process: stdout should be captured verbatim, got \"" +
               result.m_output + "\"");
    expect(result.m_exit_code == 3,
           "process: exit status should be reported, got " +
               std::to_string(result.m_exit_code));
    expect(!result.m_timed_out,
           "process: a command that exits on its own has not timed out");
}

void test_merges_stderr_into_output() {
    const ProcessResult result =
        execute_and_capture({"/bin/sh", "-c", "printf out; printf err >&2"});
    expect(result.m_output.find("out") != std::string::npos &&
               result.m_output.find("err") != std::string::npos,
           "process: stderr should be merged into the captured output, got \"" +
               result.m_output + "\"");
}

void test_zero_timeout_runs_to_completion() {
    const ProcessResult result = execute_and_capture(
        {"/bin/sh", "-c", "sleep 0.2; printf ok"}, milliseconds::zero());
    expect(result.m_output == "ok" && !result.m_timed_out,
           "process: a zero timeout means no deadline, not an instant one");
}

void test_timeout_fires_on_a_child_that_never_writes() {
    const auto start = Clock::now();
    const ProcessResult result =
        execute_and_capture({"/bin/sh", "-c", "sleep 30"}, milliseconds{200});
    const double took = elapsed_ms(start);
    expect(result.m_timed_out,
           "process: a child outlasting its budget should report a timeout");
    expect(took < static_cast<double>(k_max_reasonable_elapsed.count()),
           "process: the timeout should abandon the child promptly, took " +
               std::to_string(took) + "ms");
    expect(result.m_exit_code == 128 + SIGKILL,
           "process: a killed child should report its signal, got " +
               std::to_string(result.m_exit_code));
}

// The case the old per-runner wrappers could still hang on: EOF on the pipe
// means the child closed stdout, not that it exited, so reaping has to be
// inside the deadline too.
void test_timeout_fires_after_the_child_closes_its_output() {
    const auto start = Clock::now();
    const ProcessResult result = execute_and_capture(
        {"/bin/sh", "-c", "printf done; exec 1>&- 2>&-; sleep 30"},
        milliseconds{300});
    const double took = elapsed_ms(start);
    expect(result.m_timed_out,
           "process: a child that closes stdout but keeps running should still "
           "hit its deadline");
    expect(took < static_cast<double>(k_max_reasonable_elapsed.count()),
           "process: reaping must honour the deadline rather than blocking in "
           "wait4, took " +
               std::to_string(took) + "ms");
    expect(result.m_output == "done",
           "process: output written before the close should survive, got \"" +
               result.m_output + "\"");
}

// The orphan case this wrapper exists for: a grandchild must not outlive the
// timeout that killed its parent.
void test_timeout_kills_the_whole_process_group() {
    const std::string pid_path = scratch_path("grandchild_pid");
    std::filesystem::remove(pid_path);
    // The backgrounded inner shell stays in the child's process group (a
    // non-interactive shell does no job control), so killpg is what reaches it.
    const std::string script =
        "sh -c 'echo $$ > " + pid_path + "; sleep 30' &\nsleep 30\n";
    const ProcessResult result =
        execute_and_capture({"/bin/sh", "-c", script}, milliseconds{500});
    expect(result.m_timed_out,
           "process: the outer child should have timed out");

    pid_t grandchild = 0;
    for (int attempt = 0; attempt < 100 && grandchild == 0; ++attempt) {
        std::ifstream pid_file(pid_path);
        pid_file >> grandchild;
        if (grandchild == 0) {
            std::this_thread::sleep_for(milliseconds{10});
        }
    }
    std::filesystem::remove(pid_path);
    expect(grandchild > 0,
           "process: the test's grandchild never recorded its pid, so the "
           "group-kill assertion below would prove nothing");

    // kill(pid, 0) probes for existence. Allow a moment for the SIGKILL to
    // land and for init to reap the reparented grandchild.
    bool alive = true;
    for (int attempt = 0; attempt < 100 && alive; ++attempt) {
        alive = kill(grandchild, 0) == 0;
        if (alive) {
            std::this_thread::sleep_for(milliseconds{10});
        }
    }
    expect(!alive,
           "process: a timeout must kill the child's whole process group; "
           "grandchild " +
               std::to_string(grandchild) + " survived it");
}

// 512 MiB, allocated by dd's buffer. It has to clear this test binary's own
// resident set rather than a shell's, because that is the floor ru_maxrss
// cannot see under: an ASAN build sits around 36MB, and the margin has to hold
// for whatever the binary grows into. Still free on any machine that can build
// this, since the pages are touched once and released.
constexpr std::uint64_t k_allocation_kb = 512ULL * 1024ULL;

void test_reports_the_child_peak_resident_set() {
    // dd is spawned by the shell and waited for by it, so this also pins that
    // ru_maxrss covers descendants the child reaped itself -- without that the
    // figure would miss every tool that forks internally.
    const ProcessResult hungry = execute_and_capture(
        {"/bin/sh", "-c",
         "dd if=/dev/zero of=/dev/null bs=512M count=1 2>/dev/null"});
    expect(hungry.m_exit_code == 0,
           "process: the allocating child should succeed (is dd present?), "
           "exit " +
               std::to_string(hungry.m_exit_code));
    expect(hungry.m_peak_rss_floor_kb > 0,
           "process: the fork floor is this process's own resident set, which "
           "is never zero while it is running");
    expect(hungry.m_peak_rss_kb > hungry.m_peak_rss_floor_kb,
           "process: a 512MB buffer must clear the fork floor to be reported "
           "at all, got " +
               std::to_string(hungry.m_peak_rss_kb) + "kB against a floor of " +
               std::to_string(hungry.m_peak_rss_floor_kb) + "kB");
    expect(hungry.m_peak_rss_kb > k_allocation_kb / 2,
           "process: a 512MB buffer should show at least 256MB of peak RSS, "
           "got " +
               std::to_string(hungry.m_peak_rss_kb) + "kB");
}

// Dirties one byte per page and reads each back, returning the checksum so the
// caller can assert on it.
//
// Every access goes through a volatile view because a release build is
// otherwise free to delete the whole allocation: the compiler may elide new/
// delete pairs, and a plain front()/back() check folds to a constant that keeps
// nothing alive. That is not hypothetical -- it left this test holding 4.7MB
// instead of 512MB under -O2, asserting nothing at all. Volatile accesses are
// observable, so both the buffer and the stores into it have to survive.
std::uint64_t touch_every_page(std::vector<char>* buffer) {
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    volatile char* view = buffer->data();
    std::uint64_t checksum = 0;
    for (std::size_t offset = 0; offset < buffer->size(); offset += page) {
        view[offset] = static_cast<char>(1 + (offset % 251));
        checksum += static_cast<unsigned char>(view[offset]);
    }
    return checksum;
}

// The regression this measurement exists to avoid: a forked child inherits its
// parent's resident set as copy-on-write, and exec folds that into the child's
// maxrss, so wait4 reports the parent's footprint for any child that stayed
// under it. Reporting that as the tool's peak would have every cheap tool read
// back as however large PEREDUR happened to be -- which is what the assertion
// below would catch, since /bin/sh cannot really have grown to the size of the
// buffer this test is holding.
void test_does_not_report_the_parents_footprint_as_the_childs() {
    std::vector<char> ballast(k_allocation_kb * 1024, '\1');
    expect(touch_every_page(&ballast) > 0,
           "process: the ballast must actually be resident");

    const ProcessResult quiet =
        execute_and_capture({"/bin/sh", "-c", "exit 0"});
    expect(quiet.m_peak_rss_floor_kb > k_allocation_kb / 2,
           "process: the floor should reflect the ballast this process is "
           "holding, got " +
               std::to_string(quiet.m_peak_rss_floor_kb) + "kB");
    expect(quiet.m_peak_rss_kb == 0,
           "process: a bare shell cannot be measured under a floor this high, "
           "so it must report nothing rather than the parent's " +
               std::to_string(quiet.m_peak_rss_floor_kb) + "kB, got " +
               std::to_string(quiet.m_peak_rss_kb) + "kB");
}

// Distinctive so that "the guard let control return" cannot be confused with
// the 127 the guard itself exits with, nor with a shell's own status.
constexpr int k_passed_guard = 42;
constexpr int k_guard_fired = 127;

// Runs the guard in a child of this process, telling it `claimed_parent_pid`
// is the pid its parent read before forking, and returns the child's exit
// status (-1 if it did not exit normally). _exit rather than exit, as after a
// real fork here: the child must not run this process's atexit handlers.
int guard_exit_status(ParentDeathPolicy policy, pid_t claimed_parent_pid) {
    const pid_t child = fork();
    if (child < 0) {
        fail("process: could not fork a child to run the guard in");
    }
    if (child == 0) {
        harden_child_after_fork(policy, claimed_parent_pid);
        _exit(k_passed_guard);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child) {
        fail("process: could not reap the child running the guard");
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// The guard closes the fork/registration race by asking whether the parent is
// still the one that forked this child, and the only honest test of that is
// the pid it compares against. Reading it as "getppid() == 1" instead passes
// every case below except the two mismatches, and is wrong wherever PEREDUR is
// itself pid 1 -- the container entrypoint case, where it exited 127 before
// every tool exec and left each query with empty output and no timeout.
void test_parent_death_guard_compares_the_recorded_parent_pid() {
    expect(getpid() != 1,
           "process: this test distinguishes a dead parent from pid 1, so it "
           "means nothing if the test binary is itself pid 1");

    const pid_t parent_pid = getpid();
    expect(guard_exit_status(ParentDeathPolicy::KillWithParentThread,
                             parent_pid) == k_passed_guard,
           "process: a child whose parent is still alive must exec, not exit");

    // parent_pid + 1 is never this child's parent, so it stands for the parent
    // that died in the window before PDEATHSIG was registered.
    expect(guard_exit_status(ParentDeathPolicy::KillWithParentThread,
                             parent_pid + 1) == k_guard_fired,
           "process: a parent-pid mismatch means nothing will clean this child "
           "up, so the guard must exit 127 rather than exec a tool");

    // 1 has no privileged meaning: what decides is whether getppid() still
    // matches, so a child told its parent was pid 1 while it is not fires the
    // guard exactly as any other mismatch does.
    expect(guard_exit_status(ParentDeathPolicy::KillWithParentThread,
                             static_cast<pid_t>(1)) == k_guard_fired,
           "process: the guard must test the recorded parent pid, not whether "
           "this child has been reparented to init");

    // The other policy is for a child that outlives its forking thread, so it
    // registers nothing and has no race to close.
    expect(guard_exit_status(ParentDeathPolicy::SurviveParentThread,
                             parent_pid + 1) == k_passed_guard,
           "process: SurviveParentThread sets no PDEATHSIG, so it must not "
           "refuse to exec on a dead parent either");
}

}  // namespace

void run_process_runner_tests() {
    test_captures_output_and_exit_code();
    test_merges_stderr_into_output();
    test_zero_timeout_runs_to_completion();
    test_timeout_fires_on_a_child_that_never_writes();
    test_timeout_fires_after_the_child_closes_its_output();
    test_timeout_kills_the_whole_process_group();
    test_reports_the_child_peak_resident_set();
    test_does_not_report_the_parents_footprint_as_the_childs();
    test_parent_death_guard_compares_the_recorded_parent_pid();
}
