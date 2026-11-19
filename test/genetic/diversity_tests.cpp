#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string_view>
#include <utility>
#include <vector>

#include "config.hpp"
#include "fixtures.hpp"
#include "genetic/diversity.hpp"
#include "genetic/generation.hpp"
#include "genetic/mutation.hpp"
#include "genetic/nsga2.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "diversity";

// --- specification crowding ---

TEST(test_specification_distance_counts_differing_slots) {
    const Specification lhs({assumption0()}, {guarantee0(), guarantee1()},
                            inputs(), outputs());
    const Specification same = lhs;
    const Specification one_guarantee({assumption0()},
                                      {guarantee0(), removed(guarantee1())},
                                      inputs(), outputs());
    const Specification appended(
        {assumption0(), make_req("a", "b", timing::next_timepoint())},
        {guarantee0(), guarantee1()}, inputs(), outputs());
    expect(specification_distance(lhs, same) == 0.0,
           "specification_distance: equal specifications are at 0");
    expect(std::abs(specification_distance(lhs, one_guarantee) - 1.0 / 3.0) <
               1e-12,
           "specification_distance: a tombstone differs in one of 3 slots");
    expect(std::abs(specification_distance(lhs, appended) - 1.0 / 4.0) < 1e-12,
           "specification_distance: an appended assumption differs in one of "
           "4 slots");
    expect(specification_distance(Specification(), Specification()) == 0.0,
           "specification_distance: no slots is distance 0");
}

TEST(test_farthest_point_scores_pick_the_farthest_member_next) {
    const std::vector<double> points = {0.0, 1.0, 10.0, 9.0};
    const PairDistance distance = [&points](std::size_t lhs, std::size_t rhs) {
        return std::abs(points[lhs] - points[rhs]);
    };
    const std::vector<double> scores =
        farthest_point_scores({0, 0, 0, 0}, distance);
    // 0 first (earliest), then 10, then 1 and 9 tie at 1 and the earlier wins.
    expect(scores == std::vector<double>{4.0, 2.0, 3.0, 1.0},
           "farthest_point_scores: the pick order is 0, 10, 1, 9");
}

TEST(test_farthest_point_scores_seed_a_front_with_the_better_fronts) {
    const std::vector<double> points = {0.0, 1.0, 8.0};
    const PairDistance distance = [&points](std::size_t lhs, std::size_t rhs) {
        return std::abs(points[lhs] - points[rhs]);
    };
    // Front 1 holds 1 and 8; 8 is farther from front 0's 0, so it goes first.
    const std::vector<double> scores =
        farthest_point_scores({0, 1, 1}, distance);
    expect(scores[2] > scores[1],
           "farthest_point_scores: the member farther from the better front "
           "is picked first");
}

TEST(test_nsga2_sort_default_options_leave_crowding_alone) {
    std::vector<Scored<Specification>> lhs(3);
    lhs[0].objectives = {0.1, 0.9};
    lhs[1].objectives = {0.5, 0.5};
    lhs[2].objectives = {0.9, 0.1};
    std::vector<Scored<Specification>> rhs = lhs;
    nsga2_sort(lhs);
    nsga2_sort(rhs, {}, Nsga2Options{});
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        expect(lhs[i].objectives == rhs[i].objectives &&
                   lhs[i].crowding_distance == rhs[i].crowding_distance,
               "nsga2_sort: default options sort as without them");
    }
}

TEST(test_specification_crowding_keeps_the_unlike_members) {
    // One flat objective puts every candidate on one front, so only crowding
    // decides which survive the cut from 4 to 2: the two specifications that
    // share a guarantee are the closest pair, so one of them is shed.
    const Requirement shared = make_req("a", "x");
    const std::vector<Specification> specs = {
        Specification({}, {shared, make_req("b", "y")}, inputs(), outputs()),
        Specification({}, {shared, make_req("a", "y")}, inputs(), outputs()),
        Specification({}, {make_req("b", "x"), make_req("a", "b")}, inputs(),
                      outputs()),
    };
    Config cfg;
    cfg.crowding = Crowding::Specification;
    cfg.mutation_rate = 0.0;
    cfg.crossover_rate = 0.0;
    cfg.elitism_rate = 0.0;
    const AggregateWeightedFitnessFunction fns = constant_fitness();
    const std::vector<ScoredSpecification> population =
        score_population(cfg, specs, fns);
    const RandomSource source = make_source({}, 0);
    const std::size_t sorts = SpecificationCrowdingStats::n_sorts;
    const std::size_t cuts = SpecificationCrowdingStats::n_cut_fronts;
    const std::vector<ScoredSpecification> next =
        evolve_generation(cfg, population, 2, 0, fns, {}, source);
    expect(SpecificationCrowdingStats::n_sorts > sorts,
           "specification crowding: the sorts are counted");
    expect(SpecificationCrowdingStats::n_cut_fronts > cuts,
           "specification crowding: a cut through the front is counted");
    bool kept_unlike = false;
    for (const ScoredSpecification& scored : next) {
        kept_unlike = kept_unlike || scored.specification == specs[2];
    }
    expect(kept_unlike,
           "specification crowding: the specification sharing no slot "
           "survives the cut");
}

}  // namespace

