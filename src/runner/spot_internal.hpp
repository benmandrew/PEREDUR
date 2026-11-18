#pragma once

// What spot.cpp, realizability_memo.cpp and controller_store.cpp share. Each
// function is defined, with its comment, in the file named above it.

#include <cstdint>
#include <string>
#include <vector>

#include "runner/process.hpp"
#include "runner/spot.hpp"

namespace spot_internal {

// The universal (trivially-true) automaton, in the exact HOA shape ltl2tgba
// itself emits for the constant `true`: one accepting state over zero atoms
// with a `[t]` self-loop. Substituted for the exit-2-on-tautology bug that
// is_tautology_print_error matches; parses to the same TransferSystem a
// genuine tautology would.
constexpr const char* k_universal_hoa =
    "HOA: v1\n"
    "name: \"1\"\n"
    "States: 1\n"
    "Start: 0\n"
    "AP: 0\n"
    "acc-name: all\n"
    "Acceptance: 0 t\n"
    "properties: trans-labels explicit-labels state-acc complete\n"
    "properties: deterministic stutter-invariant weak\n"
    "--BODY--\n"
    "State: 0\n"
    "[t] 0\n"
    "--END--\n";

// spot.cpp

std::string join_comma(const std::vector<std::string>& items);
bool is_tautology_print_error(const ProcessResult& result);
// The ltlsynt per-call budget in milliseconds, 0 for none.
std::int64_t ltlsynt_timeout_ms();

// realizability_memo.cpp

std::uint64_t signature_of(const std::vector<int>& ids);
bool includes_sorted(const std::vector<int>& superset,
                     const std::vector<int>& subset);
SpecificationSides fretish_sides_of(const Specification& specification);
std::string query_scope(const SpecificationSides& sides,
                        const std::vector<std::string>& inputs,
                        const std::vector<std::string>& outputs);

// controller_store.cpp

bool controller_reuse_available();

}  // namespace spot_internal
