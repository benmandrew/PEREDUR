#pragma once

// The output gate both front ends judge their repairs by, and the streaming
// final screen fed from what it admits. Instantiated for Specification and
// tlsf::Specification.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"
#include "filter/streaming_maximal.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/scored.hpp"

// True if @p spec counts as a repair: its status on cfg.status_grading is the
// top tier and it passes every correctness-table row.
template <typename Spec>
bool passes_output_gate(const Spec& spec, const Config& cfg);

// passes_output_gate of every population entry, by index, one byte per
// candidate. Evaluated concurrently, and the verdicts are collected by index,
// so the answer does not depend on how the queries interleaved.
template <typename Spec>
std::vector<char> gate_verdicts(const std::vector<Scored<Spec>>& population,
                                const Config& cfg);

// Offers every gate-passing entry of @p population to @p accumulator, in
// population order, and returns how many passed. Empty, and nothing asked, when
// the accumulator is disabled.
template <typename Spec>
std::optional<std::size_t> accumulate_gate_passing(
    const std::vector<Scored<Spec>>& population, const Config& cfg,
    std::size_t generation, RepairAccumulator<Spec>& accumulator);

// The implication filter run during the search, fed from the accumulator, or
// null where implication_streams(cfg) is false.
template <typename Spec>
std::unique_ptr<StreamingMaximalFilter<Spec>> make_maximal_stream(
    const Spec& original, const Config& cfg, const std::string& output_dir);
