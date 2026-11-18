#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __APPLE__
#include <pthread.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "posix/descriptors.hpp"
#include "profile.hpp"
#include "runner/process.hpp"
#include "runner/process_internal.hpp"

using process_internal::Clock;
using process_internal::deadline_after;
using process_internal::exec_cancelled;
using process_internal::k_cancel_poll_ms;
using process_internal::kill_process_tree;
using process_internal::poll_timeout_ms;
using process_internal::t_exec_cancel;
using process_internal::wait_readable;
using process_internal::watched;

namespace {

// The tool server (see ToolServer in the header, and spot_forkserver.cpp for
// the protocol).
//
// A server is spawned through spawn_piped_child like any other child, so
// process.cpp still holds every fork peredur makes; the per-query forks happen
// inside the server, a separate single-threaded process. It is spawned
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

}  // namespace

namespace process_internal {

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

}  // namespace process_internal

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
