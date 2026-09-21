#pragma once

/// @file spot.hpp
/// @brief Wrappers for SPOT tools: ltl2tgba (automaton construction) and
///        ltlsynt (realizability checking), with memoising checker classes.

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "requirement.hpp"

/// Returns the directory containing SPOT tool binaries: `PEREDUR_SPOT_BIN_DIR`
/// when that environment variable is set and non-empty, and otherwise the
/// SPOT_BIN_DIR preprocessor definition baked in at build time. The
/// environment is read once, on first use, so a relocated build is pointed at
/// its tools by the environment it starts in rather than the one it was
/// compiled in.
std::string spot_bin_dir();

/// Returns the full filesystem path to the ltlsynt binary.
std::string ltlsynt_path();

/// Returns the full filesystem path to the ltl2tgba binary.
std::string ltl2tgba_path();

/// Runs ltl2tgba with -D (deterministic), -S (state-based acceptance), and
/// -H (HOA output) on the given LTL formula and returns the raw HOA text.
/// Asserts that the binary is accessible; the result is memoised by formula.
///
/// None of the three non-clean exits is an assertion. A non-zero exit throws,
/// and so does a timeout (see set_ltl2tgba_timeout), which drops the
/// individual. SPOT's exit 2 on a tautology is neither: the universal
/// automaton is substituted for it, because a formula accepting every trace
/// is a correct answer rather than a scoring failure (PR #29 — still unfixed
/// upstream).
std::string run_ltl2tgba_for_counting(const std::string& formula);

/// Per-call wall-clock budget for the ltl2tgba counting exec (process-global,
/// like RealizabilityChecker's ltlsynt timeout). A call exceeding it is killed
/// and raised as an error, so the offending individual is dropped rather than
/// stalling the run on an unbounded deterministic-automaton blowup. Zero (the
/// default) disables the timeout. Set once at startup from
/// Config::ltl2tgba_timeout.
void set_ltl2tgba_timeout(std::chrono::milliseconds timeout);

struct Ltl2tgbaStats {
    inline static std::size_t n_cache_hits = 0;
    inline static std::size_t n_cache_misses = 0;
    inline static double total_time_s = 0.0;
    /// Child-process CPU time (user+sys), from wait4(); unlike total_time_s
    /// (wall) it excludes time the parent spends blocked waiting on the child.
    inline static double total_cpu_s = 0.0;
    /// ltl2tgba exit-2-on-tautology results substituted with the universal
    /// automaton (see run_ltl2tgba_for_counting) rather than raised as errors.
    inline static std::size_t n_tautology_substitutions = 0;
    /// Counting calls abandoned at the per-call timeout (raised as errors, so
    /// the individual is dropped). Counts execs, not calls: the abandonment is
    /// memoised, and a repeat of the same formula re-raises it as a cache hit
    /// without paying the budget again.
    inline static std::size_t n_timeouts = 0;
};

/// The two conjunct sets a specification formula was built from: those whose
/// strengthening weakens the formula, and those whose strengthening
/// strengthens it. Supplying them lets the checker answer some queries by
/// comparison rather than by exec; see RealizabilityChecker's subsumption
/// table.
///
/// A conjunct may carry a tag, separated from the formula by a unit separator
/// (`\x1f`). The formula half is compared canonically, so a re-spelling of one
/// is still the same conjunct; the tag is compared verbatim. TLSF needs it,
/// one formula meaning different things in PRESET and in GUARANTEE.
///
/// @p m_scope names everything else that has to match for two queries to be
/// comparable at all; the checker adds the input/output partition to it. TLSF
/// puts its semantics there, strict and non-strict lowering to different
/// shapes.
struct SpecificationSides {
    std::vector<std::string> m_assumptions;
    std::vector<std::string> m_guarantees;
    std::string m_scope;
};

