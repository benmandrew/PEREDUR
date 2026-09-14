#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "config.hpp"

// The two top-level repair runs PEREDUR dispatches to, each returning the
// process exit status.
//
// @p seed is already parsed, so a malformed --seed cannot reach a run: absent
// means seed from the random device.

int run_tlsf_repair(const Config& cfg, const std::string& input_path,
                    const std::string& output_dir,
                    const std::optional<std::size_t>& seed);

int run_fretish_repair(const Config& cfg, const std::string& input_path,
                       const std::string& output_dir,
                       const std::optional<std::size_t>& seed);
