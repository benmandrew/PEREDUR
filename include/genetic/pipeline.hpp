#pragma once

/// @file pipeline.hpp
/// @brief One generation as an ordered list of named stages: GenerationContext,
///        PipelineStage, and the stage list a generation runs through.
///
/// The stage and breeding templates are defined in src/genetic/pipeline.cpp
/// and instantiated there for Specification and tlsf::Specification only.

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "config.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"

class DiversityState;

/// Which representation of the population is live. A generation begins and ends
/// scored; between breeding and scoring the individuals have no fitness yet, so
/// the stages either side of that boundary cannot share one element type.
enum class PopulationView : std::uint8_t { Scored, Unscored };

/// True for the selection schemes that rank by the NSGA-II crowded-comparison
/// order rather than the blended fitness scalar.
inline bool uses_nsga2_ranking(const Config& cfg) {
    return cfg.selection_scheme == SelectionScheme::Nsga2Apportion;
}

/// What ended a run, reported as `stopped_by` in the run manifest so a survival
/// analysis can tell a completed run from a censored one without inferring it
/// from the wall time.
enum class StopReason : std::uint8_t { Generations, Individuals, Deadline };

/// The run-level half of termination: the budgets that span generations rather
/// than living inside one.
///
/// One instance per run, shared by every generation. Inactive unless a config
/// asks for it, and then it costs one comparison against the parent per bred
/// offspring plus one steady_clock read.
class SearchBudget {
   public:
    using Clock = std::chrono::steady_clock;

    SearchBudget(const Config& cfg, Clock::time_point start)
        : m_max_individuals(cfg.termination == TerminationMode::Individuals
                                ? cfg.max_individuals
                                : 0),
          m_max_wall_s(cfg.max_wall_s),
          m_start(start) {}

    /// True when this budget can end a run at all. Breeding skips both the
    /// parent comparison and the clock read when it cannot, so a run under the
    /// default configuration pays nothing for the mechanism and draws from the
    /// RandomSource exactly as it did before this existed.
    [[nodiscard]] bool active() const {
        return m_max_individuals != 0 || m_max_wall_s != 0;
    }

    /// Counts one offspring that differs from the parent it was bred from.
    void count_offspring() { ++m_bred; }

    /// Counts one completed generation. Called by the driver whatever the
    /// budget is, since the manifest reports the count on every run.
    void count_generation() { ++m_generations; }

    [[nodiscard]] std::size_t generations() const { return m_generations; }

    /// Seconds since the run's search began, on the same clock the deadline is
    /// measured against and the same origin as the manifest's `wall_s`. Read by
    /// the repair accumulator, which stamps each candidate with the moment it
    /// passed the gate.
    [[nodiscard]] double elapsed_s() const {
        return std::chrono::duration<double>(Clock::now() - m_start).count();
    }

    /// Offspring bred, meaningful only under an active budget: an inactive one
    /// skips the parent comparison that decides whether a slot counted, so the
    /// manifest reports null rather than a figure that would read as zero.
    [[nodiscard]] std::size_t bred() const { return m_bred; }

    [[nodiscard]] bool exhausted() const {
        return individuals_spent() || past_deadline();
    }

    /// Which criterion ended the run, given @p fallback for a run that spent
    /// neither of these. Individuals is tested first because AuRUS tests it
    /// first, so a run that trips both reports the same reason there.
    [[nodiscard]] StopReason reason(StopReason fallback) const {
        if (individuals_spent()) {
            return StopReason::Individuals;
        }
        if (past_deadline()) {
            return StopReason::Deadline;
        }
        return fallback;
    }

   private:
    [[nodiscard]] bool individuals_spent() const {
        return m_max_individuals != 0 && m_bred >= m_max_individuals;
    }

    // Strictly greater, matching AuRUS's `current.toSeconds() > TIMEOUT`.
    [[nodiscard]] bool past_deadline() const {
        if (m_max_wall_s == 0) {
            return false;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                 Clock::now() - m_start)
                                 .count();
        return elapsed > 0 && static_cast<std::size_t>(elapsed) > m_max_wall_s;
    }

    std::size_t m_max_individuals;
    std::size_t m_max_wall_s;
    Clock::time_point m_start;
    std::size_t m_bred = 0;
    std::size_t m_generations = 0;
};

/// Orders a scored population best-first according to @p cfg's selection
/// scheme: descending weighted fitness for WeightedAverage, or the NSGA-II
/// crowded-comparison order (front rank ascending, crowding descending) for
/// Nsga2Apportion. Random sorts by the weighted scalar too, since this order
/// numbers the written repairs; its search never reads it, as the parent and
/// survivor stages shuffle instead. The sort is stable in every case so a
/// fixed RNG seed is reproducible.
template <typename Spec>
void order_population(const Config& cfg, std::vector<Scored<Spec>>& population);

