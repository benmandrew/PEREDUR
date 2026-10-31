#include "genetic/mutation.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "genetic/binary_kind.hpp"
#include "genetic/monotone.hpp"
#include "genetic/order_moves.hpp"
#include "prop_formula.hpp"

namespace {

std::string random_atom(const std::vector<std::string>& atoms,
                        const RandomSource& random_source) {
    assert(!atoms.empty());
    return atoms[random_source.next_index(atoms.size())];
}

std::string mutate_atom_name(const std::string& atom,
                             const std::vector<std::string>& atoms,
                             const RandomSource& random_source) {
    if (atom == "true") {
        return "false";
    }
    if (atom == "false") {
        return "true";
    }
    if (atoms.empty()) {
        return atom;
    }
    const std::size_t index = random_source.next_index(atoms.size());
    // Prefer a distinct atom, as the TLSF path's flip_or_replace_atom does.
    // Without this a rename is a no-op one time in the pool size, which on a
    // two-signal specification is every other draw. The step past costs no
    // draw.
    if (atoms[index] == atom && atoms.size() > 1) {
        return atoms[(index + 1) % atoms.size()];
    }
    return atoms[index];
}

// Rename the atom, negate it, or graft a drawn anchor onto it under a fresh
// connective. The graft case is what lets a weakening guard a *positive*
// literal in one draw. Before it, the only rule that grew a formula in place
// was mutate_not_subtree's, which fires at a Not node alone, so guarding a
// negated literal cost one draw and guarding a positive one cost three
// (rename, negate, graft) through intermediates that had to survive selection
// to reach the third. experiments/2026-08-14-aurus-h2h/REPORT.md §1 measures
// the consequence: minepump's ideals guard a positive antecedent, and PEREDUR
// reached one in 1 of 20 seeds while AuRUS, whose add-disjunct rule has no
// such gate, reached it in 30 of 30.
Formula mutate_atom_formula(const Formula& formula,
                            const std::vector<std::string>& atoms,
                            const RandomSource& random_source) {
    const std::optional<std::string> atom = formula.atom_name();
    if (!atom.has_value()) {
        assert(false);
        __builtin_unreachable();
    }
    // Without a pool there is no anchor to draw, so the graft case is dropped
    // rather than guarded inside it.
    const std::size_t n_moves = atoms.empty() ? 2 : 3;
    switch (random_source.next_index(n_moves)) {
        case 0:
            return Formula::make_atom(
                mutate_atom_name(*atom, atoms, random_source));
        case 1:
            return Formula::make_unary(Formula::Kind::Not, formula);
        default: {
            // Sequenced into locals: each call draws, and the evaluation order
            // of a call's arguments is unspecified. The anchor's polarity is
            // drawn as well: a guard is as often negative as positive, and
            // minepump's ideal wants `high_water & !methane` from an atom
            // `high_water`, which a positive-only anchor cannot reach without
            // a second mutation.
            const Formula atom_formula =
                Formula::make_atom(random_atom(atoms, random_source));
            const bool negate_anchor = random_source.next_bool();
            const Formula anchor =
                negate_anchor
                    ? Formula::make_unary(Formula::Kind::Not, atom_formula)
                    : atom_formula;
            const Formula::Kind kind = pick_binary_kind(random_source);
            const bool anchor_first = random_source.next_bool();
            return anchor_first ? Formula::make_binary(kind, anchor, formula)
                                : Formula::make_binary(kind, formula, anchor);
        }
    }
}

Formula mutate_not_subtree(Formula child, const std::vector<std::string>& atoms,
                           const RandomSource& random_source) {
    const int selector = static_cast<int>(random_source.next_index(3));
    switch (selector) {
        case 0:
            return child;
        case 1:
            return Formula::make_unary(Formula::Kind::Not, child);
        case 2:
            break;
        default:
            assert(false);
            __builtin_unreachable();
    }
    const Formula anchor =
        Formula::make_atom(random_atom(atoms, random_source));
    return Formula::make_binary(pick_binary_kind(random_source), anchor,
                                Formula::make_unary(Formula::Kind::Not, child));
}

Formula mutate_binary_subtree(const std::pair<Formula, Formula>& children,
                              const RandomSource& random_source) {
    if (!random_source.next_bool()) {
        return random_source.next_bool() ? children.first : children.second;
    }
    Formula combined = Formula::make_binary(pick_binary_kind(random_source),
                                            children.first, children.second);
    if (!random_source.next_bool()) {
        return combined;
    }
    return Formula::make_unary(Formula::Kind::Not, combined);
}

}  // namespace

Formula draw_literal(const std::vector<std::string>& pool,
                     const RandomSource& random_source) {
    const Formula atom =
        Formula::make_atom(pool[random_source.next_index(pool.size())]);
    return random_source.next_bool()
               ? Formula::make_unary(Formula::Kind::Not, atom)
               : atom;
}

Formula mutate_formula(const Formula& formula,
                       const std::vector<std::string>& atoms,
                       const RandomSource& random_source) {
    assert(random_source);
    const std::size_t n_subformulas = formula.n_subformulae();
    const auto mutation_function =
        [&](const Formula& subtree) -> std::optional<Formula> {
        if (random_source.next_index(n_subformulas) != 0) {
            return std::nullopt;
        }
        switch (subtree.kind()) {
            case Formula::Kind::Atom:
                return mutate_atom_formula(subtree, atoms, random_source);
            case Formula::Kind::Not: {
                auto child_opt = subtree.unary_child();
                if (!child_opt.has_value()) {
                    assert(false);
                    __builtin_unreachable();
                }
                return mutate_not_subtree(*child_opt, atoms, random_source);
            }
            case Formula::Kind::And:
            case Formula::Kind::Or:
            case Formula::Kind::Implies:
            case Formula::Kind::Iff: {
                const auto children_opt = subtree.binary_children();
                if (!children_opt.has_value()) {
                    assert(false);
                    __builtin_unreachable();
                }
                return mutate_binary_subtree(*children_opt, random_source);
            }
            // FRETISH formulae are propositional; temporal operators never
            // occur here. Leave any such subtree unmutated.
            case Formula::Kind::Next:
            case Formula::Kind::Eventually:
            case Formula::Kind::Globally:
            case Formula::Kind::Until:
            case Formula::Kind::Release:
            case Formula::Kind::WeakUntil:
                return std::nullopt;
        }
        return std::nullopt;
    };
    auto mutated = formula.rewrite_post_order(mutation_function);
    mutated.remove_double_negation();
    return mutated;
}

