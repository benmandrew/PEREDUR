#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "climb_replay_slots.hpp"
#include "config.hpp"
#include "fixtures.hpp"
#include "genetic/climb.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/output_gate.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "climb";

Specification spec(std::vector<Requirement> assumptions,
                   std::vector<Requirement> guarantees) {
    return Specification(std::move(assumptions), std::move(guarantees), {"req"},
                         {"grant"});
}

// A request is granted at once, and grant never holds: unrealizable, since
// the environment may request.
Requirement grant_now() {
    return make_req("req", "grant", timing::immediately(),
                    ConditionType::Trigger);
}
Requirement never_grant() {
    return make_req("true", "!grant", timing::always());
}
Requirement never_request() {
    return make_req("true", "!req", timing::always());
}

Specification conflict() { return spec({}, {grant_now(), never_grant()}); }

// The tick count of guarantee @p slot, which these tests keep `within n`.
std::size_t within_count(const Specification& candidate, std::size_t slot) {
    const Timing& timing = candidate.m_guarantees[slot].m_timing;
    expect(std::holds_alternative<timing::WithinTicks>(timing),
           "climb: the guarantee keeps a within timing, got " +
               candidate.m_guarantees[slot].to_string());
    return std::get<timing::WithinTicks>(timing).m_ticks;
}

// --- bisection ---------------------------------------------------------------

// A predicate accepting `within n` for n at least @p threshold stands in for a
// realizability boundary along the chain. The bisection must stop where a walk
// one tick at a time stops.
TEST(test_bisection_matches_a_linear_walk) {
    const Specification original =
        spec({}, {make_req("req", "grant", timing::within_ticks(1))});
    constexpr std::size_t k_from = 9;
    const Specification start =
        spec({}, {make_req("req", "grant", timing::within_ticks(k_from))});
    for (std::size_t threshold = 1; threshold <= k_from; ++threshold) {
        const ClimbPredicate accepts =
            [threshold](const Specification& candidate) {
                const Timing& timing = candidate.m_guarantees[0].m_timing;
                const auto* within = std::get_if<timing::WithinTicks>(&timing);
                return within != nullptr && within->m_ticks >= threshold;
            };
        std::size_t linear = k_from;
        while (
            linear > 1 &&
            accepts(spec({}, {make_req("req", "grant",
                                       timing::within_ticks(linear - 1))}))) {
            --linear;
        }
        const std::vector<Step> steps = steps_towards(start, original);
        expect(steps.size() == 1, "climb: one timing step towards within 1");
        std::size_t n_asked = 0;
        const ClimbPredicate counted = [&](const Specification& candidate) {
            ++n_asked;
            return accepts(candidate);
        };
        const std::optional<Specification> reached =
            furthest_accepted(start, steps[0], counted);
        if (threshold == k_from) {
            expect(!reached.has_value(),
                   "climb: a chain refused at its first entry gives nothing");
            continue;
        }
        if (!reached.has_value()) {
            fail("climb: bisection reaches an entry");
        }
        expect(within_count(*reached, 0) == linear,
               "climb: bisection stops at within " + std::to_string(linear) +
                   ", got " + reached->m_guarantees[0].to_string());
        // The destination, then a binary search over the seven entries before
        // it: one more than ceil(log2(8)).
        expect(n_asked <= 4, "climb: bisection asks at most 4 queries, asked " +
                                 std::to_string(n_asked));
        const Specification climbed = climb(start, original, accepts);
        expect(within_count(climbed, 0) == linear,
               "climb: the climb ends where the walk does");
    }
}

// --- step orders -------------------------------------------------------------

TEST(test_orders_are_permutations_of_the_steps) {
    const Specification original = spec(
        {}, {make_req("req", "grant", timing::within_ticks(1)), never_grant()});
    const Specification start = spec(
        {never_request()}, {make_req("req", "grant", timing::within_ticks(4)),
                            removed(never_grant())});
    const std::vector<Step> slot = steps_towards(start, original);
    expect(slot.size() == 3,
           "climb: a timing step, a restore and a drop, got " +
               std::to_string(slot.size()));
    bool any_reordered = false;
    for (std::size_t permutation = 0; permutation < 8; ++permutation) {
        std::vector<Step> ordered = slot;
        order_steps(ordered, permutation);
        std::vector<Step> again = slot;
        order_steps(again, permutation);
        expect(ordered.size() == slot.size(),
               "climb: ordering keeps every step");
        bool same_as_slot = true;
        for (std::size_t i = 0; i < slot.size(); ++i) {
            std::size_t found = 0;
            for (const Step& other : ordered) {
                found += other.m_result == slot[i].m_result ? 1 : 0;
            }
            expect(found == 1, "climb: ordering keeps each step once");
            expect(ordered[i].m_result == again[i].m_result,
                   "climb: a permutation is the same on every call");
            same_as_slot =
                same_as_slot && ordered[i].m_result == slot[i].m_result;
        }
        expect(permutation != 0 || same_as_slot,
               "climb: permutation 0 is steps_towards' order");
        any_reordered = any_reordered || !same_as_slot;
    }
    expect(any_reordered, "climb: some permutation reorders the steps");
}