/// Where a bred offspring came from: the index of its parent in the sorted
/// population breed_offspring read, and how the offspring relates to that
/// parent under classify_move. Recorded only for the parent-verdict stage.
struct OffspringOrigin {
    std::size_t parent = 0;
    /// Nothing where the parent's status already proves it realizable, since
    /// the stage then has no use for the order and classify_move is skipped.
    std::optional<MoveOrder> order;
};

/// What the parent-verdict stage did over a run
/// (Config::parent_verdict_filter). Reported under `--diagnostics` and in
/// run.json.
struct ParentVerdictStats {
    /// Offspring dropped as proved strengthenings of a parent the memo shows
    /// to be unrealizable.
    inline static std::atomic<std::size_t> n_dropped{0};
    /// Offspring proved stronger than their parent whose parent had a status
    /// below realizable but no decided verdict, and so were kept.
    inline static std::atomic<std::size_t> n_unproved{0};
};

/// The name the parent-verdict stage reports under, in the stage list and in
/// the filter report.
inline constexpr const char* k_parent_verdict_stage = "parent-verdict";

namespace generation_detail {

/// True with probability @p rate. Draws only for a rate strictly between 0
/// and 1.
bool probability_check(double rate, const RandomSource& random_source);

/// Breeds @p offspring_n new specifications from the fittest parents. Each slot
/// starts as parent i, is crossed with a random parent drawn from the top
/// @p top_n with probability crossover_rate, mutated with probability
/// mutation_rate, then simplified. The operators are the FRETISH ones or the
/// TLSF ones, chosen by @p Spec at compile time.
///
/// The per-slot draw sequence is fixed so a seed reproduces the generation, and
/// this must stay a single unit for that reason: crossover and mutation are
/// interleaved per slot, so hoisting either into a pass of its own over the
/// whole population reorders every draw after the first slot. The determinism
/// test suite pins the resulting stream.
///
/// Returns fewer than @p offspring_n specifications when @p budget runs out
/// part-way through, which is the only way it returns short. A null or inactive
/// budget cannot, so the default configuration always fills every slot.
///
/// FRETISH mutation is handed a ParentContext built from the parent's Scored
/// entry, @p status_objective naming the status in its objective vector and
/// @p original lent to it unchanged. Building it draws nothing.
///
/// Where @p origins is given, each FRETISH offspring's OffspringOrigin is
/// appended to it, in the order of the returned offspring. Classifying draws
/// nothing either.
///
/// @p diversity, where given on the FRETISH path, lends the diversity keys'
/// run state to mutation: the slot coverage under Config::slot_coverage, and
/// under Config::flip_tabu each parent's flipped slots, with each mutated
/// offspring held for its lineage until the generation is scored.
template <typename Spec>
std::vector<Spec> breed_offspring(
    const Config& cfg, const std::vector<Scored<Spec>>& sorted_pop,
    std::size_t offspring_n, std::size_t top_n,
    const RandomSource& random_source, SearchBudget* budget,
    const Spec* original = nullptr,
    std::optional<std::size_t> status_objective = std::nullopt,
    std::vector<OffspringOrigin>* origins = nullptr,
    DiversityState* diversity = nullptr);

/// Cycles through the existing survivors to pad the population back up to
/// @p target_size. Padding duplicates carry zero crowding distance, so later
/// selection sheds them first. Requires a non-empty @p survivors.
template <typename Spec>
void pad_to_size(std::vector<Spec>& survivors, std::size_t target_size) {
    const std::size_t survivor_count = survivors.size();
    survivors.reserve(target_size);
    while (survivors.size() < target_size) {
        survivors.push_back(survivors[survivors.size() % survivor_count]);
    }
}

/// Puts @p population in a uniformly random order (Fisher-Yates), for
/// SelectionScheme::Random. Each draw is sequenced into its own local.
template <typename T>
void shuffle_population(std::vector<T>& population,
                        const RandomSource& random_source) {
    for (std::size_t i = population.size(); i > 1; --i) {
        const std::size_t pick = random_source.next_index(i);
        std::swap(population[i - 1], population[pick]);
    }
}

/// Drops every repeat of a specification already present earlier in
/// @p population, preserving the order of the first occurrences.
template <typename Spec>
std::vector<Scored<Spec>> dedup_by_specification(
    std::vector<Scored<Spec>> population) {
    std::unordered_set<Spec> seen;
    seen.reserve(population.size());
    std::vector<Scored<Spec>> distinct;
    distinct.reserve(population.size());
    for (Scored<Spec>& scored : population) {
        if (seen.insert(scored.specification).second) {
            distinct.push_back(std::move(scored));
        }
    }
    return distinct;
}

/// The survivor step of SelectionScheme::Nsga2Apportion: hands the @p
/// target_size slots out over a deduplicated, crowded-comparison-ordered @p
/// distinct population. Each individual is weighted 1 / (1 + rank); every one
/// keeps at least one slot and the remaining target_size - distinct.size() are
/// apportioned by the largest-remainder (Hamilton) method over the normalised
/// weights, breaking remainder ties towards the better-ranked individual.
/// Copies are emitted contiguously in the input order, so the result stays
/// sorted best-first for breed_offspring.
///
/// Purely arithmetic: it draws no random numbers, so a seeded run stays
/// reproducible. Requires a non-empty @p distinct no longer than @p
/// target_size.
template <typename Spec>
std::vector<Scored<Spec>> replicate_to_size(
    const std::vector<Scored<Spec>>& distinct, std::size_t target_size) {
    assert(!distinct.empty());
    assert(distinct.size() <= target_size);
    const std::size_t count = distinct.size();
    const std::size_t spare = target_size - count;

    std::vector<double> weights(count);
    double total_weight = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        weights[i] = 1.0 / (1.0 + static_cast<double>(distinct[i].rank));
        total_weight += weights[i];
    }

