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
    /// The child's peak resident set in kilobytes, from wait4's ru_maxrss
    /// (which the runner normalises: that field is kilobytes on Linux and
    /// bytes on macOS), or
    /// zero when that figure could not be told apart from this process's own
    /// footprint — see m_peak_rss_floor_kb, which is the whole subtlety here.
    ///
    /// A kernel-maintained high-water mark rather than a sample, so unlike
    /// polling /proc it cannot miss a spike between reads. It covers this child
    /// and any descendant it waited for itself, which is what makes it the
    /// right number for a tool that forks internally.
    ///
    /// Also zero when the child was killed before the kernel recorded anything,
    /// and on a timeout it describes only what the process reached before the
    /// SIGKILL — a tool stopped on its way up reports less than it was heading
    /// for.
    std::uint64_t m_peak_rss_kb = 0;
    /// This process's own resident set at the moment of the fork, in kilobytes,
    /// below which the child's peak cannot be measured at all.
    ///
    /// A forked child starts as a copy-on-write copy of its parent, so its mm
    /// begins life with the parent's resident set already charged to it, and
    /// exec folds that pre-exec high-water into the child's maxrss rather than
    /// discarding it. What wait4 reports is therefore
    /// `max(parent RSS at fork, the tool's true peak)`: exact above this floor,
    /// and pure artefact at or below it. m_peak_rss_kb carries the figure only
    /// in the first case, so that the parent's own footprint can never be read
    /// back as a tool's.
    ///
    /// Itself the larger of a sample on each side of the fork, since no single
    /// read lands at the instant the kernel charges the child. Understating it
    /// would hand a tool this process's whole footprint, so it is deliberately
    /// rounded the safe way: a tool whose peak falls between the two samples is
    /// reported as unmeasurable rather than mis-attributed.
    std::uint64_t m_peak_rss_floor_kb = 0;
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

/// Whether the child should be killed when the thread that forked it goes
/// away. PR_SET_PDEATHSIG is tied to the forking *thread*, not to the process,
/// which makes it safe for a call that forks and waits in one place and wrong
/// for anything longer-lived.
///
/// macOS has no mechanism that survives the exec, so the request is made of a
/// reaper process of PEREDUR's own instead (see src/runner/process.cpp). That
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
    /// This process's resident set at the fork, as ProcessResult documents it.
    /// Carried on the child because only the spawn can observe it, while the
    /// peak it qualifies is not read until reap_with_grace, arbitrarily later.
    std::uint64_t m_rss_floor_kb = 0;
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
/// This and execute_and_capture are the only two forks a caller can reach, and
/// nothing may add a third outside process.cpp — which holds one more, for the
/// macOS reaper. posix_spawn would be the cheaper primitive, but it has no
/// attribute for PR_SET_PDEATHSIG — the one mechanism that stops a killed run
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
/// and they are its only callers.
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
///
/// `executable` names the binary for the peak-resident-set counters, which this
/// records itself exactly as execute_and_capture does. It is a parameter rather
/// than a default because a spawn path that reports no memory is invisible
/// rather than merely incomplete: the counters would simply be missing, and a
/// tool spawned this way would look like a tool that never allocates.
///
/// `rss_floor_kb` is the spawn's PipedChild::m_rss_floor_kb, without which the
/// peak recorded here would be this process's own footprint whenever the child
/// stayed under it.
double reap_with_grace(pid_t pid, std::chrono::milliseconds grace,
                       const std::string& executable,
                       std::uint64_t rss_floor_kb);
