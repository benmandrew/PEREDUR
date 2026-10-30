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
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "runner/process.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "process_runner";

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

TEST(test_captures_output_and_exit_code) {
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

TEST(test_merges_stderr_into_output) {
    const ProcessResult result =
        execute_and_capture({"/bin/sh", "-c", "printf out; printf err >&2"});
    expect(result.m_output.find("out") != std::string::npos &&
               result.m_output.find("err") != std::string::npos,
           "process: stderr should be merged into the captured output, got \"" +
               result.m_output + "\"");
}

TEST(test_zero_timeout_runs_to_completion) {
    const ProcessResult result = execute_and_capture(
        {"/bin/sh", "-c", "sleep 0.2; printf ok"}, milliseconds::zero());
    expect(result.m_output == "ok" && !result.m_timed_out,
           "process: a zero timeout means no deadline, not an instant one");
}

TEST(test_timeout_fires_on_a_child_that_never_writes) {
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
TEST(test_timeout_fires_after_the_child_closes_its_output) {
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
TEST(test_timeout_kills_the_whole_process_group) {
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
TEST(test_parent_death_guard_compares_the_recorded_parent_pid) {
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

// Past both the 64 KiB pipe buffer and MAX_ARG_STRLEN: the input must arrive
// whole, and the child must see end of file after it.
TEST(test_input_reaches_the_child_stdin_whole) {
    const std::string input(300'000, 'a');
    const ProcessResult result = execute_and_capture_with_input(
        {"/bin/sh", "-c", "wc -c"}, input, milliseconds{10'000});
    expect(result.m_exit_code == 0 && !result.m_timed_out,
           "process: reading a large stdin should succeed");
    expect(result.m_output.find("300000") != std::string::npos,
           "process: the child should read every input byte, got \"" +
               result.m_output + "\"");
}

// A child that exits without reading its input must not stall the call or
// raise SIGPIPE here, which a streamed pipe would.
TEST(test_unread_input_is_harmless) {
    const std::string input(300'000, 'a');
    const ProcessResult result = execute_and_capture_with_input(
        {"/bin/sh", "-c", "printf done"}, input, milliseconds{10'000});
    expect(result.m_exit_code == 0 && result.m_output == "done",
           "process: a child that ignores its input should still finish");
}

// A failed exec used to be exit 127 with empty output, which the runners read
// as an unrecognised answer. It now says what failed, and names E2BIG, the
// failure an oversized formula argument produces.
TEST(test_exec_failure_is_reported) {
    const ProcessResult missing =
        execute_and_capture({"/nonexistent/peredur-no-such-tool"});
    expect(missing.m_exit_code == 127,
           "process: a failed exec should exit 127, got " +
               std::to_string(missing.m_exit_code));
    expect(missing.m_output.find("cannot exec /nonexistent/peredur-no-such-"
                                 "tool: errno") != std::string::npos,
           "process: a failed exec should say so, got \"" + missing.m_output +
               "\"");
#ifdef __linux__
    // MAX_ARG_STRLEN is Linux's; macOS bounds only the argv total.
    const ProcessResult too_long = execute_and_capture(
        {"/bin/sh", "-c", "true", std::string(200'000, 'a')});
    expect(too_long.m_exit_code == 127 &&
               too_long.m_output.find("E2BIG") != std::string::npos,
           "process: an argument over MAX_ARG_STRLEN should report E2BIG, got "
           "\"" +
               too_long.m_output + "\"");
#endif
}

// The pid of the contained child the helper spawned, as it reports it, or 0.
pid_t contained_child_of(const PipedChild& helper) {
    const std::pair<std::string, bool> read =
        read_until_eof(helper.m_read_fd, milliseconds{10'000});
    if (read.second || read.first.empty()) {
        return 0;
    }
    return static_cast<pid_t>(std::stoi(read.first));
}

// GNU timeout setpgid's itself and, on expiry, signals that whole group, and
// Ctrl-C and a harness killing by group do the same. A macOS reaper still in
// PEREDUR's group died with it, and every tool, in a group of its own, ran on
// unattended: a `timeout 2400` run left two ltlsynt at 87% CPU for three
// hours. SIGKILL is in here because only leaving the group survives it.
TEST(test_a_signal_to_the_parents_group_still_kills_its_child) {
    for (const int signal_number : {SIGTERM, SIGKILL}) {
        const PipedChild helper =
            spawn_piped_child({PEREDUR_CONTAINED_CHILD_HELPER},
                              ParentDeathPolicy::SurviveParentThread,
                              ExecutableLookup::AbsolutePath);
        const pid_t child = contained_child_of(helper);
        close(helper.m_write_fd);
        close(helper.m_read_fd);
        if (child <= 0) {
            killpg(helper.m_pid, SIGKILL);
            waitpid(helper.m_pid, nullptr, 0);
            fail("process: the helper never reported its child's pid");
        }
        killpg(helper.m_pid, signal_number);
        int status = 0;
        waitpid(helper.m_pid, &status, 0);

        bool alive = true;
        for (int attempt = 0; attempt < 500 && alive; ++attempt) {
            alive = kill(child, 0) == 0;
            if (alive) {
                std::this_thread::sleep_for(milliseconds{10});
            }
        }
        if (alive) {
            killpg(child, SIGKILL);
        }
        expect(!alive, "process: signal " + std::to_string(signal_number) +
                           " to the parent's process group left its "
                           "contained child " +
                           std::to_string(child) + " running");
    }
}

}  // namespace