    std::vector<std::size_t> copies(count, 1);
    std::vector<double> remainders(count, 0.0);
    std::size_t allocated = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const double quota =
            static_cast<double>(spare) * weights[i] / total_weight;
        const double whole = std::floor(quota);
        const auto share = static_cast<std::size_t>(whole);
        copies[i] += share;
        remainders[i] = quota - whole;
        allocated += share;
    }

    std::vector<std::size_t> order(count);
    for (std::size_t i = 0; i < count; ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(),
                     [&remainders](std::size_t lhs, std::size_t rhs) {
                         return remainders[lhs] > remainders[rhs];
                     });
    for (std::size_t i = 0; allocated < spare; ++i, ++allocated) {
        ++copies[order[i % count]];
    }

    std::vector<Scored<Spec>> replicated;
    replicated.reserve(target_size);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t copy = 0; copy < copies[i]; ++copy) {
            replicated.push_back(distinct[i]);
        }
    }
    assert(replicated.size() == target_size);
    return replicated;
}

}  // namespace generation_detail

/// The mutable state one generation is threaded through.
///
/// Both representations of the population live here because the stages are not
/// type-uniform: breeding turns scored parents into unscored offspring and
/// scoring turns them back. Exactly one is live at a time, given by m_view, and
/// population_size() reports whichever it is so a stage can be measured without
/// knowing which side of that boundary it sits on.
///
/// Scoring arrives as a callable rather than a fitness object so the context
/// need not be templated on the fitness type.
template <typename Spec>
struct GenerationContext {
    using ScoreFn =
        std::function<std::vector<Scored<Spec>>(const std::vector<Spec>&)>;

    GenerationContext(const Config& cfg,
                      const std::vector<Scored<Spec>>& parents,
                      std::size_t target_size, std::size_t elitism_size,
                      const RandomSource& random_source, ScoreFn score,
                      SearchBudget* budget)
        : m_cfg(cfg),
          m_parents(parents),
          m_random_source(random_source),
          m_score(std::move(score)),
          m_budget(budget),
          m_target_size(target_size),
          m_top_n(std::min(target_size, parents.size())),
          m_elite_n(std::min(elitism_size, m_top_n)),
          m_offspring_n(m_top_n - m_elite_n),
          m_scored(parents) {}

    /// Size of whichever representation is currently live.
    [[nodiscard]] std::size_t population_size() const {
        return m_view == PopulationView::Scored ? m_scored.size()
                                                : m_candidates.size();
    }

    const Config& m_cfg;
    /// The incoming population in its original order, retained for (mu+lambda)
    /// pooling in the selection stage.
    const std::vector<Scored<Spec>>& m_parents;
    const RandomSource& m_random_source;
    ScoreFn m_score;
    /// The run's budget, or null for a run that has none. Owned by the driver,
    /// not by the generation, so it accumulates across generations and across
    /// the cores of a MUC-mode run.
    SearchBudget* m_budget;
    /// The specification the run repairs, lent to breeding; null when the
    /// caller gave none.
    const Spec* m_original = nullptr;
    /// Where the status sits in each candidate's objective vector, or nothing
    /// when the run scores none. Read by breeding to build a ParentContext.
    std::optional<std::size_t> m_status_objective;
    /// The run's diversity state (diversity.hpp), lent to every generation of
    /// a FRETISH run; null on the TLSF path and where the caller gives none.
    DiversityState* m_diversity = nullptr;

