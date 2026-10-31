#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.hpp"
#include "fitness/function.hpp"
#include "fitness/status.hpp"
#include "fixtures.hpp"
#include "genetic/order_crossover.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "runner/spot.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "order_crossover";

Requirement req(const std::string& condition, const std::string& response,
                Timing timing) {
    return Requirement(Formula(condition), Formula(response), std::move(timing),
                       ConditionType::Continual);
}

Requirement removed(Requirement requirement) {
    requirement.m_removed = true;
    return requirement;
}

Specification spec(std::vector<Requirement> assumptions,
                   std::vector<Requirement> guarantees) {
    return Specification(std::move(assumptions), std::move(guarantees),
                         {"a", "b", "c"}, {"x", "y"});
}

Requirement assumption0() { return req("a", "b", timing::eventually()); }
Requirement guarantee0() { return req("a", "x", timing::within_ticks(2)); }
Requirement guarantee1() { return req("b", "y", timing::immediately()); }

Specification original() {
    return spec({assumption0()}, {guarantee0(), guarantee1()});
}

// Each parent fixes a different slot of the original: P loosens guarantee 0,
// Q removes guarantee 1.
Specification parent_p() {
    return spec({assumption0()},
                {req("a", "x", timing::within_ticks(3)), guarantee1()});
}
Specification parent_q() {
    return spec({assumption0()}, {guarantee0(), removed(guarantee1())});
}

Scored<Specification> scored(Specification specification, double status) {
    Scored<Specification> entry;
    entry.specification = std::move(specification);
    entry.objectives = {0.5, 0.5, status};
    return entry;
}

// --- choosing the operator ---------------------------------------------------

TEST(test_kind_follows_the_verdict_table) {
    using V = ParentVerdict;
    using K = OrderCrossoverKind;
    expect(order_crossover_kind(V::Realizable, V::Realizable) == K::Join,
           "two realizable parents join");
    expect(order_crossover_kind(V::Unrealizable, V::Unrealizable) == K::Meet,
           "two unrealizable parents meet");
    expect(
        order_crossover_kind(V::Realizable, V::Unrealizable) == K::Relink &&
            order_crossover_kind(V::Unrealizable, V::Realizable) == K::Relink,
        "a mixed pair relinks, in either order");
    for (const V other : {V::Realizable, V::Unrealizable, V::Unknown}) {
        expect(!order_crossover_kind(V::Unknown, other).has_value() &&
                   !order_crossover_kind(other, V::Unknown).has_value(),
               "an unknown verdict takes no order crossover");
    }
}

TEST(test_status_objective_index_matches_the_factory) {
    const Specification subject = spec({}, {guarantee0()});
    for (const double syntactic : {0.0, 0.1}) {
        for (const double semantic : {0.0, 0.2}) {
            for (const double status : {0.0, 0.7}) {
                Config cfg;
                cfg.fitness_weight_syntactic = syntactic;
                cfg.fitness_weight_semantic = semantic;
                cfg.fitness_weight_status = status;
                if (syntactic + semantic + status == 0.0) {
                    continue;
                }
                const AggregateWeightedFitnessFunction fitness =
                    get_fitness_function(subject, cfg);
                std::optional<std::size_t> expected;
                std::size_t position = 0;
                for (const auto& objective : fitness) {
                    if (objective.name == "status") {
                        expected = position;
                    }
                    ++position;
                }
                expect(status_objective_index(cfg) == expected,
                       "status_objective_index: disagrees with the factory");
            }
        }
    }
}

// G(a -> x) and G(a -> !x) are each realizable and together are not.
Specification conflicting(std::vector<Requirement> assumptions,
                          std::vector<Requirement> extra) {
    std::vector<Requirement> guarantees = {
        req("a", "x", timing::immediately()),
        req("a", "!x", timing::immediately())};
    guarantees.insert(guarantees.end(), extra.begin(), extra.end());
    return spec(std::move(assumptions), std::move(guarantees));
}

