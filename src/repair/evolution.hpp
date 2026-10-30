#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "fitness/function.hpp"
#include "genetic/generation.hpp"
#include "requirement.hpp"

// The FRETISH-only pieces of a run: its seed population, and how its repairs
// are written out and described.

std::vector<ScoredSpecification> original_population(
    Specification& original_spec,
    const AggregateWeightedFitnessFunction& fitness_function,
    std::size_t population_size);

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
