#pragma once

/// @file profile.hpp
/// @brief Always-compiled, opt-in scope profiler recording wall and per-thread
///        CPU time per named site.
///
/// The existing per-tool counters (LtlfiltStats, RealizabilityChecker, ...)
/// report how long each *external tool* took. They cannot say where that time
/// went inside a call — a fork that copies page tables and an ltlsynt that
/// solves a hard game both land in the same "0.013s avg". This adds the
/// orthogonal axis: named scopes that nest, each recording wall time and the
/// calling thread's CPU time.
///
/// Read the two against each other. A site with large wall and near-zero CPU
/// is blocked on a child process, not computing: `proc/read` sits at a cpu/wall
/// ratio of about 0.01 on a real run. That ratio is the diagnostic.
///
/// Off unless the PEREDUR_PROFILE environment variable is set (to a file path
/// for JSON output, or to "1"/"-" for the stderr table only). Disabled, a scope
/// costs one relaxed atomic load and a branch.

#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace profile {

/// One instrumented source location. Accumulators are atomic rather than
/// mutex-guarded because scoring runs the same site on every pool worker at
/// once; a lock here would serialise exactly the parallelism being measured.
///
/// Sites are heap-allocated and never freed: site() appends to the registry
/// under a mutex and nothing ever removes from it, so a Site pointer stays
/// valid for the life of the process and a reader can sample these counters
/// without racing a destruction. Lookup is a linear strcmp scan of that
/// registry rather than a hash, which is why registration is meant to happen
/// once per source location and not once per call.
struct Site {
    const char* m_name;
    std::atomic<std::uint64_t> m_calls{0};
    std::atomic<std::uint64_t> m_wall_ns{0};
    std::atomic<std::uint64_t> m_cpu_ns{0};
    /// Wall time of the single slowest call, for separating "many small" from
    /// "one pathological" without keeping a histogram.
    std::atomic<std::uint64_t> m_max_wall_ns{0};

    explicit Site(const char* name) : m_name(name) {}
};

/// True when PEREDUR_PROFILE is set. Read once at first use and cached.
bool enabled();

/// Registers (or returns) the site named @p name. Intended to be called from a
/// function-local static initialiser, so the registry lock is paid once per
/// source location rather than once per call.
Site& site(const char* name);

/// As site(), for a name only known at run time -- a fitness objective's
/// registered name, say, rather than a source location. The string is interned
/// and outlives the caller's copy, so the Site's name pointer stays valid.
/// Call it once and keep the reference; it is not cheap enough for a hot loop.
Site& site_interned(const std::string& name);

/// Adds @p n to a free-standing counter (no timing). For quantities that are
/// not durations: bytes read, cache entries, retries.
void add_count(const char* name, std::uint64_t n = 1);

/// Keeps the largest @p value ever recorded under @p name, instead of summing.
/// For a quantity whose total is meaningless but whose worst case is not — a
/// child's peak resident set is the case this exists for, where adding the
/// peaks of a thousand calls would produce a number describing nothing.
///
/// Shares the counter namespace with add_count, so a name must be one or the
/// other for the whole run; mixing them silently gives neither. The convention
/// is a `_max` suffix. Takes a std::string because these names are built per
/// tool at run time, and the registry copies the key.
void record_max(const std::string& name, std::uint64_t value);

/// Every registered site, in registration order.
const std::vector<Site*>& sites();

/// Free-standing counters, as (name, value) pairs.
std::vector<std::pair<std::string, std::uint64_t>> counts();

/// Writes the human-readable table to @p out, sorted by total wall descending.
void report(std::ostream& out);

/// Writes the same data as JSON to @p path. A path that cannot be opened is
/// reported on stderr rather than raised: profiling must never fail a run.
void report_json(const std::string& path);

/// Writes the report to wherever PEREDUR_PROFILE points, and to stderr. A no-op
/// when disabled. Safe to call more than once.
void report_if_enabled();

/// Per-thread CPU nanoseconds, monotonic within a thread.
std::uint64_t thread_cpu_ns();

/// Steady-clock nanoseconds.
std::uint64_t wall_ns();

/// RAII timer: accumulates into a Site over its lifetime, sampling both wall
/// time and the calling thread's CPU time.
class Scope {
   public:
    explicit Scope(Site& target)
        : m_site(enabled() ? &target : nullptr),
          m_wall_start(m_site != nullptr ? wall_ns() : 0),
          m_cpu_start(m_site != nullptr ? thread_cpu_ns() : 0) {}

    ~Scope() {
        if (m_site == nullptr) {
            return;
        }
        const std::uint64_t wall = wall_ns() - m_wall_start;
        const std::uint64_t cpu = thread_cpu_ns() - m_cpu_start;
        m_site->m_calls.fetch_add(1, std::memory_order_relaxed);
        m_site->m_wall_ns.fetch_add(wall, std::memory_order_relaxed);
        m_site->m_cpu_ns.fetch_add(cpu, std::memory_order_relaxed);
        std::uint64_t prev_max =
            m_site->m_max_wall_ns.load(std::memory_order_relaxed);
        while (wall > prev_max &&
               !m_site->m_max_wall_ns.compare_exchange_weak(
                   prev_max, wall, std::memory_order_relaxed)) {
        }
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

   private:
    Site* m_site;
    std::uint64_t m_wall_start;
    std::uint64_t m_cpu_start;
};

}  // namespace profile

#define PEREDUR_PROFILE_CONCAT_INNER(a, b) a##b
#define PEREDUR_PROFILE_CONCAT(a, b) PEREDUR_PROFILE_CONCAT_INNER(a, b)

/// Times the enclosing scope under @p name. @p name must be a string literal:
/// the Site is a function-local static, so registration happens once.
#define PEREDUR_PROFILE_SCOPE(name)                                        \
    static ::profile::Site& PEREDUR_PROFILE_CONCAT(prof_site_, __LINE__) = \
        ::profile::site(name);                                             \
    const ::profile::Scope PEREDUR_PROFILE_CONCAT(prof_scope_, __LINE__) { \
        PEREDUR_PROFILE_CONCAT(prof_site_, __LINE__)                       \
    }
