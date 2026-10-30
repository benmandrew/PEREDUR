#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "thread_pool.hpp"

/// Selection scheme driving parent and survivor selection during evolution.
/// Nsga2Apportion (the default) ranks by Pareto non-domination and crowding
/// distance over the individual objectives. Its survivor step deduplicates the
/// pooled (mu + lambda) parents and offspring, ranks the distinct set, and
/// apportions the target size's slots over it by 1 / (1 + rank) under the
/// largest-remainder (Hamilton) method, so breeding pressure follows rank
/// without discarding any distinct candidate. WeightedAverage ranks by the
/// blended fitness scalar; it converges prematurely and is kept for
/// comparison.
enum class SelectionScheme : std::uint8_t { WeightedAverage, Nsga2Apportion };

/// How the status objective grades the region below realizability. Mrs (the
/// default) is the greedy maximal-realizable-subset fraction: the guarantee
/// side is split into parts, and the score is the fraction that can be kept
/// while the subset stays realizable against the full environment side.
///
/// Aurus is AuRUS's own six-level ladder, reproduced value for value (see
/// @ref status_score_aurus) so that an ablation can cross PEREDUR's grading
/// against it. It grades by which side of the specification survives on its
/// own, and it is the one scale that does not fold well-separation into its
/// realizability query, as AuRUS never asks that question.
enum class StatusGrading : std::uint8_t { Mrs, Aurus };

/// Which order the MRS walk admits guarantee-side parts in (see
/// @ref status_score_mrs). Spec is the index order; Degree is
/// @ref conflict_degree_order, computed once on the input specification and
/// replayed on every candidate, at n(n-1)/2 + n subset queries. Either is
/// constant across a run, which the cache and seed reproducibility need.
enum class MrsAdmissionOrder : std::uint8_t { Spec, Degree };

/// What ends the search (see @ref Config::termination).
///
/// Generations is PEREDUR's own budget: `Config::generations` rounds, whatever
/// they cost. Individuals is AuRUS's, so that the two tools can be given the
/// same budget: its GA stops once `numberOfVisitedIndividuals` reaches
/// `GA_MAX_NUM_INDIVIDUALS`, counted per offspring admitted and checked
/// between offspring. A cap in generations cannot be converted to one in
/// individuals afterwards, as the offspring per generation depend on
/// `population_size`, `selection_rate` and `elitism_rate`.
///
/// `Config::generations` bounds the run under both, as `GA_GENERATIONS` does
/// for AuRUS, and @ref Config::max_wall_s is independent of the choice.
enum class TerminationMode : std::uint8_t { Generations, Individuals };

struct Config {
    std::size_t generations = 10;
    /// Offspring budget for the whole run under
    /// `termination = "individuals"`, ignored under Generations. Zero is
    /// rejected in that mode rather than meaning unlimited.
    ///
    /// An offspring counts when it differs from the parent it was bred from,
    /// which is what AuRUS counts, so a slot whose draws both declined costs
    /// nothing on either side.
    std::size_t max_individuals = 0;
    /// Wall-clock deadline for the whole run in seconds; zero is no deadline.
    /// Independent of @ref termination, and honoured under both.
    ///
    /// The search stops at the first generation boundary or bred offspring past
    /// the deadline and then writes its output normally. A run killed from
    /// outside leaves no `run.json`. Filters, scoring and the final gate are
    /// not interrupted, so a run overruns by what its generation had left.
    std::size_t max_wall_s = 0;
    std::size_t population_size = 200;
    /// Objective weights for the weighted scalar, matching AuRUS's
    /// `Settings.java`: STATUS_FACTOR 0.7, LOST_MODELS_FACTOR and
    /// WON_MODELS_FACTOR 0.1 each (together PEREDUR's one semantic objective),
    /// and SYNTACTIC_FACTOR 0.1. Only the weighted scheme ranks by the scalar.
    double fitness_weight_syntactic = 0.1;
    double fitness_weight_semantic = 0.2;
    double fitness_weight_status = 0.7;
    /// How the status objective grades below realizability (see StatusGrading).
    StatusGrading status_grading = StatusGrading::Mrs;