namespace {

// The factor Config::step_scaling multiplies each large move's probability by,
// or nothing where the key is off or the parent gave no status. Nothing keeps
// every arm on the draws it took before the key existed.
using StepScale = std::optional<double>;

// The share of a scaled draw that a large move takes: its unscaled share
// @p base times @p scale, capped at 1.
double scaled_share(double base, double scale) {
    return std::min(1.0, base * scale);
}

// Under a step scale, whether a tick-count step takes one of its large
// options, which hold the share @p base of the unscaled draw, rather than its
// single edge. One next_real, so at a scale of 1 the two choices keep the
// shares they have unscaled.
bool draws_large(double base, double scale, const RandomSource& random_source) {
    const bool large = random_source.next_real() < scaled_share(base, scale);
    if (large) {
        StepScalingStats::n_large++;
    } else {
        StepScalingStats::n_small++;
    }
    return large;
}

constexpr double k_half = 0.5;
constexpr double k_two_thirds = 2.0 / 3.0;

Timing weaken_for_timing(const timing::ForTicks& for_ticks,
                         const RandomSource& random_source, StepScale scale) {
    if (for_ticks.m_ticks == 1) {
        return random_source.next_bool() ? timing::next_timepoint()
                                         : timing::immediately();
    }
    if (scale.has_value()) {
        return draws_large(k_half, *scale, random_source)
                   ? timing::for_ticks(for_ticks.m_ticks / 2)
                   : timing::for_ticks(for_ticks.m_ticks - 1);
    }
    return random_source.next_bool() ? timing::for_ticks(for_ticks.m_ticks - 1)
                                     : timing::for_ticks(for_ticks.m_ticks / 2);
}

Timing weaken_within_timing(const timing::WithinTicks& within_ticks,
                            const RandomSource& random_source,
                            StepScale scale) {
    if (scale.has_value()) {
        if (!draws_large(k_two_thirds, *scale, random_source)) {
            return timing::within_ticks(within_ticks.m_ticks + 1);
        }
        return random_source.next_bool()
                   ? timing::within_ticks(within_ticks.m_ticks * 2)
                   : timing::eventually();
    }
    std::size_t index = random_source.next_index(3);
    switch (index) {
        case 0:
            return timing::within_ticks(within_ticks.m_ticks + 1);
        case 1:
            return timing::within_ticks(within_ticks.m_ticks * 2);
        case 2:
            return timing::eventually();
        default:
            assert(false);
            __builtin_unreachable();
    }
}

Timing strengthen_for_timing(const timing::ForTicks& for_ticks,
                             const RandomSource& random_source,
                             StepScale scale) {
    if (scale.has_value()) {
        if (!draws_large(k_two_thirds, *scale, random_source)) {
            return timing::for_ticks(for_ticks.m_ticks + 1);
        }
        return random_source.next_bool()
                   ? timing::for_ticks(for_ticks.m_ticks * 2)
                   : timing::always();
    }
    std::size_t index = random_source.next_index(3);
    switch (index) {
        case 0:
            return timing::for_ticks(for_ticks.m_ticks + 1);
        case 1:
            return timing::for_ticks(for_ticks.m_ticks * 2);
        case 2:
            return timing::always();
        default:
            assert(false);
            __builtin_unreachable();
    }
}

Timing strengthen_within_timing(const timing::WithinTicks& within_ticks,
                                const RandomSource& random_source,
                                StepScale scale) {
    if (within_ticks.m_ticks == 1) {
        return random_source.next_bool() ? timing::next_timepoint()
                                         : timing::immediately();
    }
    // `after n-1` pins the response to one tick rather than stepping along the
    // `within` chain, so it counts with the halving as a large move.
    if (scale.has_value()) {
        if (!draws_large(k_two_thirds, *scale, random_source)) {
            return timing::within_ticks(within_ticks.m_ticks - 1);
        }
        return random_source.next_bool()
                   ? timing::within_ticks((within_ticks.m_ticks + 1) / 2)
                   : timing::after_ticks(within_ticks.m_ticks - 1);
    }
    std::size_t index = random_source.next_index(3);
    switch (index) {
        case 0:
            return timing::within_ticks(within_ticks.m_ticks - 1);
        case 1:
            // Halve rounding up, so the result stays >= 1 and strictly below
            // m_ticks for every m_ticks > 1.
            return timing::within_ticks((within_ticks.m_ticks + 1) / 2);
        case 2:
            return timing::after_ticks(within_ticks.m_ticks - 1);
        default:
            assert(false);
            __builtin_unreachable();
    }
}

// Timing is an alias for a std::variant, so ADL from inside <algorithm> finds
// only std's element-wise variant comparisons, not requirement.hpp's
// namespace-scope ones. Naming them in a lambda picks up the right overloads.
void sort_unique_timings(std::vector<Timing>& timings) {
    std::sort(timings.begin(), timings.end(),
              [](const Timing& lhs, const Timing& rhs) { return lhs < rhs; });
    timings.erase(std::unique(timings.begin(), timings.end(),
                              [](const Timing& lhs, const Timing& rhs) {
                                  return lhs == rhs;
                              }),
                  timings.end());
}

// The timings a donor pool lends to the two extremes of the order, derived from
// the timings the specification already uses. Always and Eventually take the
// same set from opposite sides: everything here is strictly below Always and
// strictly above Eventually, so it strengthens the one and weakens the other.
// A quantified donor lends only its tick count, spent as `for n ticks`, never
// its own kind — `within n` would be a second spelling of one count, and
// `after n` is not comparable to Always at all, forbidding the response at the
// ticks where Always demands it. Immediately and NextTimepoint lend themselves.
std::vector<Timing> donated_candidates(const std::vector<Timing>& timing_pool) {
    std::vector<Timing> candidates;
    for (const Timing& donor : timing_pool) {
        std::visit(
            [&candidates](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, timing::WithinTicks> ||
                              std::is_same_v<T, timing::ForTicks> ||
                              std::is_same_v<T, timing::AfterTicks>) {
                    // `for 0 ticks` is just Immediately spelled differently,
                    // and only AfterTicks can carry a zero count.
                    if (value.m_ticks > 0) {
                        candidates.push_back(timing::for_ticks(value.m_ticks));
                    }
                } else if constexpr (std::is_same_v<T, timing::Immediately>) {
                    candidates.push_back(timing::immediately());
                } else if constexpr (std::is_same_v<T, timing::NextTimepoint>) {
                    candidates.push_back(timing::next_timepoint());
                } else {
                    // The extremes themselves lend nothing. A stop timing lies
                    // below Always but not above Eventually, so it cannot join
                    // a set the two extremes share.
                    static_assert(std::is_same_v<T, timing::Eventually> ||
                                  std::is_same_v<T, timing::Always> ||
                                  timing::k_carries_stop<T>);
                }
            },
            donor);
    }
    sort_unique_timings(candidates);
    return candidates;
}

