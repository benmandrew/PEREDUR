#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "dashboard.hpp"
#include "filter/streaming_maximal.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/filter_report.hpp"
#include "genetic/generation.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "requirement.hpp"

// The FRETISH search, from the seed population to the specifications written
// out: everything between reading the input and reporting on the run.

// The objective names in registration order, used to label the per-objective
// means the dashboard reports. Read off the fitness function rather than a
// second hardcoded list, so a new objective needs no change here.
std::vector<std::string> fitness_objective_names(
    const AggregateWeightedFitnessFunction& fitness_function);

std::vector<ScoredSpecification> original_population(
    Specification& original_spec,
    const AggregateWeightedFitnessFunction& fitness_function,
    std::size_t population_size);

// What one search leaves behind: the final population, the per-filter totals
// summed over every generation, and -- only under cfg.accumulate_repairs -- the
// gate-passing candidates seen along the way.
struct EvolutionResult {
    std::vector<ScoredSpecification> population;
    std::vector<FilterRunStats> filter_stats;
    std::vector<Specification> accumulated;
};

// @p output_dir is where the accumulator streams each gate-passing candidate
// as it finds it, under cfg.accumulate_repairs; nothing is created there
// otherwise. @p sink is handed each candidate as it is accumulated, with the
// name of the file it went to.
EvolutionResult run_evolution(
    const Config& cfg, std::vector<ScoredSpecification> population,
    const AggregateWeightedFitnessFunction& fitness_function,
    const std::vector<FilterFunction>& filter_functions,
    RandomSource& random_source, DashboardWriter& dashboard,
    const std::string& output_dir, SearchBudget& budget,
    RepairAccumulator<Specification>::Sink sink = {});

// The gate. @p cfg supplies the status grading, which is the run's rather than
// a fixed one, so the output is judged on the scale the search scored on.
std::vector<Specification> collect_realizable_specifications(
    const Config& cfg, const std::vector<ScoredSpecification>& population);

// Applies the final screens to the realizable specifications: deduplication,
// then the implication (maximality) filter when run_implication_filter is set.
std::pair<std::vector<Specification>, std::vector<FilterRunStats>>
filter_maximal_specifications(const Config& cfg, const Specification& original,
                              const std::vector<Specification>& realizable_vec);

// What filter_maximal_specifications returns, from @p stream instead: pushes
// the realizable specifications the accumulator did not already hand it, waits
// for the last batch, and returns the maximal set in @p realizable_vec's order
// with the same per-screen accounting.
std::pair<std::vector<Specification>, std::vector<FilterRunStats>>
finish_maximal_stream(StreamingMaximalFilter<Specification>& stream,
                      const std::vector<Specification>& realizable_vec);

// Writes each of @p scored to `<output_dir>/<prefix>N.json`, N its position.
void write_specifications(
    const std::vector<ScoredSpecification>& scored,
    const AggregateWeightedFitnessFunction& fitness_function,
    const std::string& output_dir, const std::string& prefix = "repair_");
