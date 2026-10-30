#pragma once

/// @file pipeline.hpp
/// @brief End-to-end TLSF genetic-repair pipeline: parse, evolve, collect the
///        realizable survivors, and write them as repair files.

#include <string>

#include "config.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"

namespace tlsf {

/// Runs the TLSF genetic-repair pipeline on the specification at @p input_path.
///
/// Parses the input (a basic-TLSF document), evolves a population of
/// `cfg.population_size` candidates for `cfg.generations` rounds under the TLSF
/// fitness, filters, and genetic operators, collects the realizable survivors
/// (deduplicated), and writes each to `<output_dir>/repair_N.tlsf` with a
/// sidecar `<output_dir>/repair_N.fitness.json` recording its fitness. Progress
/// and a summary line are printed to stdout. A final implication pass reduces
/// the survivors to the maximal ones before they are written, unless
/// `cfg.run_implication_filter` is unset.
///
/// @param input_path    Path to the input TLSF file.
/// @param output_dir    Existing directory for the repair outputs.
/// @param cfg           Algorithm configuration.
/// @param random_source Seeded random source driving crossover and mutation.
/// @param budget        The run's termination budget, owned by the caller so it
///                      can be read back for the manifest.
/// @return 0 on success; 1 on a read or parse error.
int run_repair(const std::string& input_path, const std::string& output_dir,
               const Config& cfg, const RandomSource& random_source,
               SearchBudget& budget);

}  // namespace tlsf
