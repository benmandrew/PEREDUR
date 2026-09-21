#pragma once

/// @file pipeline.hpp
/// @brief One generation as an ordered list of named stages: GenerationContext,
///        PipelineStage, and the stage list a generation runs through.

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "config.hpp"
#include "genetic/nsga2.hpp"
#include "genetic/operators.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"

/// Which representation of the population is live. A generation begins and ends
/// scored; between breeding and scoring the individuals have no fitness yet, so
/// the stages either side of that boundary cannot share one element type.
enum class PopulationView : std::uint8_t { Scored, Unscored };

/// True for the selection schemes that rank by the NSGA-II crowded-comparison
/// order rather than the blended fitness scalar.
inline bool uses_nsga2_ranking(const Config& cfg) {
    return cfg.selection_scheme == SelectionScheme::Nsga2Truncate ||
           cfg.selection_scheme == SelectionScheme::Nsga2Apportion;
}

/// What ended a run, reported as `stopped_by` in the run manifest so a survival
/// analysis can tell a completed run from a censored one without inferring it
/// from the wall time.
enum class StopReason : std::uint8_t { Generations, Individuals, Deadline };

/// The run-level half of termination: the budgets that span generations rather
/// than living inside one.
///
/// One instance per run, shared by every generation and -- under
/// `[tlsf] repair_mode = "muc"` -- by every core the loop evolves, since the
/// budget belongs to the run and run_muc restarts the generation count per
/// core. Inactive unless a config asks for it, and then it costs one comparison
/// against the parent per bred offspring plus one steady_clock read.
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
    /// budget is, since the manifest reports the count on every run, and under
    /// `[tlsf] repair_mode = "muc"` it accumulates over every core rather than
    /// restarting with the generation index.
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

    /// True once `max_wall_s` has passed, whatever the individuals budget
    /// says. FRETISH MUC repair stops gating and screening on this alone: an
    /// individuals budget is spent by the search, and reading it there would
    /// leave every later candidate unjudged. Strictly greater, matching
    /// AuRUS's `current.toSeconds() > TIMEOUT`.
    [[nodiscard]] bool past_deadline() const {
        if (m_max_wall_s == 0) {
            return false;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                 Clock::now() - m_start)
                                 .count();
        return elapsed > 0 && static_cast<std::size_t>(elapsed) > m_max_wall_s;
    }

   private:
    [[nodiscard]] bool individuals_spent() const {
        return m_max_individuals != 0 && m_bred >= m_max_individuals;
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
/// Nsga2Truncate and Nsga2Apportion. The sort is stable in both cases so a
/// fixed RNG seed is reproducible.
template <typename Spec>
void order_population(const Config& cfg,
                      std::vector<Scored<Spec>>& population) {
    if (uses_nsga2_ranking(cfg)) {
        nsga2_sort(population);
        return;
    }
    std::stable_sort(population.begin(), population.end(),
                     [](const Scored<Spec>& lhs, const Scored<Spec>& rhs) {
                         return lhs.fitness > rhs.fitness;
                     });
}

namespace generation_detail {

constexpr std::size_t k_rate_granularity = 1'000'000;

inline bool probability_check(double rate, const RandomSource& random_source) {
    if (rate <= 0.0) {
        return false;
    }
    if (rate >= 1.0) {
        return true;
    }
    return random_source.next_index(k_rate_granularity) <
           static_cast<std::size_t>(rate *
                                    static_cast<double>(k_rate_granularity));
}

/// Breeds @p offspring_n new specifications from the fittest parents. Each slot
/// starts as parent i, is crossed with a random parent drawn from the top
/// @p top_n with probability crossover_rate, mutated with probability
/// mutation_rate, then simplified if the operator set provides a simplifier.
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
template <typename Spec>
std::vector<Spec> breed_offspring(const Config& cfg,
                                  const std::vector<Scored<Spec>>& sorted_pop,
                                  std::size_t offspring_n, std::size_t top_n,
                                  const GeneticOperators<Spec>& ops,
                                  const RandomSource& random_source,
                                  SearchBudget* budget) {
    std::vector<Spec> offspring_pop;
    offspring_pop.reserve(offspring_n);
    // Read once: an inactive budget must not reach the loop body at all, or an
    // unbudgeted run pays a specification comparison and a clock read per slot.
    const bool budgeted = budget != nullptr && budget->active();
    for (std::size_t i = 0; i < offspring_n; ++i) {
        // Tested between slots rather than after the whole generation, which is
        // where AuRUS tests it: its mutation and crossover loops both carry
        // `&& !terminate` and call checkTermination() per offspring. Stopping
        // at the generation boundary instead would overshoot the budget by up
        // to one generation's offspring, 20% of a 1000-individual cap at the
        // shipping population size.
        if (budgeted && budget->exhausted()) {
            break;
        }
        Spec offspring = sorted_pop[i].specification;
        if (probability_check(cfg.crossover_rate, random_source)) {
            const std::size_t partner = random_source.next_index(top_n);
            offspring =
                ops.crossover(offspring, sorted_pop[partner].specification,
                              random_source, cfg);
        }
        if (probability_check(cfg.mutation_rate, random_source)) {
            offspring = ops.mutate(offspring, random_source, cfg);
        }
        // Counted before simplification, and only when the slot actually
        // produced something new: AuRUS increments on `!chromosome.equals(
        // mutated)` and on crossover offspring distinct from the first parent,
        // so a slot whose two draws both declined is free on both sides.
        if (budgeted && !(offspring == sorted_pop[i].specification)) {
            budget->count_offspring();
        }
        offspring_pop.push_back(ops.simplify
                                    ? ops.simplify(std::move(offspring))
                                    : std::move(offspring));
    }
    return offspring_pop;
}

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
                      const GeneticOperators<Spec>& ops,
                      const RandomSource& random_source, ScoreFn score,
                      SearchBudget* budget)
        : m_cfg(cfg),
          m_parents(parents),
          m_ops(ops),
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

    /// Distinct specifications in whichever representation is currently live.
    ///
    /// The population is largely repeats of a handful of specifications -- the
    /// duplication the replicate selection scheme exists to undo -- and the
    /// sizes above cannot show it, since a stage that drops nothing still
    /// changes how many distinct individuals it holds. Hashes the whole
    /// population, so run_generation_pipeline calls it only with an observer
    /// attached.
    [[nodiscard]] std::size_t distinct_population_size() const {
        std::unordered_set<Spec> seen;
        seen.reserve(population_size());
        if (m_view == PopulationView::Scored) {
            for (const Scored<Spec>& scored : m_scored) {
                seen.insert(scored.specification);
            }
        } else {
            seen.insert(m_candidates.begin(), m_candidates.end());
        }
        return seen.size();
    }

    const Config& m_cfg;
    /// The incoming population in its original order, retained for (mu+lambda)
    /// pooling in the selection stage.
    const std::vector<Scored<Spec>>& m_parents;
    const GeneticOperators<Spec>& m_ops;
    const RandomSource& m_random_source;
    ScoreFn m_score;
    /// The run's budget, or null for a run that has none. Owned by the driver,
    /// not by the generation, so it accumulates across generations and across
    /// the cores of a MUC-mode run.
    SearchBudget* m_budget;

    std::size_t m_target_size;
    std::size_t m_top_n;
    std::size_t m_elite_n;
    std::size_t m_offspring_n;

    /// Parents ordered best-first, kept past breeding so the elites can be
    /// restored from it.
    std::vector<Scored<Spec>> m_sorted_parents;
    /// The unscored population, live from breeding until scoring.
    std::vector<Spec> m_candidates;
    /// Breeding's output, before any filter ran. See stage_filter_fallback.
    std::vector<Spec> m_pre_filter;
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

/// What a consumer learns about one completed stage.
struct StageObservation {
    std::string name;
    std::size_t n_in{0};
    std::size_t n_out{0};
    /// Distinct specifications among the n_out, or 0 where it was not measured.
    std::size_t distinct{0};
    double elapsed_s{0.0};
};

/// Invoked after each stage completes, in stage order.
using StageObserver = std::function<void(const StageObservation&)>;

/// Screens a rescued population, returning the part of it fit to breed from.
/// Built by correctness_rescue() and read only by stage_filter_fallback; a null
/// one rescues nothing, so the fallback restores the offspring unscreened.
template <typename Spec>
using PopulationRescue =
    std::function<std::vector<Spec>(const std::vector<Spec>&)>;

namespace pipeline_detail {

template <typename Spec>
void stage_order_parents(GenerationContext<Spec>& ctx) {
    // Parents are selected from the whole population, unfiltered: the filters
    // screen only the offspring bred below.
    order_population(ctx.m_cfg, ctx.m_scored);
}

template <typename Spec>
void stage_breed(GenerationContext<Spec>& ctx) {
    ctx.m_sorted_parents = std::move(ctx.m_scored);
    ctx.m_scored.clear();
    ctx.m_candidates = generation_detail::breed_offspring(
        ctx.m_cfg, ctx.m_sorted_parents, ctx.m_offspring_n, ctx.m_top_n,
        ctx.m_ops, ctx.m_random_source, ctx.m_budget);
    // The filter chain is judged as a whole rather than filter by filter, so
    // the unfiltered offspring have to survive until every filter has run.
    ctx.m_pre_filter = ctx.m_candidates;
    ctx.m_view = PopulationView::Unscored;
}

/// Rescues the generation when the filters between breeding and here rejected
/// every offspring, an outcome that would otherwise end the run. The test is on
/// the combined result, not on each filter in turn: a filter that empties the
/// population is fine as long as an earlier or later one does not, which is why
/// this cannot be folded into the filter stages themselves.
///
/// What comes back is screened. @p rescue re-applies the correctness filters
/// alone to the unfiltered offspring, so the rescue re-admits the candidates
/// dropped for being duplicates or oversized and none of the candidates dropped
/// for being unfit to breed from. Restoring the offspring wholesale instead
/// would trade correctness for survival at the exact moment correctness is
/// being enforced hardest: the sharper the filter, the more often it empties
/// the population, so the rescue fires precisely where it would undo the most
/// work.
///
/// When the rescue itself comes back empty, no correct candidate was bred at
/// all this generation. The elites are then the whole of the next population:
/// they bypass the filters by construction and are already the best the run has
/// found, so carrying them alone beats re-admitting candidates already known to
/// be unfit. Only a run with no elites at all restores the offspring
/// unscreened, and only because the alternative is an empty population, which
/// stage_pad cannot pad and the run cannot continue from.
template <typename Spec>
void stage_filter_fallback(GenerationContext<Spec>& ctx,
                           const PopulationRescue<Spec>& rescue) {
    if (ctx.m_candidates.empty()) {
        if (rescue) {
            ctx.m_candidates = rescue(ctx.m_pre_filter);
        }
        if (ctx.m_candidates.empty() && ctx.m_elite_n == 0) {
            ctx.m_candidates = std::move(ctx.m_pre_filter);
        }
    }
    ctx.m_pre_filter.clear();
}

/// Elites bypass crossover, mutation, and the offspring filters: the top
/// elite_n specifications carry over unchanged so the best candidates are never
/// lost to a stochastic operator or removed by a filter.
template <typename Spec>
void stage_restore_elites(GenerationContext<Spec>& ctx) {
    for (std::size_t i = 0; i < ctx.m_elite_n; ++i) {
        ctx.m_candidates.push_back(ctx.m_sorted_parents[i].specification);
    }
}

template <typename Spec>
void stage_pad(GenerationContext<Spec>& ctx) {
    assert(!ctx.m_candidates.empty());
    generation_detail::pad_to_size(ctx.m_candidates, ctx.m_target_size);
}

template <typename Spec>
void stage_score(GenerationContext<Spec>& ctx) {
    ctx.m_scored = ctx.m_score(ctx.m_candidates);
    ctx.m_candidates.clear();
    ctx.m_view = PopulationView::Scored;
}

template <typename Spec>
void stage_select(GenerationContext<Spec>& ctx) {
    if (!uses_nsga2_ranking(ctx.m_cfg)) {
        order_population(ctx.m_cfg, ctx.m_scored);
        return;
    }
    // (mu + lambda) survivor selection: pool the incoming parents with the
    // freshly scored offspring, rank the union by the crowded-comparison order,
    // and keep the best target_size. This is NSGA-II's elitism, so no
    // non-dominated candidate is ever lost; padding duplicates carry zero
    // crowding distance and are shed first. Parents keep their cached objective
    // vectors, so pooling adds no re-scoring.
    //
    // The pool takes m_parents, in their original order, not m_sorted_parents:
    // nsga2_sort is stable, so pre-sorting the parents would settle ties
    // between equally-ranked candidates differently.
    std::vector<Scored<Spec>> pool = ctx.m_parents;
    pool.insert(pool.end(), std::make_move_iterator(ctx.m_scored.begin()),
                std::make_move_iterator(ctx.m_scored.end()));

    const bool apportion =
        ctx.m_cfg.selection_scheme == SelectionScheme::Nsga2Apportion;
    if (apportion) {
        // Ranking the distinct specifications rather than the ~target_size
        // slots holding them stops the truncation below from cutting
        // arbitrarily through the rank-0 front.
        pool = generation_detail::dedup_by_specification(std::move(pool));
    }
    nsga2_sort(pool);

    if (pool.size() > ctx.m_target_size) {
        pool.resize(ctx.m_target_size);
    } else if (apportion && pool.size() < ctx.m_target_size) {
        // Deduplication alone would leave too few slots to breed from, so
        // selection pressure is re-expressed as replication multiplicity.
        pool = generation_detail::replicate_to_size(pool, ctx.m_target_size);
    }
    ctx.m_scored = std::move(pool);
}

}  // namespace pipeline_detail

/// The ordered stages of one generation, with @p filter_stages spliced in
/// between breeding and the filter fallback. A caller that adds a filter gets a
/// new entry in the returned list without this function changing, which is what
/// lets a consumer derive the stage set rather than hardcode it.
template <typename Spec>
std::vector<PipelineStage<Spec>> make_generation_pipeline(
    std::vector<PipelineStage<Spec>> filter_stages,
    PopulationRescue<Spec> rescue = nullptr) {
    constexpr std::size_t k_fixed_stages = 6;
    std::vector<PipelineStage<Spec>> stages;
    stages.reserve(filter_stages.size() + k_fixed_stages);
    stages.emplace_back("order-parents",
                        pipeline_detail::stage_order_parents<Spec>);
    stages.emplace_back("breed", pipeline_detail::stage_breed<Spec>);
    for (PipelineStage<Spec>& stage : filter_stages) {
        stages.push_back(std::move(stage));
    }
    stages.emplace_back("filter-fallback", [rescue = std::move(rescue)](
                                               GenerationContext<Spec>& ctx) {
        pipeline_detail::stage_filter_fallback(ctx, rescue);
    });
    stages.emplace_back("restore-elites",
                        pipeline_detail::stage_restore_elites<Spec>);
    stages.emplace_back("pad", pipeline_detail::stage_pad<Spec>);
    stages.emplace_back("score", pipeline_detail::stage_score<Spec>);
    stages.emplace_back("select", pipeline_detail::stage_select<Spec>);
    return stages;
}

/// Runs @p stages in order over @p ctx, reporting each completed stage to
/// @p observe, and returns the resulting scored population.
template <typename Spec>
std::vector<Scored<Spec>> run_generation_pipeline(
    GenerationContext<Spec>& ctx,
    const std::vector<PipelineStage<Spec>>& stages,
    const StageObserver& observe = nullptr) {
    for (const PipelineStage<Spec>& stage : stages) {
        const auto start = std::chrono::steady_clock::now();
        stage(ctx);
        if (observe) {
            const double elapsed = std::chrono::duration<double>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();
            observe({stage.name(), stage.n_in(), stage.n_out(),
                     ctx.distinct_population_size(), elapsed});
        }
    }
    return std::move(ctx.m_scored);
}
