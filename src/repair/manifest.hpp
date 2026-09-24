#pragma once

/// @file manifest.hpp
/// @brief Writes the per-run provenance record beside a run's repairs.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "config.hpp"
#include "genetic/pipeline.hpp"

/// The manifest's filename within a run's output directory. Exposed because
/// that directory is also the one `compare --repairs` is pointed at, so every
/// reader walking it for repairs has to know which entry is not one.
inline constexpr const char* k_run_manifest_name = "run.json";

/// Which of the two repair paths a run took, for the config keys only the
/// other one reads.
enum class RepairInput : std::uint8_t { Fretish, Tlsf };

/// Dotted paths of the config keys that @p input never reads and that @p cfg
/// sets to something other than the built-in default, in a fixed order.
///
/// The key set is one shared schema, so a config meant for one path is
/// accepted by the other. Campaign configs and example-config.toml set every
/// key, so a key the path ignores is only worth mentioning when its value
/// differs from the default: that is a config that asks for something the run
/// will not do.
std::vector<std::string> ignored_non_default_keys(const Config& cfg,
                                                  RepairInput input);

/// Writes `<output_dir>/run.json`: what produced this directory.
///
/// An output directory used to name its own inputs nowhere. The seed went to
/// stdout, the effective configuration went nowhere at all, and the commit was
/// only ever available by asking the binary that had already exited. A reader
/// holding the directory alone -- an artefact reviewer, or the same user a
/// month later -- could not tell which of the run's settings produced it.
/// scripts/run_experiments.py already writes a manifest per campaign; this is
/// the same record for a single run.
///
/// Includes the per-tool call and timeout counts because the tool budgets
/// default on: a wall-clock budget makes output depend on the machine, so two
/// runs of one seed can differ legitimately, and the timeout counts are what
/// separates that from a real difference.
///
/// Reports how the run ended, from @p budget: `stopped_by`, `generations_run`
/// and `individuals_bred`. A survival analysis needs to tell a run that spent
/// its whole budget from one that finished early, and the wall time alone
/// cannot -- a run that stops on its deadline and one killed by the harness
/// look the same from outside, except that the killed one writes no manifest at
/// all.
///
/// The config block records every key the path @p input never reads as null,
/// whatever it was set to, so an archived run does not claim a setting its
/// search could not have used.
///
/// Best-effort. A directory that cannot be written warns on stderr and the run
/// still reports its repairs, which are the thing the user asked for.
void write_run_manifest(const std::string& output_dir,
                        const std::string& input_path, std::size_t seed,
                        const Config& cfg, double wall_s,
                        const SearchBudget& budget, RepairInput input);
