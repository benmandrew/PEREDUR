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

// Which population entries pass the output gate -- realizable and clear of
// every correctness-table row -- by index, one byte per candidate. Evaluated
// concurrently, and the verdicts are collected by index, so the answer does not
// depend on how the queries interleaved. Exposed so that the accumulator can
// ask the same question the final collection asks rather than a second one of
// its own.
std::vector<char> gate_verdicts(
    const std::vector<Scored<Specification>>& population, const Config& cfg);

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

// keep_maximal runs during the search, fed from the accumulator, or null
// where the key is off. Both repair modes stream: the MUC loop accumulates
// every gate-passing repair it reintegrates into a whole specification, which
// is a candidate in the same sense the monolithic path's are.
std::unique_ptr<StreamingMaximalFilter<Specification>> make_maximal_stream(
    const Specification& original, const Config& cfg,
    const std::string& output_dir);

// What keep_maximal returns, from @p stream instead: pushes the survivors
// the accumulator did not already hand it and waits for the last batch.
std::vector<Scored<Specification>> finish_maximal_stream(
    const std::vector<Scored<Specification>>& survivors,
    StreamingMaximalFilter<Specification>& stream);

}  // namespace tlsf::internal
