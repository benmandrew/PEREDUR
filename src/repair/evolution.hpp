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
// summed over every generation, and the gate-passing candidates seen along the
// way.
struct EvolutionResult {
    std::vector<ScoredSpecification> population;
    std::vector<FilterRunStats> filter_stats;
    std::vector<Specification> accumulated;
};

// @p output_dir is where the accumulator streams each gate-passing candidate
// as it finds it. @p sink is handed each candidate as it is accumulated, with
// the name of the file it went to.
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

// The final screen where the implication filter is off: deduplication alone,
// with its row for the filter report.
std::pair<std::vector<Specification>, std::vector<FilterRunStats>>
dedup_realizable_specifications(
    const std::vector<Specification>& realizable_vec);

// The final screens where the implication filter is on, from @p stream: pushes
// the realizable specifications the accumulator did not already hand it, waits
// for the last batch, and returns the maximal set in @p realizable_vec's order
// with a row each for deduplication and implication.
std::pair<std::vector<Specification>, std::vector<FilterRunStats>>
finish_maximal_stream(StreamingMaximalFilter<Specification>& stream,
                      const std::vector<Specification>& realizable_vec);

// Prints, for each repair in the order write_specifications numbered them, its
// file name and then what it changed against @p original: a `-` line for each
// live requirement of the original the repair no longer has and a `+` line for
// each one only the repair has. Requirements are matched by equality within a
// side, never by position.
void print_repair_changes(const Specification& original,
                          const std::vector<ScoredSpecification>& scored);

void write_specifications(
    const std::vector<ScoredSpecification>& scored,
    const AggregateWeightedFitnessFunction& fitness_function,
    const std::string& output_dir);
