#include "genetic/pipeline.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "config.hpp"
#include "fitness/status.hpp"
#include "genetic/crossover.hpp"
#include "genetic/demotion.hpp"
#include "genetic/diversity.hpp"
#include "genetic/mutation.hpp"
#include "genetic/nsga2.hpp"
#include "genetic/order_crossover.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "genetic/simplify.hpp"
#include "tlsf/crossover.hpp"
#include "tlsf/mutation.hpp"
#include "tlsf/specification.hpp"

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

/// True when @p parent's status proves it realizable.
template <typename Spec>
bool status_proves_realizable(const Scored<Spec>& parent,
                              std::optional<std::size_t> status_objective) {
    return status_objective.has_value() &&
           *status_objective < parent.objectives.size() &&
           parent.objectives[*status_objective] >= k_status_realizable;
}

/// Appends to @p origins, where set, where the offspring @p offspring of the
/// parent at @p index came from. A parent whose status proves it realizable
/// is not classified, since the parent-verdict stage acts only on an
/// unrealizable one.
inline void record_origin(std::vector<OffspringOrigin>* origins,
                          const Scored<Specification>& parent,
                          std::size_t index, const Specification& offspring,
                          std::optional<std::size_t> status_objective) {
    if (origins == nullptr) {
        return;
    }
    OffspringOrigin origin{index, std::nullopt};
    if (!status_proves_realizable(parent, status_objective)) {
        origin.order = classify_move(parent.specification, offspring);
    }
    origins->push_back(origin);
}

/// What mutation is told about the FRETISH parent @p parent.
inline ParentContext parent_context(
    const Scored<Specification>& parent, bool crossed,
    const Specification* original,
    std::optional<std::size_t> status_objective) {
    ParentContext context;
    if (status_objective.has_value() &&
        *status_objective < parent.objectives.size()) {
        context.status = parent.objectives[*status_objective];
        context.realizable = *context.status >= k_status_realizable;
    }
    context.refused_guarantees = parent.refused_guarantees;
    context.crossed = crossed;
    context.original = original;
    return context;
}