// The timings a donor pool lends to Always alone: `until s` for every stop a
// live Until or Before carries. Always implies `until s` whatever s is, so any
// stop is sound, but `until s` does not imply Eventually, so these stay out of
// the set donated_candidates shares between the two extremes. Until sorts after
// every other kind, so appending these keeps a stop-free pool's candidate list,
// and the index a draw lands on, exactly as it was.
std::vector<Timing> donated_to_always(const std::vector<Timing>& timing_pool) {
    std::vector<Timing> candidates = donated_candidates(timing_pool);
    std::vector<Timing> stops;
    for (const Timing& donor : timing_pool) {
        if (const Formula* stop = timing_stop(donor)) {
            stops.push_back(timing::until(*stop));
        }
    }
    sort_unique_timings(stops);
    candidates.insert(candidates.end(), stops.begin(), stops.end());
    return candidates;
}

// Moves an extreme of the order inwards, into one of the @p candidates its pool
// donates. With nothing to donate it returns @p extreme unchanged, and costs no
// draw, so a specification with no interior timing anywhere cannot acquire one.
Timing move_off_extreme(const Timing& extreme,
                        const std::vector<Timing>& candidates,
                        const RandomSource& random_source) {
    if (candidates.empty()) {
        return extreme;
    }
    return candidates[random_source.next_index(candidates.size())];
}

Timing strengthen_timing(const Timing& timing,
                         const std::vector<Timing>& timing_pool,
                         const RandomSource& random_source, StepScale scale) {
    const auto mutation_function = [&](const auto& value) -> Timing {
        using T = std::decay_t<decltype(value)>;
        // Always is the top of the order and has no strengthening. It is also
        // `until false`, the strongest stop, and the one kind above `until s`.
        constexpr bool strengthens_to_always =
            std::is_same_v<T, timing::Always> ||
            std::is_same_v<T, timing::Until>;
        if constexpr (std::is_same_v<T, timing::Immediately> ||
                      std::is_same_v<T, timing::NextTimepoint>) {
            return timing::for_ticks(1);
        } else if constexpr (strengthens_to_always) {
            return timing::always();
        } else if constexpr (std::is_same_v<T, timing::ForTicks>) {
            return strengthen_for_timing(value, random_source, scale);
        } else if constexpr (std::is_same_v<T, timing::AfterTicks>) {
            // `after n` expands to ¬R at ticks 0..n ∧ R at tick n+1, pinning
            // the response to a single tick. Nothing in the timing order lies
            // strictly above it: `after n-1` and `always` both demand R at a
            // tick where `after n` demands ¬R, so they contradict rather than
            // strengthen it. AfterTicks therefore has no strengthening.
            return timing::after_ticks(value.m_ticks);
        } else if constexpr (std::is_same_v<T, timing::WithinTicks>) {
            return strengthen_within_timing(value, random_source, scale);
        } else if constexpr (std::is_same_v<T, timing::Eventually>) {
            return move_off_extreme(timing::eventually(),
                                    donated_candidates(timing_pool),
                                    random_source);
        } else if constexpr (std::is_same_v<T, timing::Before>) {
            // `before s` is incomparable with every other kind.
            return value;
        } else {
            static_assert(timing::k_unhandled_timing<T>);
        }
    };
    return std::visit(mutation_function, timing);
}

Timing weaken_timing(const Timing& timing,
                     const std::vector<Timing>& timing_pool,
                     const RandomSource& random_source, StepScale scale) {
    const auto mutation_function = [&](const auto& value) -> Timing {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, timing::Immediately> ||
                      std::is_same_v<T, timing::NextTimepoint>) {
            return timing::within_ticks(1);
        } else if constexpr (std::is_same_v<T, timing::Always>) {
            return move_off_extreme(timing::always(),
                                    donated_to_always(timing_pool),
                                    random_source);
        } else if constexpr (std::is_same_v<T, timing::ForTicks>) {
            return weaken_for_timing(value, random_source, scale);
        } else if constexpr (std::is_same_v<T, timing::AfterTicks>) {
            return timing::within_ticks(value.m_ticks + 1);
        } else if constexpr (std::is_same_v<T, timing::WithinTicks>) {
            return weaken_within_timing(value, random_source, scale);
        } else if constexpr (std::is_same_v<T, timing::Eventually>) {
            return timing::eventually();
        } else if constexpr (timing::k_carries_stop<T>) {
            // No kind lies below `until s`, and `before s` is incomparable with
            // every kind, so both weaken only through their stop.
            return value;
        } else {
            static_assert(timing::k_unhandled_timing<T>);
        }
    };
    return std::visit(mutation_function, timing);
}

// --- Scope and condition type -------------------------------------------
//
// Both fields carry an implication order, so both can be mutated in a
// direction the way timings are, rather than redrawn freely and left to the
// filters. Every edge below was measured with `ltlfilt --implied-by` over the
// lowered formula; test_scope_order_is_pinned re-derives them, so a wrong entry
// fails the suite rather than surfacing later as a candidate that moved the
// wrong way.

// Continual implies Trigger for every scope and every timing: a trigger fires
// on the rising edges of its condition, which are a subset of the timepoints
// where the condition holds. So Continual is the strengthening and Trigger the
// weakening, and this order needs no table. The two coincide at `always` under
// the five plain scopes and at `eventually` under the three `only` ones, and
// are strictly ordered everywhere else.
// The order has two elements, so the direction alone names the target and the
// current value is not read: strengthening an already-Continual requirement
// leaves it where it is, exactly as strengthen_timing does at Always.
ConditionType mutate_condition_type(Direction direction) {
    return direction == Direction::Strengthen ? ConditionType::Continual
                                              : ConditionType::Trigger;
}

