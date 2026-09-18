#pragma once

/// @file mucs.hpp
/// @brief Minimal Unrealizable Core (MUC) extraction for TLSF specifications.
///
/// Given an unrealizable tlsf::Specification, extract_muc() returns the
/// smallest subset of the *system* obligations (the guarantee-side sections
/// PRESET, ASSERT, GUARANTEE) that is still unrealizable when checked against
/// the *full*, unchanged environment side (INITIALLY, REQUIRE, ASSUME). The
/// environment side is held fixed in every realizability query: relaxing an
/// assumption can only make synthesis harder, so it is never part of the core.
///
/// The extractor is QuickXplain (Junker 2004): O(k·log(n/k)) oracle calls for
/// a core of size k out of n guarantee-side formulae. It relies on
/// unrealizability being monotone in the guarantee-side set — adding a system
/// obligation can never restore realizability — which makes a minimal
/// unrealizable subset well defined.

#include <cstddef>
#include <functional>
#include <vector>

#include "prop_formula.hpp"
#include "tlsf/specification.hpp"

namespace tlsf {

/// A guarantee-side formula tagged with the section it came from, so the core
/// can be reconstructed into a valid Specification and reported per section.
/// `section_id` follows the six-section index convention shared with the TLSF
/// fitness code: 1 = PRESET, 4 = ASSERT, 5 = GUARANTEE (the only sections a
/// core can contain).
struct CoreFormula {
    std::size_t section_id = 0;
    Formula formula;
};

/// The result of MUC extraction: the minimal culprit formulae, and a rebuilt
/// Specification holding the full environment side plus exactly those formulae
/// (unrealizable, and minimal — dropping any one member makes it realizable).
///
/// `n_undecided` counts the probes the oracle could not decide. QuickXplain
/// reads an undecided probe as a conflict, which keeps it shrinking, so a core
/// extracted over one was never confirmed unrealizable: at a 10s probe budget
/// on a large specification that is the difference between a core and a guess.
/// Non-zero means the result is provisional, and callers say so.
struct MinimalUnrealizableCore {
    std::vector<CoreFormula> formulae;
    Specification spec;
    std::size_t n_undecided = 0;
};

/// Realizability oracle: returns true iff `spec` is realizable. Matches the
/// sense of RealizabilityChecker::check_realizability_ltl, minus its undecided
/// case, which the oracle's author resolves. Injected so the extractor's logic
/// is testable without invoking ltlsynt.
using RealizabilityOracle = std::function<bool(const Specification&)>;

/// Extracts a minimal unrealizable core from `spec` using the given oracle.
/// Precondition (asserted): `spec` is unrealizable. When `spec` has no
/// guarantee-side formulae the core is empty.
MinimalUnrealizableCore extract_muc(const Specification& spec,
                                    const RealizabilityOracle& is_realizable);

/// As above, screening the single-formula subsets @p max_in_flight at a time
/// before the walk. A formula that is a conflict on its own is a minimal core,
/// returned without walking to it; the rest leave their verdicts in the
/// oracle's own caches, which the serial walk then reads. QuickXplain itself
/// stays serial — each of its probes chooses the next — so this is the only
/// concurrency the extraction has.
///
/// @p max_in_flight <= 1 screens nothing and calls the oracle only from this
/// thread. Above 1 the oracle is called concurrently and must be thread-safe.
/// The result does not depend on which probe answered first.
MinimalUnrealizableCore extract_muc(const Specification& spec,
                                    const RealizabilityOracle& is_realizable,
                                    std::size_t max_in_flight);

/// As above, using the process-wide RealizabilityChecker (ltlsynt) as oracle.
MinimalUnrealizableCore extract_muc(const Specification& spec);

/// The guarantee-side formulae of `spec` NOT present in `core` (set difference
/// by value, one occurrence removed per core member). These are the formulae a
/// MUC repair carries over untouched when reintegrating a repaired core.
std::vector<CoreFormula> non_core_formulae(
    const Specification& spec, const std::vector<CoreFormula>& core);

/// Reintegrates a repaired sub-specification into the full context by appending
/// the carried-over `non_core` formulae to the matching sections of
/// `repaired_subspec`. Pair with non_core_formulae() taken from the spec the
/// core was extracted from.
Specification reintegrate(const Specification& repaired_subspec,
                          const std::vector<CoreFormula>& non_core);

/// Human-readable name of a section id, for reporting (e.g. "GUARANTEE").
const char* section_name(std::size_t section_id);

}  // namespace tlsf
