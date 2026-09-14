#include "genetic/crossover.hpp"

#include <cassert>
#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

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

Formula select_subformula(const Formula& formula,
                          const RandomSource& random_source) {
    switch (formula.kind()) {
        case Formula::Kind::Atom:
            return formula;
        case Formula::Kind::Not: {
            const auto child_opt = formula.unary_child();
            if (!child_opt.has_value()) {
                assert(false);
                __builtin_unreachable();
            }
            const Formula& child = *child_opt;
            if (!random_source.next_bool()) {
                return formula;
            }
            return select_subformula(child, random_source);
        }
        case Formula::Kind::And:
        case Formula::Kind::Or:
        case Formula::Kind::Implies:
        case Formula::Kind::Iff: {
            const auto children_opt = formula.binary_children();
            if (!children_opt.has_value()) {
                assert(false);
                __builtin_unreachable();
            }
            const auto& children = *children_opt;
            if (!random_source.next_bool()) {
                return formula;
            }
            if (!random_source.next_bool()) {
                return select_subformula(children.first, random_source);
            }
            return select_subformula(children.second, random_source);
        }
        // FRETISH formulae are propositional; temporal operators never occur
        // here. Treat any such subtree as an opaque leaf.
        case Formula::Kind::Next:
        case Formula::Kind::Eventually:
        case Formula::Kind::Globally:
        case Formula::Kind::Until:
        case Formula::Kind::Release:
        case Formula::Kind::WeakUntil:
            return formula;
    }
    assert(false);
    __builtin_unreachable();
}

// The graft site is drawn uniformly over the subject's nodes, as the TLSF path
// draws over its graft sites. A fair coin at each node of the walk instead,
// which is what this did before 2026-08-19, reaches the k-th node with
// probability 2^-k and grafts nowhere at all with probability 2^-n: over half
// of all grafts land on the leftmost-deepest leaf, and a single-atom condition
// or response is returned verbatim every other time by an operator documented
// as always recombining.
Formula graft_at_uniform_site(
    const Formula& formula, const RandomSource& random_source,
    const std::function<Formula(const Formula&)>& merge) {
    const std::size_t site = random_source.next_index(formula.n_subformulae());
    std::size_t visited = 0;
    return formula.rewrite_post_order(
        [&](const Formula& subtree) -> std::optional<Formula> {
            if (visited++ != site) {
                return std::nullopt;
            }
            return merge(subtree);
        });
}

Formula replace_subformula(const Formula& formula, const Formula& donor,
                           const RandomSource& random_source) {
    Formula replacement = select_subformula(donor, random_source);
    return graft_at_uniform_site(
        formula, random_source,
        [&replacement](const Formula&) { return replacement; });
}

Formula combine_subformula(const Formula& formula, const Formula& donor,
                           const RandomSource& random_source) {
    const Formula donor_subformula = select_subformula(donor, random_source);
    return graft_at_uniform_site(
        formula, random_source, [&](const Formula& subtree) {
            if (random_source.next_bool()) {
                return Formula::make_binary(pick_binary_kind(random_source),
                                            subtree, donor_subformula);
            }
            return Formula::make_binary(pick_binary_kind(random_source),
                                        donor_subformula, subtree);
        });
}

// AuRUS merges two conjuncts by grafting, and only by grafting: with equal
// probability it replaces a subformula of the first with one drawn from the
// second (replaceSubformula), or joins the two under a fresh binary operator
// (combineSubformula). Neither branch copies a parent's field verbatim, so
// every crossover recombines.
Formula crossover_formula(const Formula& first_parent,
                          const Formula& second_parent,
                          const RandomSource& random_source) {
    if (random_source.next_bool()) {
        return replace_subformula(first_parent, second_parent, random_source);
    }
    return combine_subformula(first_parent, second_parent, random_source);
}