TEST(test_proved_unrealizable_reads_decided_queries) {
    RealizabilityChecker checker;
    const Specification core = conflicting({}, {});
    const Specification wider = conflicting({}, {guarantee1()});
    const Specification assumed = conflicting({assumption0()}, {guarantee1()});
    expect(!checker.proved_unrealizable(wider),
           "proved_unrealizable: nothing is proved before any query");
    expect(checker.check_realizability(core) == false,
           "the conflicting pair is unrealizable");
    expect(checker.proved_unrealizable(core),
           "proved_unrealizable: the query itself");
    expect(checker.proved_unrealizable(wider),
           "proved_unrealizable: more guarantees stay unrealizable");
    expect(!checker.proved_unrealizable(assumed),
           "proved_unrealizable: an extra assumption may make it realizable");
}

TEST(test_proved_unrealizable_ignores_speculative_queries) {
    RealizabilityChecker checker;
    const Specification core = conflicting({}, {});
    {
        const SpeculativeQueryScope speculative;
        expect(checker.check_realizability(core) == false,
               "the conflicting pair is unrealizable");
    }
    expect(!checker.proved_unrealizable(core),
           "proved_unrealizable: a speculative verdict proves nothing");
    // The walk asking the same query itself is a cache hit, and counts.
    expect(checker.check_realizability(core) == false,
           "the cached verdict is unchanged");
    expect(checker.proved_unrealizable(core),
           "proved_unrealizable: a cache hit by a non-speculative caller");
}

TEST(test_parent_verdict_needs_a_proof) {
    RealizabilityChecker checker;
    const std::optional<std::size_t> status_index = 2;
    const Scored<Specification> realizable = scored(original(), 1.0);
    expect(parent_verdict(realizable, status_index, checker) ==
               ParentVerdict::Realizable,
           "parent_verdict: a full status is realizable");
    // A status below the top is not a proof: an ill-separated candidate scores
    // there too.
    const Scored<Specification> low = scored(conflicting({}, {}), 0.5);
    expect(parent_verdict(low, status_index, checker) == ParentVerdict::Unknown,
           "parent_verdict: a low status alone is unknown");
    expect(checker.check_realizability(low.specification) == false,
           "the conflicting pair is unrealizable");
    expect(parent_verdict(low, status_index, checker) ==
               ParentVerdict::Unrealizable,
           "parent_verdict: a decided query proves it unrealizable");
    expect(parent_verdict(realizable, std::nullopt, checker) ==
               ParentVerdict::Unknown,
           "parent_verdict: no status objective, no realizable verdict");
}

// --- the operators -----------------------------------------------------------

TEST(test_join_implies_both_parents) {
    const RandomSource source = make_source({}, 0);
    const OrderCrossoverOutcome outcome =
        join_crossover(parent_p(), parent_q(), 1, source);
    if (!outcome.m_child.has_value()) {
        fail("join_crossover: no child");
    }
    expect(outcome.m_proved, "join_crossover: every slot is proved");
    const Specification& child = *outcome.m_child;
    expect(specification_implies_by_rule(child, parent_p()) &&
               specification_implies_by_rule(child, parent_q()),
           "join_crossover: the child implies both parents");
    // Q's slot 0 and P's slot 1 are the stronger of each pair.
    expect(child == original(),
           "join_crossover: the join of the two fixes is the original, got " +
               child.to_string());
}

TEST(test_meet_is_implied_by_both_parents) {
    const RandomSource source = make_source({}, 0);
    const OrderCrossoverOutcome outcome =
        meet_crossover(parent_p(), parent_q(), 1, source);
    if (!outcome.m_child.has_value()) {
        fail("meet_crossover: no child");
    }
    expect(outcome.m_proved, "meet_crossover: every slot is proved");
    const Specification& child = *outcome.m_child;
    expect(specification_implies_by_rule(parent_p(), child) &&
               specification_implies_by_rule(parent_q(), child),
           "meet_crossover: both parents imply the child");
    const Specification both_fixes =
        spec({assumption0()},
             {req("a", "x", timing::within_ticks(3)), removed(guarantee1())});
    expect(child == both_fixes,
           "meet_crossover: the child carries both fixes, got " +
               child.to_string());
}

