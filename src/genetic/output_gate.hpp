#pragma once

// The output gate both front ends judge their repairs by, and the streaming
// final screen fed from what it admits. Instantiated for Specification and
// tlsf::Specification.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"
#include "filter/streaming_maximal.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/scored.hpp"
#include "requirement.hpp"

// True if @p spec counts as a repair: its status on cfg.status_grading is the
// top tier and it passes every correctness-table row.
template <typename Spec>
bool passes_output_gate(const Spec& spec, const Config& cfg);

// The gate with its realizability query's undecided answer kept apart, which
// FRETISH MUC repair needs to tell a spec that fails the gate from one the gate
// could not judge.
enum class GateVerdict : std::uint8_t { Pass, Fail, Undecided };

// Pass exactly where passes_output_gate holds. Asks whole-specification
// realizability first: false is Fail, and undecided is Undecided where every
// correctness-table row passes and Fail otherwise. Only a decided realizable
// specification goes on to the status query, whose subset walk under
// status_grading = "mrs" the realizability memo then answers by subsumption
// rather than by asking ltlsynt about near-whole specifications one by one.
GateVerdict output_gate_verdict(const Specification& spec, const Config& cfg);

// output_gate_verdict of every population entry, by index, evaluated
// concurrently as gate_verdicts is and launched in index order. @p stop is
// asked before each entry starts; once it holds, no further entry starts and
// the rest are nullopt. A running entry finishes. Empty never stops.
std::vector<std::optional<GateVerdict>> output_gate_verdicts(
    const std::vector<Scored<Specification>>& population, const Config& cfg,
    const std::function<bool()>& stop = {});

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
