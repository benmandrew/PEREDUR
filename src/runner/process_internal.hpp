#pragma once

// What process.cpp and tool_server.cpp share. Each function is defined, with
// its comment, in the file named above it.

#include <sys/types.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "runner/process.hpp"

namespace process_internal {

using Clock = std::chrono::steady_clock;

// How often a read under a cancel flag wakes to look at it. Only a cancelled
// speculative query waits this long past its cancellation, against the
// seconds it would otherwise run on.
constexpr int k_cancel_poll_ms = 20;

// process.cpp

extern thread_local ExecCancelScope::Signal t_exec_cancel;
bool watched(const ExecCancelScope::Signal& signal);
bool exec_cancelled(const ExecCancelScope::Signal& signal,
                    Clock::time_point start);
std::optional<int> poll_timeout_ms(
    const std::optional<Clock::time_point>& deadline);
bool wait_readable(int read_fd,
                   const std::optional<Clock::time_point>& deadline,
                   const ExecCancelScope::Signal& signal,
                   Clock::time_point start);
void kill_process_tree(pid_t pid);
std::optional<Clock::time_point> deadline_after(
    std::chrono::milliseconds timeout);

// tool_server.cpp

std::optional<ProcessResult> try_tool_server(
    const std::vector<std::string>& arguments, const std::string* input,
    std::chrono::milliseconds timeout, ExecutableLookup lookup,
    Clock::time_point& charged_from);

}  // namespace process_internal
