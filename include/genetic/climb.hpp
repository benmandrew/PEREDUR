#pragma once

/// @file climb.hpp
/// @brief The lattice climb at the output: each gated FRETISH repair is
///        strengthened towards the original, one proved step at a time,
///        while it still passes the output gate.

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "config.hpp"
#include "fitness/function.hpp"
#include "genetic/order_moves.hpp"
#include "requirement.hpp"

/// The climb's counters, for `--diagnostics` and `run.json`.
struct ClimbStats {
    /// Distinct gated repairs a climb started from.
    inline static std::atomic<std::size_t> n_climbed{0};
    /// Steps tried, each counted once however many candidates its bisection
    /// put to the gate.
    inline static std::atomic<std::size_t> n_steps_tried{0};
    /// Steps whose result the climb moved to.
    inline static std::atomic<std::size_t> n_steps_accepted{0};
    /// Candidates put to the gate predicate.
    inline static std::atomic<std::size_t> n_queries{0};
    /// ltlsynt execs while the climb ran. With the five counts below, it
    /// splits every realizability query the climb's gate asked by what
    /// answered it; each query lands in exactly one.
    inline static std::atomic<std::size_t> n_ltlsynt_execs{0};
    /// Queries answered by the memo's entry for the same formula.
    inline static std::atomic<std::size_t> n_memo_hits{0};
    /// Queries settled by conjunct-set inclusion in a decided entry.
    inline static std::atomic<std::size_t> n_subsumed{0};
    /// Queries settled by `requirement_implies` replacements of a decided
    /// entry.
    inline static std::atomic<std::size_t> n_rule_subsumed{0};
    /// Queries over no environment inputs, decided by satisfiability.
    inline static std::atomic<std::size_t> n_satisfiability_routed{0};
    /// Queries a stored controller was shown to realize.
    inline static std::atomic<std::size_t> n_controller_hits{0};
    /// Wall-clock seconds spent in climb_repairs, from the distinct repairs in
    /// to the distinct results out. Written by the calling thread alone.
    inline static double wall_s{0.0};
};

/// Whether a candidate the climb reached is still a repair.
using ClimbPredicate = std::function<bool(const Specification&)>;

/// How one climb orders its steps.
struct ClimbRule {
    ClimbOrder order = ClimbOrder::Slot;
    /// 0 for @ref order itself. Each later index orders the steps by a fixed
    /// pseudo-random priority over their slot, side, kind and field, which is
    /// the same in every round of the climb and draws no random number.
    std::size_t permutation = 0;
    /// Syntactic similarity to the original, read under
    /// ClimbOrder::Similarity when @ref permutation is 0.
    std::function<double(const Specification&)> similarity;
};

/// @p steps reordered under @p rule. The slot order is the input order, and
/// every reordering is stable, so ties keep it.
void order_steps(std::vector<Step>& steps, const Specification& current,
                 const ClimbRule& rule);

/// The furthest result of @p step from @p current that @p accepts, or nothing
/// where it accepts none.
///
/// A Field step on the timing of a slot whose timing_chain has intermediate
/// entries is bisected: the destination is tried first, then a binary search
/// finds the furthest accepted entry of the chain. The entries strengthen in
/// chain order, and a strengthening of an unrealizable specification is
/// unrealizable, so the accepted entries are a prefix and the search finds the
/// entry a walk one tick at a time would stop at. Every other step has one
/// result, its own.
std::optional<Specification> furthest_accepted(const Specification& current,
                                               const Step& step,
                                               const ClimbPredicate& accepts);

/// @p start climbed towards @p original: in each round the steps_towards the
/// original are tried in @p rule's order, the first one with an accepted
/// result is taken, and the climb stops in the first round where none is.
///
/// The result implies @p start by rule, @p accepts holds of it wherever it
/// held of @p start, and no step from it has an accepted result. A result
/// already visited is never taken again, so the climb ends even where two
/// steps are proved stronger than each other.
Specification climb(const Specification& start, const Specification& original,
                    const ClimbPredicate& accepts, const ClimbRule& rule);

/// The predicate the output gate applies, passes_output_gate on @p cfg with the
/// run's @p status objective, behind a whole-specification realizability
/// screen. The screen rejects only a candidate ltlsynt decides is
/// unrealizable, which the gate would reject too, and its decided verdicts let
/// the realizability memo settle later candidates by rule.
ClimbPredicate climb_gate(const Config& cfg, FitnessFunction status);

/// Every distinct climb result of @p repairs, in order: the results of each
/// distinct repair, @p cfg.climb_orders climbs of it, in input order. The
/// repairs are climbed concurrently and the results collected by index, so
/// the answer does not depend on how the gate's queries interleave.
std::vector<Specification> climb_repairs(
    const std::vector<Specification>& repairs, const Specification& original,
    const Config& cfg, const ClimbPredicate& accepts);
