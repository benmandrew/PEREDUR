#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

#include "fitness/status.hpp"
#include "fixtures.hpp"
#include "genetic/demotion.hpp"
#include "genetic/nsga2.hpp"
#include "genetic/scored.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "nsga2";

// --- dominates ---

TEST(test_dominates_strict_improvement_in_all_objectives) {
    expect(dominates({0.6, 0.7}, {0.5, 0.5}),
           "dominates: better in every objective dominates");
}

TEST(test_dominates_equal_in_one_better_in_other) {
    expect(dominates({0.5, 0.7}, {0.5, 0.5}),
           "dominates: equal in one, strictly better in the other, dominates");
}

TEST(test_dominates_requires_strict_improvement_somewhere) {
    expect(!dominates({0.5, 0.5}, {0.5, 0.5}),
           "dominates: identical vectors do not dominate each other");
}

TEST(test_dominates_mutually_incomparable) {
    expect(!dominates({0.6, 0.4}, {0.4, 0.6}),
           "dominates: a trade-off pair is mutually non-dominating (lhs)");
    expect(!dominates({0.4, 0.6}, {0.6, 0.4}),
           "dominates: a trade-off pair is mutually non-dominating (rhs)");
}

// --- non_domination_ranks ---

TEST(test_ranks_layered_fronts) {
    // Front 0: the two trade-off extremes {0.9,0.1} and {0.1,0.9} plus the
    // balanced {0.5,0.5} — mutually non-dominating. {0.4,0.4} is dominated only
    // by {0.5,0.5} (front 1); {0.2,0.2} is dominated by both (front 2).
    const std::vector<std::vector<double>> objectives = {
        {0.9, 0.1}, {0.1, 0.9}, {0.5, 0.5}, {0.4, 0.4}, {0.2, 0.2}};
    const std::vector<std::size_t> ranks = non_domination_ranks(objectives);
    expect(ranks.size() == 5, "ranks: one entry per individual");
    expect(ranks[0] == 0 && ranks[1] == 0 && ranks[2] == 0,
           "ranks: mutually non-dominating solutions share front 0");
    expect(ranks[3] == 1, "ranks: {0.4,0.4} dominated only by {0.5,0.5}");
    expect(ranks[4] == 2, "ranks: {0.2,0.2} dominated across two fronts");
}

TEST(test_ranks_total_order_chain) {
    const std::vector<std::vector<double>> objectives = {
        {0.1, 0.1}, {0.2, 0.2}, {0.3, 0.3}};
    const std::vector<std::size_t> ranks = non_domination_ranks(objectives);
    expect(ranks[2] == 0 && ranks[1] == 1 && ranks[0] == 2,
           "ranks: a fully ordered chain yields one solution per front");
}

TEST(test_ranks_identical_vectors_share_front) {
    const std::vector<std::vector<double>> objectives = {
        {0.5, 0.5}, {0.5, 0.5}, {0.5, 0.5}};
    const std::vector<std::size_t> ranks = non_domination_ranks(objectives);
    expect(ranks[0] == 0 && ranks[1] == 0 && ranks[2] == 0,
           "ranks: identical (non-dominating) vectors all sit on front 0");
}

// --- crowding_distances ---

TEST(test_crowding_boundaries_are_infinite) {
    const std::vector<std::vector<double>> objectives = {
        {0.0, 1.0}, {0.5, 0.5}, {1.0, 0.0}};
    const std::vector<std::size_t> ranks = {0, 0, 0};
    const std::vector<double> distances = crowding_distances(objectives, ranks);
    expect(std::isinf(distances[0]) && std::isinf(distances[2]),
           "crowding: the per-objective extremes get infinite distance");
    expect(std::isfinite(distances[1]),
           "crowding: an interior solution gets a finite distance");
    // Interior gap, normalised per objective: obj0 (1-0)/(1-0)=1, obj1
    // (1-0)/(1-0)=1 -> total 2.
    expect(distances[1] == 2.0,
           "crowding: interior distance is the sum of normalised neighbour "
           "gaps");
}

