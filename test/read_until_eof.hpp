#pragma once

#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

/// Reads `read_fd` until the writer closes it, or until `timeout` expires.
/// Returns what was read and whether the deadline was hit; on a timeout the
/// content is partial and the caller still owns the kill and the reap. Zero
/// means no deadline, which is the only way this can block indefinitely.
inline std::pair<std::string, bool> read_until_eof(
    int read_fd, std::chrono::milliseconds timeout) {
    using Clock = std::chrono::steady_clock;
    const bool bounded = timeout > std::chrono::milliseconds::zero();
    const Clock::time_point deadline = Clock::now() + timeout;
    std::string output;
    std::array<char, 4096> buffer{};
    while (true) {
        int wait_ms = -1;
        if (bounded) {
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - Clock::now())
                    .count();
            if (left <= 0) {
                return {output, true};
            }
            wait_ms = left > std::numeric_limits<int>::max()
                          ? std::numeric_limits<int>::max()
                          : static_cast<int>(left);
        }
        struct pollfd pfd{};
        pfd.fd = read_fd;
        pfd.events = POLLIN;
        const int ready = poll(&pfd, 1, wait_ms);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            return {output, true};
        }
        const ssize_t got = read(read_fd, buffer.data(), buffer.size());
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            return {output, false};
        }
        output.append(buffer.data(), static_cast<std::size_t>(got));
    }
}
