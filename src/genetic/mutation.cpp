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

#include "genetic/monotone.hpp"
#include "prop_formula.hpp"

namespace {

Formula::Kind pick_binary_kind(const RandomSource& random_source) {
    const int selector = static_cast<int>(random_source.next_index(4));
    switch (selector) {
        case 0:
            return Formula::Kind::And;
        case 1:
            return Formula::Kind::Or;
        case 2:
            return Formula::Kind::Implies;
        case 3:
            return Formula::Kind::Iff;
        default:
            assert(false);
            __builtin_unreachable();
    }
}

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

Timing weaken_for_timing(const timing::ForTicks& for_ticks,
                         const RandomSource& random_source) {
    if (for_ticks.m_ticks == 1) {
        return random_source.next_bool() ? timing::next_timepoint()
                                         : timing::immediately();
    }
    return random_source.next_bool() ? timing::for_ticks(for_ticks.m_ticks - 1)
                                     : timing::for_ticks(for_ticks.m_ticks / 2);
}

Timing weaken_within_timing(const timing::WithinTicks& within_ticks,
                            const RandomSource& random_source) {
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
                             const RandomSource& random_source) {
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
                                const RandomSource& random_source) {
    if (within_ticks.m_ticks == 1) {
        return random_source.next_bool() ? timing::next_timepoint()
                                         : timing::immediately();
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
                         const RandomSource& random_source) {
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
            return strengthen_for_timing(value, random_source);
        } else if constexpr (std::is_same_v<T, timing::AfterTicks>) {
            // `after n` expands to ¬R at ticks 0..n ∧ R at tick n+1, pinning
            // the response to a single tick. Nothing in the timing order lies
            // strictly above it: `after n-1` and `always` both demand R at a
            // tick where `after n` demands ¬R, so they contradict rather than
            // strengthen it. AfterTicks therefore has no strengthening.
            return timing::after_ticks(value.m_ticks);
        } else if constexpr (std::is_same_v<T, timing::WithinTicks>) {
            return strengthen_within_timing(value, random_source);
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
                     const RandomSource& random_source) {
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
            return weaken_for_timing(value, random_source);
        } else if constexpr (std::is_same_v<T, timing::AfterTicks>) {
            return timing::within_ticks(value.m_ticks + 1);
        } else if constexpr (std::is_same_v<T, timing::WithinTicks>) {
            return weaken_within_timing(value, random_source);
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

// The scope order depends on the timing and the condition type, because the
// scope boundary relaxes a bounded obligation but *tightens* an unbounded one:
// `in m ... eventually r` demands the response arrive before the mode ends,
// which global scope does not, so `global => in` holds at `always` and fails at
// `eventually`. Measured tick counts 2 and 4 give identical orders, so the
// table is independent of n. `until` and `before`, measured at an atom stop and
// at a conjunction, fall in the same cells as the tick-bounded timings, so it
// is independent of the stop too.
//
// `notin => before` is the one edge holding in every cell: the interval
// strictly before the first entry into the mode is contained in the set of
// points where the mode does not hold.
//
// The three "only" scopes are an antichain in every cell -- each constrains a
// different complement -- so a requirement already carrying one cannot move on
// this field at all.
struct ScopeEdge {
    ScopeKind m_stronger;
    ScopeKind m_weaker;
};

std::vector<ScopeEdge> scope_order(const Timing& timing,
                                   ConditionType condition_type) {
    const ScopeEdge notin_before{ScopeKind::NotIn, ScopeKind::Before};
    std::vector<ScopeEdge> from_global{{ScopeKind::Global, ScopeKind::In},
                                       {ScopeKind::Global, ScopeKind::NotIn},
                                       {ScopeKind::Global, ScopeKind::Before},
                                       {ScopeKind::Global, ScopeKind::After},
                                       notin_before};
    const bool is_eventually =
        std::holds_alternative<timing::Eventually>(timing);
    const bool is_always = std::holds_alternative<timing::Always>(timing);
    if (is_eventually) {
        // Under `eventually` the boundary turns `F r` into `!boundary U r`, a
        // strengthening, so global sits below three of the four scopes.
        if (condition_type == ConditionType::Continual) {
            return {{ScopeKind::Global, ScopeKind::After}, notin_before};
        }
        return {notin_before};
    }
    if (condition_type == ConditionType::Continual || is_always) {
        return from_global;
    }
    // A trigger's rising edge is itself confined by the boundary, which costs
    // the three edges a continual condition keeps.
    return {{ScopeKind::Global, ScopeKind::Before}, notin_before};
}

// A scope other than Global names a mode, so a move onto one is available only
// where the specification declares at least one.
Scope move_scope(const Scope& scope, Direction direction, const Timing& timing,
                 ConditionType condition_type,
                 const std::vector<std::string>& mode_pool,
                 const RandomSource& random_source) {
    std::vector<Scope> candidates;
    for (const ScopeEdge& edge : scope_order(timing, condition_type)) {
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
// the breeding stream is what it was before it existed.
Formula rewrite_field(const Formula& field,
                      const std::vector<std::string>& atoms,
                      std::optional<MonotoneDirection> direction,
                      const Config& cfg, const RandomSource& random_source) {
    if (cfg.p_monotone > 0.0 && direction.has_value() && !atoms.empty() &&
        random_source.next_real() < cfg.p_monotone) {
        return monotone_rewrite(field, *direction, atoms, random_source);
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

Timing mutate_timing(const Timing& timing, Direction direction,
                     const std::vector<Timing>& timing_pool,
                     const RandomSource& random_source) {
    assert(random_source);
    return direction == Direction::Strengthen
               ? strengthen_timing(timing, timing_pool, random_source)
               : weaken_timing(timing, timing_pool, random_source);
}

Requirement mutate_requirement(const Requirement& requirement,
                               const std::vector<std::string>& atoms,
                               const std::vector<std::string>& condition_atoms,
                               Direction direction,
                               const std::vector<Timing>& timing_pool,
                               const std::vector<std::string>& mode_pool,
                               const RandomSource& random_source,
                               const Config& cfg) {
    Requirement mutated = requirement;
    // Under cfg.p_monotone these two rewrites move along the implication order
    // and are direction-agnostic otherwise, rewriting the propositional
    // structure freely and relying on the population filters to discard
    // candidates that moved the wrong way. The key defaults to 0, so that is
    // still what a run does unless it is turned on.
    if (random_source.next_real() < cfg.p_response) {
        mutated.m_response = rewrite_field(
            requirement.m_response, atoms,
            response_direction(requirement, direction), cfg, random_source);
    }
    if (random_source.next_real() < cfg.p_trigger) {
        mutated.m_condition = rewrite_field(
            requirement.m_condition, condition_atoms,
            condition_direction(requirement, direction), cfg, random_source);
    }
    if (random_source.next_real() < cfg.p_timing) {
        mutated.m_timing = mutate_timing(requirement.m_timing, direction,
                                         timing_pool, random_source);
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
                                            cfg, random_source));
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

namespace {

// Global indices (assumptions first, then guarantees) of requirements that may
// be mutated. Non-weakenable and removed requirements are excluded: rewriting a
// removed requirement spends the mutation on content nothing reads.
std::vector<std::size_t> collect_weakenable_indices(
    const Specification& specification) {
    const std::size_t n_assumptions = specification.m_assumptions.size();
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < n_assumptions; ++i) {
        const Requirement& req = specification.m_assumptions[i];
        if (req.m_weakenable && !req.m_removed) {
            indices.push_back(i);
        }
    }
    for (std::size_t i = 0; i < specification.m_guarantees.size(); ++i) {
        const Requirement& req = specification.m_guarantees[i];
        if (req.m_weakenable && !req.m_removed) {
            indices.push_back(n_assumptions + i);
        }
    }
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
    for (std::size_t i = 0; i < specification.m_guarantees.size(); ++i) {
        const Requirement& req = specification.m_guarantees[i];
        if (req.m_weakenable && !req.m_removed) {
            indices.push_back(i);
        }
    }
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

}  // namespace

Specification mutate_specification(const Specification& specification,
                                   const RandomSource& random_source,
                                   const Config& cfg) {
    assert(random_source);
    const std::size_t n_assumptions = specification.m_assumptions.size();
    assert(n_assumptions + specification.m_guarantees.size() > 0);
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
        if (!removable.empty() && count_live(specification.m_guarantees) > 1 &&
            random_source.next_real() < cfg.p_remove_guarantee) {
            return remove_guarantee(specification, removable, random_source);
        }
    }
    std::vector<std::string> atoms;
    atoms.insert(atoms.end(), specification.m_in_atoms.begin(),
                 specification.m_in_atoms.end());
    atoms.insert(atoms.end(), specification.m_out_atoms.begin(),
                 specification.m_out_atoms.end());
    const std::vector<Timing> timing_pool = collect_timing_pool(specification);
    const std::vector<std::size_t> weakenable_indices =
        collect_weakenable_indices(specification);
    if (weakenable_indices.empty()) {
        return specification;
    }
    const std::size_t idx =
        weakenable_indices[random_source.next_index(weakenable_indices.size())];
    std::vector<Requirement> assumptions = specification.m_assumptions;
    std::vector<Requirement> guarantees = specification.m_guarantees;
    // Weakening the assume-guarantee specification means weakening a guarantee
    // but *strengthening* an assumption, so the direction follows the side. It
    // was a config flag until 2026-08-26, kept only so the two directions could
    // be crossed as an experiment factor; no sweep ever crossed them and
    // gen_configs.py could not emit the key.
    const bool is_assumption = idx < n_assumptions;
    const Direction direction =
        is_assumption ? Direction::Strengthen : Direction::Weaken;
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
    target[local_idx] = mutate_requirement(
        target[local_idx], mutation_atoms, condition_atoms, direction,
        timing_pool, specification.m_modes, random_source, cfg);
    if (creates_duplicate(target, local_idx)) {
        return specification;
    }
    return Specification(std::move(assumptions), std::move(guarantees),
                         specification.m_in_atoms, specification.m_out_atoms,
                         specification.m_modes);
}
