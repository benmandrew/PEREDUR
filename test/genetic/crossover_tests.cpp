#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "fixtures.hpp"
#include "genetic/crossover.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "crossover";

// Every crossover grafts: there is no branch that copies a parent's field
// verbatim, so an all-ones source replaces rather than inherits.
TEST(test_crossover_always_recombines) {
    const Requirement first_parent{Formula("P"), Formula("Q"),
                                   timing::immediately()};
    const Requirement second_parent{Formula("R"), Formula("S"),
                                    timing::next_timepoint()};
    const Requirement offspring = crossover_requirements(
        first_parent, second_parent, make_source({1, 1}, 1U));
    expect(offspring.m_condition.to_string() == "R",
           "crossover: the replace branch grafts the second parent's "
           "condition");
    expect(offspring.m_response.to_string() == "S",
           "crossover: the replace branch grafts the second parent's "
           "response");
}

TEST(test_timing_crossover_can_swap_parameters) {
    const Requirement first_parent{Formula("P"), Formula("Q"),
                                   timing::within_ticks(5)};
    const Requirement second_parent{Formula("P"), Formula("Q"),
                                    timing::for_ticks(10)};
    // A field spends four draws -- branch, site, orientation, connective --
    // so the timing draw is the ninth.
    const Requirement offspring =
        crossover_requirements(first_parent, second_parent,
                               make_source({0, 0, 0, 0, 0, 0, 0, 0, 2}, 0));
    const auto* within = std::get_if<timing::WithinTicks>(&offspring.m_timing);
    expect(within != nullptr,
           "crossover: parameter crossover should preserve the operator from"
           " the first parent when selected");
    expect(within->m_ticks == 10,
           "crossover: parameter crossover should be able to swap ticks");
}

// Condition type and scope each come whole from one parent, on the tenth and
// eleventh draws, after the two formula fields' eight and the timing's one.
// Each coin is independent, so either field can come from the second parent
// while the other stays with the first.
TEST(test_condition_type_and_scope_cross_over) {
    const Scope in_mode{ScopeKind::In, "m"};
    const Requirement first_parent(Formula("P"), Formula("Q"),
                                   timing::immediately(),
                                   ConditionType::Continual, true, false);
    const Requirement second_parent(
        Formula("P"), Formula("Q"), timing::immediately(),
        ConditionType::Trigger, true, false, in_mode);
    const std::vector<std::size_t> formula_and_timing(9, 0);

    std::vector<std::size_t> type_draws = formula_and_timing;
    type_draws.insert(type_draws.end(), {1, 0});
    const Requirement type_swapped = crossover_requirements(
        first_parent, second_parent, make_source(type_draws, 0));
    expect(type_swapped.m_condition_type == ConditionType::Trigger,
           "crossover: the condition type should come from the second parent "
           "when its coin says so");
    expect(type_swapped.m_scope == first_parent.m_scope,
           "crossover: the scope coin is independent of the condition type's");

    std::vector<std::size_t> scope_draws = formula_and_timing;
    scope_draws.insert(scope_draws.end(), {0, 1});
    const Requirement scope_swapped = crossover_requirements(
        first_parent, second_parent, make_source(scope_draws, 0));
    expect(scope_swapped.m_condition_type == ConditionType::Continual,
           "crossover: the condition type should stay with the first parent "
           "when its coin says so");
    expect(scope_swapped.m_scope == in_mode,
           "crossover: the scope, mode included, should come from the second "
           "parent when its coin says so");
    // m_ltl is derived, so it has to be rebuilt after both fields move or the
    // offspring is scored and synthesised as a requirement it no longer is.
    expect(scope_swapped.m_ltl == requirement_to_ltl(scope_swapped),
           "crossover: the offspring's LTL must be lowered from its own "
           "fields");
}

TEST(test_formula_crossover_can_combine_atoms) {
    const Requirement first_parent{Formula("P"), Formula("Q"),
                                   timing::immediately()};
    const Requirement second_parent{Formula("R"), Formula("S"),
                                    timing::next_timepoint()};
    const Requirement offspring = crossover_requirements(
        first_parent, second_parent, make_source({0, 1, 1, 0}, 0));
    expect(offspring.m_condition.to_string() == "(P) & (R)",
           "crossover: condition crossover should be able to combine atoms");
}

