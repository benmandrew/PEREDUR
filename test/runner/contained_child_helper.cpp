// A stand-in for PEREDUR in the process_runner and spot_server_runner suites:
// spawns one contained child, prints its pid, and waits on it. It has to be a
// process of its own rather than a fork of the test binary, which has started
// its reaper long before the test runs and would hand the fork its own
// registration pipe.
//
// With `--server <server> <tool>`, the child is a tool server spawned as the
// client spawns one, holding a request that runs for minutes, so a test can
// kill this process mid-tool and see what survives it.

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <string_view>

#include "posix/descriptors.hpp"
#include "runner/process.hpp"

namespace {

// F(a & X^24 b) as a deterministic automaton: about 2^24 states.
std::string slow_request(const std::string& tool) {
    std::string formula = "F(a & ";
    for (int step = 0; step < 24; ++step) {
        formula += "X ";
    }
    formula += "b)";
    return "6\n" + tool + "\n-D\n-S\n-H\n-F\n-\n" +
           std::to_string(formula.size()) + "\n" + formula;
}

}  // namespace

int main(int argc, char** argv) {
    const bool server = argc == 4 && std::string_view(argv[1]) == "--server";
    PipedChild child;
    try {
        if (server) {
            child = spawn_piped_child({argv[2]},
                                      ParentDeathPolicy::SurviveParentThread,
                                      ExecutableLookup::AbsolutePath);
        } else {
            child = spawn_piped_child({"/bin/sh", "-c", "exec sleep 60"},
                                      ParentDeathPolicy::KillWithParentThread,
                                      ExecutableLookup::AbsolutePath);
        }
    } catch (...) {
        // An empty report, which the test reads as a failed spawn.
        return 1;
    }
    if (server) {
        const std::string request = slow_request(argv[3]);
        if (!write_all(child.m_write_fd, request.data(), request.size())) {
            return 1;
        }
    }
    std::printf("%d\n", static_cast<int>(child.m_pid));
    std::fflush(stdout);
    // End of file is how the test knows the pid is complete.
    close(STDOUT_FILENO);
    int status = 0;
    waitpid(child.m_pid, &status, 0);
    return 0;
}