class RealizabilityChecker {
   public:
    inline static std::size_t n_cache_misses = 0;
    inline static std::size_t n_cache_hits = 0;
    /// Queries the subsumption table settled without an exec. Counted apart
    /// from n_cache_hits because they are a different claim: a hit is the same
    /// question asked twice, and one of these is a question never asked, whose
    /// answer follows from one that was.
    inline static std::size_t n_subsumed = 0;
    inline static double total_time_s = 0.0;
    inline static double total_cpu_s = 0.0;
    /// ltlsynt calls abandoned at the per-call timeout, reported as nullopt.
    /// Counts execs, not calls: the undecided outcome is memoised like any
    /// other, so a formula that times out costs one exec however often the
    /// search revisits it.
    inline static std::size_t n_timeouts = 0;
    /// ltlsynt calls that ran to completion and reported a limit of its own
    /// rather than a verdict, reported as nullopt like a timeout. Counted
    /// separately because the two say different things: a timeout is a budget
    /// this run chose, and this is a ceiling the tool was built with.
    inline static std::size_t n_capability_errors = 0;

    /// Checks whether the specification is realizable using ltlsynt. Returns
    /// true (realizable), false (unrealizable), or nullopt (timed out, so
    /// undecided). Results are memoised by the full specification formula, so
    /// repeated calls with identical inputs incur no additional tool
    /// invocations.
    std::optional<bool> check_realizability(const Specification& specification);

    /// Realizability of a raw LTL formula with the given input/output atom
    /// partition (mode-agnostic core shared by the FRETISH and TLSF front
    /// ends). Memoised by the formula verbatim together with the atom
    /// partition; the formula is deliberately *not* normalised first, for the
    /// reason given where check_realizability_ltl is defined in spot.cpp.
    ///
    /// nullopt means undecided rather than unrealizable, and the distinction
    /// is the caller's to resolve: "unrealizable" is the safe reading where a
    /// true answer admits a repair, and the unsafe one for the well-separation
    /// filter, which keeps a candidate exactly when the query comes back
    /// unrealizable. Spell the choice with value_or at each call site.
    std::optional<bool> check_realizability_ltl(
        const std::string& ltl_formula, const std::vector<std::string>& inputs,
        const std::vector<std::string>& outputs);

    /// As above, with the conjunct sets the formula was assembled from.
    ///
    /// Realizability is monotone in both sides: a realizable specification
    /// stays realizable when a guarantee is dropped or an assumption added,
    /// and an unrealizable one stays unrealizable in the other direction. So a
    /// query standing in the right relation to one already decided is answered
    /// by comparison. This is worth 18.8% of the ltlsynt execs a run makes,
    /// pooled over 14 specifications, and the figure splits hard by front end:
    /// 39.7 to 51.6% on FRETISH, where the minimal-realizable-set walk asks
    /// about growing subsets of components by construction, against 4.9 to
    /// 17.8% on TLSF, where the queries are mutations of one another rather
    /// than subsets.
    ///
    /// Only decided entries subsume: an undecided one is memoised, but it
    /// carries no direction and so answers nothing.
    ///
    /// Supplying sides is sound wherever every conjunct sits at a determinate
    /// polarity in the lowered formula. The FRETISH lowering is the easy case,
    /// being the two conjunctions joined by an implication. The TLSF one wraps
    /// whole sections rather than conjuncts and needed checking rather than
    /// assuming. Over
    ///
    ///     theta_e -> (theta_s & [strict: psi_s W !psi_e]
    ///                 & ((G psi_e & phi_e) -> ([!strict: G psi_s] & phi_s)))
    ///
    /// the three environment sections (INITIALLY, REQUIRE, ASSUME) are all
    /// negative and the three system ones (PRESET, ASSERT, GUARANTEE) all
    /// positive. REQUIRE is the one that looks like a counterexample and is
    /// not: it occurs twice, in `G psi_e` under the inner antecedent and in
    /// `!psi_e` under a weak-until, and the negation flips the second back, so
    /// both occurrences are negative and strengthening it weakens the formula
    /// either way. The empty-section cases agree, introducing a section
    /// introducing the term it governs in the same direction.
    ///
    /// So both front ends pass sides. What the TLSF one must also pass is a
    /// section tag per conjunct, one formula meaning different things in
    /// PRESET and in GUARANTEE, and its semantics as the scope.

    std::optional<bool> check_realizability_ltl(
        const std::string& ltl_formula, const std::vector<std::string>& inputs,
        const std::vector<std::string>& outputs,
        const SpecificationSides& sides);