template <typename TimingVariant>
Timing make_parameterized_timing(std::size_t ticks) {
    if constexpr (std::is_same_v<TimingVariant, timing::WithinTicks>) {
        return timing::within_ticks(ticks);
    } else if constexpr (std::is_same_v<TimingVariant, timing::AfterTicks>) {
        return timing::after_ticks(ticks);
    } else {
        return timing::for_ticks(ticks);
    }
}

template <typename TimingVariant>
constexpr bool is_parameterized_timing_v =
    std::is_same_v<TimingVariant, timing::WithinTicks> ||
    std::is_same_v<TimingVariant, timing::ForTicks> ||
    std::is_same_v<TimingVariant, timing::AfterTicks>;

template <typename First, typename Second>
Timing crossover_parameterized_timing(const First& first_value,
                                      const Second& second_value,
                                      const RandomSource& random_source) {
    const int selector = static_cast<int>(random_source.next_index(4));
    switch (selector) {
        case 0:
            return first_value;
        case 1:
            return second_value;
        case 2:
            return make_parameterized_timing<First>(second_value.m_ticks);
        case 3:
            return make_parameterized_timing<Second>(first_value.m_ticks);
        default:
            assert(false);
            __builtin_unreachable();
    }
}

template <typename First, typename Second>
Timing crossover_timing_values(const First& first_value,
                               const Second& second_value,
                               const RandomSource& random_source) {
    if constexpr (std::is_same_v<First, Second>) {
        if constexpr (is_parameterized_timing_v<First>) {
            return crossover_parameterized_timing(first_value, second_value,
                                                  random_source);
        }
        if (!random_source.next_bool()) {
            return first_value;
        }
        return second_value;
    }
    if constexpr (is_parameterized_timing_v<First> &&
                  is_parameterized_timing_v<Second>) {
        return crossover_parameterized_timing(first_value, second_value,
                                              random_source);
    }
    if (!random_source.next_bool()) {
        return first_value;
    }
    return second_value;
}

Timing crossover_timing(const Timing& first_parent, const Timing& second_parent,
                        const RandomSource& random_source) {
    return std::visit(
        [&](const auto& first_value) -> Timing {
            return std::visit(
                [&](const auto& second_value) -> Timing {
                    return crossover_timing_values(first_value, second_value,
                                                   random_source);
                },
                second_parent);
        },
        first_parent);
}

// Condition type and scope are both short implication orders with no parameter
// to recombine, so each is taken whole from one parent on a coin, as the timing
// crossover does for two unparameterised timings.
ConditionType crossover_condition_type(ConditionType first_parent,
                                       ConditionType second_parent,
                                       const RandomSource& random_source) {
    return random_source.next_bool() ? second_parent : first_parent;
}

// The mode travels with the kind. crossover_specifications has already required
// both parents to declare the same modes, so the donor's scope names one the
// offspring declares.
Scope crossover_scope(const Scope& first_parent, const Scope& second_parent,
                      const RandomSource& random_source) {
    return random_source.next_bool() ? second_parent : first_parent;
}

}  // namespace

Requirement crossover_requirements(const Requirement& first_parent,
                                   const Requirement& second_parent,
                                   const RandomSource& random_source) {
    assert(random_source);
    Requirement offspring = first_parent;
    offspring.m_condition = crossover_formula(
        first_parent.m_condition, second_parent.m_condition, random_source);
    offspring.m_response = crossover_formula(
        first_parent.m_response, second_parent.m_response, random_source);
    offspring.m_timing = crossover_timing(
        first_parent.m_timing, second_parent.m_timing, random_source);
    offspring.m_condition_type =
        crossover_condition_type(first_parent.m_condition_type,
                                 second_parent.m_condition_type, random_source);
    offspring.m_scope = crossover_scope(first_parent.m_scope,
                                        second_parent.m_scope, random_source);
    offspring.m_ltl = requirement_to_ltl(offspring);
    return offspring;
}