TEST(test_crowding_denser_neighbour_has_smaller_distance) {
    // Four mutually non-dominating points along a trade-off front. Index 1
    // sits between two close neighbours (0.0 and 0.2 on obj0), while index 2
    // has a distant upper neighbour (1.0), so index 1 is the more crowded and
    // must receive the smaller distance. With only three points every interior
    // point's neighbours are the extremes, which is why four are needed here.
    const std::vector<std::vector<double>> objectives = {
        {0.0, 1.0}, {0.1, 0.9}, {0.2, 0.8}, {1.0, 0.0}};
    const std::vector<std::size_t> ranks = {0, 0, 0, 0};
    const std::vector<double> distances = crowding_distances(objectives, ranks);
    expect(std::isfinite(distances[1]) && std::isfinite(distances[2]),
           "crowding: both interior solutions get finite distances");
    expect(distances[1] < distances[2],
           "crowding: the interior solution with closer neighbours is more "
           "crowded and gets the smaller distance");
}

TEST(test_crowding_zero_range_objective_contributes_nothing) {
    // obj1 is constant across the front; it must not produce NaN/inf for the
    // interior member, and obj0 alone drives the finite distance.
    const std::vector<std::vector<double>> objectives = {
        {0.0, 0.5}, {0.5, 0.5}, {1.0, 0.5}};
    const std::vector<std::size_t> ranks = {0, 0, 0};
    const std::vector<double> distances = crowding_distances(objectives, ranks);
    expect(std::isfinite(distances[1]) && distances[1] == 1.0,
           "crowding: a zero-range objective adds nothing and yields no NaN");
}

TEST(test_crowding_constant_objective_pins_nobody) {
    // Every objective is constant across the front, so nothing distinguishes
    // any member and none should be preferred. The infinities used to be
    // assigned before the zero-range guard, which pinned two members picked by
    // an unstable sort over the tied ones -- selection noise with no basis in
    // the objectives.
    const std::vector<std::vector<double>> objectives = {
        {0.5, 0.5}, {0.5, 0.5}, {0.5, 0.5}, {0.5, 0.5}};
    const std::vector<std::size_t> ranks = {0, 0, 0, 0};
    const std::vector<double> distances = crowding_distances(objectives, ranks);
    expect(std::all_of(distances.begin(), distances.end(),
                       [](double dist) { return dist == 0.0; }),
           "crowding: a front constant in every objective should leave every "
           "distance at zero, pinning nobody");
}

TEST(test_crowding_zero_range_objective_adds_no_infinities) {
    // obj0 varies and legitimately anchors its two extremes; obj1 is constant
    // and must anchor nothing. A property assertion rather than a regression
    // guard: this case also passed before the guard moved, because the unstable
    // sort happened to leave obj0's extremes at the ends of obj1's all-tied
    // ordering. test_crowding_constant_objective_pins_nobody above is the case
    // that actually fails without the fix.
    const std::vector<std::vector<double>> objectives = {
        {0.0, 0.5}, {0.3, 0.5}, {0.6, 0.5}, {1.0, 0.5}};
    const std::vector<std::size_t> ranks = {0, 0, 0, 0};
    const std::vector<double> distances = crowding_distances(objectives, ranks);
    expect(std::isinf(distances[0]) && std::isinf(distances[3]),
           "crowding: the varying objective's extremes stay anchored");
    expect(std::isfinite(distances[1]) && std::isfinite(distances[2]),
           "crowding: a constant objective must not anchor interior members");
}

// --- nsga2_sort ---

Scored<int> make_scored(int value, std::vector<double> objectives) {
    Scored<int> scored;
    scored.specification = value;
    scored.objectives = std::move(objectives);
    return scored;
}

