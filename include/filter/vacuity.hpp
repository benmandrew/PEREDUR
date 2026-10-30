#pragma once

/// @file vacuity.hpp
/// @brief Detection of vacuously-realizable specifications: ones carrying a
///        false condition or a valid guarantee, or whose assumptions are
///        jointly unsatisfiable.

#include "genetic/generation.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"

/// Returns whether @p specification carries a syntactically trivial
/// requirement: a condition that is the literal atom `false`, in either an
/// assumption or a guarantee. See specification_has_false_condition.
///
/// Purely syntactic, so it costs no solver call and is checked first. It is
/// the assumption side that needs it: a vacuously-true assumption is
/// satisfiable, so no semantic test here rejects one. On the guarantee side
/// specification_has_valid_guarantee below decides the same case and more.
///
/// There is deliberately no syntactic test for a `true` guarantee response.
/// Whether one is vacuous depends on the timing -- the AfterTicks lowering
/// negates the response, making `true` the strongest guarantee rather than a
/// no-op -- and only the semantic check reads the lowered formula.
bool specification_is_trivially_vacuous(const Specification& specification);

/// Returns whether @p specification's assumptions are jointly unsatisfiable.
///
/// Realizability is decided on `(assumptions) -> (guarantees)`, so a
/// specification whose assumptions contradict one another is realizable for
/// free: a false antecedent makes the implication a tautology regardless of
/// the guarantees. Such a specification is not a repair, and the weakening
/// filter cannot reject it — an unsatisfiable assumption implies every other
/// assumption, so it passes every implication test.
///
/// Only assumptions are checked. Unsatisfiable *guarantees* need no guard:
/// they make the implication unsatisfiable and so are already reported
/// unrealizable, which the search punishes on its own.
///
/// Conservative under uncertainty: a specification with no assumptions, or one
/// whose satisfiability check times out, is reported as not vacuous, so a slow
/// check never silently discards a candidate.
///
/// @param specification The specification to test
/// @param checker       Satisfiability checker for the LTL query; must be
///                      thread-safe when called concurrently
bool specification_has_unsatisfiable_assumptions(
    const Specification& specification, SatisfiabilityChecker& checker);

/// Returns whether any single guarantee of @p specification is *valid* — its
/// negation unsatisfiable — and so demands nothing of the system. This is the
/// only test that catches a gutted response, and it reads the lowered formula
/// rather than the response, which is what makes it right per timing: a `true`
/// response is a no-op under Immediately or Always and the strongest possible
/// guarantee under AfterTicks, whose lowering negates it.
///
/// Tested per guarantee rather than over the guarantee conjunction, and the
/// two are not interchangeable. Per-guarantee rejects a spec that guts one
/// conjunct into a no-op while the rest still constrain the system, which is
/// exactly what mutation reaches and what the conjunction test would miss —
/// the conjunction is valid only when every guarantee is. It is also the
/// cheaper query: smaller formulae, and a cache key per requirement rather than
/// per guarantee side, so a guarantee carried over from a parent is free.
/// Returns on the first valid guarantee found.
///
/// The assumption side takes no such split. That check is joint
/// *satisfiability*, which does not distribute: `G p` and `G !p` are each
/// satisfiable and jointly are not.
///
/// Conservative under uncertainty: a guarantee whose check times out is read
/// as falsifiable, so a slow check never silently discards a candidate.
///
/// @param specification The specification to test
/// @param checker       Satisfiability checker for the LTL queries; must be
///                      thread-safe when called concurrently
bool specification_has_valid_guarantee(const Specification& specification,
                                       SatisfiabilityChecker& checker);

/// Returns whether @p specification is vacuous by any of the tests above,
/// cheapest first: the syntactic screen costs nothing, the guarantee check is
/// a small query against a cache keyed per requirement, and the assumption
/// conjunction is one large query whose key changes whenever any assumption
/// mutates.
///
/// Shared by the per-generation filter below and the final repair screen in
/// collect_realizable_specifications — elites bypass the offspring filters, so
/// the screen cannot rely on the filter having seen what it is about to write
/// out.
bool specification_is_vacuous(const Specification& specification,
                              SatisfiabilityChecker& checker);
