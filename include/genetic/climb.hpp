#pragma once

/// @file climb.hpp
/// @brief The lattice climb at the output: each gated repair is strengthened
///        towards the original, one proved step at a time, while it still
///        passes the output gate. Templated over the specification type and
///        read through OrderedLanguage; FRETISH is the one instantiation.

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "config.hpp"
#include "fitness/function.hpp"
#include "genetic/ordered_language.hpp"
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
    /// Gate checks that raised instead of answering, each refusing its step.
    inline static std::atomic<std::size_t> n_gate_errors{0};
    /// Wall-clock seconds spent in climb_repairs, from the distinct repairs in
    /// to the distinct results out. Written by the calling thread alone.
    inline static double wall_s{0.0};
};

/// Whether a candidate the climb reached is still a repair.
template <typename Spec>
using ClimbPredicateT = std::function<bool(const Spec&)>;

/// The FRETISH climb predicate.
using ClimbPredicate = ClimbPredicateT<Specification>;

/// Keeps a parameter out of template argument deduction, so a lambda or a
/// null can be passed where a std::function of the specification type is
/// expected, the type being deduced from the other arguments.
template <typename T>
struct ClimbNonDeduced {
    using type = T;
};

/// @p steps reordered under step permutation @p permutation. Permutation 0 is
/// the input order, slot order as steps_towards lists it. Each later index
/// orders the steps by a fixed pseudo-random priority over their slot, side,
/// kind and field, the same in every round of a climb, and draws no random
/// number. The sort is stable, so ties keep the input order.
template <typename Spec, typename Field>
void order_steps(std::vector<OrderStep<Spec, Field>>& steps,
                 std::size_t permutation);

/// The furthest result of @p step from @p current that @p accepts, or nothing
/// where it accepts none.
///
/// A step whose chain (OrderedLanguage::step_chain; on FRETISH, the
/// timing_chain of a timing step) has intermediate entries is bisected: the
/// destination is tried first, then a binary search finds the furthest
/// accepted entry of the chain. The entries strengthen in chain order, and a
/// strengthening of an unrealizable specification is unrealizable, so the
/// accepted entries are a prefix and the search finds the entry a walk one
/// tick at a time would stop at. Every other step has one result, its own.
template <typename Spec>
std::optional<Spec> furthest_accepted(
    const Spec& current, const typename OrderedLanguage<Spec>::Step& step,
    const typename ClimbNonDeduced<ClimbPredicateT<Spec>>::type& accepts);

/// @p start climbed towards @p original: in each round the steps_towards the
/// original are tried in order_steps' order for @p permutation (at 0, the
/// order steps_towards lists them: assumption slots, then guarantee slots,
/// then the appended assumptions), the first one with an accepted result is
/// taken, and the climb stops in the first round where none is.
///
/// The result implies @p start by rule, @p accepts holds of it wherever it
/// held of @p start, and no step from it has an accepted result. A result
/// already visited is never taken again, so the climb ends even where two
/// steps are proved stronger than each other.
template <typename Spec>
Spec climb(const Spec& start, const Spec& original,
           const typename ClimbNonDeduced<ClimbPredicateT<Spec>>::type& accepts,
           std::size_t permutation = 0);

/// The predicate the output gate applies, passes_output_gate on @p cfg with the
/// run's @p status objective, behind a whole-specification realizability
/// screen. The screen rejects only a candidate ltlsynt decides is
/// unrealizable, which the gate would reject too, and its decided verdicts let
/// the realizability memo settle later candidates by rule.
template <typename Spec = Specification>
ClimbPredicateT<Spec> climb_gate(
    const Config& cfg,
    typename ClimbNonDeduced<FitnessFunctionT<Spec>>::type status);

/// Every distinct climb result of @p repairs, in order: @p n_orders climbs of
/// each distinct repair, under permutations 0 to @p n_orders - 1, in input
/// order. The repairs are climbed concurrently and the results collected by
/// index, so the answer does not depend on how the gate's queries interleave.
/// Results of different orders can be comparable; the final implication
/// filter keeps the maximal ones.
template <typename Spec>
std::vector<Spec> climb_repairs(
    const std::vector<Spec>& repairs, const Spec& original,
    const typename ClimbNonDeduced<ClimbPredicateT<Spec>>::type& accepts,
    std::size_t n_orders = 1);
