#pragma once

// Guarantee-side core extraction for FRETISH specifications, the counterpart
// of tlsf::extract_muc for MUC repair mode.
//
// A core is a set of live guarantees that is unrealizable against the whole
// environment side: every assumption, and the modes, which ltlsynt plays as
// inputs. Assumptions are never part of a core, since relaxing one can only
// make synthesis harder. Cores are named by *slot*, a guarantee's position in
// Specification::m_guarantees, because specifications pair their requirements
// by position (see "Removable guarantees" in docs/dev/operators.md) and a
// repaired core is written back into the slots it came from.

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "requirement.hpp"

namespace fretish {

// Realizability of a whole specification: true, false, or nullopt for a query
// that never answered. Injected so the extraction is testable without ltlsynt.
using RealizabilityVerdict =
    std::function<std::optional<bool>(const Specification&)>;

// Asked before each probe starts; true means start no more. Empty never stops.
// Called concurrently, so it must be thread-safe. A probe already running when
// it turns true runs to its end.
using StopRequested = std::function<bool()>;

// @p spec with only the guarantees at @p slots, in slot order, and its whole
// environment side. @p slots must name live guarantees. Built the way the MRS
// walk builds its subsets, so the two ask ltlsynt the same formula for the same
// set and share its memo.
Specification guarantee_subset(const Specification& spec,
                               const std::vector<std::size_t>& slots);

// What a screen of the small guarantee subsets found. `conflict` holds the
// slots of the lowest-ranked subset decided unrealizable, if any; subsets are
// ranked by size first and then lexicographically by slot, so the answer does
// not depend on which probe finished first. `n_undecided` counts the subsets
// the screen could not decide, and `all_decided_realizable` is true only when
// every subset it asked was decided realizable. `interrupted` is set when a
// stop request left some subset unasked; such a screen is never
// all_decided_realizable, and its unasked subsets are not counted undecided.
struct SubsetScreen {
    std::optional<std::vector<std::size_t>> conflict;
    std::size_t n_undecided = 0;
    bool all_decided_realizable = true;
    bool interrupted = false;
};

// Screens the subsets of @p spec's live guarantees of size 1, then 2, and so
// on up to @p max_size, stopping after the first size holding a conflict. A
// conflict found at size s is minimal, because no subset smaller than s is
// one. Each size is one concurrent region of up to @p max_in_flight probes;
// the verdict function is then called concurrently and must be thread-safe.
// An undecided probe is not read as a conflict. Once @p stop holds, no further
// probe starts, and the screen ends after the size in progress.
SubsetScreen screen_small_subsets(const Specification& spec,
                                  const RealizabilityVerdict& verdict,
                                  std::size_t max_size,
                                  std::size_t max_in_flight,
                                  const StopRequested& stop = {});

// A core and where it came from. `slots` are positions in the specification
// it was extracted from, ascending; `spec` is guarantee_subset of those.
// `n_undecided` counts the probes that never answered, over the screen and
// the walk both. `provisional` is set when the walk read one of them as a
// conflict, since a core resting on such a probe was never confirmed
// unrealizable; undecided screen probes never make a core provisional, the
// screen returning only decided conflicts.
struct GuaranteeCore {
    std::vector<std::size_t> slots;
    Specification spec;
    std::size_t n_undecided = 0;
    bool provisional = false;
    // Set when @p stop cut the screen or the walk short; the core is then
    // empty, since a partial screen's conflict need not be the lowest-ranked
    // one and a partial walk's need not be minimal.
    bool interrupted = false;
};

// Extracts a guarantee-side core from @p spec. Screens every subset of up to
// @p screen_depth live guarantees first and returns the lowest-ranked conflict
// there. Only where no subset that small conflicts, and @p allow_walk is set,
// does it fall back to QuickXplain over every live guarantee, reading an
// undecided probe as a conflict. The walk is the caller's to forbid: it is
// sound only on a specification known to be unrealizable, and on a large one
// whose cheap conflicts are gone its probes stop answering.
//
// Returns an empty core when there is nothing to report: no live guarantee,
// no small conflict with the walk forbidden, a walk that found none, or an
// extraction @p stop interrupted.
GuaranteeCore extract_core(const Specification& spec,
                           const RealizabilityVerdict& verdict,
                           std::size_t screen_depth, std::size_t max_in_flight,
                           bool allow_walk, const StopRequested& stop = {});

// @p context with the guarantees at @p slots replaced, in order, by those of
// @p repaired_core, and the environment side taken from @p repaired_core,
// whose search may have added assumptions. @p repaired_core must have one
// guarantee per slot, which holds for any descendant of guarantee_subset:
// removal tombstones a guarantee rather than erasing it.
Specification reintegrate(const Specification& repaired_core,
                          const Specification& context,
                          const std::vector<std::size_t>& slots);

}  // namespace fretish
