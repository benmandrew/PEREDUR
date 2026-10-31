#pragma once

/// @file implication_check.hpp
/// @brief Pairwise implication check between two whole specifications, used
///        by the implication filter.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "requirement.hpp"
#include "runner/black.hpp"

/// Returns whether spec `from` logically implies spec `dest`, as one query
/// over the two whole-specification lowerings: `from.to_ltl() & !dest.to_ltl()`
/// is unsatisfiable exactly when the implication holds.
///
/// Returns true if implication is confirmed, false if it is definitively
/// refuted, or nullopt if the check timed out and the result is uncertain.
///
/// Exact, so a false return is a fact about the two specifications rather than
/// a limit of the check. It decomposed per requirement until 2026-09-11 --
/// each of dest's guarantees implied by *some single* guarantee of from, and
/// the assumptions the other way round -- which missed every implication
/// holding only via several requirements together, and asked 9 to 26 queries
/// per pair where this asks one. `tlsf_spec_implies` has had this shape since
/// the TLSF path existed, and the two now agree.
///
/// The query keeps that exactness while dropping what it need not ask. Where
/// every live assumption of `from` is implied by one of `dest`'s, `dest`'s
/// assumptions give `from`'s guarantees, so a guarantee they imply cannot be
/// the one that fails. The query then asks only about `dest`'s other
/// guarantees (residual_destination), and answers true with no query where
/// there are none. A repair carries most of its original's guarantees
/// verbatim, and SPOT builds an automaton for the negated side, so the shared
/// guarantees are most of that automaton.
///
/// @param from     The candidate stronger specification.
/// @param dest     The candidate weaker specification.
/// @param checker  Satisfiability checker for LTL queries; must be
///                 thread-safe when called concurrently.
std::optional<bool> spec_implies(const Specification& from,
                                 const Specification& dest,
                                 SatisfiabilityChecker& checker);

/// `dest` with every live guarantee that `from` implies marked removed, or
/// nothing where that would change the query's answer or drop nothing. A
/// requirement implies another here when the two lower identically, or when
/// requirement_implies proves it of the two in the same slot.
///
/// The answer is unchanged when every live assumption of `from` is implied
/// in that sense by one of `dest`'s. Write A and B for the two lowerings and
/// B' for this one's. B implies B', and A and B' together imply B, since
/// under `dest`'s assumptions A gives `from`'s guarantees and those imply the
/// dropped ones. So `A & !B` and `A & !B'` hold on the same traces.
///
/// @param from  The candidate stronger specification.
/// @param dest  The candidate weaker specification.
std::optional<Specification> residual_destination(const Specification& from,
                                                  const Specification& dest);

/// Whether `stronger -> weaker` is propositionally valid, decided by
/// enumerating the assignments of `stronger & !weaker`. True only where it is
/// proven: a formula wider than `k_exhaustive_count_max_atoms` atoms, or one
/// carrying a temporal operator, reads as unproven.
bool formula_implies(const Formula& stronger, const Formula& weaker);

/// Whether requirement @p stronger implies @p weaker by rule, without asking a
/// solver. True only where it is proven, so false means unproven.
///
/// The rules move one field at a time from @p stronger towards @p weaker,
/// each step an implication in the context the other fields give it: a
/// response or stop along its polarity, a continual condition backwards, a
/// trigger's condition only to an equivalent one, and the timing, scope and
/// condition type along their orders (timing_implies, scope_implies). The
/// propositional steps are decided by enumerating assignments, so a field
/// wider than `k_exhaustive_count_max_atoms` atoms does not move. Every order
/// of the steps is tried, since a step can hold only after another one.
bool requirement_implies(const Requirement& stronger,
                         const Requirement& weaker);

/// A proof of one requirement implying another, in the shape of
/// requirement_implies. Callers pass a memoised one where they ask the same
/// pairs repeatedly.
using RequirementImplication =
    std::function<bool(const Requirement&, const Requirement&)>;

/// The lowerings of the live entries of @p reqs: the pool rule_cover reads.
std::unordered_set<std::string> live_lowerings(
    const std::vector<Requirement>& reqs);

/// How rule_cover proved a requirement implied, if it did.
enum class RuleCover : std::uint8_t { None, Lowering, Rule };

/// Whether @p req is implied by a live entry of @p pool without asking a
/// solver: one with the same lowering, or by @p implies the entry in @p req's
/// own slot. Slots pair by index, tombstones included, so the entry there is
/// the one the two specifications inherited from one original.
///
/// @param req             The requirement to cover.
/// @param slot            Its index in its own specification.
/// @param pool            The other specification's requirements on the
///                        same side.
/// @param pool_lowerings  live_lowerings of @p pool.
/// @param implies         The proof asked of the entry in @p slot.
RuleCover rule_cover(const Requirement& req, std::size_t slot,
                     const std::vector<Requirement>& pool,
                     const std::unordered_set<std::string>& pool_lowerings,
                     const RequirementImplication& implies);

/// Whether spec `from` implies spec `dest` by rule alone: every live
/// assumption of `from` is covered by `dest`'s assumptions and every live
/// guarantee of `dest` by `from`'s, in the sense of rule_cover. A tombstone is
/// a `true` requirement, so it needs no cover and covers nothing.
///
/// Sound and incomplete, and never launches a process: false means unproven.
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
