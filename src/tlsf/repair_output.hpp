#pragma once

// What a finished TLSF run leaves behind: the repair files in the output
// directory, and what each one changed.

#include <cstddef>
#include <string>
#include <vector>

#include "fitness/function.hpp"
#include "genetic/scored.hpp"
#include "tlsf/specification.hpp"

namespace tlsf::internal {

// Writes each survivor to `<output_dir>/repair_N.tlsf` with a sidecar
// `repair_N.fitness.json` holding its per-objective breakdown.
void write_survivors(
    const std::vector<Scored<Specification>>& survivors,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    const std::string& output_dir);

// Prints, for each survivor in the order write_survivors numbered them, its
// file name and then what it changed against @p original.
void print_repair_changes(const Specification& original,
                          const std::vector<Scored<Specification>>& survivors);

}  // namespace tlsf::internal
