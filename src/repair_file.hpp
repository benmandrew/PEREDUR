#pragma once

#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <string>

#include "fitness/function.hpp"
#include "genetic/scored.hpp"
#include "serialisation.hpp"

// Pieces of the repair_N output that the FRETISH and TLSF drivers share.

// Opens @p path for writing, throwing when it cannot be opened.
inline std::ofstream open_repair_file(const std::string& path) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open output file: " + path);
    }
    return file;
}

// The weighted total of @p scored plus each objective's score and weight, in
// registration order.
template <typename Spec>
serialisation::FitnessRecord repair_fitness_record(
    const Scored<Spec>& scored,
    const AggregateWeightedFitnessFunctionT<Spec>& fitness) {
    serialisation::FitnessRecord record;
    record.total = scored.fitness;
    std::size_t objective = 0;
    for (const WeightedFitnessFunctionT<Spec>& wff : fitness) {
        record.components.push_back(
            {wff.name, scored.objectives[objective++], wff.weight});
    }
    return record;
}