TEST(test_nsga2_sort_orders_by_rank_then_crowding) {
    std::vector<Scored<int>> population;
    population.push_back(make_scored(0, {0.4, 0.4}));  // front 1
    population.push_back(make_scored(1, {0.9, 0.1}));  // front 0, boundary
    population.push_back(make_scored(2, {0.5, 0.5}));  // front 0, interior
    population.push_back(make_scored(3, {0.1, 0.9}));  // front 0, boundary
    nsga2_sort(population);
    expect(population.front().rank == 0,
           "nsga2_sort: front-0 solutions come first");
    expect(population.back().specification == 0 && population.back().rank == 1,
           "nsga2_sort: the dominated solution sorts last");
    // Within front 0 the two boundaries (infinite crowding) precede the
    // interior solution 2.
    expect(population[2].specification == 2,
           "nsga2_sort: the crowded interior solution sorts after the "
           "boundaries within its front");
}

TEST(test_nsga2_sort_is_deterministic) {
    const auto build = []() {
        std::vector<Scored<int>> pop;
        pop.push_back(make_scored(0, {0.5, 0.5}));
        pop.push_back(make_scored(1, {0.5, 0.5}));
        pop.push_back(make_scored(2, {0.9, 0.1}));
        return pop;
    };
    std::vector<Scored<int>> first = build();
    std::vector<Scored<int>> second = build();
    nsga2_sort(first);
    nsga2_sort(second);
    for (std::size_t i = 0; i < first.size(); ++i) {
        expect(first[i].specification == second[i].specification,
               "nsga2_sort: identical input yields identical order (stable)");
    }
}

// --- maximality demotion ---

// Two guarantees over input a and outputs x, y; the first under `first`.
Specification two_guarantees(const Timing& first,
                             std::vector<Requirement> assumptions = {}) {
    return Specification(std::move(assumptions),
                         {make_req("a", "x", first), make_req("a", "y")}, {"a"},
                         {"x", "y"});
}

Specification tight_spec() { return two_guarantees(timing::within_ticks(2)); }

// Status first, then a similarity score that leaves the pair on one front.
Scored<Specification> scored_with_status(Specification spec, double status) {
    Scored<Specification> scored;
    scored.specification = std::move(spec);
    scored.objectives = {status, 0.5};
    return scored;
}

std::vector<bool> flags_of(const Specification& first,
                           const Specification& second, double first_status,
                           double second_status) {
    return proved_weaker_realizable({scored_with_status(first, first_status),
                                     scored_with_status(second, second_status)},
                                    0);
}

TEST(test_demotion_flags_a_weaker_timing) {
    const std::vector<bool> flags =
        flags_of(tight_spec(), two_guarantees(timing::within_ticks(3)),
                 k_status_realizable, k_status_realizable);
    expect(!flags[0] && flags[1],
           "demotion: within 3 is proved weaker than within 2, so only the "
           "looser candidate is demoted");
}

TEST(test_demotion_flags_a_tombstoned_guarantee) {
    Specification gutted = tight_spec();
    gutted.m_guarantees[1].m_removed = true;
    const std::vector<bool> flags = flags_of(
        gutted, tight_spec(), k_status_realizable, k_status_realizable);
    expect(flags[0] && !flags[1],
           "demotion: a tombstone reads as true, so the candidate missing a "
           "guarantee is the weaker one");
}

TEST(test_demotion_flags_a_stronger_assumption) {
    const Specification assuming = two_guarantees(
        timing::within_ticks(2), {continual("a", timing::always())});
    const std::vector<bool> flags = flags_of(
        tight_spec(), assuming, k_status_realizable, k_status_realizable);
    expect(!flags[0] && flags[1],
           "demotion: an added assumption weakens the specification, so the "
           "candidate carrying it is demoted");
}

TEST(test_demotion_spares_unrealizable_candidates) {
    const Specification loose = two_guarantees(timing::within_ticks(3));
    const std::vector<bool> weaker_unrealizable = flags_of(
        tight_spec(), loose, k_status_realizable, k_status_unrealizable);
    expect(!weaker_unrealizable[0] && !weaker_unrealizable[1],
           "demotion: an unrealizable candidate is never demoted");
    const std::vector<bool> stronger_unrealizable = flags_of(
        tight_spec(), loose, k_status_unrealizable, k_status_realizable);
    expect(!stronger_unrealizable[0] && !stronger_unrealizable[1],
           "demotion: an unrealizable candidate demotes nothing, however "
           "strong it is");
    const std::vector<bool> walk_partial =
        flags_of(tight_spec(), loose, 0.9, k_status_realizable);
    expect(!walk_partial[1],
           "demotion: a status below the top tier is not read as realizable");
}

