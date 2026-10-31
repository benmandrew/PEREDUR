#pragma once

/// @file scored.hpp
/// @brief The Scored<Spec> template pairing a specification element with its
///        aggregated fitness score and multi-objective selection metadata.

#include <cstddef>
#include <vector>

/// A specification element paired with its fitness scores.
///
/// @c fitness is the weighted-average scalar used by the WeightedAverage
/// selection scheme, by diagnostics, and by the output records. @c objectives
/// holds the individual per-objective scores (registration order) that the
/// default NSGA-II selection ranks over; @c rank and @c crowding_distance are
/// the NSGA-II attributes derived from those objectives (unused, left at their
/// defaults, under the weighted-average scheme). @c refused_guarantees is the
/// fitness cache's ObjectiveRecord::refused for the specification, which
/// breeding reads to aim mutation (see ParentContext).
template <typename Spec>
struct Scored {
    Spec specification;
    double fitness = 0.0;
    std::vector<double> objectives;
    std::size_t rank = 0;
    double crowding_distance = 0.0;
    std::vector<std::size_t> refused_guarantees;
};
