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
// population order, and returns how many passed. Zero, and nothing asked, when
// the accumulator is disabled; a caller that needs to tell "none passed" from
// "nothing was counted" reads accumulator.enabled() itself, which is what the
// only such caller already does to decide whether it has a column to print.
//
// A plain count rather than an optional because gcc 11 cannot correlate
// std::optional's engaged flag with its payload across this explicit
// instantiation's return, and dereferencing the result raised
// -Wmaybe-uninitialized under -Werror.
template <typename Spec>
std::size_t accumulate_gate_passing(
    const std::vector<Scored<Spec>>& population, const Config& cfg,
    std::size_t generation, RepairAccumulator<Spec>& accumulator);

// The implication filter run during the search, fed from the accumulator, or
// null where implication_streams(cfg) is false.
template <typename Spec>
std::unique_ptr<StreamingMaximalFilter<Spec>> make_maximal_stream(
    const Spec& original, const Config& cfg, const std::string& output_dir);
