#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "fixtures.hpp"
#include "genetic/diversity.hpp"
#include "genetic/generation.hpp"
#include "genetic/nsga2.hpp"
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