bool probability_check(double rate, const RandomSource& random_source) {
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

/// The FRETISH order crossover of parents @p first and @p partner of @p
/// sorted_pop into @p offspring, returning the kind that ran, or nothing where
/// the arm did not fire or produced no child, leaving @p offspring alone.
///
/// The verdicts are read before the probability, and neither draws, so the
/// arm costs no draw at `p_order_crossover = 0` or on a pair whose verdicts
/// leave the operator unknown. The original's assumption count is not known
/// here, so it is taken as the fewest any parent carries: every candidate
/// keeps the original's slots, and pairing an appended assumption by index as
/// well is still sound, since each slot's bound is proved.
inline std::optional<OrderCrossoverKind> order_crossover_arm(
    const Config& cfg, const std::vector<Scored<Specification>>& sorted_pop,
    std::size_t first, std::size_t partner, const RandomSource& random_source,
    Specification& offspring) {
    if (cfg.p_order_crossover <= 0.0) {
        return std::nullopt;
    }
    const std::optional<std::size_t> status_index = status_objective_index(cfg);
    const ParentVerdict first_verdict =
        parent_verdict(sorted_pop[first], status_index, global_real_checker());
    if (first_verdict == ParentVerdict::Unknown) {
        return std::nullopt;
    }
    const std::optional<OrderCrossoverKind> kind = order_crossover_kind(
        first_verdict, parent_verdict(sorted_pop[partner], status_index,
                                      global_real_checker()));
    if (!kind.has_value() ||
        !probability_check(cfg.p_order_crossover, random_source)) {
        return std::nullopt;
    }
    std::size_t n_original_assumptions =
        sorted_pop[first].specification.m_assumptions.size();
    for (const Scored<Specification>& parent : sorted_pop) {
        n_original_assumptions = std::min(
            n_original_assumptions, parent.specification.m_assumptions.size());
    }
    OrderCrossoverOutcome outcome =
        order_crossover(*kind, sorted_pop[first].specification,
                        sorted_pop[partner].specification,
                        first_verdict == ParentVerdict::Realizable,
                        n_original_assumptions, random_source);
    if (!outcome.m_child.has_value()) {
        return std::nullopt;
    }
    offspring = std::move(*outcome.m_child);
    return kind;
}

/// The FRETISH crossover of @p offspring, parent @p first of @p sorted_pop,
/// with parent @p partner: the order crossover where it yields a child, else
/// the subformula graft. Returns the order crossover's kind where it ran.
inline std::optional<OrderCrossoverKind> fretish_crossover(
    const Config& cfg, const std::vector<Scored<Specification>>& sorted_pop,
    std::size_t first, std::size_t partner, const RandomSource& random_source,
    Specification& offspring) {
    std::optional<OrderCrossoverKind> kind = order_crossover_arm(
        cfg, sorted_pop, first, partner, random_source, offspring);
    if (!kind.has_value()) {
        offspring = crossover_specifications(
            offspring, sorted_pop[partner].specification, random_source);
    }
    return kind;
}

template <typename Spec>
std::vector<Spec> breed_offspring(const Config& cfg,
                                  const std::vector<Scored<Spec>>& sorted_pop,
                                  std::size_t offspring_n, std::size_t top_n,
                                  const RandomSource& random_source,
                                  SearchBudget* budget, const Spec* original,
                                  std::optional<std::size_t> status_objective,
                                  std::vector<OffspringOrigin>* origins) {
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
        bool crossed = false;
        [[maybe_unused]] std::optional<OrderCrossoverKind> order_kind;
        [[maybe_unused]] bool stepped = false;
        if (probability_check(cfg.crossover_rate, random_source)) {
            crossed = true;
            const std::size_t partner = random_source.next_index(top_n);
            const Spec& other = sorted_pop[partner].specification;
            if constexpr (std::is_same_v<Spec, tlsf::Specification>) {
                offspring = tlsf_crossover(offspring, other, random_source);
            } else {
                order_kind = fretish_crossover(cfg, sorted_pop, i, partner,
                                               random_source, offspring);
            }
        }
        if (probability_check(cfg.mutation_rate, random_source)) {
            if constexpr (std::is_same_v<Spec, tlsf::Specification>) {
                offspring = tlsf_mutate(offspring, random_source, cfg);
            } else {
                const ParentContext parent = parent_context(
                    sorted_pop[i], crossed, original, status_objective);
                offspring = mutate_specification(offspring, random_source, cfg,
                                                 &parent, &stepped);
            }
        }
        // Counted before simplification, and only when the slot actually
        // produced something new: AuRUS increments on `!chromosome.equals(
        // mutated)` and on crossover offspring distinct from the first parent,
        // so a slot whose two draws both declined is free on both sides.
        if (budgeted && !(offspring == sorted_pop[i].specification)) {
            budget->count_offspring();
        }
        if constexpr (std::is_same_v<Spec, tlsf::Specification>) {
            offspring_pop.push_back(tlsf_simplify(std::move(offspring)));
        } else {
            offspring_pop.push_back(simplify_offspring(std::move(offspring)));
            OrderCrossoverStats::note_offspring(order_kind,
                                                offspring_pop.back());
            DirectionGateStats::note_offspring(stepped, offspring_pop.back());
            record_origin(origins, sorted_pop[i], i, offspring_pop.back(),
                          status_objective);
        }
    }
    return offspring_pop;
}

}  // namespace generation_detail