    std::size_t m_target_size;
    std::size_t m_top_n;
    std::size_t m_elite_n;
    std::size_t m_offspring_n;
    /// Where the status objective sits in each objective vector, set only
    /// where maximality demotion is on and the run registers that objective.
    std::optional<std::size_t> m_status_index;

    /// Parents ordered best-first, kept past breeding so the elites can be
    /// restored from it.
    std::vector<Scored<Spec>> m_sorted_parents;
    /// The unscored population, live from breeding until scoring.
    std::vector<Spec> m_candidates;
    /// Breeding's output, before any filter ran. See stage_filter_fallback.
    std::vector<Spec> m_pre_filter;
    /// Where each entry of m_pre_filter came from, recorded only for the
    /// parent-verdict stage and empty otherwise.
    std::vector<OffspringOrigin> m_origins;
    /// The scored population, live at the start of a generation and from
    /// scoring onwards.
    std::vector<Scored<Spec>> m_scored;

    PopulationView m_view = PopulationView::Scored;
};

/// A named, instrumented step of one generation.
///
/// Deliberately mirrors FilterFunctionT's interface -- name(), n_in(), n_out()
/// -- so a consumer can walk the stage list and report every stage without
/// knowing which stages exist or how many there are.
template <typename Spec>
class PipelineStage {
   public:
    using Fn = std::function<void(GenerationContext<Spec>&)>;

    PipelineStage(std::string name, Fn func)
        : m_name(std::move(name)), m_fn(std::move(func)) {}

    void operator()(GenerationContext<Spec>& ctx) const {
        m_n_in = ctx.population_size();
        m_fn(ctx);
        m_n_out = ctx.population_size();
    }

    [[nodiscard]] const std::string& name() const { return m_name; }
    [[nodiscard]] std::size_t n_in() const { return m_n_in; }
    [[nodiscard]] std::size_t n_out() const { return m_n_out; }

   private:
    std::string m_name;
    Fn m_fn;
    mutable std::size_t m_n_in{0};
    mutable std::size_t m_n_out{0};
};

/// One filter's input and output population sizes, summed over every
/// generation it ran in: a row of the filter report.
struct FilterRunStats {
    std::string name;
    std::size_t total_in = 0;
    std::size_t total_out = 0;
};

/// What a consumer learns about one completed stage.
struct StageObservation {
    std::string name;
    std::size_t n_in{0};
    std::size_t n_out{0};
};

/// Invoked after each stage completes, in stage order.
using StageObserver = std::function<void(const StageObservation&)>;

/// A realizability verdict for a parent, settled without launching a process,
/// or nothing where none is known. See stage_parent_verdict.
template <typename Spec>
using ParentVerdictLookup = std::function<std::optional<bool>(const Spec&)>;

/// Screens a rescued population, returning the part of it fit to breed from.
/// Built by correctness_rescue() and read only by stage_filter_fallback; a null
/// one rescues nothing, so the fallback restores the offspring unscreened.
template <typename Spec>
using PopulationRescue =
    std::function<std::vector<Spec>(const std::vector<Spec>&)>;

namespace pipeline_detail {

/// Drops each offspring proved Stronger than its parent where @p verdict shows
/// that parent unrealizable. Realizability is closed under weakening, so a
/// strengthening of an unrealizable specification is unrealizable too, and
/// scoring one spends a population slot on a candidate the output gate would
/// refuse anyway. Breeding recorded each offspring's origin in m_origins,
/// aligned with m_pre_filter; the candidates have since been deduplicated, so
/// they are matched to it by value.
///
/// A status below 1 is no proof, since an ill-separated realizable candidate
/// scores below 1 too, and neither is an undecided query. @p verdict must
/// therefore be a decided answer, and is asked once per parent with a proved
/// strengthening among its offspring. The stage draws nothing.
template <typename Spec>
void stage_parent_verdict(GenerationContext<Spec>& ctx,
                          const ParentVerdictLookup<Spec>& verdict);

}  // namespace pipeline_detail

/// The ordered stages of one generation, with @p filter_stages spliced in
/// between breeding and the filter fallback. A caller that adds a filter gets a
/// new entry in the returned list without this function changing, which is what
/// lets a consumer derive the stage set rather than hardcode it.
template <typename Spec>
std::vector<PipelineStage<Spec>> make_generation_pipeline(
    std::vector<PipelineStage<Spec>> filter_stages,
    PopulationRescue<Spec> rescue = nullptr);

/// Runs @p stages in order over @p ctx, reporting each completed stage to
/// @p observe, and returns the resulting scored population.
template <typename Spec>
std::vector<Scored<Spec>> run_generation_pipeline(
    GenerationContext<Spec>& ctx,
    const std::vector<PipelineStage<Spec>>& stages,
    const StageObserver& observe = nullptr);
