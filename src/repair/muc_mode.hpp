#pragma once

// MUC repair mode for FRETISH, the counterpart of tlsf::internal::run_muc:
// extract a guarantee-side core, evolve only that sub-specification, put every
// core survivor back into its context, and gate each result as a repair of the
// whole specification.

#include <cstddef>
#include <string>
#include <vector>

#include "config.hpp"
#include "dashboard.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/filter_report.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "requirement.hpp"

// Run-wide counters for run.json. Zero on a monolithic run and on TLSF.
struct MucStats {
    // Reintegrated specifications whose whole-spec realizability query never
    // answered, each counted once per gate check, and those the deadline left
    // ungated.
    inline static std::size_t n_gate_undecided{0};
    // Reintegrated specifications the wall deadline left without a verdict:
    // never gated, or gated undecided and not screened to the end. Each is in
    // n_gate_undecided and none is provisional.
    inline static std::size_t n_deadline_unscreened{0};
    // provisional_N.json files written.
    inline static std::size_t n_provisional{0};
};

struct MucRepairResult {
    // Distinct gate-passing repairs of the whole specification, in the order
    // they were found.
    std::vector<Specification> repairs;
    // What the accumulator collected, under cfg.accumulate_repairs.
    std::vector<Specification> accumulated;
    // Distinct reintegrated specifications the gate could not judge but whose
    // every guarantee subset of up to cfg.muc_screen_depth was decided
    // realizable. Never among `repairs`, and never handed to the accumulator
    // or the maximality stream.
    std::vector<Specification> provisional;
    std::vector<FilterRunStats> filter_stats;
};

// Repairs @p original core by core, for up to cfg.muc_max_iterations cores.
// Each head-of-loop check asks whole-spec realizability: realizable ends the
// loop, unrealizable extracts a core (screening subsets of up to
// cfg.muc_screen_depth guarantees, then QuickXplain), and undecided screens
// the small subsets alone and ends the loop when none conflicts. @p budget is
// the run's, not a core's. @p output_dir and @p sink mean what they mean to
// run_evolution, and receive only confirmed repairs.
MucRepairResult run_fretish_muc(const Config& cfg,
                                const Specification& original,
                                RandomSource& random_source,
                                DashboardWriter& dashboard,
                                const std::string& output_dir,
                                SearchBudget& budget,
                                RepairAccumulator<Specification>::Sink sink);
