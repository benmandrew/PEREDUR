#pragma once

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>

// Descriptor helpers shared by the tool runners and the crash handler. The
// crash handler calls them from a signal handler, so both must stay
// async-signal-safe: no allocation, no locks, no exceptions.

// pipe2(O_CLOEXEC) where it exists; pipe plus FD_CLOEXEC where it does not.
// The second form is not atomic against a concurrent fork. The runners close
// that window with SpawnGuard (src/runner/process.cpp); the crash handler
// cannot take a lock and accepts it.
inline int make_cloexec_pipe(std::array<int, 2>& fds) {
#ifdef __APPLE__
    if (pipe(fds.data()) != 0) {
        return -1;
    }
    for (const int pipe_fd : fds) {
        if (fcntl(pipe_fd, F_SETFD, FD_CLOEXEC) != 0) {
            close(fds[0]);
            close(fds[1]);
            return -1;
        }
    }
    return 0;
#else
    return pipe2(fds.data(), O_CLOEXEC);
#endif
}

// Writes all @p size bytes, retrying interrupted and short writes. False on
// any other failure, including a write that makes no progress.
inline bool write_all(int file_fd, const void* data, std::size_t size) {
    const char* bytes = static_cast<const char*>(data);
    std::size_t written = 0;
    while (written < size) {
        const ssize_t result = write(file_fd, bytes + written, size - written);
        if (result > 0) {
            written += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}