// A scope other than Global names a mode, so a move onto one is available only
// where the specification declares at least one.
Scope move_scope(const Scope& scope, Direction direction, const Timing& timing,
                 ConditionType condition_type,
                 const std::vector<std::string>& mode_pool,
                 const RandomSource& random_source) {
    std::vector<Scope> candidates;
    for (const ScopeEdge& edge : scope_edges(timing, condition_type)) {
        const ScopeKind from = direction == Direction::Strengthen
                                   ? edge.m_weaker
                                   : edge.m_stronger;
        const ScopeKind target = direction == Direction::Strengthen
                                     ? edge.m_stronger
                                     : edge.m_weaker;
        if (from != scope.m_kind) {
            continue;
        }
        if (target == ScopeKind::Global) {
            candidates.push_back(Scope{});
            continue;
        }
        if (!scope.is_global()) {
            // Every edge between two non-Global scopes holds one mode at a
            // time: `notin m` implies `before m`, and implies nothing about
            // `before m'`. The mode travels with the move, and the pool is not
            // consulted.
            candidates.push_back(Scope{target, scope.m_mode});
            continue;
        }
        // Global implies the scoped form for *every* mode, so any declared one
        // is a sound target.
        for (const std::string& mode : mode_pool) {
            candidates.push_back(Scope{target, mode});
        }
    }
    if (candidates.empty()) {
        return scope;
    }
    return candidates[random_source.next_index(candidates.size())];
}

// Which way a monotone rewrite of a field has to move for the requirement to
// move @p direction, given whether the field sits under a negation.
MonotoneDirection monotone_of(Direction direction, bool flipped) {
    return (direction == Direction::Weaken) != flipped
               ? MonotoneDirection::Weaken
               : MonotoneDirection::Strengthen;
}

// The direction a monotone rewrite of the response must take, or nothing where
// the response occurs at both polarities in the lowered requirement and no
// rewrite of it says anything about the whole.
//
// scope_wrap places its argument positively in every one of its eight
// branches, and both the continual wrapper `G(c -> body)` and the trigger's
// two conjuncts place `body` positively, so what is left to decide is the
// timing and the scope's polarity.
std::optional<MonotoneDirection> response_direction(
    const Requirement& requirement, Direction direction) {
    // expand_after builds `!r & X(!r & X(... & X(r)))` and the bounded form
    // conjoins `for n (!r)` with `within n+1 r`, so `after n ticks` holds its
    // response at both polarities either way.
    if (std::holds_alternative<timing::AfterTicks>(requirement.m_timing)) {
        return std::nullopt;
    }
    // An "only" scope carries the negation of the timing's obligation
    // (negated_timing_body), which puts the response under a Not.
    return monotone_of(direction, is_only_scope(requirement.m_scope.m_kind));
}

// The same for the condition. Continual lowers to `G(c -> body)`, an
// antecedent and so uniformly negative; a trigger's rising edge is
// `(!c & Xc)`, which holds the condition at both polarities at once, and its
// second conjunct `(c -> body)` at neither of those two consistently.
std::optional<MonotoneDirection> condition_direction(
    const Requirement& requirement, Direction direction) {
    if (requirement.m_condition_type != ConditionType::Continual) {
        return std::nullopt;
    }
    return monotone_of(direction, true);
}

// The same for a stop. It occurs positively in `until`'s body and negatively
// in `before`'s, and an "only" scope lowers the dual timing over the negated
// obligation, which swaps the two. Both condition wrappers place the body
// positively, so the condition type does not enter.
MonotoneDirection stop_direction(const Requirement& requirement,
                                 Direction direction) {
    const bool is_before =
        std::holds_alternative<timing::Before>(requirement.m_timing);
    return monotone_of(direction,
                       is_before != is_only_scope(requirement.m_scope.m_kind));
}

// @p timing with its stop replaced by @p stop, keeping its kind.
Timing with_stop(const Timing& timing, Formula stop) {
    assert(timing_stop(timing) != nullptr);
    if (std::holds_alternative<timing::Before>(timing)) {
        return timing::before(std::move(stop));
    }
    return timing::until(std::move(stop));
}

// The monotone arm, offered ahead of the general rewrite. cfg.p_monotone is
// read before the RandomSource is touched, so at 0 the arm costs no draw and
// the breeding stream is what it was before it existed. @p free_rewrite, where
// given, is set when the rewrite is the general one, which has no direction.
Formula rewrite_field(const Formula& field,
                      const std::vector<std::string>& atoms,
                      std::optional<MonotoneDirection> direction,
                      const Config& cfg, const RandomSource& random_source,
                      bool* free_rewrite) {
    if (cfg.p_monotone > 0.0 && direction.has_value() && !atoms.empty() &&
        random_source.next_real() < cfg.p_monotone) {
        return monotone_rewrite(field, *direction, atoms, random_source);
    }
    if (free_rewrite != nullptr) {
        *free_rewrite = true;
    }
    return mutate_formula(field, atoms, random_source);
}

}  // namespace

std::vector<Timing> collect_timing_pool(const Specification& specification) {
    std::vector<Timing> pool;
    pool.reserve(specification.m_assumptions.size() +
                 specification.m_guarantees.size());
    // Removed requirements contribute no timings: the extremes draw from the
    // shapes the specification actually uses, and a deleted requirement's is
    // not one of them.
    const auto add = [&pool](const std::vector<Requirement>& reqs) {
        for (const Requirement& req : reqs) {
            if (req.m_removed) {
                continue;
            }
            pool.push_back(req.m_timing);
        }
    };
    add(specification.m_assumptions);
    add(specification.m_guarantees);
    sort_unique_timings(pool);
    return pool;
}