namespace {

// The slots of @p requirements crossover may read or write: a deleted
// requirement is content its parent has thrown away, and a non-weakenable one
// is locked, never changed and never acting as a crossover source.
std::vector<std::size_t> crossover_slots(
    const std::vector<Requirement>& requirements) {
    std::vector<std::size_t> slots;
    for (std::size_t i = 0; i < requirements.size(); ++i) {
        if (requirements[i].m_weakenable && !requirements[i].m_removed) {
            slots.push_back(i);
        }
    }
    return slots;
}

// AuRUS recombines a side by drawing one conjunct from each parent — from
// anywhere in either list — merging that pair, and leaving the rest of the
// first parent's side alone. The donor is unrelated to the slot it lands in,
// which is the whole point: a subformula of guarantee 3 can graft into
// guarantee 0. Recombining every slot against the same index, as this did
// until now, confines each graft to the pair of requirements that already
// occupy the same position and can never move material between them.
//
// AuRUS removes the target conjunct and appends the merged one, which
// reorders the side and shortens it when the merge fails. PEREDUR writes the
// merge back into the target's own slot instead: slot i of a candidate must
// keep descending from slot i of the original, since the timing and semantic
// similarity objectives pair the two by position. Deletion stays mutation's
// move alone — crossover can neither resurrect a deleted requirement nor
// delete a live one.
std::vector<Requirement> crossover_req_lists(
    const std::vector<Requirement>& first,
    const std::vector<Requirement>& second, const RandomSource& random_source) {
    std::vector<Requirement> offspring = first;
    const std::vector<std::size_t> targets = crossover_slots(first);
    const std::vector<std::size_t> donors = crossover_slots(second);
    if (targets.empty() || donors.empty()) {
        return offspring;
    }
    // Sequenced into locals: both draw, and argument evaluation order is
    // unspecified.
    const std::size_t target =
        targets[random_source.next_index(targets.size())];
    const std::size_t donor = donors[random_source.next_index(donors.size())];
    offspring[target] =
        crossover_requirements(first[target], second[donor], random_source);
    return offspring;
}

}  // namespace

Specification crossover_specifications(const Specification& first_parent,
                                       const Specification& second_parent,
                                       const RandomSource& random_source) {
    assert(random_source);
    // Only the signals have to match. The two sides no longer need equal
    // lengths: the offspring keeps the first parent's shape whatever the
    // second parent's is, since the merge is written back into a slot of the
    // first. That is what lets an individual that has gained an assumption
    // still breed, which under index-for-index pairing it could not.
    // Modes are signals too, and join the guard for the same reason: a scope
    // grafted from a parent that declares a different mode set would name a
    // mode the offspring has never declared.
    if (first_parent.m_in_atoms != second_parent.m_in_atoms ||
        first_parent.m_out_atoms != second_parent.m_out_atoms ||
        first_parent.m_modes != second_parent.m_modes) {
        return first_parent;
    }
    // Both calls draw, and the order in which arguments of one call are
    // evaluated is unspecified -- gcc runs them right to left, clang left to
    // right -- so passing them directly hands the two lists each other's draws
    // depending on the compiler. Sequence them to keep a seed reproducible.
    std::vector<Requirement> assumptions = crossover_req_lists(
        first_parent.m_assumptions, second_parent.m_assumptions, random_source);
    std::vector<Requirement> guarantees = crossover_req_lists(
        first_parent.m_guarantees, second_parent.m_guarantees, random_source);
    Specification offspring(std::move(assumptions), std::move(guarantees),
                            first_parent.m_in_atoms, first_parent.m_out_atoms,
                            first_parent.m_modes);
    // Specification constructor deduplicates; if dedup reduced the count the
    // offspring has a different structure than the parents and cannot safely
    // participate in future crossovers — fall back to first_parent.
    if (offspring.m_assumptions.size() != first_parent.m_assumptions.size() ||
        offspring.m_guarantees.size() != first_parent.m_guarantees.size()) {
        return first_parent;
    }
    return offspring;
}