namespace {

// --- novelty ---

Scored<Specification> scored_with_status(Specification spec, double status) {
    Scored<Specification> scored;
    scored.specification = std::move(spec);
    scored.objectives = {status};
    return scored;
}

TEST(test_novelty_counts_the_accumulated_repairs_implying_a_candidate) {
    const Specification strong({}, {guarantee0(), guarantee1()}, inputs(),
                               outputs());
    const Specification gutted({}, {guarantee0(), removed(guarantee1())},
                               inputs(), outputs());
    const Specification other({}, {make_req("b", "x"), guarantee1()}, inputs(),
                              outputs());
    std::vector<Specification> accumulated = {strong};
    DiversityState state(&accumulated);
    const std::vector<Scored<Specification>> population = {
        scored_with_status(gutted, 1.0), scored_with_status(other, 1.0),
        scored_with_status(gutted, 0.5)};
    std::vector<double> objective = novelty_objective(state, population, 0);
    expect(objective == std::vector<double>{-1.0, 0.0, 0.0},
           "novelty: a repair implying a realizable candidate counts, an "
           "incomparable one does not, and an unrealizable one scores 0");
    // M only grows, and a measured candidate catches up on the new members.
    const std::size_t checks = NoveltyStats::n_checks;
    accumulated.push_back(gutted);
    objective = novelty_objective(state, population, 0);
    expect(objective[0] == -2.0,
           "novelty: the candidate counts the repair accumulated since, "
           "itself included");
    expect(NoveltyStats::n_checks - checks == 2,
           "novelty: each candidate is checked against the new member alone");
    expect(novelty_objective(state, population, std::nullopt) ==
               std::vector<double>(3, 0.0),
           "novelty: without a status objective every value is 0");
}

TEST(test_novelty_ranks_a_repair_below_an_incomparable_one) {
    const Specification strong({}, {guarantee0(), guarantee1()}, inputs(),
                               outputs());
    const Specification gutted({}, {guarantee0(), removed(guarantee1())},
                               inputs(), outputs());
    const Specification other({}, {make_req("b", "x"), guarantee1()}, inputs(),
                              outputs());
    const std::vector<Specification> accumulated = {strong};
    DiversityState state(&accumulated);
    std::vector<Scored<Specification>> population = {
        scored_with_status(gutted, 1.0), scored_with_status(other, 1.0)};
    Nsga2Options options;
    options.extra_objective = novelty_objective(state, population, 0);
    nsga2_sort(population, {}, options);
    expect(population[0].specification == other && population[0].rank == 0 &&
               population[1].rank == 1,
           "novelty: the candidate a found repair implies drops a front");
}

}  // namespace