TEST(test_unproved_slot_takes_a_parent_on_a_coin) {
    const Specification lhs = spec(
        {assumption0()}, {req("a", "x", timing::after_ticks(2)), guarantee1()});
    const Specification rhs = spec(
        {assumption0()}, {req("a", "y", timing::after_ticks(2)), guarantee1()});
    for (const std::size_t coin : {0, 1}) {
        const RandomSource source = make_source({coin}, 0);
        const OrderCrossoverOutcome outcome =
            join_crossover(lhs, rhs, 1, source);
        if (!outcome.m_child.has_value()) {
            fail("join_crossover: no partial child");
        }
        expect(!outcome.m_proved, "join_crossover: a partial join is unproved");
        const Specification& donor = coin == 1 ? rhs : lhs;
        expect(outcome.m_child->m_guarantees[0] == donor.m_guarantees[0],
               "join_crossover: the coin picks the donor");
    }
}

TEST(test_relink_strengthens_the_realizable_parent) {
    // R carries both fixes, U neither: U is stronger in both slots.
    const Specification realizable =
        spec({assumption0()},
             {req("a", "x", timing::within_ticks(3)), removed(guarantee1())});
    const Specification unrealizable = original();
    const std::optional<Specification> upper =
        assemble(specification_join(realizable, unrealizable, 1), realizable);
    if (!upper.has_value()) {
        fail("the join of the pair is complete");
    }
    // Every coin pattern over the two sites, the last forcing site 1.
    const std::vector<std::vector<std::size_t>> scripts = {
        {1, 1}, {1, 0}, {0, 1}, {0, 0, 1}};
    for (const std::vector<std::size_t>& script : scripts) {
        const RandomSource source = make_source(script, 0);
        const OrderCrossoverOutcome outcome =
            relink_crossover(realizable, unrealizable, 1, source);
        if (!outcome.m_child.has_value()) {
            fail("relink_crossover: no child");
        }
        const Specification& child = *outcome.m_child;
        expect(outcome.m_proved, "relink_crossover: the child is proved");
        expect(classify_move(realizable, child) == MoveOrder::Stronger,
               "relink_crossover: the child strengthens the realizable "
               "parent, got " +
                   child.to_string());
        expect(specification_implies_by_rule(*upper, child),
               "relink_crossover: the join of the pair implies the child");
    }
}

TEST(test_relink_without_a_site_gives_nothing) {
    const RandomSource source = make_source({}, 0);
    expect(!relink_crossover(original(), parent_p(), 1, source)
                .m_child.has_value(),
           "relink_crossover: a weaker partner offers no site");
}

// --- the arm in breed_offspring ----------------------------------------------

std::vector<Scored<Specification>> realizable_population() {
    return {scored(parent_p(), 1.0), scored(parent_q(), 1.0)};
}

TEST(test_breed_joins_realizable_parents) {
    Config cfg;
    cfg.crossover_rate = 1.0;
    cfg.mutation_rate = 0.0;
    cfg.p_order_crossover = 1.0;
    const std::size_t attempted_before =
        OrderCrossoverStats::n_attempted[static_cast<std::size_t>(
            OrderCrossoverKind::Join)];
    // The partner draw picks parent 1 for slot 0 and parent 0 for slot 1.
    const RandomSource source = make_source({1, 0}, 0);
    const std::vector<Specification> offspring =
        generation_detail::breed_offspring(cfg, realizable_population(), 2, 2,
                                           source, nullptr);
    expect(offspring.size() == 2, "breed_offspring: two offspring");
    for (const Specification& child : offspring) {
        expect(specification_implies_by_rule(child, parent_p()) &&
                   specification_implies_by_rule(child, parent_q()),
               "breed_offspring: a join implies both parents, got " +
                   child.to_string());
    }
    expect(OrderCrossoverStats::n_attempted[static_cast<std::size_t>(
               OrderCrossoverKind::Join)] == attempted_before + 2,
           "breed_offspring: both joins are counted");
}

}  // namespace