    /// Which budget ends the search (see TerminationMode).
    TerminationMode termination = TerminationMode::Generations;
    /// Which order the MRS walk admits parts in (see MrsAdmissionOrder). Read
    /// only under StatusGrading::Mrs.
    MrsAdmissionOrder mrs_admission_order = MrsAdmissionOrder::Degree;
    std::size_t default_model_counting_bound = 20;
    bool run_implication_filter = true;
    std::chrono::milliseconds black_timeout{1000};
    /// Per-call wall-clock budget for ltlsynt realizability checks; 0 disables
    /// it. ltlsynt has no internal timeout, and the search occasionally builds
    /// queries that run for minutes. A call over budget is killed and reported
    /// as undecided: no repair is admitted on it, and the well-separation check
    /// rejects the candidate. An undecided verdict is memoised like any other
    /// (see RealizabilityChecker), so a formula abandoned once stays undecided
    /// for the rest of the run.
    std::chrono::milliseconds ltlsynt_timeout{10'000};
    /// Per-call wall-clock budget for the ltl2tgba model-counting exec; 0
    /// disables it. The deterministic (-D) construction can blow up
    /// super-exponentially on deeply nested formulae (multi-GB, minutes to
    /// hours). A call over budget is killed and the individual is dropped,
    /// counted against max_scoring_failure_rate. Looser than the ltlsynt
    /// budget because a legitimate determinisation can take tens of seconds.
    std::chrono::milliseconds ltl2tgba_timeout{60'000};
    /// Per-call wall-clock budget for each ltlfilt exec. Its passes can blow up
    /// on the deep nested-X conjunctions the search builds, and an abandoned
    /// call costs one undecided query, never an individual.
    std::chrono::milliseconds ltlfilt_timeout{10'000};
    /// Print the CPU-attribution report (own code against the external tools,
    /// via getrusage and per-tool wait4). Set by `peredur --cpu-report` alone:
    /// it asks about one interactive run rather than about the search, so it is
    /// not a TOML key.
    bool report_cpu_timing = false;
    /// Print the filter report and the engine-internal counters at exit. Set
    /// by `peredur --diagnostics` alone, like report_cpu_timing. Every figure
    /// is also written to `<output-dir>/run.json` on every successful run.
    bool report_diagnostics = false;
    /// Follow the closing count with what each written repair changed: the
    /// formulae it dropped and the ones it put in their place. Set by
    /// `peredur --repair-summary` alone, like report_cpu_timing.
    bool report_repair_summary = false;
    /// Stream per-stage and per-generation progress to
    /// `<output-dir>/progress.jsonl` and copy the dashboard page beside it.
    /// Off by default, since a campaign pays for the file and its flushes with
    /// nobody watching. `--dashboard` turns it on for one run.
    bool dashboard = false;
    SelectionScheme selection_scheme = SelectionScheme::Nsga2Apportion;
    double selection_rate = 0.5;
    /// The top elitism_rate fraction of the population carries over into the
    /// next generation verbatim, bypassing crossover, mutation and the
    /// offspring filters. Must be strictly less than selection_rate, as the
    /// elites are a subset of the selected parents. The output gate still
    /// screens what elites carry in.
    double elitism_rate = 0.1;
    double crossover_rate = 0.1;
    double mutation_rate = 1.0;
    double p_trigger = 0.5;
    double p_response = 0.5;
    double p_timing = 0.15;
    /// FRETISH only: per-requirement probability of moving the condition type
    /// along its implication order. Continual implies Trigger for every scope
    /// and every timing, so strengthening picks Continual and weakening
    /// Trigger. The two coincide at `always` under a plain scope and at
    /// `eventually` under an `only` one, and differ strictly elsewhere.
    double p_condition_type = 0.15;
    /// FRETISH only: per-requirement probability of moving the scope along its
    /// implication order. That order depends on the timing and the condition
    /// type, since a scope boundary relaxes a bounded obligation but tightens
    /// an unbounded one; scope_edges in src/requirement.cpp holds the table. A
    /// move onto a non-Global scope needs a declared mode.
    double p_scope = 0.15;
    /// FRETISH only: per-requirement probability of rewriting the stop
    /// condition of an `until` or `before` timing. The stop occurs positively
    /// in `until` and negatively in `before`, so a directional rewrite moves it
    /// the opposite way under `before`. The arm is read only on a requirement
    /// holding a stop timing, before the RandomSource is touched, so it costs
    /// no draw on a specification without one.
    double p_stop = 0.15;
    /// Probability that a rewrite is a *monotone* one (monotone_rewrite,
    /// include/genetic/monotone.hpp), whose result is comparable to the formula
    /// it replaces under implication. The general rewriters leave the
    /// implication order.
    ///
    /// On the TLSF path it is a third rewrite arm for a chosen section formula,
    /// offered ahead of the temporal and propositional ones, and the direction
    /// is a fair coin. On the FRETISH path it is offered inside the p_response
    /// and p_trigger arms, so it changes which rewrite fires and not how often
    /// one does, and the direction is the requirement's own. The FRETISH arm
    /// sits out a field whose polarity in the lowered requirement is not
    /// determinate: a trigger's rising edge `(!c & Xc)` holds its condition at
    /// both polarities, and `after n ticks` does the same to its response.
    double p_monotone = 0.25;
    /// Shared by both modes: append a new environment assumption (over input
    /// atoms) rather than rewriting an existing requirement or formula. This
    /// repairs unrealizability that needs a stronger environment (such as a
    /// fairness assumption), which the rewrite-only operators cannot express.
    double p_add_assumption = 0.05;
    /// The mirror of p_add_assumption, shared by both modes: delete one
    /// guarantee rather than rewriting it. Some repairs are unreachable
    /// without it. A deleted guarantee is tombstoned in place rather than
    /// erased, so that comparisons against the original stay aligned; see
    /// Requirement::m_removed.
    ///
    /// The operators are a cascade with early return, p_add_assumption tested
    /// first, so the realised rate is 0.95 × 0.05 = 0.0475. The probability is
    /// read before the RNG is drawn, so at 0 the search runs exactly as it did
    /// before the operator existed.
    double p_remove_guarantee = 0.05;
    /// Of the assumptions p_add_assumption appends, the fraction guarded by a
    /// random input atom rather than by `true`. `G F <input>` is strictly
    /// stronger than `G(c -> F <input>)`, so the unconditional form keeps the
    /// majority of the draw.
    double p_conditional_assumption = 0.25;
    /// TLSF-mode mutation: probability of mutating an assumption-side section
    /// (INITIALLY/REQUIRE/ASSUME) rather than a guarantee-side one
    /// (PRESET/ASSERT/GUARANTEE). The guarantee side takes the complement.
    double tlsf_p_assumption = 0.3;
    /// TLSF-mode mutation: once a section formula has been chosen for
    /// rewriting, the probability of applying the temporal-structure mutation
    /// (which may insert, drop, or swap X/F/G/U/R/W operators, following
    /// Brizzio et al.) rather than the skeleton-preserving propositional
    /// rewrite. At 0 the temporal skeleton of existing formulae is never
    /// altered.
    double tlsf_p_temporal = 0.2;
    /// TLSF-mode mutation: of the assumptions p_add_assumption appends, the
    /// fraction that are a copy of an existing live ASSUME conjunct rather
    /// than a fresh one built from the template. Ordinary mutation then edits
    /// the copy on later generations. The template emits at most seven nodes,
    /// far fewer than the assumption-shaped ideals, but it is the only form
    /// that can introduce a fairness property no existing assumption carries.
    /// Falls back to the template when the ASSUME section holds nothing live
    /// to copy.
    double tlsf_p_clone_assumption = 0.25;

    /// Scoring thread pool size. The default is available_parallelism(), which
    /// is the hardware concurrency narrowed by the CPU affinity mask and the
    /// cgroup CPU quota where those apply -- so a containerised run is sized by
    /// what the container was given rather than by what the host has.
    std::size_t parallel = available_parallelism();
    /// How many steps past its current one the MRS walk queries ahead, under
    /// both outcomes of each step it has not yet settled, on pool workers that
    /// would otherwise sit idle (see status_score_mrs). 0 turns it off. The
    /// walk still asks every query it asked before, in the same order, so the
    /// score is unchanged; what moves is wall time, and the ltlsynt execs
    /// spent on the branches the walk does not take.
    ///
    /// Depth d keeps up to 2^(d+1) - 2 queries in flight beside the walk's
    /// own, and lets the walk settle up to d + 1 steps per query time where it
    /// settled one. Each deeper level doubles the queries for one more step,
    /// and only one query per level is ever used. Values above 8 read as 8.
    std::size_t mrs_speculation_depth = 2;
    /// A fitness function that throws (in practice an external tool failing on
    /// one evolved formula) costs that individual rather than the whole run.
    /// Above this fraction of a generation the tooling is broken rather than
    /// the formula, and the run aborts. A single failure is always tolerated,
    /// whatever the population size. The ltl2tgba budget drops individuals by
    /// design, and on the heavy TLSF specifications enough land in one
    /// generation to trip a 5% bound.
    double max_scoring_failure_rate = 0.15;
};

/// Applies every per-tool timeout in `cfg` to the process-global runner
/// singletons. Drivers call this rather than setting the tools they remember,
/// so none runs unbounded whatever the config says.
void apply_tool_timeouts(const Config& cfg);
