#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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

}  // namespace
