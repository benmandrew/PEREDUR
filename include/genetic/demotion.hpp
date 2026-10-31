#pragma once

/// @file demotion.hpp
/// @brief Maximality demotion: the FRETISH selection rule that moves a
///        realizable candidate one front down when another realizable
///        candidate is proved strictly stronger.

#include <cstddef>
#include <vector>

#include "genetic/scored.hpp"
#include "requirement.hpp"

/// Process-wide record of what maximality demotion did to a run.
struct DemotionStats {
    /// Candidates demoted at survivor selection, summed over every generation.
    /// The parent ordering applies the same rule again and is not counted.
    inline static std::size_t n_demoted{0};
};

/// One flag per element of @p population: set where the element is realizable
/// and another realizable element is proved strictly stronger than it.
///
/// Realizable means the status objective, at @p status_index of each
/// objective vector, reads `k_status_realizable`. Y is proved stronger than X
/// when spec_implies_by_rule proves Y implies X, and strictly stronger when it
/// does not also prove X implies Y. The proof is by rule alone, so no process
/// is launched and an unproven pair is left alone.
///
/// The flags depend on the set of elements alone, not their order. Each
/// distinct requirement pair is proved at most once per call.
std::vector<bool> proved_weaker_realizable(
    const std::vector<Scored<Specification>>& population,
    std::size_t status_index);
