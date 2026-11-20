#pragma once

/// @file psp/filter.hpp
/// @brief The whole-specification implication check and the valid-guarantee
///        test for psp::Specification, which the offline drivers use.

#include <optional>

#include "psp/specification.hpp"
#include "runner/black.hpp"

namespace psp {

/// Whether @p from implies @p dest: true when `from.to_ltl() &
/// !dest.to_ltl()` is unsatisfiable, false when it is satisfiable, and
/// nothing when the query is undecided. Exact, as `tlsf_spec_implies` is. An
/// undecided query is counted in ImplicationFilterStats::n_timeouts.
std::optional<bool> spec_implies(const Specification& from,
                                 const Specification& dest,
                                 SatisfiabilityChecker& checker);

/// Whether some live guarantee of @p spec is valid, and so demands nothing.
/// A guarantee whose check is undecided reads as falsifiable, as on the
/// other two paths.
bool has_valid_guarantee(const Specification& spec,
                         SatisfiabilityChecker& checker);

}  // namespace psp
