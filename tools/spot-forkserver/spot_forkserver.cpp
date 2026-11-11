// spot-forkserver: runs Spot's own ltlsynt, ltlfilt, ltl2tgba and autfilt in a
// forked child per request, so a query skips the exec and the tool's start-up,
// most of which is building BuDDy's node table. The design, its measurements
// and the reasons for the zygote are in "Tool subprocesses" in
// docs/dev/performance.md; src/runner/process.cpp is the only client.
//
// The tools' object files are linked in with each `main` renamed to
// <tool>_main (cmake/spot_forkserver_objects.cmake), and a child calls that
// main with the request's argv and exits with its return value, so the answer
// is the tool's own.
//
// Protocol, on stdin and stdout:
//   ready    "ready\n", once, after start-up and before the first request is
//            read, so the client can start a call's clock after it;
//   request  "<argc>\n", argc lines of argv (argv[0] names the tool by its
//            basename), "<input_length>\n", then that many bytes of stdin;
//   reply    "<exit_code> <child_cpu_us> <output_length>\n", then the child's
//            stdout and stderr merged, as an exec's pipe would merge them;
//   kill     "k\n", sent by the client after a request whose deadline has
//            passed. The running tool is killed, and its reply, with what it
//            had printed, is the request's reply as usual. A kill that
//            arrives after the reply is ignored, so the client always reads
//            exactly one reply per request.
// exit_code is the child's exit status, or 128 + the signal that killed it.
//
// Three processes share one process group: this server, a zygote and the
// current tool child. The client kills the whole group when a kill does not
// bring the reply back in time.
//
// - The server reads requests and writes replies. It never runs tool code.
// - The zygote is forked once, before any request is read, after BuDDy's table
//   is built. It never allocates again: per request it reads one byte, forks
//   the tool child, waits for it, killing it if the server passes on a kill,
//   and writes back a fixed-size status. It kills the child itself, since as
//   the unreaped child's parent it is the one process for which that pid
//   cannot yet name another. A child
//   forked from the server instead would inherit a heap shaped by every request
//   served so far, and ltlsynt picks between valid controllers by heap layout,
//   so the same request could get different controllers depending on history.
//   From the zygote, every child starts from one heap.
// - The tool child reads its argv into static buffers from a memfd the server
//   rewrote, takes its stdin from another and writes its output into a third.
//
// Containment: the client spawns the server with no death signal, because the
// server outlives the thread that spawned it. The server instead watches its
// stdin while a child runs, and the kernel closes that pipe when the client
// dies, whatever killed it: on hang-up the server kills its process group.
// The zygote dies with the server and the child with the zygote, through
// PR_SET_PDEATHSIG, which is safe here because every forking process is
// single-threaded.
//
// This is the one fork outside src/runner/process.cpp. The rule against others
// exists because a fork of the multithreaded peredur inherits other threads'
// descriptors and locks, and this binary is a separate, single-threaded
// process.

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include <spot/twa/bdddict.hh>

extern "C" int ltlsynt_main(int, char**);
extern "C" int ltlfilt_main(int, char**);
extern "C" int ltl2tgba_main(int, char**);
extern "C" int autfilt_main(int, char**);

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif

