#pragma once

/// @file process.hpp
/// @brief The fork/exec wrapper behind every external tool runner (ltl2tgba,
///        ltlsynt, black, ltlfilt, ganak, node), holding the timeout and
///        orphan-containment policy in one place.

#include <sys/types.h>  // NOLINT(build/include_order) — pid_t

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/// Outcome of one external tool invocation.
struct ProcessResult {
    /// The child's exit status, or 128 + signal number if a signal killed it
    /// (including the SIGKILL this wrapper sends on timeout).
    int m_exit_code = -1;
    /// Merged stdout and stderr, as far as it was read before EOF or timeout.
    std::string m_output;
    /// The child's user+sys CPU seconds, from wait4's rusage.
    double m_cpu_s = 0.0;
    /// Wall seconds from the start of the call to the reap.
    double m_wall_s = 0.0;
    /// True if the deadline expired: the output is partial and the process
    /// group was killed.
    bool m_timed_out = false;
};

/// How to resolve arguments[0] to a binary.
enum class ExecutableLookup : std::uint8_t {
    /// execv, for the full paths CMake resolves for the tools it fetches or
    /// builds (ltl2tgba, ltlsynt, ltlfilt, black, ganak).
    AbsolutePath,
    /// execvp, for a general system interpreter found on PATH (node).
    SearchPath,
};

/// Runs `arguments` with stdout and stderr merged into the returned output,
/// reaping the child before returning.
///
/// A non-zero `timeout` is a wall-clock budget over the whole call, covering
/// both waiting for output and reaping. On expiry the child's process group is
/// SIGKILLed and `m_timed_out` is set. Zero means no deadline, which is the
/// only way this function can block indefinitely.
ProcessResult execute_and_capture(
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds timeout = std::chrono::milliseconds::zero(),
    ExecutableLookup lookup = ExecutableLookup::AbsolutePath);

/// As execute_and_capture, with `input` as the child's stdin.
///
/// This is how a formula reaches a tool. Linux caps any single argv string at
/// MAX_ARG_STRLEN (128 KiB), and an exec over it fails with E2BIG before the
/// tool runs, so a formula passed with `-f` stops working at that size. The
/// input is written to an in-memory file before the fork, not streamed
/// through a pipe, so it can be any size without deadlocking against the
/// output this call reads, and the timeout behaves exactly as above.
///
/// Throws std::runtime_error if the input file cannot be created or written.
ProcessResult execute_and_capture_with_input(
    const std::vector<std::string>& arguments, const std::string& input,
    std::chrono::milliseconds timeout = std::chrono::milliseconds::zero(),
    ExecutableLookup lookup = ExecutableLookup::AbsolutePath);

/// Whether the child should be killed when the thread that forked it goes
/// away. PR_SET_PDEATHSIG is tied to the forking *thread*, not to the process,
/// which makes it safe for a call that forks and waits in one place and wrong
/// for anything longer-lived.
///
/// macOS has no mechanism that survives the exec, so the request is made of a
/// reaper process of PEREDUR's own instead (see src/runner/process.cpp), by
/// the parent while the posix_spawn'd child is still suspended. That
/// is a weaker guarantee — it depends on the reaper being alive and
/// schedulable, where the Linux one holds with every thread of PEREDUR wedged
/// — and it is process-scoped rather than thread-scoped. The distinction below
/// is preserved regardless, by registering only the first case: collapsing the
/// two would contain the persistent formaliser child as well, which is the
/// behaviour this enum exists to keep it out of.
enum class ParentDeathPolicy : std::uint8_t {
    /// Set PR_SET_PDEATHSIG, or register with the reaper. Correct only when
    /// the forking thread waits for the child before returning, as
    /// execute_and_capture does.
    KillWithParentThread,
    /// Leave PDEATHSIG unset and register nothing, for a child that outlives
    /// the thread that spawned it. Setting it here would kill the child as
    /// soon as that thread returned — for a lazily-spawned persistent process
    /// that means the first pool worker to touch it. Containment on parent
    /// death is then the owner's teardown to arrange.
    SurviveParentThread,
};

/// A child holding a pipe on each side: the parent writes its stdin and reads
/// its stdout. Both descriptors and the reap belong to the caller.
struct PipedChild {
    pid_t m_pid = -1;
    /// The parent's write end of the child's stdin. Closing it is what the
    /// child sees as end of file.
    int m_write_fd = -1;
    /// The parent's read end of the child's stdout.
    int m_read_fd = -1;
};

/// Forks and execs `arguments` with a pipe onto each of the child's stdin and
/// stdout, applying the same containment as execute_and_capture. Unlike that
/// function it neither writes, reads nor reaps: a bidirectional exchange is the
/// caller's protocol to drive, and how long the child lives is the caller's to
/// decide — hence `policy`.
///
/// Both pipes are created with O_CLOEXEC, without which a fork on any other
/// scoring thread would inherit them and hold this call's ends open past its
/// own exec, so its reader never sees end of file.
///
/// This and execute_and_capture (which execute_and_capture_with_input shares)
/// are the only two spawns a caller can reach, and nothing may add a third
/// outside process.cpp — which holds one more, for the macOS reaper. macOS
/// uses posix_spawn, a fixed 74 ms cheaper per call than a fork from an ASan
/// build, and forks only if it fails. Linux always forks: posix_spawn has no
/// attribute for PR_SET_PDEATHSIG, the one mechanism that stops a killed run
/// from stranding multi-GB tool processes (PR #47).
PipedChild spawn_piped_child(const std::vector<std::string>& arguments,
                             ParentDeathPolicy policy, ExecutableLookup lookup);

/// Reads `read_fd` until the writer closes it, or until `timeout` expires.
/// Returns what was read and whether the deadline was hit; on a timeout the
/// content is partial and the caller still owns the kill and the reap. Zero
/// means no deadline, which is the only way this can block indefinitely.
std::pair<std::string, bool> read_until_eof(int read_fd,
                                            std::chrono::milliseconds timeout);

/// Applies the child half of the containment policy: puts the child in its own
/// process group, and applies `policy`. Call only between fork() and exec(),
/// where it must stay async-signal-safe; under KillWithParentThread it _exit()s
/// if the parent is already gone. Both forks in process.cpp call it themselves,
/// and they are its only callers. A macOS posix_spawn child never runs it; see
/// spawn_without_fork in process.cpp for how its work is done there.
///
/// `parent_pid` is the caller's own pid, read *before* the fork: that is what
/// "already gone" is tested against. Testing for reparenting to pid 1 instead
/// is wrong wherever this process is itself pid 1, which is the normal case
/// inside a container.
///
/// Note that the new process group is not the terminal's foreground group, so
/// a Ctrl-C no longer reaches the tool directly. Under KillWithParentThread the
/// PDEATHSIG half covers that: the signal kills this process, and the kernel
/// then kills the child.
void harden_child_after_fork(ParentDeathPolicy policy, pid_t parent_pid);

/// Reaps `pid`, giving it `grace` to exit on its own before killing its process
/// group. Returns the child's user+sys CPU seconds. For a child that is asked
/// to shut down cleanly first, such as the formaliser on stdin EOF.
double reap_with_grace(pid_t pid, std::chrono::milliseconds grace);