namespace {

// True when the operator moving @p direction under @p scope goes through the
// Global order's strengthenings: the only scopes reverse the order, so there
// the opposite operator is the one moving in direction.
bool global_strengthen(Direction direction, ScopeKind scope) {
    return (direction == Direction::Strengthen) != is_only_scope(scope);
}

// mutate_timing under a step scale, which the tick-count steps read.
Timing mutate_timing_scaled(const Timing& timing, Direction direction,
                            ScopeKind scope,
                            const std::vector<Timing>& timing_pool,
                            const RandomSource& random_source,
                            StepScale scale) {
    assert(random_source);
    const bool strengthen = global_strengthen(direction, scope);
    Timing moved =
        strengthen
            ? strengthen_timing(timing, timing_pool, random_source, scale)
            : weaken_timing(timing, timing_pool, random_source, scale);
    // The other scopes drop edges from the Global order, and a step along a
    // dropped edge is refused. Every step is an edge under Global and After,
    // so their moves, and the draws that pick them, are unchanged.
    const bool sound = direction == Direction::Strengthen
                           ? timing_implies(moved, timing, scope)
                           : timing_implies(timing, moved, scope);
    assert(sound || (scope != ScopeKind::Global && scope != ScopeKind::After));
    return sound ? moved : timing;
}

}  // namespace

Timing mutate_timing(const Timing& timing, Direction direction, ScopeKind scope,
                     const std::vector<Timing>& timing_pool,
                     const RandomSource& random_source) {
    return mutate_timing_scaled(timing, direction, scope, timing_pool,
                                random_source, std::nullopt);
}

namespace {

// True when the timing arm moving @p requirement in @p direction moves it off
// Always or Eventually. Such a move has no single edge to take instead, since
// each extreme moves only into a donated timing, so under a step scale the
// arm's rate is what scales. Every other timing keeps cfg.p_timing and scales
// inside its tick-count step.
bool moves_off_extreme(const Requirement& requirement, Direction direction) {
    return global_strengthen(direction, requirement.m_scope.m_kind)
               ? std::holds_alternative<timing::Eventually>(
                     requirement.m_timing)
               : std::holds_alternative<timing::Always>(requirement.m_timing);
}

// mutate_requirement, reporting through @p free_rewrite whether any field took
// the general rewrite rather than a move along its order, under the step scale
// @p scale.
Requirement mutate_requirement_tracked(
    const Requirement& requirement, const std::vector<std::string>& atoms,
    const std::vector<std::string>& condition_atoms, Direction direction,
    const std::vector<Timing>& timing_pool,
    const std::vector<std::string>& mode_pool,
    const RandomSource& random_source, const Config& cfg, bool* free_rewrite,
    StepScale scale) {
    Requirement mutated = requirement;
    // Under cfg.p_monotone these two rewrites move along the implication order
    // and are direction-agnostic otherwise, rewriting the propositional
    // structure freely and relying on the population filters to discard
    // candidates that moved the wrong way. The key defaults to 0, so that is
    // still what a run does unless it is turned on.
    if (random_source.next_real() < cfg.p_response) {
        mutated.m_response =
            rewrite_field(requirement.m_response, atoms,
                          response_direction(requirement, direction), cfg,
                          random_source, free_rewrite);
    }
    if (random_source.next_real() < cfg.p_trigger) {
        mutated.m_condition =
            rewrite_field(requirement.m_condition, condition_atoms,
                          condition_direction(requirement, direction), cfg,
                          random_source, free_rewrite);
    }
    // The draw against the rate is the same one, scaled or not.
    const bool scaled_extreme =
        scale.has_value() && moves_off_extreme(requirement, direction);
    const double timing_rate =
        scaled_extreme ? scaled_share(cfg.p_timing, *scale) : cfg.p_timing;
    if (random_source.next_real() < timing_rate) {
        if (scaled_extreme) {
            StepScalingStats::n_large++;
        }
        mutated.m_timing = mutate_timing_scaled(
            requirement.m_timing, direction, requirement.m_scope.m_kind,
            timing_pool, random_source, scale);
    }
    // Read off the mutated timing, which the arm above may have just moved onto
    // or off a stop. A requirement without one returns before the draw, so no
    // current specification's breeding stream moves at any value of the key.
    const Formula* stop = timing_stop(mutated.m_timing);
    if (cfg.p_stop > 0.0 && stop != nullptr &&
        random_source.next_real() < cfg.p_stop) {
        mutated.m_timing = with_stop(
            mutated.m_timing, rewrite_field(*stop, condition_atoms,
                                            stop_direction(mutated, direction),
                                            cfg, random_source, free_rewrite));
    }
    // Both arms test their probability before touching the RandomSource, so at
    // the default of 0 neither costs a draw and the breeding stream is what it
    // was before scopes existed -- the p_remove_guarantee discipline. The
    // determinism goldens are recorded at that default and pin both keys.
    if (cfg.p_condition_type > 0.0 &&
        random_source.next_real() < cfg.p_condition_type) {
        mutated.m_condition_type = mutate_condition_type(direction);
    }
    if (cfg.p_scope > 0.0 && random_source.next_real() < cfg.p_scope) {
        // Read off the mutated timing and condition type, not the original's:
        // the order table is a fact about the requirement being written, and an
        // arm above may have just moved either of them.
        mutated.m_scope =
            move_scope(requirement.m_scope, direction, mutated.m_timing,
                       mutated.m_condition_type, mode_pool, random_source);
    }
    mutated.m_ltl = requirement_to_ltl(mutated);
    return mutated;
}

}  // namespace

Requirement mutate_requirement(const Requirement& requirement,
                               const std::vector<std::string>& atoms,
                               const std::vector<std::string>& condition_atoms,
                               Direction direction,
                               const std::vector<Timing>& timing_pool,
                               const std::vector<std::string>& mode_pool,
                               const RandomSource& random_source,
                               const Config& cfg) {
    return mutate_requirement_tracked(
        requirement, atoms, condition_atoms, direction, timing_pool, mode_pool,
        random_source, cfg, nullptr, std::nullopt);
}

