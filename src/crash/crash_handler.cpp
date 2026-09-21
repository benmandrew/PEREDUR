#include "crash/crash_handler.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <csignal>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <cpptrace/cpptrace.hpp>

#include "posix/descriptors.hpp"

namespace {

constexpr std::size_t k_path_buffer_size = 4096;
constexpr std::size_t k_num_buffer_size = 32;
constexpr std::size_t k_frame_buffer_size = 100;
constexpr std::size_t k_metadata_buffer_size = 4096;

std::array<char, k_path_buffer_size> g_tracer_path = {};
std::array<char, k_path_buffer_size> g_crash_dir = {};
std::array<char, k_metadata_buffer_size> g_crash_metadata = {};

// atomic_flag is the only type the standard guarantees lock-free, hence the
// only one safe to touch from a signal handler.
std::atomic_flag g_crash_in_progress = ATOMIC_FLAG_INIT;

void copy_path_to_buffer(std::array<char, k_path_buffer_size>& destination,
                         const std::filesystem::path& path) {
    const std::string value = path.string();
    if (value.size() + 1 > destination.size()) {
        throw std::runtime_error("path exceeds crash handler buffer size");
    }
    std::memcpy(destination.data(), value.c_str(), value.size() + 1);
}

std::size_t format_unsigned(char* destination, std::size_t destination_size,
                            std::size_t value) {
    std::array<char, k_num_buffer_size> tmp = {};
    std::size_t length = 0;
    do {
        tmp[length++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value > 0 && length < tmp.size());
    if (length + 1 > destination_size) {
        return 0;
    }
    for (std::size_t index = 0; index < length; ++index) {
        destination[index] = tmp[length - 1 - index];
    }
    destination[length] = '\0';
    return length;
}

// Walks one trace at startup so cpptrace's lazy loader and symbol
// initialisation happen here rather than inside the handler, which can neither
// allocate nor dlopen. Called from init_cpptrace, before any signal can arrive.
void warmup_cpptrace() {
    std::array<cpptrace::frame_ptr, 10> buf = {};
    const std::size_t frame_count =
        cpptrace::safe_generate_raw_trace(buf.data(), buf.size());
    if (frame_count > 0) {
        cpptrace::safe_object_frame frame{};
        cpptrace::get_safe_object_frame(buf[0], &frame);
    }
}

// Everything in this function runs inside a signal handler, so it is confined
// to async-signal-safe calls: no malloc, no iostreams, no std::string, no
// locks. That one constraint is what every design choice here answers to --
// the tracer path, crash directory and metadata are fixed buffers filled in
// ahead of time rather than built here, format_unsigned exists because
// snprintf is not on the safe list, and write_all is a raw write(2) loop for
// the same reason. A printf or an allocation added below does not fail
// loudly; it deadlocks or corrupts the very report it was meant to enrich.
void crash_handler(int signo, [[maybe_unused]] siginfo_t* siginfo,
                   [[maybe_unused]] void* context) {
    // A faulting scoring-pool thread usually takes its siblings down with it,
    // so the handler must expect concurrent entry. Only the first thread
    // reports; the rest park until it calls _exit and takes the process with
    // it. Parking rather than returning keeps the report from being truncated,
    // and parking rather than reporting keeps one crash to one log and one
    // tracer.
    if (g_crash_in_progress.test_and_set()) {
        while (true) {
            pause();
        }
    }

    std::array<cpptrace::frame_ptr, k_frame_buffer_size> buf = {};
    const std::size_t frame_count =
        cpptrace::safe_generate_raw_trace(buf.data(), buf.size());

    std::array<char, k_num_buffer_size> pid_buffer = {};
    std::array<char, k_num_buffer_size> signal_buffer = {};
    std::array<char, k_num_buffer_size> timestamp_buffer = {};
    const std::size_t pid_length =
        format_unsigned(pid_buffer.data(), pid_buffer.size(),
                        static_cast<std::size_t>(getpid()));
    const std::size_t signal_length =
        format_unsigned(signal_buffer.data(), signal_buffer.size(),
                        static_cast<std::size_t>(signo));
    timespec now{};
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        now.tv_sec = 0;
    }
    const std::size_t timestamp_length =
        format_unsigned(timestamp_buffer.data(), timestamp_buffer.size(),
                        static_cast<std::size_t>(now.tv_sec));
    if (pid_length == 0 || signal_length == 0 || timestamp_length == 0) {
        _exit(1);
    }

    const std::size_t crash_dir_length = std::strlen(g_crash_dir.data());
    const char* suffix = ".log";
    const char* prefix = "/crash_";
    const char* separator = "_";
    const std::size_t total_length = crash_dir_length + std::strlen(prefix) +
                                     pid_length + std::strlen(separator) +
                                     timestamp_length + std::strlen(suffix);
    std::array<char, k_path_buffer_size> log_path = {};
    if (total_length + 1 > log_path.size()) {
        _exit(1);
    }

    std::size_t cursor = 0;
    std::memcpy(log_path.data() + cursor, g_crash_dir.data(), crash_dir_length);
    cursor += crash_dir_length;
    std::memcpy(log_path.data() + cursor, prefix, std::strlen(prefix));
    cursor += std::strlen(prefix);
    std::memcpy(log_path.data() + cursor, pid_buffer.data(), pid_length);
    cursor += pid_length;
    std::memcpy(log_path.data() + cursor, separator, std::strlen(separator));
    cursor += std::strlen(separator);
    std::memcpy(log_path.data() + cursor, timestamp_buffer.data(),
                timestamp_length);
    cursor += timestamp_length;
    std::memcpy(log_path.data() + cursor, suffix, std::strlen(suffix));
    cursor += std::strlen(suffix);
    log_path[cursor] = '\0';

    // O_CLOEXEC is load-bearing. Crashes arrive on several scoring-pool threads
    // at once, so a concurrent handler's fork() copies this pipe's write end
    // into its own tracer. Without close-on-exec that copy survives execl and
    // holds the pipe open, our tracer never reads EOF, and waitpid below blocks
    // forever. pipe2 sets the flag atomically; pipe+fcntl would race the fork.
    //
    // That race is reopened on macOS, which has no pipe2, and is accepted here
    // rather than closed. The mutual exclusion that closes it for the two
    // runner forks (SpawnGuard in src/runner/process.cpp) cannot be used from
    // a signal handler: taking a lock there is not async-signal-safe and
    // deadlocks outright if this thread already held it when the signal
    // arrived. pipe and fcntl are both on the async-signal-safe list, so the
    // shim below is legal where a mutex is not, and the residual window costs
    // at worst a lost crash report from one of several simultaneous crashes.
    std::array<int, 2> pipefd = {-1, -1};
    if (make_cloexec_pipe(pipefd) != 0) {
        _exit(1);
    }

    pid_t child_pid = fork();
    if (child_pid == 0) {
        close(pipefd[1]);
        // dup2 clears close-on-exec on the new descriptor, which is what lets
        // stdin survive the execl that O_CLOEXEC closes every other copy on.
        if (dup2(pipefd[0], STDIN_FILENO) < 0) {
            _exit(1);
        }
        close(pipefd[0]);
        execl(g_tracer_path.data(), "signal_tracer", log_path.data(),
              signal_buffer.data(), pid_buffer.data(), g_crash_metadata.data(),
              static_cast<char*>(nullptr));
        _exit(1);
    }
    if (child_pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        _exit(1);
    }

    close(pipefd[0]);
    for (std::size_t index = 0; index < frame_count; ++index) {
        cpptrace::safe_object_frame frame{};
        cpptrace::get_safe_object_frame(buf[index], &frame);
        if (!write_all(pipefd[1], &frame, sizeof(frame))) {
            break;
        }
    }
    close(pipefd[1]);
    waitpid(child_pid, nullptr, 0);
    _exit(1);
}

}  // namespace

void register_crash_metadata(const std::string& text) {
    if (text.size() + 1 > g_crash_metadata.size()) {
        throw std::runtime_error("crash metadata exceeds buffer size");
    }
    std::memcpy(g_crash_metadata.data(), text.c_str(), text.size() + 1);
}

void init_cpptrace(const char* executable_name) {
    const std::filesystem::path executable_path =
        std::filesystem::absolute(std::filesystem::path(executable_name));
    copy_path_to_buffer(g_tracer_path,
                        executable_path.parent_path() / "signal_tracer");

    const std::filesystem::path crash_dir_path =
        std::filesystem::absolute(std::filesystem::path("crashes"));
    std::filesystem::create_directories(crash_dir_path);
    copy_path_to_buffer(g_crash_dir, crash_dir_path);
    warmup_cpptrace();
    cpptrace::register_terminate_handler();
    struct sigaction sig_action{};
    sig_action.sa_sigaction = crash_handler;
    sig_action.sa_flags = SA_SIGINFO;
    sigemptyset(&sig_action.sa_mask);
    sigaction(SIGSEGV, &sig_action, nullptr);
    sigaction(SIGABRT, &sig_action, nullptr);
    sigaction(SIGFPE, &sig_action, nullptr);
}
