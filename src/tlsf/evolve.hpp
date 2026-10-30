#pragma once

// The search itself: the per-generation filter set and the generation loop
// TLSF repair runs over a specification.

#include <cstddef>
#include <string>
#include <vector>

#include "config.hpp"
#include "dashboard.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/filter_report.hpp"
#include "genetic/generation.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "tlsf/specification.hpp"

namespace tlsf::internal {

// Says where one evolve run should report its progress. A null writer
// disables reporting entirely.
struct DashboardProgress {
    DashboardWriter* writer = nullptr;
    std::vector<std::string> objective_names;
};

// Evolves `spec` under `cfg` against `fitness`, returning the final scored
// population and, via `filter_stats_out`, this run's per-filter in/out totals.
//
// Gate-passing candidates are offered to `accumulator_out` at the end of each
// generation. Unlike the FRETISH path this costs a gate sweep the run would not
// otherwise make, so it happens only when the accumulator is enabled.
std::vector<Scored<Specification>> evolve_population(
    const Specification& spec, const Config& cfg,
    const RandomSource& random_source,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    std::vector<FilterRunStats>& filter_stats_out,
    const DashboardProgress& progress,
    RepairAccumulator<Specification>& accumulator_out, SearchBudget& budget);

}  // namespace tlsf::internal