namespace {

// Appends @p offset plus the index of each requirement in @p reqs that is
// weakenable and not removed. Rewriting or removing a removed requirement
// spends the operator on content nothing reads.
void append_live_weakenable(const std::vector<Requirement>& reqs,
                            std::size_t offset,
                            std::vector<std::size_t>& indices) {
    for (std::size_t i = 0; i < reqs.size(); ++i) {
        if (reqs[i].m_weakenable && !reqs[i].m_removed) {
            indices.push_back(offset + i);
        }
    }
}

// Global indices (assumptions first, then guarantees) of requirements that may
// be mutated.
std::vector<std::size_t> collect_weakenable_indices(
    const Specification& specification) {
    std::vector<std::size_t> indices;
    append_live_weakenable(specification.m_assumptions, 0, indices);
    append_live_weakenable(specification.m_guarantees,
                           specification.m_assumptions.size(), indices);
    return indices;
}

// True if the requirement at @p idx is equal to any other requirement in the
// list, using the same ordering-based equality as the rest of the algorithm.
bool creates_duplicate(const std::vector<Requirement>& requirements,
                       std::size_t idx) {
    for (std::size_t i = 0; i < requirements.size(); ++i) {
        if (i == idx) {
            continue;
        }
        const bool equal = !(requirements[i] < requirements[idx]) &&
                           !(requirements[idx] < requirements[i]);
        if (equal) {
            return true;
        }
    }
    return false;
}

// The atom pool a freshly added assumption draws its condition from: inputs
// plus outputs. Conditioning on system behaviour adds no obligation the system
// can dodge, so an output is safe in the condition. The response is drawn from
// the inputs alone (see add_assumption).
std::vector<std::string> assumption_condition_pool(
    const Specification& specification) {
    std::vector<std::string> pool = specification.m_in_atoms;
    pool.insert(pool.end(), specification.m_out_atoms.begin(),
                specification.m_out_atoms.end());
    return pool;
}

// Draws the condition of a freshly added assumption from @p pool. `true` stays
// in the draw so the unconditional GR(1) fairness assumption G F <atom> remains
// reachable — nothing else in the operator set can produce it, since
// mutate_atom_name rewrites a `true` condition to `false` rather than to an
// atom. The negation flip applies to atoms only: a negated `true` is `false`,
// and an assumption with a false condition constrains nothing.
Formula add_assumption_condition(const std::vector<std::string>& pool,
                                 const RandomSource& random_source,
                                 double p_conditional) {
    if (random_source.next_real() >= p_conditional) {
        return Formula("true");
    }
    Formula condition =
        Formula::make_atom(pool[random_source.next_index(pool.size())]);
    if (random_source.next_bool()) {
        condition = Formula::make_unary(Formula::Kind::Not, condition);
    }
    return condition;
}

// Builds a new environment assumption:
// `whenever <literal|true> C shall eventually satisfy <input literal>` -- i.e.
// G(c -> F r), a conditional fairness assumption (each of condition and
// response is negated on a coin flip). Appending it strengthens the
// environment, which is how the algorithm repairs unrealizability that the
// rewrite-only operators cannot reach. It mirrors tlsf_add_assumption.
//
// The response is always an *input*, though the condition may be an output. An
// assumption that obliges an output is one the system can defeat by never
// raising its own signal, which discharges every guarantee at a stroke.
// Well-separation does not catch that in general: it rejects the unconditional
// `G F <output>`, but it passes the guarded `G(<lit> -> F <output>)`, because
// it asks whether the environment *can* satisfy the assumptions, and an
// environment that never raises the condition can. Drawing the response from
// the inputs closes the gap without a solver query. With no input there is
// nothing the environment alone can be obliged to do, so nothing is added.
//
// Condition and response are drawn independently from overlapping pools, so
// they can coincide, and `G(l -> F l)` is a tautology that constrains nothing.
// Flipping the condition's polarity in that case costs no draw and yields an
// assumption that says something.
Specification add_assumption(const Specification& specification,
                             const RandomSource& random_source,
                             const Config& cfg) {
    const std::vector<std::string>& inputs = specification.m_in_atoms;
    if (inputs.empty()) {
        return specification;
    }
    Formula response =
        Formula::make_atom(inputs[random_source.next_index(inputs.size())]);
    if (random_source.next_bool()) {
        response = Formula::make_unary(Formula::Kind::Not, response);
    }
    Formula condition =
        add_assumption_condition(assumption_condition_pool(specification),
                                 random_source, cfg.p_conditional_assumption);
    if (condition == response) {
        condition = Formula::make_unary(Formula::Kind::Not, condition);
        condition.remove_double_negation();
    }
    std::vector<Requirement> assumptions = specification.m_assumptions;
    assumptions.emplace_back(std::move(condition), std::move(response),
                             timing::eventually(), ConditionType::Continual,
                             /*weakenable=*/true);
    return Specification(std::move(assumptions), specification.m_guarantees,
                         specification.m_in_atoms, specification.m_out_atoms,
                         specification.m_modes);
}

// Guarantee slots that removal may take: weakenable, and not already removed.
std::vector<std::size_t> removable_guarantee_indices(
    const Specification& specification) {
    std::vector<std::size_t> indices;
    append_live_weakenable(specification.m_guarantees, 0, indices);
    return indices;
}

// Deletes one guarantee by tombstoning it in place. The slot stays so that
// every comparison against the original keeps pairing the same requirements;
// erasing it would shift the rest and score unrelated pairs against each other.
// The mirror of add_assumption: that one strengthens the environment, this one
// weakens the system's obligations, and some repairs are only reachable by
// dropping a guarantee outright.
Specification remove_guarantee(const Specification& specification,
                               const std::vector<std::size_t>& removable,
                               const RandomSource& random_source) {
    assert(!removable.empty());
    std::vector<Requirement> guarantees = specification.m_guarantees;
    const std::size_t choice =
        removable[random_source.next_index(removable.size())];
    guarantees[choice].m_removed = true;
    return Specification(specification.m_assumptions, std::move(guarantees),
                         specification.m_in_atoms, specification.m_out_atoms,
                         specification.m_modes);
}

// The structural actions, tried before any rewrite: the specification with an
// assumption added or a guarantee removed, or nothing where neither fired.
std::optional<Specification> structural_action(
    const Specification& specification, const RandomSource& random_source,
    const Config& cfg, StepScale scale) {
    // Low-probability structural action: add a new environment assumption. The
    // Specification constructor deduplicates, so re-adding an existing
    // assumption is a harmless no-op. Available only when the specification
    // has an input, since the response of an added assumption must be one; a
    // spec without inputs spends no draw here and goes on to the rewrites.
    if (!specification.m_in_atoms.empty() &&
        random_source.next_real() < cfg.p_add_assumption) {
        return add_assumption(specification, random_source, cfg);
    }
    // The mirror action: delete a guarantee. Never the last live one, since a
    // specification with nothing left to guarantee is realizable by doing
    // nothing and is no repair at all. The probability is read before the draw
    // so that a zero costs no draw at all, which is what lets a config set it
    // to 0 and reproduce a run from before the operator existed.
    if (cfg.p_remove_guarantee > 0.0) {
        const std::vector<std::size_t> removable =
            removable_guarantee_indices(specification);
        // The floor is on live guarantees, not removable ones: removing the
        // only weakenable guarantee is fine while locked ones remain.
        // Removal is a large move, so a step scale scales its rate. The draw
        // against it is the same one either way.
        const double rate = scale.has_value()
                                ? scaled_share(cfg.p_remove_guarantee, *scale)
                                : cfg.p_remove_guarantee;
        if (!removable.empty() && count_live(specification.m_guarantees) > 1 &&
            random_source.next_real() < rate) {
            if (scale.has_value()) {
                StepScalingStats::n_large++;
            }
            return remove_guarantee(specification, removable, random_source);
        }
    }
    return std::nullopt;
}

// Which way Config::direction_gated_mutation lets a mutation move the whole
// specification, read from what breeding knows about the parent.
enum class Gate : std::uint8_t {
    // No gate: the key is off, there is no context or status, or crossover
    // fired and the first parent's status says little about the offspring.
    None,
    // An unrealizable parent: weaker, equal or sideways.
    Weaken,
    // A realizable parent: stronger, equal or sideways.
    Strengthen,
};

Gate gate_of(const Config& cfg, const ParentContext* parent) {
    if (!cfg.direction_gated_mutation || parent == nullptr || parent->crossed ||
        !parent->status.has_value()) {
        return Gate::None;
    }
    return parent->realizable ? Gate::Strengthen : Gate::Weaken;
}

// True when @p child, a free rewrite of @p parent, may stand under @p gate.
// A result proved to move the forbidden way is refused. One the rules cannot
// order either way is kept with probability cfg.p_sideways, which is read
// before the RandomSource is touched, so at 0 or 1 it costs no draw.
bool admitted_by_gate(Gate gate, const Specification& parent,
                      const Specification& child, const Config& cfg,
                      const RandomSource& random_source) {
    const MoveOrder order = classify_move(parent, child);
    const MoveOrder forbidden =
        gate == Gate::Weaken ? MoveOrder::Stronger : MoveOrder::Weaker;
    if (order == forbidden) {
        DirectionGateStats::n_forbidden++;
        return false;
    }
    if (order != MoveOrder::Incomparable || cfg.p_sideways >= 1.0) {
        return true;
    }
    if (cfg.p_sideways <= 0.0 || random_source.next_real() >= cfg.p_sideways) {
        DirectionGateStats::n_sideways_refused++;
        return false;
    }
    return true;
}

// The step scale Config::step_scaling sets from @p parent's status:
// `1 + k * (1 - 2 * status)`, above 1 for a parent more than half way from
// realizable and below it nearer. The key is read first, so at 0 nothing else
// is, and a crossover offspring, whose first parent's status is only an
// estimate of it, is unscaled like a parent without a status.
StepScale step_scale(const Config& cfg, const ParentContext* parent) {
    if (cfg.step_scaling <= 0.0 || parent == nullptr || parent->crossed ||
        !parent->status.has_value()) {
        return std::nullopt;
    }
    StepScalingStats::n_scaled++;
    const double status = std::clamp(*parent->status, 0.0, 1.0);
    return 1.0 + (cfg.step_scaling * (1.0 - (2.0 * status)));
}

// A realizable parent's steps back towards its original, each marked where
// Config::bisect_steps moved it to the midpoint of its timing chain.
struct BackSteps {
    std::vector<Step> m_steps;
    std::vector<bool> m_bisected;
};

// Moves a timing step from @p current to the midpoint of the `within n` or
// `for n` chain it crosses, rounding towards the destination, and returns
// whether it moved. A chain with no intermediate entry, or a midpoint that
// would merge two entries, leaves the step going all the way.
bool bisect_step(const Specification& current, Step& step) {
    const std::vector<Timing> chain = timing_step_chain(current, step);
    if (chain.size() < 2) {
        return false;
    }
    std::optional<Specification> midpoint = with_slot_timing(
        current, step.m_side, step.m_slot, chain[(chain.size() - 1) / 2]);
    if (!midpoint.has_value()) {
        return false;
    }
    step.m_result = std::move(*midpoint);
    return true;
}

// The steps a realizable parent may take back towards its original, or none
// where it lent no original. Bisection replaces a step's result and draws
// nothing, so the list keeps its length.
BackSteps steps_back(const Specification& specification,
                     const ParentContext* parent, const Config& cfg) {
    BackSteps back;
    if (parent == nullptr || parent->original == nullptr) {
        return back;
    }
    back.m_steps = steps_towards(specification, *parent->original);
    back.m_bisected.assign(back.m_steps.size(), false);
    if (cfg.bisect_steps) {
        for (std::size_t i = 0; i < back.m_steps.size(); ++i) {
            back.m_bisected[i] = bisect_step(specification, back.m_steps[i]);
        }
    }
    return back;
}

// Takes step @p choice of @p back, counting it.
Specification take_step(BackSteps& back, std::size_t choice) {
    DirectionGateStats::n_steps++;
    if (back.m_bisected[choice]) {
        DirectionGateStats::n_bisected++;
    }
    return std::move(back.m_steps[choice].m_result);
}

// The rates a realizable parent's rewrite runs at: every arm that moves along
// an order is off, since its strengthenings come from steps_towards instead,
// and only the free rewrites remain. An arm at 0 that still draws its
// next_real draws it, so the rewrite keeps the shape of its stream.
Config free_rewrite_rates(const Config& cfg) {
    Config free = cfg;
    free.p_timing = 0.0;
    free.p_condition_type = 0.0;
    free.p_scope = 0.0;
    free.p_monotone = 0.0;
    return free;
}

}  // namespace