TEST(test_demotion_spares_incomparable_and_equivalent_candidates) {
    const Specification other_response(
        {}, {make_req("a", "y", timing::within_ticks(2)), make_req("a", "y")},
        {"a"}, {"x", "y"});
    const std::vector<bool> incomparable = flags_of(
        tight_spec(), other_response, k_status_realizable, k_status_realizable);
    expect(!incomparable[0] && !incomparable[1],
           "demotion: neither of two incomparable candidates is demoted");
    const std::vector<bool> equal = flags_of(
        tight_spec(), tight_spec(), k_status_realizable, k_status_realizable);
    expect(!equal[0] && !equal[1],
           "demotion: two equivalent candidates are each proved stronger than "
           "the other, so neither is strictly weaker");
}

TEST(test_demotion_flags_do_not_depend_on_order) {
    const std::vector<Specification> specs = {
        two_guarantees(timing::within_ticks(4)),
        two_guarantees(timing::within_ticks(3)), tight_spec()};
    std::vector<Scored<Specification>> forward;
    std::vector<Scored<Specification>> backward;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        forward.push_back(scored_with_status(specs[i], k_status_realizable));
        backward.push_back(scored_with_status(specs[specs.size() - 1 - i],
                                              k_status_realizable));
    }
    const std::vector<bool> forward_flags =
        proved_weaker_realizable(forward, 0);
    const std::vector<bool> backward_flags =
        proved_weaker_realizable(backward, 0);
    expect(forward_flags == std::vector<bool>{true, true, false},
           "demotion: every candidate below the strongest of a chain is "
           "demoted, and by one front only");
    expect(backward_flags == std::vector<bool>{false, true, true},
           "demotion: reversing the pool reverses the flags");
}

TEST(test_nsga2_sort_demotes_a_flagged_member_one_front) {
    std::vector<Scored<int>> population;
    population.push_back(make_scored(0, {0.9, 0.1}));    // front 0
    population.push_back(make_scored(1, {0.1, 0.9}));    // front 0
    population.push_back(make_scored(2, {0.05, 0.05}));  // front 1
    nsga2_sort(population, {true, false, false});
    expect(
        population.front().specification == 1 && population.front().rank == 0,
        "nsga2_sort: the unflagged front-0 member leads");
    expect(population[1].rank == 1 && population[2].rank == 1,
           "nsga2_sort: the demoted member joins front 1 beside the member "
           "that was already there");
}

TEST(test_crowding_skips_an_empty_front) {
    // Ranks with no member at 0, as when every front-0 member is demoted.
    const std::vector<std::vector<double>> objectives = {
        {0.0, 1.0}, {0.5, 0.5}, {1.0, 0.0}};
    const std::vector<std::size_t> ranks = {1, 1, 1};
    const std::vector<double> distances = crowding_distances(objectives, ranks);
    expect(std::isinf(distances[0]) && std::isinf(distances[2]) &&
               distances[1] == 2.0,
           "crowding: an empty front is skipped and the next is measured as "
           "usual");
}

TEST(test_nsga2_sort_demotes_a_whole_front) {
    std::vector<Scored<int>> population;
    population.push_back(make_scored(0, {0.9, 0.1}));    // front 0
    population.push_back(make_scored(1, {0.1, 0.9}));    // front 0
    population.push_back(make_scored(2, {0.05, 0.05}));  // front 1
    nsga2_sort(population, {true, true, false});
    expect(population[0].rank == 1 && population[1].rank == 1 &&
               population[2].rank == 1,
           "nsga2_sort: demoting all of front 0 leaves it empty and every "
           "member in front 1");
}

}  // namespace
