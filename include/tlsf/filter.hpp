#pragma once

/// @file filter.hpp
/// @brief The TLSF vacuity tests and implication check. Deduplication, the
///        bloat cap, well-separation, the implication filter and the
///        correctness table are shared with the FRETISH path and instantiated
///        for tlsf::Specification in the `filter/` headers.

#include <cstddef>
#include <optional>

#include "genetic/generation.hpp"
#include "runner/black.hpp"
#include "tlsf/specification.hpp"

/// Whether @p spec carries a section formula that is a trivial literal: `false`
/// in an assumption section (INITIALLY, REQUIRE, ASSUME), or `true` in a
/// guarantee section (PRESET, ASSERT, GUARANTEE). Either makes
/// `(assumptions) -> (guarantees)` hold for free — the first falsifies the
/// antecedent, the second contributes a no-op conjunct to the consequent.
/// A tlsf::Specification has no condition/response split, so this is where the
/// FRETISH false-condition and true-guarantee tests land on this path.
///
/// Purely syntactic, so it costs no solver call and is checked first. `true`
/// and `false` are ordinary atoms in this AST. Both cases are subsumed by the
/// semantic tests below — a `false` conjunct makes the assumption side
/// unsatisfiable, a `true` one makes a guarantee valid — and are kept as their
/// fast path. Unlike on the FRETISH path, where a `false` *condition* lowers to
/// a satisfiable assumption and nothing semantic rejects it.
bool tlsf_is_trivially_vacuous(const tlsf::Specification& spec);

/// Whether @p spec's assumption-side conjunction (INITIALLY, REQUIRE, ASSUME)
/// is unsatisfiable, making the spec vacuously realizable: a false antecedent
/// turns `(assumptions) -> (guarantees)` into a tautology whatever the
/// guarantees say, so such a spec is not a repair. The TLSF counterpart of
/// specification_has_unsatisfiable_assumptions, and the same check — the two
/// differ only in how they reach the assumption conjunction.
///
/// Conservative under uncertainty: a spec with no assumption formulae, or one
/// whose satisfiability check times out, is reported as not vacuous, so a slow
/// check never silently discards a candidate.
bool tlsf_has_unsatisfiable_assumptions(const tlsf::Specification& spec,
                                        SatisfiabilityChecker& checker);

/// Whether any single guarantee-section formula of @p spec (PRESET, ASSERT,
/// GUARANTEE) is *valid* — its negation unsatisfiable — and so demands nothing
/// of the system. The TLSF counterpart of specification_has_valid_guarantee,
/// including its reasons for splitting the guarantee side per formula while
/// the assumption side stays one joint satisfiability query. ASSERT is
/// G-wrapped by the lowering, but `G psi` is valid exactly when psi is, so the
/// raw section formula is the query. Returns on the first valid formula found;
/// a formula whose check times out is read as falsifiable.
bool tlsf_has_valid_guarantee(const tlsf::Specification& spec,
                              SatisfiabilityChecker& checker);

/// Whether @p spec is vacuous by any of the tests above, cheapest first: the
/// syntactic screen costs nothing, the guarantee check is a small query against
/// a cache keyed per section formula, and the assumption conjunction is one
/// large query whose key changes whenever any assumption mutates. The TLSF
/// counterpart of specification_is_vacuous.
///
/// Shared by the per-generation filter below and the final repair screen in
/// tlsf::run_repair — the filter can be disabled outright, and elites bypass
/// the offspring filters anyway, so the screen cannot rely on it having seen
/// the specifications it is about to write out.
bool tlsf_is_vacuous(const tlsf::Specification& spec,
                     SatisfiabilityChecker& checker);

/// Returns a filter dropping the specifications tlsf_is_vacuous accepts — the
/// TLSF counterpart of make_vacuity_filter, carrying the same "vacuity" stage
/// name so filter reports and dashboard labels line up across the two paths. A
/// spec with no assumption formulae is kept; an uncertain (timed-out)
/// satisfiability result is treated as satisfiable and the spec is kept.
///
/// Runs every generation, as on the FRETISH path, and the final repair screen
/// applies the predicate again to whatever reaches it.
///
/// @param max_in_flight Concurrent checks. Each spec carrying assumptions costs
///                      a `black` subprocess on a cache miss, and the miss rate
///                      rises with population diversity, so a serial sweep here
///                      dominates a diverse run. 1 evaluates serially.
FilterFunctionT<tlsf::Specification> tlsf_make_vacuity_filter(
    std::size_t max_in_flight = 1);

/// Whether spec @p from logically implies spec @p dest: true when
/// `(from.to_ltl()) & !(dest.to_ltl())` is unsatisfiable, false when
/// satisfiable, nullopt when the black query times out. Unlike the FRETISH
/// assume-guarantee decomposition this is a complete whole-formula check
/// (tlsf::Specification lowers to a single LTL formula via to_ltl()).
std::optional<bool> tlsf_spec_implies(const tlsf::Specification& from,
                                      const tlsf::Specification& dest,
                                      SatisfiabilityChecker& checker);
