// A stand-in for PEREDUR in the process_runner suite: spawns one contained
// child, prints its pid, and waits on it. It has to be a process of its own
// rather than a fork of the test binary, which has started its reaper long
// before the test runs and would hand the fork its own registration pipe.

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>

#include "runner/process.hpp"

int main() {
    PipedChild child;
    try {
        child = spawn_piped_child({"/bin/sh", "-c", "exec sleep 60"},
                                  ParentDeathPolicy::KillWithParentThread,
                                  ExecutableLookup::AbsolutePath);
    } catch (...) {
        // An empty report, which the test reads as a failed spawn.
        return 1;
    }
    std::printf("%d\n", static_cast<int>(child.m_pid));
    std::fflush(stdout);
    // End of file is how the test knows the pid is complete.
    close(STDOUT_FILENO);
    int status = 0;
    waitpid(child.m_pid, &status, 0);
    return 0;
}
