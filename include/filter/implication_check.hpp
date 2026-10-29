#pragma once

/// @file implication_check.hpp
/// @brief Pairwise implication check between two whole specifications, used
///        by the implication filter.

#include <optional>

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
/// every live assumption of `from` is also one of `dest`'s, `dest`'s
/// assumptions give `from`'s guarantees, so a guarantee the two share cannot
/// be the one that fails. The query then asks only about `dest`'s other
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

/// `dest` with every live guarantee whose lowering `from` also carries marked
/// removed, or nothing where that would change the query's answer or drop
/// nothing.
///
/// The answer is unchanged when every live assumption of `from` is also one of
/// `dest`'s. Write A and B for the two lowerings and B' for this one's. B
/// implies B', and A and B' together imply B, since under `dest`'s assumptions
/// A gives `from`'s guarantees and the shared ones among them. So `A & !B` and
/// `A & !B'` hold on the same traces.
///
/// @param from  The candidate stronger specification.
/// @param dest  The candidate weaker specification.
std::optional<Specification> residual_destination(const Specification& from,
                                                  const Specification& dest);