// Two removed guarantees, of which the predicate admits either but not both:
// each order restores the one it tries first, so the maximal elements differ
// by order, and only extra orders find the second.
TEST(test_extra_orders_reach_other_maximal_elements) {
    const Requirement first = make_req("true", "grant", timing::always());
    const Requirement second = never_grant();
    const Specification original = spec({}, {first, second});
    const Specification start = spec({}, {removed(first), removed(second)});
    const ClimbPredicate accepts = [](const Specification& candidate) {
        return count_live(candidate.m_guarantees) < 2;
    };
    const std::vector<Specification> one =
        climb_repairs({start}, original, accepts);
    expect(one.size() == 1, "climb: one order gives one result, got " +
                                std::to_string(one.size()));
    expect(one.size() == 1 && !one[0].m_guarantees[0].m_removed &&
               one[0].m_guarantees[1].m_removed,
           "climb: the slot order restores the first guarantee");
    const std::vector<Specification> many =
        climb_repairs({start}, original, accepts, 8);
    expect(many.size() == 2, "climb: eight orders reach both maxima, got " +
                                 std::to_string(many.size()));
    expect(!many.empty() && many[0] == one[0],
           "climb: the first order's result comes first");
    for (const Specification& climbed : many) {
        expect(count_live(climbed.m_guarantees) == 1,
               "climb: each result restores exactly one guarantee");
    }
}

// --- climb-replay's removed slots
// ---------------------------------------------

// A repair read back from JSON has lost its removed guarantees. Each live one
// goes back to the slot it shares most fields with, and the slots left over
// hold the original's requirement, removed.
TEST(test_replay_restores_removed_slots) {
    const Requirement first = make_req("req", "grant", timing::within_ticks(2));
    const Requirement second = never_grant();
    const Requirement third = make_req("true", "!req", timing::always());
    const std::vector<Requirement> original{first, second, third};
    // The third slot edited in its timing, the second removed.
    const Requirement edited =
        make_req("true", "!req", timing::within_ticks(4));
    const std::vector<Requirement> slots =
        climb_replay::restore_removed_slots({first, edited}, original);
    expect(slots.size() == 3, "replay: three slots");
    expect(slots[0] == first && !slots[0].m_removed,
           "replay: the first slot keeps its requirement");
    expect(slots[1].m_removed, "replay: the second slot is removed");
    expect(slots[2] == edited, "replay: the edited requirement takes its slot");

    // Only the second left: it shares every field with slot 1 alone.
    const std::vector<Requirement> only =
        climb_replay::restore_removed_slots({second}, original);
    expect(only[0].m_removed && !only[1].m_removed && only[2].m_removed,
           "replay: a lone survivor takes its own slot");
    expect(climb_replay::restore_removed_slots(original, original) == original,
           "replay: a full set is unchanged");
}

// --- against ltlsynt ---------------------------------------------------------

// The repair S assumes requests never come and grants within 3 ticks. Dropping
// the assumption is unrealizable against never_grant, so the climb tightens
// the timing back to the original's and keeps the assumption.
TEST(test_climbed_repair_is_maximal_over_its_steps) {
    const Specification original = conflict();
    const Specification start = spec(
        {never_request()}, {make_req("req", "grant", timing::within_ticks(3),
                                     ConditionType::Trigger),
                            never_grant()});
    Config cfg;
    const ClimbPredicate gate = climb_gate(cfg, nullptr);
    expect(!passes_output_gate(original, cfg),
           "climb: the original fails the gate");
    expect(passes_output_gate(start, cfg), "climb: the repair passes the gate");

    const Specification climbed = climb(start, original, gate);
    expect(passes_output_gate(climbed, cfg),
           "climb: the climbed repair passes the gate");
    expect(specification_implies_by_rule(climbed, start),
           "climb: the climbed repair implies its start by rule");
    expect(!(climbed == start), "climb: the repair moved");
    expect(climbed.m_guarantees[0] == grant_now(),
           "climb: the timing returned to the original's, got " +
               climbed.m_guarantees[0].to_string());
    expect(climbed.m_assumptions.size() == 1,
           "climb: the assumption the repair needs stays");
    for (const Step& step : steps_towards(climbed, original)) {
        expect(!passes_output_gate(step.m_result, cfg),
               "climb: no single step from the result passes the gate");
    }
}

// With the guarantee that makes the conflict removed, restoring it is the step
// the gate refuses, and the timing still climbs.
TEST(test_climb_restores_only_what_stays_realizable) {
    const Specification original = conflict();
    const Specification start =
        spec({}, {make_req("req", "grant", timing::within_ticks(5),
                           ConditionType::Trigger),
                  removed(never_grant())});
    Config cfg;
    const std::vector<Specification> climbed =
        climb_repairs({start, start}, original, climb_gate(cfg, nullptr));
    expect(climbed.size() == 1,
           "climb: a repaired twice gives one result, got " +
               std::to_string(climbed.size()));
    expect(climbed[0].m_guarantees[0] == grant_now(),
           "climb: the timing climbed to the original's");
    expect(climbed[0].m_guarantees[1].m_removed,
           "climb: the conflicting guarantee stays removed");
    expect(passes_output_gate(climbed[0], cfg),
           "climb: the result passes the gate");
}

// Every extra order gives a maximal result, and the results are distinct.
TEST(test_extra_orders_give_distinct_results) {
    const Specification original = conflict();
    const Specification start = spec(
        {never_request()}, {make_req("req", "grant", timing::within_ticks(3),
                                     ConditionType::Trigger),
                            removed(never_grant())});
    Config cfg;
    const ClimbPredicate gate = climb_gate(cfg, nullptr);
    const std::vector<Specification> climbed =
        climb_repairs({start}, original, gate, 4);
    expect(!climbed.empty() && climbed.size() <= 4,
           "climb: between one and four results, got " +
               std::to_string(climbed.size()));
    for (std::size_t i = 0; i < climbed.size(); ++i) {
        expect(passes_output_gate(climbed[i], cfg),
               "climb: each result passes the gate");
        expect(specification_implies_by_rule(climbed[i], start),
               "climb: each result implies the start by rule");
        for (std::size_t j = 0; j < i; ++j) {
            expect(!(climbed[i] == climbed[j]), "climb: results are distinct");
        }
    }
}

}  // namespace
