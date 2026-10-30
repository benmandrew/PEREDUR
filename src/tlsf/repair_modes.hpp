#pragma once

// The TLSF repair strategy. It takes the parsed original and returns the
// repairs found, scored against it, for the driver to screen and write out.

#include <string>
#include <vector>

#include "config.hpp"
#include "filter/streaming_maximal.hpp"
#include "fitness/function.hpp"
#include "genetic/output_gate.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "tlsf/specification.hpp"

namespace tlsf::internal {

// Evolves the whole spec once and returns the repairs the post-search tail
// keeps, scored against it.
// @p output_dir is where the accumulator streams each gate-passing candidate as
// it finds it. A non-null @p stream is handed each candidate as it is
// accumulated.
SearchOutput<Specification> run_monolithic(
    const Specification& original, const Config& cfg,
    const RandomSource& random_source,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    const std::string& output_dir, SearchBudget& budget,
    StreamingMaximalFilter<Specification>* stream);

}  // namespace tlsf::internal
