#pragma once

// Screens over a scored population: which candidates count as repairs, and the
// two final passes that reduce the repairs to the ones worth writing out.

#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "filter/streaming_maximal.hpp"
#include "fitness/function.hpp"
#include "genetic/scored.hpp"
#include "runner/black.hpp"
#include "tlsf/specification.hpp"

namespace tlsf::internal {

// Realizable survivors of the population, deduplicated by value while
// preserving fitness order.
std::vector<Scored<Specification>> realizable_survivors(
    const std::vector<Scored<Specification>>& population, const Config& cfg,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness);

// Adds the accumulated repairs @p survivors does not already hold, scoring each
// against the original for output and reordering the whole set under
// cfg.selection_scheme. They passed the gate in the generation they were
// collected in, so they are not re-checked.
std::vector<Scored<Specification>> merge_accumulated_survivors(
    std::vector<Scored<Specification>> survivors,
    const std::vector<Specification>& accumulated, const Config& cfg,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness);

// Keeps the survivors not dominated by another, mirroring the FRETISH final
// implication filter. Equivalent survivors collapse to the one closest to
// @p original under syntactic similarity.
std::vector<Scored<Specification>> keep_maximal(
    const std::vector<Scored<Specification>>& survivors,
    const Specification& original, const Config& cfg,
    SatisfiabilityChecker& checker);

// What keep_maximal returns, from @p stream instead: pushes the survivors
// the accumulator did not already hand it and waits for the last batch.
std::vector<Scored<Specification>> finish_maximal_stream(
    const std::vector<Scored<Specification>>& survivors,
    StreamingMaximalFilter<Specification>& stream);

}  // namespace tlsf::internal