std::vector<std::size_t> targeted_mutation_slots(
    const Specification& specification,
    const std::vector<std::size_t>& weakenable, const ParentContext& parent) {
    if (!parent.status.has_value() || parent.crossed) {
        return {};
    }
    const std::size_t n_assumptions = specification.m_assumptions.size();
    std::vector<std::size_t> targeted;
    if (!parent.realizable) {
        // An empty refused set on an unrealizable parent means the walk never
        // graded it, so it says nothing about where the conflict lies.
        if (parent.refused_guarantees.empty()) {
            return {};
        }
        for (const std::size_t idx : weakenable) {
            if (idx < n_assumptions ||
                std::binary_search(parent.refused_guarantees.begin(),
                                   parent.refused_guarantees.end(),
                                   idx - n_assumptions)) {
                targeted.push_back(idx);
            }
        }
        return targeted;
    }
    if (parent.original == nullptr) {
        return {};
    }
    const Specification& original = *parent.original;
    for (const std::size_t idx : weakenable) {
        const bool is_assumption = idx < n_assumptions;
        const std::size_t local = is_assumption ? idx : idx - n_assumptions;
        const std::vector<Requirement>& own = is_assumption
                                                  ? specification.m_assumptions
                                                  : specification.m_guarantees;
        const std::vector<Requirement>& theirs =
            is_assumption ? original.m_assumptions : original.m_guarantees;
        if (local >= theirs.size() || !(own[local] == theirs[local])) {
            targeted.push_back(idx);
        }
    }
    return targeted;
}

