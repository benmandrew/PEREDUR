#pragma once

/// @file well_separation.hpp
/// @brief Detection of specifications that are not well-separated: ones the
///        system can vacuously satisfy by forcing its own assumptions to fail.

#include <atomic>
#include <cstddef>

#include "genetic/generation.hpp"
#include "requirement.hpp"
#include "runner/spot.hpp"

struct WellSeparationStats {
    /// ltlsynt queries that raised rather than answering, and were resolved as
    /// undecided instead of propagating. Atomic because the filter runs its
    /// checks concurrently, unlike the tool stats guarded by a cache mutex.
    inline static std::atomic<std::size_t> n_errors{0};
};

/// Returns whether the system can vacuously satisfy @p specification by
/// falsifying its own assumptions. Instantiated for Specification, whose
/// assumptions are its assumption requirements, and tlsf::Specification, whose
/// assumptions are its INITIALLY, REQUIRE and ASSUME sections.
///
/// Realizability is decided on `(assumptions) -> (guarantees)`, so a candidate
/// is satisfied for free on any trace where the assumptions fail. A candidate
/// is *well-separated* when the system cannot force that outcome: no system
/// strategy makes the assumptions fail against every environment. Equivalently,
/// the specification obtained by replacing the guarantees with `false` --
/// `(assumptions) -> false`, i.e. `!(assumptions)` -- must be *unrealizable*.
/// If it is realizable, the system has a strategy that drives the output atoms
/// so the assumptions break, satisfying the original specification without
/// repairing anything.
///
/// This is complementary to the vacuity filter's satisfiability check:
/// assumptions can be perfectly satisfiable yet still forcibly falsifiable by
/// the system, because satisfiability treats every atom symmetrically whereas
/// realizability respects the input/output partition. Joint unsatisfiability of
/// the assumptions is the vacuity filter's concern, not this one; it runs
/// first.
///
/// The ltlsynt query runs only when an assumption references an output atom.
/// Input-only assumptions are well-separated by construction and answered
/// without a solver call, since the system controls nothing it could use to
/// break them.
///
/// A specification with no assumptions has nothing to falsify and is reported
/// well-separated without a solver call. An ltlsynt query that times out is
/// undecided, and reads here as *not* well-separated, so the candidate is
/// dropped: this filter keeps a candidate exactly when the query comes back
/// unrealizable, which makes the fallback every other caller uses the unsafe
/// one here.
///
/// A query that *raises* is resolved identically: an exception is a query that
/// produced no verdict, the same as one killed at its budget, so it is caught,
/// counted in `WellSeparationStats::n_errors`, and dropped by the same
/// `value_or` fallback. Letting it propagate instead would end the run, because
/// filters run outside the scoring pool's `Config::max_scoring_failure_rate`
/// tolerance -- and the throw is reachable in practice, since SPOT 2.15.1's
/// `ltlsynt` aborts with "Too many acceptance sets used" on specifications the
/// search reaches on its own.
///
/// @param specification The specification to test
/// @param checker       Realizability checker for the ltlsynt query;
/// thread-safe
///                      for concurrent calls
template <typename Spec>
bool specification_is_not_well_separated(const Spec& specification,
                                         RealizabilityChecker& checker);

/// Returns a filter dropping specifications that are not well-separated: ones
/// the system can vacuously satisfy by forcing its own assumptions to fail.
/// @p checker is captured by reference and must outlive the returned filter.
/// Instantiated for Specification (the default) and tlsf::Specification.
///
/// @param checker       Realizability checker for the ltlsynt query; must be
///                      thread-safe when max_in_flight exceeds 1
/// @param max_in_flight Concurrent checks. Each is a full ltlsynt query.
template <typename Spec = Specification>
FilterFunctionT<Spec> make_well_separation_filter(
    RealizabilityChecker& checker, std::size_t max_in_flight = 1);
