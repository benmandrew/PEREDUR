#pragma once

/// @file demotion.hpp
/// @brief Maximality demotion: the FRETISH selection rule that moves a
///        realizable candidate one front down when another realizable
///        candidate is proved strictly stronger.

#include <cstddef>
#include <optional>
#include <vector>

#include "genetic/scored.hpp"
#include "requirement.hpp"

/// The first front of a population by its objectives alone, and the gutted
/// repairs on it: see final_front_gutting.
struct FrontGutting {
    /// Distinct members on the first non-dominated front, before any
    /// demotion.
    std::size_t n_members = 0;
    /// Of those, the realizable ones.
    std::size_t n_realizable = 0;
    /// Of those, the ones proved_weaker_realizable flags: another realizable
    /// member of the population is proved strictly stronger.
    std::size_t n_gutted = 0;
};

/// Process-wide record of what maximality demotion did to a run.
struct DemotionStats {
    /// Candidates demoted at survivor selection, summed over every generation.
    /// The parent ordering applies the same rule again and is not counted.
    inline static std::size_t n_demoted{0};
    /// The final population's first front, measured once when the search
    /// ends, whether or not demotion is on. Nothing where the run scored no
    /// status objective or is not FRETISH.
    inline static std::optional<FrontGutting> final_front;
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

/// The first front of the distinct members of @p population, ranked by
/// non_domination_ranks on the objectives alone, with the members of it that
/// proved_weaker_realizable flags over the distinct members. A demoted
/// candidate counts on the front its objectives give it, so the count does not
/// read zero merely because demotion moved it. Launches no process and draws
/// nothing.
FrontGutting final_front_gutting(
    const std::vector<Scored<Specification>>& population,
    std::size_t status_index);