namespace {

using ToolMain = int (*)(int, char**);

constexpr std::size_t k_max_args = 4096;
constexpr std::size_t k_max_arg_bytes = std::size_t{1} << 20;
constexpr int k_unserved_exit = 127;
// Buffers above this are freed after the reply instead of kept for the next
// request, so an idle server holds no more than this for a past large call.
constexpr std::size_t k_keep_bytes = std::size_t{1} << 20;

void release_if_large(std::string& text) {
    if (text.capacity() > k_keep_bytes) {
        std::string().swap(text);
    }
}

// The tool child's argv, in static storage so that reading it allocates
// nothing: the child's heap is the zygote's, plus what the tool itself does.
std::array<char, k_max_arg_bytes> g_arg_bytes{};
std::array<char*, k_max_args + 1> g_argv{};

ToolMain tool_for(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    const std::string_view name =
        slash == std::string_view::npos ? path : path.substr(slash + 1);
    if (name == "ltlsynt") {
        return ltlsynt_main;
    }
    if (name == "ltlfilt") {
        return ltlfilt_main;
    }
    if (name == "ltl2tgba") {
        return ltl2tgba_main;
    }
    if (name == "autfilt") {
        return autfilt_main;
    }
    return nullptr;
}

bool write_all(int file_fd, const char* data, std::size_t size) {
    while (size > 0) {
        const ssize_t written = write(file_fd, data, size);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        data += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

bool read_all(int file_fd, char* data, std::size_t size) {
    while (size > 0) {
        const ssize_t got = read(file_fd, data, size);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            return false;
        }
        data += got;
        size -= static_cast<std::size_t>(got);
    }
    return true;
}

// Requests arrive on stdin, read through a buffer, since a line read one byte
// per syscall costs a syscall a byte.
class RequestReader {
   public:
    bool line(std::string& out) {
        out.clear();
        while (true) {
            const std::size_t newline = m_buffer.find('\n', m_offset);
            if (newline != std::string::npos) {
                out.assign(m_buffer, m_offset, newline - m_offset);
                m_offset = newline + 1;
                return true;
            }
            if (!fill()) {
                return false;
            }
        }
    }

    // Consumes a kill if one is next in what has been read.
    bool take_kill() {
        if (m_buffer.compare(m_offset, 2, "k\n") != 0) {
            return false;
        }
        m_offset += 2;
        return true;
    }

    [[nodiscard]] bool has_buffered() const {
        return m_offset < m_buffer.size();
    }

    // One read's worth more; false at end of input.
    bool fill() {
        m_buffer.erase(0, m_offset);
        m_offset = 0;
        std::array<char, 65536> chunk{};
        while (true) {
            const ssize_t got = read(STDIN_FILENO, chunk.data(), chunk.size());
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                return false;
            }
            m_buffer.append(chunk.data(), static_cast<std::size_t>(got));
            return true;
        }
    }

    bool bytes(std::string& out, std::size_t size) {
        while (m_buffer.size() - m_offset < size) {
            if (!fill()) {
                return false;
            }
        }
        out.assign(m_buffer, m_offset, size);
        m_offset += size;
        return true;
    }

    void release_if_large() {
        if (m_buffer.capacity() > k_keep_bytes) {
            m_buffer.erase(0, m_offset);
            m_offset = 0;
            m_buffer.shrink_to_fit();
        }
    }

   private:
    std::string m_buffer;
    std::size_t m_offset = 0;
};

struct ZygoteStatus {
    int m_wait_status = 0;
    std::int64_t m_cpu_us = 0;
};

std::int64_t cpu_microseconds(const struct rusage& usage) {
    const std::int64_t user_us =
        (static_cast<std::int64_t>(usage.ru_utime.tv_sec) * 1000000) +
        usage.ru_utime.tv_usec;
    const std::int64_t sys_us =
        (static_cast<std::int64_t>(usage.ru_stime.tv_sec) * 1000000) +
        usage.ru_stime.tv_usec;
    return user_us + sys_us;
}

// Dies with `parent_pid`, and at once if it is already gone.
void die_with_parent(pid_t parent_pid) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() != parent_pid) {
        _exit(1);
    }
}

struct Descriptors {
    int m_argv = -1;
    int m_input = -1;
    int m_output = -1;
};

[[noreturn]] void run_tool_child(const Descriptors& fds) {
    const ssize_t size =
        pread(fds.m_argv, g_arg_bytes.data(), g_arg_bytes.size() - 1, 0);
    int argc = 0;
    if (size > 0) {
        char* cursor = g_arg_bytes.data();
        char* const end = cursor + size;
        while (cursor < end && std::cmp_less(argc, k_max_args)) {
            g_argv[static_cast<std::size_t>(argc)] = cursor;
            ++argc;
            while (cursor < end && *cursor != '\n') {
                ++cursor;
            }
            *cursor = '\0';
            ++cursor;
        }
    }
    g_argv[static_cast<std::size_t>(argc)] = nullptr;
    if (dup2(fds.m_input, STDIN_FILENO) < 0 ||
        dup2(fds.m_output, STDOUT_FILENO) < 0 ||
        dup2(fds.m_output, STDERR_FILENO) < 0) {
        _exit(k_unserved_exit);
    }
    const ToolMain tool = argc > 0 ? tool_for(g_argv[0]) : nullptr;
    if (tool == nullptr) {
        constexpr std::string_view k_message =
            "spot-forkserver: no such tool\n";
        write_all(STDERR_FILENO, k_message.data(), k_message.size());
        _exit(k_unserved_exit);
    }
    // exit, not _exit: it flushes stdio and runs the tool's static
    // destructors, as the tool's own return from main would.
    std::exit(tool(argc, g_argv.data()));
}