namespace pipeline_detail {

/// The maximality-demotion flags over @p population (see
/// proved_weaker_realizable), or none where demotion is off or @p Spec is not
/// FRETISH: TLSF has no rule to prove one candidate stronger than another.
template <typename Spec>
std::vector<bool> demotion_flags(
    [[maybe_unused]] const GenerationContext<Spec>& ctx,
    [[maybe_unused]] const std::vector<Scored<Spec>>& population) {
    if constexpr (std::is_same_v<Spec, Specification>) {
        if (ctx.m_cfg.maximality_demotion && ctx.m_status_index.has_value()) {
            return proved_weaker_realizable(population, *ctx.m_status_index);
        }
    }
    return {};
}

/// The NSGA-II options the diversity keys ask for over @p population, which
/// must outlive the sort: under `genetic.crowding = "specification"`, crowding
/// by farthest-point order under specification_distance. Empty where every
/// key is at its default or @p Spec is not FRETISH. Draws nothing.
template <typename Spec>
Nsga2Options selection_options(
    [[maybe_unused]] const GenerationContext<Spec>& ctx,
    [[maybe_unused]] const std::vector<Scored<Spec>>& population) {
    Nsga2Options options;
    if constexpr (std::is_same_v<Spec, Specification>) {
        if (ctx.m_cfg.crowding == Crowding::Specification) {
            SpecificationCrowdingStats::n_sorts++;
            options.distance = [&population](std::size_t lhs, std::size_t rhs) {
                SpecificationCrowdingStats::n_distances++;
                return specification_distance(population[lhs].specification,
                                              population[rhs].specification);
            };
        }
    }
    return options;
}

template <typename Spec>
void stage_order_parents(GenerationContext<Spec>& ctx) {
    // Parents are selected from the whole population, unfiltered: the filters
    // screen only the offspring bred below.
    if (ctx.m_cfg.selection_scheme == SelectionScheme::Random) {
        // Breeding reads the parents in order and the elites off the front,
        // so a shuffle makes both a uniform draw over the population.
        generation_detail::shuffle_population(ctx.m_scored,
                                              ctx.m_random_source);
        return;
    }
    if (uses_nsga2_ranking(ctx.m_cfg)) {
        // The rule is applied again here, or a candidate demoted at the last
        // selection would breed and stand as an elite from its old front.
        nsga2_sort(ctx.m_scored, demotion_flags(ctx, ctx.m_scored),
                   selection_options(ctx, ctx.m_scored));
        return;
    }
    order_population(ctx.m_cfg, ctx.m_scored);
}

template <typename Spec>
void stage_breed(GenerationContext<Spec>& ctx) {
    ctx.m_sorted_parents = std::move(ctx.m_scored);
    ctx.m_scored.clear();
    ctx.m_origins.clear();
    bool record_origins = false;
    if constexpr (std::is_same_v<Spec, Specification>) {
        record_origins = ctx.m_cfg.parent_verdict_filter;
    }
    ctx.m_candidates = generation_detail::breed_offspring(
        ctx.m_cfg, ctx.m_sorted_parents, ctx.m_offspring_n, ctx.m_top_n,
        ctx.m_random_source, ctx.m_budget, ctx.m_original,
        ctx.m_status_objective, record_origins ? &ctx.m_origins : nullptr);
    // The filter chain is judged as a whole rather than filter by filter, so
    // the unfiltered offspring have to survive until every filter has run.
    ctx.m_pre_filter = ctx.m_candidates;
    ctx.m_view = PopulationView::Unscored;
}

template <typename Spec>
void stage_parent_verdict(GenerationContext<Spec>& ctx,
                          const ParentVerdictLookup<Spec>& verdict) {
    assert(ctx.m_origins.size() == ctx.m_pre_filter.size());
    std::unordered_set<Spec> doomed;
    std::vector<std::optional<bool>> unrealizable(ctx.m_sorted_parents.size());
    for (std::size_t k = 0; k < ctx.m_origins.size(); ++k) {
        const OffspringOrigin& origin = ctx.m_origins[k];
        if (origin.order != MoveOrder::Stronger) {
            continue;
        }
        std::optional<bool>& known = unrealizable[origin.parent];
        if (!known.has_value()) {
            known =
                verdict(ctx.m_sorted_parents[origin.parent].specification) ==
                std::optional<bool>(false);
        }
        if (*known) {
            doomed.insert(ctx.m_pre_filter[k]);
        } else {
            ParentVerdictStats::n_unproved++;
        }
    }
    if (doomed.empty()) {
        return;
    }
    const std::size_t before = ctx.m_candidates.size();
    ctx.m_candidates.erase(
        std::remove_if(ctx.m_candidates.begin(), ctx.m_candidates.end(),
                       [&doomed](const Spec& candidate) {
                           return doomed.count(candidate) != 0;
                       }),
        ctx.m_candidates.end());
    ParentVerdictStats::n_dropped += before - ctx.m_candidates.size();
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
    if constexpr (std::is_same_v<Spec, Specification>) {
        OrderCrossoverStats::settle(ctx.m_scored,
                                    status_objective_index(ctx.m_cfg));
        DirectionGateStats::settle(ctx.m_scored,
                                   status_objective_index(ctx.m_cfg));
    }
}

template <typename Spec>
void stage_select(GenerationContext<Spec>& ctx) {
    if (ctx.m_cfg.selection_scheme == SelectionScheme::Random) {
        // The same (mu + lambda) pool as below, cut by a uniform shuffle in
        // place of the crowded-comparison order, so no objective decides who
        // survives. A short pool is padded by cycling, which favours nobody.
        std::vector<Scored<Spec>> pool = ctx.m_parents;
        pool.insert(pool.end(), std::make_move_iterator(ctx.m_scored.begin()),
                    std::make_move_iterator(ctx.m_scored.end()));
        pool = generation_detail::dedup_by_specification(std::move(pool));
        generation_detail::shuffle_population(pool, ctx.m_random_source);
        if (pool.size() > ctx.m_target_size) {
            pool.resize(ctx.m_target_size);
        } else if (!pool.empty()) {
            generation_detail::pad_to_size(pool, ctx.m_target_size);
        }
        ctx.m_scored = std::move(pool);
        return;
    }
    if (!uses_nsga2_ranking(ctx.m_cfg)) {
        order_population(ctx.m_cfg, ctx.m_scored);
        return;
    }
    // (mu + lambda) survivor selection: pool the incoming parents with the
    // freshly scored offspring, rank the distinct union by the
    // crowded-comparison order, and keep the best target_size. This is
    // NSGA-II's elitism, so no non-dominated candidate is ever lost. Parents
    // keep their cached objective vectors, so pooling adds no re-scoring.
    //
    // The pool takes m_parents, in their original order, not m_sorted_parents:
    // nsga2_sort is stable, so pre-sorting the parents would settle ties
    // between equally-ranked candidates differently.
    std::vector<Scored<Spec>> pool = ctx.m_parents;
    pool.insert(pool.end(), std::make_move_iterator(ctx.m_scored.begin()),
                std::make_move_iterator(ctx.m_scored.end()));

    // Ranking the distinct specifications rather than the ~target_size slots
    // holding them stops the truncation below from cutting arbitrarily through
    // the rank-0 front.
    pool = generation_detail::dedup_by_specification(std::move(pool));
    const std::vector<bool> demoted = demotion_flags(ctx, pool);
    DemotionStats::n_demoted += static_cast<std::size_t>(
        std::count(demoted.begin(), demoted.end(), true));
    nsga2_sort(pool, demoted, selection_options(ctx, pool));

    if (pool.size() > ctx.m_target_size) {
        if (std::is_same_v<Spec, Specification> &&
            ctx.m_cfg.crowding == Crowding::Specification &&
            ctx.m_target_size > 0 &&
            pool[ctx.m_target_size - 1].rank == pool[ctx.m_target_size].rank) {
            SpecificationCrowdingStats::n_cut_fronts++;
        }
        pool.resize(ctx.m_target_size);
    } else if (pool.size() < ctx.m_target_size) {
        // Deduplication alone would leave too few slots to breed from, so
        // selection pressure is re-expressed as replication multiplicity.
        pool = generation_detail::replicate_to_size(pool, ctx.m_target_size);
    }
    ctx.m_scored = std::move(pool);
}

}  // namespace pipeline_detail

