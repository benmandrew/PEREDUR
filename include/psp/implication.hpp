#pragma once

/// @file psp/implication.hpp
/// @brief The implication order over PSP requirements and specifications,
///        decided by rule: the pattern and scope orders, the polarities of P,
///        S, Q and R, and the search that chains them.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "psp/requirement.hpp"
#include "psp/specification.hpp"

namespace psp {

/// The scopes that share one pattern order. `scripts/psp_field_orders.py`
/// measures three (`pattern_order_classes` in `docs/dev/psp-orders.json`):
/// under before, R at the first point leaves the scope empty, so universality
/// no longer implies existence; under between and after-until a boundary
/// discharges bounded recurrence, so it implies no response or existence.
enum class ScopeClass : std::uint8_t {
    /// Globally and after, which close no scope.
    Unbounded,
    /// Between and after-until.
    Bounded,
    /// Before.
    Before,
};

/// The class @p scope's pattern order is that of.
ScopeClass scope_class(PspScopeKind scope);

/// The patterns that share one scope order (`scope_order_cells` in
/// `docs/dev/psp-orders.json`).
enum class PatternCell : std::uint8_t {
    /// Absence, universality, bounded invariance, bounded existence and
    /// bounded recurrence: globally above after above after-until above
    /// between, and globally above before.
    Invariant,
    /// Existence: after-until above between only.
    Existence,
    /// Precedence: after-until above between, and globally above before.
    Precedence,
    /// Response and bounded response: after-until above between, and
    /// globally above after.
    Response,
};

/// The cell @p pattern's scope order is that of, at any count.
PatternCell pattern_cell(const Pattern& pattern);

/// Whether @p stronger implies @p weaker over the same P, S, Q and R under
/// every scope of @p in_class. Reflexive. Within a bounded pattern, bounded
/// response, bounded existence and bounded recurrence weaken as the count
/// grows and bounded invariance strengthens. Measured at counts 1 to 4; the
/// rule extends each count relation to every count.
///
/// The edges hold over atoms that the two sides need not share, so they hold
/// when a field one side lacks takes any value on the other.
bool pattern_implies(const Pattern& stronger, const Pattern& weaker,
                     ScopeClass in_class);

/// Whether @p stronger implies @p weaker over the same pattern, P, S, Q and R,
/// for every pattern of @p cell. Reflexive.
bool scope_implies(PspScopeKind stronger, PspScopeKind weaker,
                   PatternCell cell);

/// The four formula fields of a pattern requirement.
enum class FormulaField : std::uint8_t { P, S, Q, R };

/// How a requirement moves when one formula field weakens propositionally.
enum class Polarity : std::uint8_t {
    /// Weakening the field weakens the requirement.
    Monotone,
    /// Weakening the field strengthens the requirement.
    Antitone,
    /// Neither: only an equivalent formula keeps the requirement.
    None,
};

/// The polarity of @p field in the cell (@p pattern, @p scope), as
/// `docs/dev/psp-orders.json` records it, or nothing where the pattern or
/// scope does not read the field. P is antitone and S monotone wherever they
/// occur, except that P has no polarity in bounded existence. Q is antitone
/// except under after for existence and precedence. R has no polarity under
/// before and between, except that it is antitone in existence before R, and
/// it is monotone under after-until except for existence, response and
/// bounded response.
std::optional<Polarity> field_polarity(FormulaField field,
                                       const Pattern& pattern,
                                       PspScopeKind scope);

/// Whether requirement @p stronger implies @p weaker by rule, without asking a
/// solver. True only where it is proven, so false means unproven.
///
/// The rule moves one of the six fields (pattern with its count, scope, P, S,
/// Q and R) at a time from @p stronger towards @p weaker, each step an
/// implication in the context the other fields give it: the pattern along
/// pattern_implies under the current scope's class, the scope along
/// scope_implies in the current pattern's cell, and each formula along its
/// field_polarity in the current cell. Every order of the steps is tried,
/// since a step can hold only after another one. P0 found no implication
/// that needs two fields moved at once, so no step does.
///
/// A field that one side does not read takes the other side's value, so it
/// never needs a step of its own. The propositional steps are decided by
/// formula_implies, so a formula wider than `k_exhaustive_count_max_atoms`
/// atoms does not move.
///
/// A fixed requirement implies another only where the two lower identically.
/// Tombstones are not read here: the specification-level checks skip them.
bool requirement_implies(const Requirement& stronger,
                         const Requirement& weaker);

/// A proof of one PSP requirement implying another, in the shape of
/// requirement_implies. Callers pass a memoised one where they ask the same
/// pairs repeatedly.
using RequirementImplication =
    std::function<bool(const Requirement&, const Requirement&)>;

/// The lowerings of the live entries of @p reqs: the pool the rule cover
/// reads.
std::unordered_set<std::string> live_lowerings(
    const std::vector<Requirement>& reqs);

/// Whether specification @p from implies @p dest by rule alone: every live
/// assumption of @p from is covered by @p dest's assumptions, and every live
/// guarantee of @p dest by @p from's guarantees. An entry is covered by one
/// with the same lowering anywhere in the pool, or by the live entry in its
/// own slot where @p implies proves it. A tombstone is a `true` requirement,
/// so it needs no cover and covers nothing. Sound and incomplete, and never
/// launches a process: false means unproven.
///
/// @param from              The candidate stronger specification.
/// @param dest              The candidate weaker specification.
/// @param from_guarantees   live_lowerings of `from.m_guarantees`.
/// @param dest_assumptions  live_lowerings of `dest.m_assumptions`.
/// @param implies           The proof asked of each pair of slots, such as
///                          requirement_implies.
bool spec_implies_by_rule(
    const Specification& from, const Specification& dest,
    const std::unordered_set<std::string>& from_guarantees,
    const std::unordered_set<std::string>& dest_assumptions,
    const RequirementImplication& implies);

/// spec_implies_by_rule over the live lowerings of the two specifications,
/// with requirement_implies as the proof.
bool spec_implies_by_rule(const Specification& from, const Specification& dest);

/// @p dest with every live guarantee that @p from covers by rule marked
/// removed, or nothing where that would change the implication query's answer
/// or drop nothing. The answer is unchanged when every live assumption of
/// @p from is covered by @p dest's, for the reason the FRETISH
/// `residual_destination` gives.
std::optional<Specification> residual_destination(const Specification& from,
                                                  const Specification& dest);

}  // namespace psp
