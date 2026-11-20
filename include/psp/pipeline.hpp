#pragma once

/// @file pipeline.hpp
/// @brief The PSP-only pieces of a repair run: its seed population, and how
///        its repairs are written out and described.

#include <cstddef>
#include <string>
#include <vector>

#include "fitness/function.hpp"
#include "genetic/scored.hpp"
#include "psp/specification.hpp"

namespace psp {

/// @p population_size copies of @p original, each scored once.
std::vector<Scored<Specification>> original_population(
    const Specification& original,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    std::size_t population_size);

/// Writes each repair as `repair_N.psp`, atom prefixes stripped and
/// tombstones dropped, and its per-objective breakdown as
/// `repair_N.fitness.json`. The breakdown carries `gutted`: the guarantee
/// slots whose scope the repair emptied and the original did not (see
/// gutted_guarantees). A gutted repair is written, not refused, and a warning
/// naming it goes to stderr. Returns the number of gutted repairs.
std::size_t write_repairs(
    const std::vector<Scored<Specification>>& repairs,
    const Specification& original,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    const std::string& output_dir);

/// Prints, for each repair in write_repairs' order, its file name and the
/// live requirements it dropped (`-`) and added (`+`) against @p original.
void print_repair_changes(const Specification& original,
                          const std::vector<Scored<Specification>>& repairs);

/// What the PSP output flagged, recorded in run.json.
struct GuttingStats {
    /// Repairs written with at least one gutted guarantee.
    inline static std::size_t n_gutted_repairs = 0;
};

}  // namespace psp