template <typename Spec>
std::vector<PipelineStage<Spec>> make_generation_pipeline(
    std::vector<PipelineStage<Spec>> filter_stages,
    PopulationRescue<Spec> rescue) {
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

template <typename Spec>
std::vector<Scored<Spec>> run_generation_pipeline(
    GenerationContext<Spec>& ctx,
    const std::vector<PipelineStage<Spec>>& stages,
    const StageObserver& observe) {
    for (const PipelineStage<Spec>& stage : stages) {
        stage(ctx);
        if (observe) {
            observe({stage.name(), stage.n_in(), stage.n_out()});
        }
    }
    return std::move(ctx.m_scored);
}

template void order_population(const Config&,
                               std::vector<Scored<Specification>>&);
template std::vector<Specification> generation_detail::breed_offspring(
    const Config&, const std::vector<Scored<Specification>>&, std::size_t,
    std::size_t, const RandomSource&, SearchBudget*, const Specification*,
    std::optional<std::size_t>, std::vector<OffspringOrigin>*);
template void pipeline_detail::stage_parent_verdict(
    GenerationContext<Specification>&,
    const ParentVerdictLookup<Specification>&);
template std::vector<PipelineStage<Specification>> make_generation_pipeline(
    std::vector<PipelineStage<Specification>>, PopulationRescue<Specification>);
template std::vector<Scored<Specification>> run_generation_pipeline(
    GenerationContext<Specification>&,
    const std::vector<PipelineStage<Specification>>&, const StageObserver&);
template void order_population(const Config&,
                               std::vector<Scored<tlsf::Specification>>&);
template std::vector<tlsf::Specification> generation_detail::breed_offspring(
    const Config&, const std::vector<Scored<tlsf::Specification>>&, std::size_t,
    std::size_t, const RandomSource&, SearchBudget*, const tlsf::Specification*,
    std::optional<std::size_t>, std::vector<OffspringOrigin>*);
template void pipeline_detail::stage_parent_verdict(
    GenerationContext<tlsf::Specification>&,
    const ParentVerdictLookup<tlsf::Specification>&);
template std::vector<PipelineStage<tlsf::Specification>>
    make_generation_pipeline(std::vector<PipelineStage<tlsf::Specification>>,
                             PopulationRescue<tlsf::Specification>);
template std::vector<Scored<tlsf::Specification>> run_generation_pipeline(
    GenerationContext<tlsf::Specification>&,
    const std::vector<PipelineStage<tlsf::Specification>>&,
    const StageObserver&);