Specification mutate_specification(const Specification& specification,
                                   const RandomSource& random_source,
                                   const Config& cfg,
                                   const ParentContext* parent) {
    assert(random_source);
    const std::size_t n_assumptions = specification.m_assumptions.size();
    assert(n_assumptions + specification.m_guarantees.size() > 0);
    const Gate gate = gate_of(cfg, parent);
    const StepScale scale = step_scale(cfg, parent);
    // Both structural actions weaken the specification, so a realizable parent
    // under the gate skips them before their draws.
    const bool strengthening = gate == Gate::Strengthen;
    std::optional<Specification> acted =
        strengthening
            ? std::nullopt
            : structural_action(specification, random_source, cfg, scale);
    if (acted.has_value()) {
        return std::move(*acted);
    }
    std::vector<std::string> atoms;
    atoms.insert(atoms.end(), specification.m_in_atoms.begin(),
                 specification.m_in_atoms.end());
    atoms.insert(atoms.end(), specification.m_out_atoms.begin(),
                 specification.m_out_atoms.end());
    const std::vector<Timing> timing_pool = collect_timing_pool(specification);
    std::vector<std::size_t> weakenable_indices =
        collect_weakenable_indices(specification);
    // A realizable parent's strengthenings, each proved by classify_move and
    // moving one field, tombstone or appended assumption back towards the
    // original. Empty without the gate.
    BackSteps back =
        strengthening ? steps_back(specification, parent, cfg) : BackSteps{};
    const std::vector<Step>& steps = back.m_steps;
    if (weakenable_indices.empty() && steps.empty()) {
        return specification;
    }
    if (cfg.blame_targeted_slots && parent != nullptr) {
        std::vector<std::size_t> targeted =
            targeted_mutation_slots(specification, weakenable_indices, *parent);
        if (!targeted.empty()) {
            if (targeted.size() < weakenable_indices.size()) {
                SlotTargetingStats::n_targeted++;
            }
            weakenable_indices = std::move(targeted);
        }
    }
    // Under the gate a realizable parent draws once over its steps and its
    // rewritable slots together, each counted once, so a parent far from the
    // original mostly steps back and one at it mostly rewrites sideways.
    // Without the gate `steps` is empty and this is the slot draw alone.
    const std::size_t choice =
        random_source.next_index(steps.size() + weakenable_indices.size());
    if (choice < steps.size()) {
        return take_step(back, choice);
    }
    const std::size_t idx = weakenable_indices[choice - steps.size()];
    std::vector<Requirement> assumptions = specification.m_assumptions;
    std::vector<Requirement> guarantees = specification.m_guarantees;
    // Weakening the assume-guarantee specification means weakening a guarantee
    // but *strengthening* an assumption, so the direction follows the side. It
    // was a config flag until 2026-08-26, kept only so the two directions could
    // be crossed as an experiment factor; no sweep ever crossed them and
    // gen_configs.py could not emit the key.
    const bool is_assumption = idx < n_assumptions;
    const Direction direction = is_assumption != strengthening
                                    ? Direction::Strengthen
                                    : Direction::Weaken;
    // An existing assumption draws from the same pool as a freshly added one,
    // inputs plus outputs, as guarantees do.
    const std::vector<std::string>& mutation_atoms = atoms;
    if (mutation_atoms.empty()) {
        // Without atoms, mutate_formula's structural rewrites cannot draw a
        // replacement atom; leave the specification unchanged.
        return specification;
    }
    // Triggers are evaluated at the current timepoint, where the current state
    // is available through input atoms; output atoms denote the next state, so
    // letting one into a trigger produces a self-referential guard the
    // synthesiser can vacuously discharge. Draw trigger atoms from inputs only.
    // Fall back to the mutation pool when there are no inputs, since mutation
    // cannot then draw an atom for a trigger at all — and that fallback is the
    // second route an output could reach an assumption, so it falls back to the
    // gated pool rather than to the full one.
    const std::vector<std::string>& condition_atoms =
        specification.m_in_atoms.empty() ? mutation_atoms
                                         : specification.m_in_atoms;
    // Both requirement lists live behind the same mutate-then-dedup-check
    // logic; pick the target and its local index so that logic appears once.
    // Exactly one mutate_requirement call happens either way, keeping the RNG
    // draw sequence (and thus seed reproducibility) identical to the two-arm
    // form.
    std::vector<Requirement>& target = is_assumption ? assumptions : guarantees;
    const std::size_t local_idx = is_assumption ? idx : idx - n_assumptions;
    bool free_rewrite = false;
    target[local_idx] = mutate_requirement_tracked(
        target[local_idx], mutation_atoms, condition_atoms, direction,
        timing_pool, specification.m_modes, random_source,
        strengthening ? free_rewrite_rates(cfg) : cfg, &free_rewrite, scale);
    if (creates_duplicate(target, local_idx)) {
        return specification;
    }
    Specification mutated(std::move(assumptions), std::move(guarantees),
                          specification.m_in_atoms, specification.m_out_atoms,
                          specification.m_modes);
    // The directed arms move the way the gate allows by construction, so only
    // a free rewrite needs sorting. Returning the parent leaves an offspring
    // equal to it, which deduplication removes.
    if (gate != Gate::None && free_rewrite &&
        !admitted_by_gate(gate, specification, mutated, cfg, random_source)) {
        return specification;
    }
    return mutated;
}