// Waits until `child` exits, killing it on any byte from the server, which
// sends one only to pass on the client's kill, or on the server's end of file.
// The child is unreaped until the wait4 after this, so its pid is still its
// own when the kill lands.
void await_child(pid_t child, int wake_fd) {
    const int child_fd = static_cast<int>(syscall(SYS_pidfd_open, child, 0));
    if (child_fd < 0) {
        // Without a descriptor to watch, no kill is passed on, and the client
        // kills the whole group when its kill goes unanswered.
        return;
    }
    std::array<pollfd, 2> watch{};
    watch[0] = {.fd = child_fd, .events = POLLIN, .revents = 0};
    watch[1] = {.fd = wake_fd, .events = POLLIN, .revents = 0};
    nfds_t watching = 2;
    while (true) {
        if (poll(watch.data(), watching, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (watch[0].revents != 0) {
            break;
        }
        if (watch[1].revents != 0) {
            char byte = 0;
            const ssize_t got = read(wake_fd, &byte, 1);
            if (got < 0 && errno == EINTR) {
                continue;
            }
            kill(child, SIGKILL);
            if (got != 1) {
                watching = 1;
            }
        }
    }
    close(child_fd);
}

[[noreturn]] void zygote_main(int wake_fd, int status_fd,
                              const Descriptors& fds) {
    const pid_t zygote_pid = getpid();
    char wake = 0;
    while (true) {
        const ssize_t got = read(wake_fd, &wake, 1);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got != 1) {
            _exit(0);
        }
        if (wake == 'k') {
            // A kill that reached the server after its tool had finished.
            continue;
        }
        const pid_t child = fork();
        if (child == 0) {
            die_with_parent(zygote_pid);
            close(wake_fd);
            close(status_fd);
            run_tool_child(fds);
        }
        ZygoteStatus status;
        if (child < 0) {
            status.m_wait_status = k_unserved_exit << 8;
        } else {
            await_child(child, wake_fd);
            struct rusage usage{};
            while (wait4(child, &status.m_wait_status, 0, &usage) < 0 &&
                   errno == EINTR) {
            }
            status.m_cpu_us = cpu_microseconds(usage);
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        if (!write_all(status_fd, reinterpret_cast<const char*>(&status),
                       sizeof status)) {
            _exit(0);
        }
    }
}

bool rewrite(int file_fd, const std::string& text) {
    if (ftruncate(file_fd, 0) != 0 || lseek(file_fd, 0, SEEK_SET) != 0) {
        return false;
    }
    if (!write_all(file_fd, text.data(), text.size())) {
        return false;
    }
    return lseek(file_fd, 0, SEEK_SET) == 0;
}

// Kills the zygote without reaping it. Reaping would add the zygote's
// children, the tools, to this server's own children's CPU, and peredur
// reaps the server: the tools' CPU would then reach peredur's
// RUSAGE_CHILDREN as well as the replies that already report it (see
// print_cpu_report). The server exits next, so init reaps the zygote.
void abandon_zygote(pid_t zygote) { kill(zygote, SIGKILL); }

// Kills whatever is left in this server's process group, itself included.
// The tool child already dies with the zygote through PR_SET_PDEATHSIG; this
// is for anything that escaped it. Only as the group's leader, since a server
// started without a group of its own would otherwise kill its client's.
[[noreturn]] void kill_own_group(pid_t zygote) {
    abandon_zygote(zygote);
    if (getpgrp() == getpid()) {
        killpg(getpid(), SIGKILL);
    }
    _exit(1);
}

// Passes every kill already read on to the zygote; false if it is gone.
bool forward_kills(int wake_fd, RequestReader& reader) {
    while (reader.take_kill()) {
        if (!write_all(wake_fd, "k", 1)) {
            return false;
        }
    }
    return true;
}

// Takes what poll reported on stdin while a tool runs. False when the client
// has hung up. End of a stdin that is not a pipe, as when a test feeds a file
// of requests, is no hang-up: it sets `at_end`, the tool finishes, and the
// server exits at its next read.
bool take_client_input(decltype(pollfd::revents) revents, RequestReader& reader,
                       bool stdin_hangs_up, bool& at_end) {
    if ((revents & POLLIN) != 0) {
        if (!reader.fill()) {
            if (stdin_hangs_up) {
                return false;
            }
            at_end = true;
        }
        return true;
    }
    return (revents & (POLLHUP | POLLERR)) == 0;
}

// Waits for the zygote's status, passing on the client's kills, or for the
// client to hang up. Returns false on hang-up or a broken zygote.
bool await_status(int wake_fd, int status_fd, RequestReader& reader,
                  bool stdin_hangs_up, ZygoteStatus& status) {
    std::array<pollfd, 2> watch{};
    watch[0] = {.fd = status_fd, .events = POLLIN, .revents = 0};
    bool at_end = false;
    while (forward_kills(wake_fd, reader)) {
        // More is read only while nothing else is waiting, so a request read
        // early waits its turn. POLLHUP and POLLERR are reported regardless.
        watch[1] = {.fd = at_end ? -1 : STDIN_FILENO,
                    .events = static_cast<decltype(pollfd::events)>(
                        reader.has_buffered() ? 0 : POLLIN),
                    .revents = 0};
        if (poll(watch.data(), watch.size(), -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (watch[0].revents != 0) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            return read_all(status_fd, reinterpret_cast<char*>(&status),
                            sizeof status);
        }
        if (!take_client_input(watch[1].revents, reader, stdin_hangs_up,
                               at_end)) {
            return false;
        }
    }
    return false;
}

int exit_code_of(int wait_status) {
    if (WIFEXITED(wait_status)) {
        return WEXITSTATUS(wait_status);
    }
    if (WIFSIGNALED(wait_status)) {
        return 128 + WTERMSIG(wait_status);
    }
    return -1;
}

bool reply(int exit_code, std::int64_t cpu_us, const std::string& output) {
    const std::string header = std::to_string(exit_code) + " " +
                               std::to_string(cpu_us) + " " +
                               std::to_string(output.size()) + "\n";
    return write_all(STDOUT_FILENO, header.data(), header.size()) &&
           write_all(STDOUT_FILENO, output.data(), output.size());
}

// The server's half of the processes: the memory files the tool child reads
// and writes, the zygote, and the pipes to it.
struct Server {
    Descriptors m_fds;
    // Whether stdin is a pipe or socket, whose end means the client is gone.
    bool m_stdin_hangs_up = true;
    pid_t m_zygote = -1;
    int m_wake_fd = -1;
    int m_status_fd = -1;
};

// Creates the memory files and pipes and forks the zygote; false on failure,
// with the reason on stderr.
bool start(Server& server) {
    // The zygote kills a tool only through a pidfd (Linux 5.3), so a server
    // that could not would answer every kill with the client's timeout.
    const int probe = static_cast<int>(syscall(SYS_pidfd_open, getpid(), 0));
    if (probe < 0) {
        std::perror("spot-forkserver: pidfd_open");
        return false;
    }
    close(probe);
    struct stat stdin_stat{};
    server.m_stdin_hangs_up = fstat(STDIN_FILENO, &stdin_stat) != 0 ||
                              S_ISFIFO(stdin_stat.st_mode) ||
                              S_ISSOCK(stdin_stat.st_mode);
    Descriptors& fds = server.m_fds;
    fds.m_argv = memfd_create("spot-forkserver-argv", MFD_CLOEXEC);
    fds.m_input = memfd_create("spot-forkserver-input", MFD_CLOEXEC);
    fds.m_output = memfd_create("spot-forkserver-output", MFD_CLOEXEC);
    std::array<int, 2> wake{-1, -1};
    std::array<int, 2> status{-1, -1};
    if (fds.m_argv < 0 || fds.m_input < 0 || fds.m_output < 0 ||
        pipe2(wake.data(), O_CLOEXEC) != 0 ||
        pipe2(status.data(), O_CLOEXEC) != 0) {
        std::perror("spot-forkserver: setup");
        return false;
    }
    const pid_t server_pid = getpid();
    server.m_zygote = fork();
    if (server.m_zygote < 0) {
        std::perror("spot-forkserver: fork");
        return false;
    }
    if (server.m_zygote == 0) {
        die_with_parent(server_pid);
        close(wake[1]);
        close(status[0]);
        // The request and reply pipes are the client's; holding a copy would
        // keep the reply pipe open past the server's death.
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        zygote_main(wake[0], status[1], fds);
    }
    close(wake[0]);
    close(status[1]);
    server.m_wake_fd = wake[1];
    server.m_status_fd = status[0];
    return true;
}

// Reads one request into `args` (one argument a line) and `input`; false at
// end of input or on a request the client could not have written.
bool read_request(RequestReader& reader, std::string& args,
                  std::string& input) {
    std::string line;
    // Skipping any kill that came after its request's reply.
    do {
        if (!reader.line(line)) {
            return false;
        }
    } while (line == "k");
    const std::int64_t argc = std::strtoll(line.c_str(), nullptr, 10);
    if (argc <= 0 || std::cmp_greater(argc, k_max_args)) {
        return false;
    }
    args.clear();
    for (std::int64_t idx = 0; idx < argc; ++idx) {
        if (!reader.line(line)) {
            return false;
        }
        args += line;
        args += '\n';
    }
    if (!reader.line(line)) {
        return false;
    }
    const std::uint64_t input_size = std::strtoull(line.c_str(), nullptr, 10);
    return reader.bytes(input, static_cast<std::size_t>(input_size));
}

// Runs one request through the zygote and replies; false when the server
// can serve no more.
bool answer(const Server& server, RequestReader& reader,
            const std::string& args, const std::string& input,
            std::string& output) {
    const Descriptors& fds = server.m_fds;
    if (args.size() >= k_max_arg_bytes) {
        return reply(k_unserved_exit, 0, "spot-forkserver: argv too long\n");
    }
    if (!rewrite(fds.m_argv, args) || !rewrite(fds.m_input, input) ||
        !rewrite(fds.m_output, "")) {
        std::perror("spot-forkserver: rewrite");
        return false;
    }
    ZygoteStatus done;
    if (!write_all(server.m_wake_fd, "g", 1)) {
        return false;
    }
    if (!await_status(server.m_wake_fd, server.m_status_fd, reader,
                      server.m_stdin_hangs_up, done)) {
        // The client hung up with a tool running.
        kill_own_group(server.m_zygote);
    }
    const off_t size = lseek(fds.m_output, 0, SEEK_END);
    output.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
    if (!output.empty() && pread(fds.m_output, output.data(), output.size(),
                                 0) != static_cast<ssize_t>(output.size())) {
        std::perror("spot-forkserver: output");
        return false;
    }
    if (!reply(exit_code_of(done.m_wait_status), done.m_cpu_us, output)) {
        return false;
    }
    // Emptied now rather than at the next request, so an idle server holds
    // none of its last call's input or output.
    if (!rewrite(fds.m_input, "") || !rewrite(fds.m_output, "")) {
        std::perror("spot-forkserver: rewrite");
        return false;
    }
    return true;
}

int serve() {
    // BuDDy's table, built once here so every child inherits it. Spot's first
    // bdd_dict runs bdd_init(1 << 19, 2), which is most of a tool's start-up.
    // The dictionary registers no variables.
    static const spot::bdd_dict_ptr k_warm = spot::make_bdd_dict();

    Server server;
    if (!start(server)) {
        return 1;
    }
    if (!write_all(STDOUT_FILENO, "ready\n", 6)) {
        abandon_zygote(server.m_zygote);
        return 1;
    }
    RequestReader reader;
    std::string args;
    std::string input;
    std::string output;
    while (read_request(reader, args, input) &&
           answer(server, reader, args, input, output)) {
        release_if_large(input);
        release_if_large(output);
        reader.release_if_large();
    }
    // End of input or a broken pipe: the zygote goes with the server.
    abandon_zygote(server.m_zygote);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        std::fprintf(stderr,
                     "usage: %s\n"
                     "Serves Spot tool requests on stdin; see "
                     "tools/spot-forkserver/spot_forkserver.cpp.\n",
                     argv[0]);
        return 2;
    }
    return serve();
}
