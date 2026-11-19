#pragma once

/// @file diversity.hpp
/// @brief Mechanisms that spread a FRETISH search over unlike repairs: the
///        slot distance between specifications behind
///        `genetic.crowding = "specification"`, and the counters each
///        mechanism writes to run.json.

#include <atomic>
#include <cstddef>

#include "requirement.hpp"

/// The share of slots in which @p lhs and @p rhs hold different requirements:
/// a Hamming distance over the slots, in [0, 1].
///
/// Slots pair by index, assumptions with assumptions and guarantees with
/// guarantees, as every comparison against the original does. A slot one side
/// lacks (an appended assumption) differs, and so does a slot tombstoned on
/// one side only, since Requirement equality reads `m_removed`. The count is
/// divided by the longer assumption list plus the longer guarantee list; two
/// specifications with no slot at all are at distance 0. Draws nothing.
double specification_distance(const Specification& lhs,
                              const Specification& rhs);

/// What `genetic.crowding = "specification"` did over a run. Reported under
/// `--diagnostics` and in run.json; zero whenever the key is "objective".
struct SpecificationCrowdingStats {
    /// NSGA-II sorts whose crowding was farthest-point order, at parent
    /// ordering and at survivor selection.
    inline static std::atomic<std::size_t> n_sorts{0};
    /// Survivor selections whose truncation cut through a front, so that the
    /// farthest-point order decided which members of that front survived.
    inline static std::atomic<std::size_t> n_cut_fronts{0};
    /// Distances between two specifications computed for those sorts.
    inline static std::atomic<std::size_t> n_distances{0};
};
