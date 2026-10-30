#pragma once

/// @file ltlfilt.hpp
/// @brief Wrapper for ltlfilt (SPOT): satisfiability, the weak-operator
///        rewrite black needs, and equivalence.

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

struct LtlfiltStats {
    /// rewrite_weak_operators' memo. n_remove_wm_execs is its exec count.
    inline static std::size_t n_remove_wm_hits = 0;
    inline static std::size_t n_remove_wm_execs = 0;
    /// spot_satisfiable, which is memoised nowhere -- check_satisfiability
    /// caches the decided answer one layer up, so a repeat never reaches it.
    /// Counted here because total_time_s covers these execs and nothing else
    /// did: reporting one pass's misses as ltlfilt's call count understated
    /// the true figure by 35.5% over a 14-specification corpus, and divided
    /// one tool's seconds by another set's calls.
    inline static std::size_t n_satisfiable_execs = 0;
    /// Every ltlfilt subprocess this process launched, over both counted entry
    /// points. total_time_s is over exactly this set.
    [[nodiscard]] static std::size_t n_execs() {
        return n_remove_wm_execs + n_satisfiable_execs;
    }
    inline static double total_time_s = 0.0;
    /// Child-process CPU time (user+sys), from wait4(); unlike total_time_s
    /// (wall) it excludes time the parent spends blocked waiting on the child.
    inline static double total_cpu_s = 0.0;
    /// Calls abandoned at the per-call timeout. Every caller treats these as
    /// inconclusive rather than as errors, so unlike the ltl2tgba and ltlsynt
    /// counters this one costs no individual: it measures how often a pass
    /// hit its blowup case.
    inline static std::size_t n_timeouts = 0;
};

/// Returns the full filesystem path to the ltlfilt binary.
std::string ltlfilt_path();

/// Per-call wall-clock budget for every ltlfilt exec (process-global, like the
/// ltlsynt and ltl2tgba budgets). A call exceeding it is killed and reported as
/// inconclusive: rewrite_weak_operators returns nothing and ltl_equivalent
/// returns true. Zero disables the timeout. Unlike the other tool budgets this
/// defaults to a non-zero value, because ltlfilt's passes can blow up on deep
/// nested-X conjunctions and an abandoned call costs one undecided query. Set
/// once at startup from Config::ltlfilt_timeout.
void set_ltlfilt_timeout(std::chrono::milliseconds timeout);

/// Whether `formula` carries a weak-until (`W`) or strong-release (`M`)
/// operator token. Purely lexical -- a bare `W`/`M` not flanked by identifier
/// characters -- so it costs nothing and can guard the subprocess below.
///
/// A single-letter atom actually named `W` or `M` reads as an operator here,
/// but such an atom is already ambiguous to every LTL parser in the pipeline,
/// so the false positive costs an indeterminate answer rather than a wrong one.
bool has_weak_operator(const std::string& formula);

/// Returns `formula` with the weak-until (`W`) and strong-release (`M`)
/// operators rewritten in terms of `U` and `R`, via `ltlfilt --remove-wm`.
/// std::nullopt if the binary is inaccessible, the call fails or times out, or
/// the result still carries either operator.
///
/// This never falls back to returning `formula` unchanged. Its caller needs
/// the rewrite to have actually happened: handing the original back would pass
/// black exactly the operator the rewrite exists to keep away from it, and a
/// wrong answer is worse than no answer. See check_satisfiability for what
/// black does with them.
///
/// The result is fully parenthesised (`-p`), which is not cosmetic. ltlfilt
/// otherwise emits SPOT's compact spellings -- `X!a`, `!Xf1` -- and black
/// rejects those as a syntax error, so the rewrite would trade a wrong answer
/// for no answer at all on most inputs.
///
/// Memoised on the canonical key: one subprocess per unique input.
std::optional<std::string> rewrite_weak_operators(const std::string& formula);

/// Decides satisfiability of `formula` via `ltlfilt --satisfiable`, which
/// translates to a Büchi automaton and checks emptiness. Returns true (SAT),
/// false (UNSAT), or std::nullopt when the answer is inconclusive: the binary
/// is inaccessible, the call exceeded `timeout`, or SPOT rejected the input.
///
/// Cost is independent of the verdict, which is what makes this complementary
/// to black rather than a drop-in replacement. Measured over 5,579 queries
/// taken from real runs it decided every one, p99 15ms and max 140ms, and it
/// stays flat where black does not: a validity check over an X-chain 640 deep
/// costs 30ms here and does not finish in 60s there. It has its own blowup
/// case in the opposite direction, on satisfiable formulae whose automaton is
/// large -- chained `<->` over `G F` terms, or period-2 counters -- so a
/// caller wanting an answer on those needs black behind it.
///
/// Deliberately not memoised: check_satisfiability caches the decided answer
/// under its own key, so a second cache here would hold the same verdicts
/// twice. Weak-until needs no rewriting on this path either, unlike the black
/// one -- SPOT emits `W` and `M` itself and reads back what it writes.
std::optional<bool> spot_satisfiable(const std::string& formula,
                                     std::chrono::milliseconds timeout);

/// Returns true if `lhs` and `rhs` are logically equivalent LTL formulae,
/// checked via `ltlfilt --equivalent-to`. This is a best-effort
/// cross-validation helper (e.g. for differential-testing the hand-rolled
/// LTL translator against another source of truth), not a correctness
/// boundary: if the binary is inaccessible or the check is otherwise
/// inconclusive (any exit status other than ltlfilt's own match/no-match
/// codes 0/1), it returns true rather than reporting a false mismatch.
bool ltl_equivalent(const std::string& lhs, const std::string& rhs);