    /// Per-call wall-clock budget for the ltlsynt exec (process-global, shared
    /// by every RealizabilityChecker). A call exceeding it is killed and
    /// reported as undecided — ltlsynt has no internal timeout, and the genetic
    /// search occasionally generates synthesis queries that run for minutes.
    /// Zero (the default) disables the timeout. Set once at startup from
    /// Config::ltlsynt_timeout.
    static void set_timeout(std::chrono::milliseconds timeout);

   private:
    /// One decided query, with its two sides interned to integer ids so that
    /// the inclusion tests are over small sorted vectors rather than over
    /// strings, and with a 64-bit signature of each side so that most entries
    /// are rejected by one AND.
    struct Decided {
        std::vector<int> m_assumptions;
        std::vector<int> m_guarantees;
        std::uint64_t m_assumption_signature = 0;
        std::uint64_t m_guarantee_signature = 0;
        bool m_realizable = false;
    };

    std::optional<bool> subsumed_verdict(
        const std::string& scope, const std::vector<int>& assumptions,
        const std::vector<int>& guarantees, std::uint64_t assumption_signature,
        std::uint64_t guarantee_signature) const;
    std::vector<int> intern_side(const std::vector<std::string>& conjuncts);

    mutable std::mutex m_cache_mutex;
    /// Canonical conjunct to id, and the decided queries over those ids. Both
    /// live under m_cache_mutex, being read and written on the same paths as
    /// the memo itself.
    std::unordered_map<std::string, int> m_conjunct_ids;
    /// Decided queries by scope. Two queries over different atom partitions,
    /// or different TLSF semantics, are over different formula shapes, and
    /// nothing follows from one about the other, so they never meet.
    std::unordered_map<std::string, std::vector<Decided>> m_decided;
    /// Undecided (nullopt) is memoised alongside the decided verdicts, as
    /// black's satisfiability cache does: ltlsynt is deterministic and its
    /// call durations are sharply bimodal, so a formula that blew the budget
    /// once is in the minutes-long tail rather than near the boundary, and
    /// re-asking it re-pays the budget for the same non-answer. Caching it is
    /// safe in a way caching a fabricated `false` was not, because nullopt is
    /// not a verdict: each caller still resolves it in its own direction.
    std::unordered_map<std::string, std::optional<bool>> m_cache;
};

/// Returns the process-lifetime RealizabilityChecker instance. All callers
/// that do not need test isolation should use this instead of constructing
/// their own, so they share the memoisation cache.
RealizabilityChecker& global_real_checker();

/// What the startup budget screen found, for the run manifest to record.
/// Process-global for the same reason InputScreen is: the manifest is written
/// by a driver that no longer holds the specification. Set once, before the
/// search starts.
struct BudgetScreen {
    /// Wall time of the input specification's own realizability query, in
    /// milliseconds. Negative when no screen ran, which is the case when the
    /// budget is unlimited.
    inline static std::int64_t observed_ms = -1;
    /// Whether that query came back decided. False means every realizability
    /// query the run makes is likely to be abandoned too.
    inline static bool decided = true;
};

/// Times one realizability query on the run's own input and reports whether the
/// configured budget can decide it. Returns the warning to print, or an empty
/// string when there is nothing to say.
///
/// A specification whose own query exceeds @p budget does not lose the 0.1% of
/// calls the budget was tuned to shed (see Config::ltlsynt_timeout): it loses
/// all of them. Every candidate scores undecided, so the status objective is a
/// constant that still costs a full budget per distinct candidate, and the
/// final gate rejects even a correct repair. The failure is invisible from
/// outside -- the run completes, reports no repairs, and reads as a search that
/// found nothing.
///
/// A warning rather than a rejection, following the input screen: the budget
/// may be deliberate, and the other three objectives still work under it. The
/// verdict lands in the manifest so a campaign can partition on it rather than
/// grep a log.
///
/// The query is memoised like any other, so the screen costs nothing beyond the
/// call the run was about to make anyway. It is skipped when @p budget is zero,
/// since an unlimited budget cannot fail to decide.
///
/// @param query  Realizability of the run's input specification, as the calling
///               front end phrases it
/// @param budget The configured per-call ltlsynt budget
std::string ltlsynt_budget_screen(
    const std::function<std::optional<bool>()>& query,
    std::chrono::milliseconds budget);
