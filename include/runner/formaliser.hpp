#pragma once

/// @file formaliser.hpp
/// @brief Wrapper around the external FRET requirement-formaliser CLI: a
///        persistent Node.js process started once and reused, reading one
///        requirement per stdin line and writing one LTL formula per stdout
///        line, unlike the one-process-per-call spot/ganak/black wrappers.

#include <sys/types.h>  // NOLINT(build/include_order) — pid_t

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/// Returns the filesystem path to fretCLI.main.js: `PEREDUR_FORMALISER_SCRIPT`
/// when that environment variable is set and non-empty, and otherwise the
/// FORMALISER_SCRIPT_PATH preprocessor definition baked in at build time. The
/// environment is read once, on first use.
///
/// Unlike the other runner/ tool paths, this one has no baked-in default: it
/// is a machine-local path that must be supplied explicitly at configure time.
/// Throws std::runtime_error when the resolved path does not exist, naming the
/// environment variable where that is what supplied it.
std::string formaliser_script_path();

/// Returns the argv used to launch the formaliser: `node` invoked on
/// formaliser_script_path() with the fixed `formalize --logic ft-inf
/// --batch` arguments that select the persistent line-by-line protocol.
/// `node` is looked up via PATH (see PersistentProcess::ensure_spawned)
/// rather than a build-time-resolved path, since it's a general system
/// interpreter rather than a tool CMake fetches/builds.
std::vector<std::string> formaliser_command();

/// Owns a long-lived child process's stdin/stdout pipes across many
/// request/response round trips, rather than spawning fresh per call like
/// the other runner/ wrappers. Not internally synchronised: a single
/// in-order request/response channel has no way to tell two concurrent
/// callers' responses apart, so callers must serialise their own access
/// (RequirementFormaliser::m_proc_mutex does this).
class PersistentProcess {
   public:
    explicit PersistentProcess(std::vector<std::string> command);
    ~PersistentProcess();

    PersistentProcess(const PersistentProcess&) = delete;
    PersistentProcess& operator=(const PersistentProcess&) = delete;

    /// Writes `line` (must not contain '\n') followed by a newline to the
    /// child's stdin, then reads and returns one line of the child's
    /// response from stdout. Spawns the child lazily on the first call.
    /// Throws std::runtime_error if the child has exited/closed its stdout
    /// before producing a response.
    std::string request(const std::string& line);

   private:
    void ensure_spawned();

    std::vector<std::string> m_command;
    bool m_spawned = false;
    pid_t m_pid = -1;
    int m_write_fd = -1;
    int m_read_fd = -1;
    /// Kept from the spawn for the destructor's reap, which is the only place
    /// this child's peak resident set is read. See ProcessResult.
    std::uint64_t m_rss_floor_kb = 0;
    std::string m_read_buffer;
};

/// Formalises requirement text into LTL via the persistent CLI process,
/// memoising results by requirement text so repeated calls with identical
/// input incur no additional subprocess round trips.
class RequirementFormaliser {
   public:
    inline static std::size_t n_cache_hits = 0;
    inline static std::size_t n_cache_misses = 0;
    inline static double total_time_s = 0.0;
    /// Total user+sys CPU of the persistent node child, sampled once via wait4
    /// when the process is reaped at teardown (per-request attribution isn't
    /// possible for a long-lived child). Unlike the other tools this is a
    /// whole-run aggregate, not a per-call sum.
    inline static double total_cpu_s = 0.0;

    explicit RequirementFormaliser(std::vector<std::string> command);

    /// Formalises requirement_text (one line, no embedded '\n') into an LTL
    /// formula string. Cache hits never touch the subprocess; a cache miss
    /// takes m_proc_mutex for the full write-request/read-response round
    /// trip, since the child's stdin/stdout are a single ordered channel
    /// shared across all callers — interleaving two callers' writes would
    /// leave no way to match responses back to requests.
    std::string formalise(const std::string& requirement_text);

   private:
    std::mutex m_cache_mutex;
    std::unordered_map<std::string, std::string> m_cache;

    std::mutex m_proc_mutex;
    PersistentProcess m_proc;
};

/// Returns the process-lifetime RequirementFormaliser instance, constructed
/// with formaliser_command(). All callers that do not need test isolation
/// should use this instead of constructing their own, so they share the
/// memoisation cache and the single persistent child process.
RequirementFormaliser& global_formaliser();