namespace {

// --- slot coverage ---

// Two guarantees and no input, so no structural action draws and the slot
// draw is the first draw of the mutation.
Specification two_guarantees() {
    return Specification({}, {guarantee0(), guarantee1()}, {}, outputs());
}

Config coverage_config() {
    Config cfg;
    cfg.p_remove_guarantee = 0.0;
    cfg.slot_coverage = true;
    return cfg;
}

TEST(test_slot_coverage_weights_the_draw_away_from_edited_slots) {
    const Specification spec = two_guarantees();
    SlotCoverage coverage;
    coverage.guarantees = {5, 0};
    ParentContext parent;
    parent.coverage = &coverage;
    // 0.2 of the mass: the first slot's weight is 1/6 against 1, so its share
    // is 1/7 and 0.2 lands in the second slot. A uniform draw of 200000
    // modulo 2 lands in the first.
    const RandomSource source = make_source({200000}, 0);
    const std::size_t weighted = SlotCoverageStats::n_weighted;
    const Specification mutated =
        mutate_specification(spec, source, coverage_config(), &parent);
    expect(SlotCoverageStats::n_weighted == weighted + 1,
           "slot coverage: unequal weights take the weighted draw");
    expect(mutated.m_guarantees[0] == spec.m_guarantees[0],
           "slot coverage: the slot the repairs edited is not drawn");
}

TEST(test_slot_coverage_with_equal_weights_draws_uniformly) {
    const Specification spec = two_guarantees();
    SlotCoverage coverage;
    coverage.guarantees = {2, 2};
    ParentContext parent;
    parent.coverage = &coverage;
    const std::size_t weighted = SlotCoverageStats::n_weighted;
    const std::size_t draws = SlotCoverageStats::n_draws;
    const RandomSource with = make_source({7, 3, 1, 4}, 0);
    const RandomSource without = make_source({7, 3, 1, 4}, 0);
    Config off = coverage_config();
    off.slot_coverage = false;
    const Specification lhs =
        mutate_specification(spec, with, coverage_config(), &parent);
    const Specification rhs = mutate_specification(spec, without, off);
    expect(SlotCoverageStats::n_draws == draws + 1 &&
               SlotCoverageStats::n_weighted == weighted,
           "slot coverage: equal weights are counted but not weighted");
    expect(lhs == rhs, "slot coverage: equal weights draw as the key off");
}

TEST(test_slot_coverage_counts_the_slots_accumulated_repairs_edit) {
    const Specification original({assumption0()}, {guarantee0(), guarantee1()},
                                 inputs(), outputs());
    std::vector<Specification> accumulated = {
        Specification({assumption0()}, {guarantee0(), removed(guarantee1())},
                      inputs(), outputs()),
    };
    DiversityState state(&accumulated);
    expect(state.slot_coverage(original).guarantees ==
               std::vector<std::size_t>{0, 1},
           "slot coverage: a tombstoned guarantee counts as edited");
    accumulated.push_back(Specification(
        {assumption0(), make_req("a", "b", timing::next_timepoint())},
        {make_req("b", "x"), removed(guarantee1())}, inputs(), outputs()));
    const SlotCoverage& coverage = state.slot_coverage(original);
    expect(coverage.guarantees == std::vector<std::size_t>{1, 2} &&
               coverage.assumptions == std::vector<std::size_t>{0, 1},
           "slot coverage: later repairs add to the counts, an appended "
           "assumption included");
    expect(coverage_of(coverage, 2, 1) == 2 && coverage_of(coverage, 5, 1) == 0,
           "coverage_of: reads a guarantee by global index, 0 past the end");
}

}  // namespace

namespace {

// --- spread seed population ---

TEST(test_spread_init_weakens_the_targets_round_robin) {
    const Specification original({assumption0()}, {guarantee0(), guarantee1()},
                                 inputs(), outputs());
    Config cfg;
    const RandomSource source = make_random_source_from_seed(7);
    // Guarantee 1 refused: the targets are guarantee 1 then assumption 0.
    const std::vector<Specification> seeds =
        spread_initial_population(original, {1}, 7, cfg, source);
    expect(seeds.size() == 7 && seeds[0] == original,
           "spread_init: the population is the original then the seeds");
    expect(SpreadInitStats::n_targets == 2,
           "spread_init: one refused guarantee and one assumption");
    expect(SpreadInitStats::n_seeded + SpreadInitStats::n_failed == 6 &&
               SpreadInitStats::n_seeded > 0,
           "spread_init: every seed is counted, and some are weakenings");
    for (std::size_t k = 1; k < seeds.size(); ++k) {
        const Specification& seed = seeds[k];
        if (seed == original) {
            continue;
        }
        expect(classify_move(original, seed) == MoveOrder::Weaker,
               "spread_init: every seed is proved weaker than the original");
        const bool on_guarantee = (k - 1) % 2 == 0;
        expect(seed.m_guarantees[0] == original.m_guarantees[0],
               "spread_init: an unrefused guarantee is never moved");
        expect(on_guarantee ? seed.m_assumptions == original.m_assumptions
                            : seed.m_guarantees == original.m_guarantees,
               "spread_init: each seed moves its own target slot alone");
    }
}

TEST(test_spread_init_with_one_individual_is_the_original) {
    const Specification original({}, {guarantee0()}, inputs(), outputs());
    const RandomSource source = make_random_source_from_seed(1);
    const std::vector<Specification> seeds =
        spread_initial_population(original, {}, 1, Config{}, source);
    expect(seeds.size() == 1 && seeds[0] == original,
           "spread_init: a population of one is the original alone");
}

}  // namespace
