#pragma once

/// @file black.hpp
/// @brief Wrapper around the black LTL satisfiability checker, with a
///        memoising SatisfiabilityChecker and a process-lifetime global
///        instance.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

/// Returns the path to the black binary: `PEREDUR_BLACK_PATH` when that
/// environment variable is set and non-empty, and otherwise the
/// BLACK_EXECUTABLE_PATH preprocessor definition baked in at build time. The
/// environment is read once, on first use.
std::string black_executable_path();

/// Which answer the caller expects, which is what decides whether a query SPOT
/// could not settle is worth escalating to black.
///
/// The two backends fail in opposite directions. black is a bounded model
/// checker, so it finds a model quickly and proves that none exists only by
/// exhausting a completeness bound; SPOT builds an automaton and checks
/// emptiness, at a cost set by the automaton's size rather than by the answer.
/// Escalating an `ExpectSat` query therefore has something to gain -- black is
/// fast on exactly the satisfiable formulae whose automata are large -- while
/// escalating an `ExpectUnsat` one black could only answer by exhausting a
/// bound SPOT already failed to build spends a second subprocess to reach the
/// same non-answer.
///
/// This is a property of the call site rather than of the formula, so it costs
/// nothing to compute: an implication check is `A & !B` and answers UNSAT
/// exactly when the implication holds, while a vacuity or validity screen
/// answers SAT on all but the candidates it exists to catch. Predicting it
/// from the formula instead would mean predicting the verdict, which is the
/// question being asked.
enum class QueryPolarity : std::uint8_t {
    /// Vacuity, validity and status queries. Escalates to black.
    ExpectSat,
    /// Implication checks, phrased `A & !B`. Does not escalate.
    ExpectUnsat,
};

class SatisfiabilityChecker {
   public:
    inline static std::atomic<std::size_t> n_cache_misses{0};
    inline static std::atomic<std::size_t> n_cache_hits{0};
    /// Calls answered from ltlfilt's constant folding without consulting black.
    inline static std::atomic<std::size_t> n_constant_folded{0};
    /// Calls abandoned because the formula carries a weak-until or
    /// strong-release operator that could not be rewritten away, and black is
    /// unsound on those. Reported as indeterminate, like a timeout. Non-zero
    /// means queries are going unanswered -- check that ltlfilt is reachable.
    inline static std::atomic<std::size_t> n_weak_operator_unresolved{0};
    /// Queries settled by SPOT without reaching black. Expected to be nearly
    /// every cache miss; a low figure means ltlfilt is unreachable or the
    /// population has drifted into automaton-hostile formulae.
    inline static std::atomic<std::size_t> n_spot_decided{0};
    /// black subprocesses actually launched, which is no longer the same as
    /// the cache-miss count: only an `ExpectSat` query SPOT left undecided
    /// gets one.
    inline static std::atomic<std::size_t> n_black_calls{0};
    /// Queries SPOT left undecided that were not escalated, because their
    /// polarity says black would exhaust its bound and answer nothing.
    inline static std::atomic<std::size_t> n_escalations_declined{0};
    inline static std::size_t n_timeouts = 0;
    inline static double total_time_s = 0.0;
    /// Child-process CPU time (user+sys), from wait4(); unlike total_time_s
    /// (wall) it excludes time the parent spends blocked waiting on the child.
    inline static double total_cpu_s = 0.0;

    /// Returns true (SAT), false (UNSAT), or nullopt (timed out / unknown).
    ///
    /// SPOT is asked first and settles almost everything; `polarity` decides
    /// whether a query it could not settle is escalated to black. Callers that
    /// omit it get `ExpectSat`, which escalates -- the conservative default,
    /// since a spurious escalation costs one subprocess and a missing one
    /// costs an answer.
    std::optional<bool> check_satisfiability(
        const std::string& ltl_formula,
        QueryPolarity polarity = QueryPolarity::ExpectSat);

    void set_timeout(std::chrono::milliseconds timeout) { m_timeout = timeout; }

    /// SPOT's own per-query budget, before any escalation to black. 500ms is
    /// tuned for the search loop; an offline implication sweep wants the same
    /// budget it gives black, since an `ExpectUnsat` query SPOT leaves
    /// undecided is never escalated and reads as "keep both".
    void set_spot_budget(std::chrono::milliseconds budget) {
        m_spot_budget = budget;
    }

   private:
    std::chrono::milliseconds m_timeout{1000};
    std::chrono::milliseconds m_spot_budget{500};
    /// Cache lookups (the common case once the population converges) take a
    /// shared lock so concurrent hits don't serialise on one another; only an
    /// actual insert needs the exclusive lock.
    mutable std::shared_mutex m_cache_mutex;
    std::unordered_map<std::string, std::optional<bool>> m_cache;
};

/// Returns the process-lifetime SatisfiabilityChecker instance. All callers
/// that do not need test isolation should use this instead of constructing
/// their own, so they share the memoisation cache.
SatisfiabilityChecker& global_sat_checker();
