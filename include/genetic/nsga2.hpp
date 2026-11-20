#pragma once

/// @file nsga2.hpp
/// @brief NSGA-II selection primitives: Pareto dominance, fast non-dominated
///        sorting, crowding distance, and the crowded-comparison ordering of a
///        scored population.
///
/// All objectives follow the project convention that higher is better. The
/// free functions operate on plain objective matrices so they are testable
/// without any Specification type; nsga2_sort() adapts them to a scored
/// population by reading each element's per-objective vector.

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "genetic/scored.hpp"

/// True iff @p lhs Pareto-dominates @p rhs: at least as good in every
/// objective and strictly better in at least one (higher is better). The two
/// vectors must have the same length.
bool dominates(const std::vector<double>& lhs, const std::vector<double>& rhs);

/// Deb's fast non-dominated sort. Returns the 0-based non-domination front
/// index of each individual (0 = the Pareto front, higher = increasingly
/// dominated). O(k * N^2) in the objective count k and population size N.
///
/// @param objectives One equal-length objective vector per individual;
///                   must be non-empty with a common non-zero width.
std::vector<std::size_t> non_domination_ranks(
    const std::vector<std::vector<double>>& objectives);

/// Crowding distance of each individual (Deb et al. 2002), computed within its
/// own front. For each objective the front is ordered and interior members
/// accumulate the range-normalised gap between their neighbours; the per-front
/// extremes of every objective receive an infinite distance so the boundaries
/// of the front are always preferred. Objectives with zero range across a
/// front contribute nothing.
///
/// @param objectives One equal-length objective vector per individual.
/// @param ranks      Front index per individual, as from non_domination_ranks;
///                   must be the same length as @p objectives.
std::vector<double> crowding_distances(
    const std::vector<std::vector<double>>& objectives,
    const std::vector<std::size_t>& ranks);

/// A distance between the individuals at two indices of a population.
using PairDistance = std::function<double(std::size_t, std::size_t)>;

/// A crowding score per individual from greedy farthest-point selection under
/// @p distance, front by front in rank order. Each front starts from the
/// members of every better front; while members remain, the one whose nearest
/// already-picked individual is farthest is picked next, the earliest index
/// winning a tie, and the first front's first pick is its earliest member.
/// Each member scores the number of its front's members picked no earlier
/// than it, so sorting a front by score descending gives the pick order, and
/// truncating after the better fronts keeps the greedy fill of the last one.
///
/// @param ranks    Front index per individual, as from non_domination_ranks.
/// @param distance Symmetric and non-negative; asked at most once per pair.
std::vector<double> farthest_point_scores(const std::vector<std::size_t>& ranks,
                                          const PairDistance& distance);

/// What Nsga2Options::tiebreak changed in one sort, against the order the
/// same sort gives by rank and crowding alone.
struct TiebreakReport {
    /// Some member stands at another position.
    bool reordered = false;
    /// The first Nsga2Options::keep members are another set.
    bool kept_changed = false;
};

/// Optional changes to how nsga2_sort ranks and spreads a population. The
/// default changes nothing.
struct Nsga2Options {
    /// Where non-empty, one further objective per element (higher is better),
    /// appended to each objective vector for ranking and objective crowding.
    /// Nothing is stored on the elements.
    std::vector<double> extra_objective;
    /// Where set, crowding is farthest_point_scores under this distance in
    /// place of crowding_distances.
    PairDistance distance;
    /// Where non-empty, one within-front key per element (higher is better),
    /// compared after the rank and before crowding, which then only breaks
    /// its ties. Takes no part in ranking or crowding.
    std::vector<double> tiebreak;
    /// Where set alongside @c tiebreak, receives what the key changed against
    /// the order by rank and crowding alone.
    TiebreakReport* report = nullptr;
    /// The number of leading members the caller keeps, read for the report;
    /// 0 where it keeps them all.
    std::size_t keep = 0;
};

/// The crowded-comparison order of @p ranks and @p crowding, both one per
/// element: indices by rank ascending, then by @p tiebreak descending where
/// it is non-empty, then by crowding descending, stably. Fills @p report, where
/// given, against the order without @p tiebreak.
std::vector<std::size_t> tiebroken_order(const std::vector<std::size_t>& ranks,
                                         const std::vector<double>& crowding,
                                         const std::vector<double>& tiebreak,
                                         TiebreakReport* report,
                                         std::size_t keep);

/// Each element's per-objective vector, in population order: the matrix the
/// free functions above take.
template <typename Spec>
std::vector<std::vector<double>> objectives_of(
    const std::vector<Scored<Spec>>& population) {
    std::vector<std::vector<double>> objectives;
    objectives.reserve(population.size());
    for (const Scored<Spec>& scored : population) {
        objectives.push_back(scored.objectives);
    }
    return objectives;
}

/// Computes non-domination ranks and crowding distances from each element's
/// objectives, stores them on the elements, and stable-sorts the population by
/// the crowded-comparison operator: front rank ascending, then crowding
/// distance descending. The sort is stable so a fixed RNG seed yields a
/// deterministic order.
///
/// Each element flagged in @p demoted moves one front down before crowding is
/// measured, so it competes for crowding distance within the front it lands
/// in, and a 1 / (1 + rank) apportionment reads its demoted rank. An empty
/// @p demoted demotes nothing; otherwise it is one flag per element.
///
/// @p options may add an objective, replace the crowding measure and put a key
/// before crowding within each front (see Nsga2Options); its default leaves the
/// sort as above.
template <typename Spec>
void nsga2_sort(std::vector<Scored<Spec>>& population,
                const std::vector<bool>& demoted = {},
                const Nsga2Options& options = {}) {
    if (population.empty()) {
        return;
    }
    assert(demoted.empty() || demoted.size() == population.size());
    std::vector<std::vector<double>> objectives = objectives_of(population);
    if (!options.extra_objective.empty()) {
        assert(options.extra_objective.size() == population.size());
        for (std::size_t i = 0; i < population.size(); ++i) {
            objectives[i].push_back(options.extra_objective[i]);
        }
    }
    std::vector<std::size_t> ranks = non_domination_ranks(objectives);
    for (std::size_t i = 0; i < demoted.size(); ++i) {
        if (demoted[i]) {
            ++ranks[i];
        }
    }
    const std::vector<double> distances =
        options.distance ? farthest_point_scores(ranks, options.distance)
                         : crowding_distances(objectives, ranks);
    for (std::size_t i = 0; i < population.size(); ++i) {
        population[i].rank = ranks[i];
        population[i].crowding_distance = distances[i];
    }
    if (!options.tiebreak.empty()) {
        assert(options.tiebreak.size() == population.size());
        const std::vector<std::size_t> order = tiebroken_order(
            ranks, distances, options.tiebreak, options.report, options.keep);
        std::vector<Scored<Spec>> sorted;
        sorted.reserve(population.size());
        for (const std::size_t i : order) {
            sorted.push_back(std::move(population[i]));
        }
        population = std::move(sorted);
        return;
    }
    std::stable_sort(population.begin(), population.end(),
                     [](const Scored<Spec>& lhs, const Scored<Spec>& rhs) {
                         if (lhs.rank != rhs.rank) {
                             return lhs.rank < rhs.rank;
                         }
                         return lhs.crowding_distance > rhs.crowding_distance;
                     });
}
