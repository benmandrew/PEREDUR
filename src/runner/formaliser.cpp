#include "runner/formaliser.hpp"

#include <unistd.h>

#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "runner/process.hpp"
#include "tool_paths.hpp"

namespace {

// How long the child gets to act on the stdin EOF before its process group is
// killed at teardown. Generous for a node process that only has to flush and
// exit, and short enough that a wedged one cannot hold up shutdown.
constexpr std::chrono::milliseconds k_shutdown_grace{2000};

}  // namespace

std::string formaliser_script_path() {
#ifdef FORMALISER_SCRIPT_PATH
    static const ToolPath k_script =
        tool_path_from_env("PEREDUR_FORMALISER_SCRIPT", FORMALISER_SCRIPT_PATH);
    // Checked unconditionally (not assert()) since the resolved path is a
    // machine-local one either way: the compiled-in default can go stale
    // between configure time and a run without CMake ever re-running, an
    // override can name anything at all, and assert() is a no-op in the
    // NDEBUG release/relwithdebinfo builds, which would otherwise spawn
    // `node` on a missing script and hang rather than fail. The path is
    // resolved once but re-checked per call, the file being able to go
    // missing under a live run.
    if (access(k_script.m_path.c_str(), F_OK) != 0) {
        throw std::runtime_error(
            "formaliser script not found: " + k_script.m_path +
            (k_script.m_from_env ? " (from PEREDUR_FORMALISER_SCRIPT)" : ""));
    }
    return k_script.m_path;
#else
    assert(false);
    return "";
#endif
}

std::vector<std::string> formaliser_command() {
    return {"node",      formaliser_script_path(),
            "formalize", "--logic",
            "ft-inf",    "--batch"};
}

PersistentProcess::PersistentProcess(std::vector<std::string> command)
    : m_command(std::move(command)) {}

PersistentProcess::~PersistentProcess() {
    if (!m_spawned) {
        return;
    }
    // Closing the write end sends EOF on the child's stdin, which a
    // well-behaved --batch CLI treats as "no more requests" and exits on.
    close(m_write_fd);
    // Bounded rather than a blocking wait4: a node process wedged mid-request
    // never acts on the EOF, and teardown must not hang on it. Past the grace
    // period reap_with_grace kills the whole process group.
    //
    // The CPU total is sampled once here rather than per request: the child is
    // long-lived, so this is its whole-run user+sys CPU across every
    // formalise() call.
    RequirementFormaliser::total_cpu_s +=
        reap_with_grace(m_pid, k_shutdown_grace, "node", m_rss_floor_kb);
    close(m_read_fd);
}

void PersistentProcess::ensure_spawned() {
    if (m_spawned) {
        return;
    }
    // SurviveParentThread, unlike the one-shot tools: this child is spawned
    // lazily by whichever worker thread formalises first and then serves every
    // later caller, and PDEATHSIG would have the kernel kill it the moment that
    // one thread returned. The concurrency test catches exactly that, as a
    // second worker reading a response from a node process that is already
    // dead. The cost is that an abnormally killed PEREDUR leaves this child
    // behind; the destructor covers the normal path.
    //
    // SearchPath, because `node` is a general system interpreter looked up on
    // PATH, not a tool CMake resolves to an absolute path at build time like
    // ltl2tgba/ganak/black.
    const PipedChild child =
        spawn_piped_child(m_command, ParentDeathPolicy::SurviveParentThread,
                          ExecutableLookup::SearchPath);
    m_pid = child.m_pid;
    m_write_fd = child.m_write_fd;
    m_read_fd = child.m_read_fd;
    m_rss_floor_kb = child.m_rss_floor_kb;
    m_spawned = true;
}

namespace {

void write_all(int write_fd, const std::string& data) {
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t bytes_written =
            write(write_fd, data.data() + written, data.size() - written);
        if (bytes_written > 0) {
            written += static_cast<std::size_t>(bytes_written);
            continue;
        }
        if (bytes_written < 0 && errno == EINTR) {
            continue;
        }
        throw std::runtime_error("failed to write to formaliser process stdin");
    }
}

}  // namespace

std::string PersistentProcess::request(const std::string& line) {
    assert(line.find('\n') == std::string::npos);
    ensure_spawned();
    write_all(m_write_fd, line + "\n");

    // Buffered line read: a single read() may return more or less than one
    // full line, so leftover bytes past the first '\n' are kept for the
    // next call.
    while (true) {
        const auto newline_pos = m_read_buffer.find('\n');
        if (newline_pos != std::string::npos) {
            std::string result = m_read_buffer.substr(0, newline_pos);
            m_read_buffer.erase(0, newline_pos + 1);
            return result;
        }
        std::array<char, 4096> read_buf{};
        // Blocking read while m_proc_mutex (held by the caller) is locked is
        // intentional: the child's stdin/stdout are a single ordered
        // channel, so a response must be fully read before another caller's
        // request can be written, or replies could be attributed to the
        // wrong caller.
        const ssize_t bytes_read =
            read(  // NOLINT(clang-analyzer-unix.BlockInCriticalSection)
                m_read_fd, read_buf.data(), read_buf.size());
        if (bytes_read > 0) {
            m_read_buffer.append(read_buf.data(),
                                 static_cast<std::size_t>(bytes_read));
            continue;
        }
        if (bytes_read == 0) {
            throw std::runtime_error(
                "formaliser process closed stdout before responding");
        }
        if (errno == EINTR) {
            continue;
        }
        throw std::runtime_error("failed to read from formaliser process");
    }
}

RequirementFormaliser::RequirementFormaliser(std::vector<std::string> command)
    : m_proc(std::move(command)) {}

std::string RequirementFormaliser::formalise(
    const std::string& requirement_text) {
    {
        std::scoped_lock lock(m_cache_mutex);
        const auto found = m_cache.find(requirement_text);
        if (found != m_cache.end()) {
            n_cache_hits++;
            return found->second;
        }
        n_cache_misses++;
    }
    const auto start = std::chrono::steady_clock::now();
    std::string ltl;
    {
        std::scoped_lock lock(m_proc_mutex);
        ltl = m_proc.request(requirement_text);
    }
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    std::scoped_lock lock(m_cache_mutex);
    total_time_s += elapsed;
    m_cache.emplace(requirement_text, ltl);
    return ltl;
}

RequirementFormaliser& global_formaliser() {
    static RequirementFormaliser instance(formaliser_command());
    return instance;
}