TEST(test_crossover_keeps_non_weakenable_from_first_parent) {
    // guarantees[0] is locked in both parents, guarantees[1] is weakenable, so
    // the locked slot is the target of no merge and the source of no donor:
    // the only eligible pair is (first[1], second[1]).
    const Requirement first_locked(Formula("a"), Formula("x"),
                                   timing::immediately(),
                                   ConditionType::Continual, false);
    const Requirement first_weak(Formula("P"), Formula("Q"),
                                 timing::immediately());
    const Requirement second_locked(Formula("b"), Formula("y"),
                                    timing::immediately(),
                                    ConditionType::Continual, false);
    const Requirement second_weak(Formula("R"), Formula("S"),
                                  timing::immediately());
    const Specification first_parent({}, {first_locked, first_weak}, {}, {});
    const Specification second_parent({}, {second_locked, second_weak}, {}, {});
    const Specification offspring = crossover_specifications(
        first_parent, second_parent, make_source({0, 0, 0, 1, 1, 0}, 0));
    expect(offspring.m_guarantees.size() == 2,
           "crossover: guarantee count should be preserved");
    expect(offspring.m_guarantees[0] == first_locked,
           "crossover: a non-weakenable requirement must be taken verbatim "
           "from the first parent");
    expect(offspring.m_guarantees[1].m_condition.to_string() == "(P) & (R)",
           "crossover: the weakenable requirement should still cross over");
}

// The donor is drawn from anywhere in the second parent's list, so a
// subformula of guarantee 1 can graft into guarantee 0. Under the index-for-
// index crossover this replaced, slot 0 could only ever see the second
// parent's slot 0.
TEST(test_crossover_donor_comes_from_any_slot) {
    const Requirement first_a{Formula("P"), Formula("Q"),
                              timing::immediately()};
    const Requirement first_b{Formula("T"), Formula("U"),
                              timing::immediately()};
    const Requirement second_a{Formula("R"), Formula("S"),
                               timing::immediately()};
    const Requirement second_b{Formula("V"), Formula("W"),
                               timing::immediately()};
    const Specification first_parent({}, {first_a, first_b}, {}, {});
    const Specification second_parent({}, {second_a, second_b}, {}, {});

    bool saw_cross_slot = false;
    std::size_t seed = 0;
    for (; seed < 60; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const Specification offspring =
            crossover_specifications(first_parent, second_parent, rng);
        expect(offspring.m_guarantees.size() == 2,
               "crossover: guarantee count should be preserved");
        std::size_t changed = 0;
        for (std::size_t i = 0; i < offspring.m_guarantees.size(); ++i) {
            if (!(offspring.m_guarantees[i] == first_parent.m_guarantees[i])) {
                ++changed;
            }
        }
        expect(changed <= 1,
               "crossover: at most one requirement per side "
               "merges");
        // V is the second parent's slot 1; finding it in slot 0 needs a
        // cross-slot donor.
        if (offspring.m_guarantees[0].m_condition.to_string().find('V') !=
            std::string::npos) {
            saw_cross_slot = true;
        }
    }
    expect(saw_cross_slot,
           "crossover: a donor from a different slot reaches the target slot");
}

// The offspring keeps the first parent's shape whatever the second parent's
// is, so unequal list lengths no longer stop two individuals breeding.
TEST(test_crossover_accepts_unequal_lengths) {
    const Requirement first_a{Formula("P"), Formula("Q"),
                              timing::immediately()};
    const Requirement second_a{Formula("R"), Formula("S"),
                               timing::immediately()};
    const Requirement second_b{Formula("V"), Formula("W"),
                               timing::immediately()};
    const Specification first_parent({}, {first_a}, {}, {});
    const Specification second_parent({}, {second_a, second_b}, {}, {});

    bool saw_change = false;
    for (std::size_t seed = 0; seed < 20; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const Specification offspring =
            crossover_specifications(first_parent, second_parent, rng);
        expect(offspring.m_guarantees.size() == 1,
               "crossover: the offspring keeps the first parent's shape");
        saw_change = saw_change || !(offspring == first_parent);
    }
    expect(saw_change,
           "crossover: parents of different guarantee counts still breed");
}

}  // namespace
