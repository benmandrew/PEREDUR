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
#include <cstddef>
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
template <typename Spec>
void nsga2_sort(std::vector<Scored<Spec>>& population) {
    if (population.empty()) {
        return;
    }
    const std::vector<std::vector<double>> objectives =
        objectives_of(population);
    const std::vector<std::size_t> ranks = non_domination_ranks(objectives);
    const std::vector<double> distances = crowding_distances(objectives, ranks);
    for (std::size_t i = 0; i < population.size(); ++i) {
        population[i].rank = ranks[i];
        population[i].crowding_distance = distances[i];
    }
    std::stable_sort(population.begin(), population.end(),
                     [](const Scored<Spec>& lhs, const Scored<Spec>& rhs) {
                         if (lhs.rank != rhs.rank) {
                             return lhs.rank < rhs.rank;
                         }
                         return lhs.crowding_distance > rhs.crowding_distance;
                     });
}
