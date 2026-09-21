#pragma once

// The two TLSF repair strategies. Both take the parsed original and return the
// repairs found, scored against it, for the driver to screen and write out.

#include <string>
#include <vector>

#include "config.hpp"
#include "evolve.hpp"
#include "filter/streaming_maximal.hpp"
#include "fitness/function.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "tlsf/specification.hpp"

namespace tlsf::internal {

// Monolithic repair: evolve the whole spec once, collect realizable survivors.
// @p output_dir is where the accumulator streams each gate-passing candidate as
// it finds it, under cfg.accumulate_repairs; nothing is created there
// otherwise. A non-null @p stream is handed each candidate as it is
// accumulated.
std::vector<Scored<Specification>> run_monolithic(
    const Specification& original, const Config& cfg,
    const RandomSource& random_source,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    const DashboardProgress& progress, const std::string& output_dir,
    SearchBudget& budget, StreamingMaximalFilter<Specification>* stream);

// MUC repair: iteratively extract a minimal unrealizable core, evolve only that
// sub-specification, reintegrate *every* realizable-on-sub-spec repair with the
// untouched non-core guarantees, and gate each as a repair of the original;
// the loop then continues on the first that passed, until it is realizable or
// the iteration cap trips. Returns every gate-passing repair found across the
// iterations, scored against the original, or empty if none was found.
//
// @p output_dir and @p stream carry the same meaning as in run_monolithic: what
// they receive are whole reintegrated specifications, so accumulation and the
// maximality stream mean here what they mean there.
std::vector<Scored<Specification>> run_muc(
    const Specification& original, const Config& cfg,
    const RandomSource& random_source,
    const AggregateWeightedFitnessFunctionT<Specification>& output_fitness,
    const DashboardProgress& progress, const std::string& output_dir,
    SearchBudget& budget, StreamingMaximalFilter<Specification>* stream);

}  // namespace tlsf::internal
