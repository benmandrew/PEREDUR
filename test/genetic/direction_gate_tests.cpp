// The status-gated mutation direction (Config::direction_gated_mutation): an
// unrealizable parent never yields a child proved stronger, and a realizable
// one never a child proved weaker.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "fixtures.hpp"
#include "genetic/mutation.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/random_source.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "direction_gate";

constexpr std::size_t k_seeds = 400;

std::vector<std::string> inputs() { return {"a", "b"}; }
std::vector<std::string> outputs() { return {"x", "y"}; }

// The original: one fairness assumption and three guarantees.
Specification original_spec() {
    return Specification(
        {make_req("true", "a", timing::eventually())},
        {make_req("a", "x"), make_req("b", "y", timing::within_ticks(2)),
         make_req("a", "y", timing::eventually())},
        inputs(), outputs());
}

// A descendant of original_spec() weakened three ways: guarantee 0
// tombstoned, guarantee 1's timing relaxed and an assumption appended.
Specification weakened_spec() {
    Requirement removed = make_req("a", "x");
    removed.m_removed = true;
    return Specification({make_req("true", "a", timing::eventually()),
                          make_req("true", "b", timing::eventually())},
                         {removed, make_req("b", "y", timing::within_ticks(4)),
                          make_req("a", "y", timing::eventually())},
                         inputs(), outputs());
}

// Every rewrite arm on, the structural actions frequent, and the monotone
// arm on so that the directed moves are exercised beside the free ones.
Config gated_config() {
    Config cfg;
    cfg.direction_gated_mutation = true;
    cfg.p_response = 0.5;
    cfg.p_trigger = 0.5;
    cfg.p_timing = 0.3;
    cfg.p_condition_type = 0.15;
    cfg.p_scope = 0.15;
    cfg.p_monotone = 0.25;
    cfg.p_add_assumption = 0.2;
    cfg.p_remove_guarantee = 0.2;
    return cfg;
}

ParentContext context_for(bool realizable, const Specification* original) {
    ParentContext context;
    context.status = realizable ? 1.0 : 1.0 / 3.0;
    context.realizable = realizable;
    context.original = original;
    return context;
}

// How often each order turned up over k_seeds mutations of @p parent.
struct OrderTally {
    std::size_t n_stronger = 0;
    std::size_t n_weaker = 0;
    std::size_t n_incomparable = 0;
};

OrderTally tally(const Specification& parent, const Config& cfg,
                 const ParentContext* context) {
    OrderTally counts;
    for (std::size_t seed = 0; seed < k_seeds; ++seed) {
        const RandomSource source = make_random_source_from_seed(seed);
        const Specification child =
            mutate_specification(parent, source, cfg, context);
        switch (classify_move(parent, child)) {
            case MoveOrder::Stronger:
                counts.n_stronger++;
                break;
            case MoveOrder::Weaker:
                counts.n_weaker++;
                break;
            case MoveOrder::Incomparable:
                counts.n_incomparable++;
                break;
            case MoveOrder::Equal:
                break;
        }
    }
    return counts;
}

TEST(test_unrealizable_parent_never_yields_a_proved_strengthening) {
    const Specification original = original_spec();
    const ParentContext context = context_for(false, &original);
    Config ungated = gated_config();
    ungated.direction_gated_mutation = false;
    expect(tally(original, ungated, &context).n_stronger > 0,
           "without the gate some free rewrite should strengthen, or the "
           "check below passes over nothing");
    const std::size_t before = DirectionGateStats::n_forbidden.load();
    expect(tally(original, gated_config(), &context).n_stronger == 0,
           "an unrealizable parent must never yield a child proved stronger");
    expect(DirectionGateStats::n_forbidden.load() > before,
           "each refused strengthening should be counted");
}

TEST(test_realizable_parent_never_yields_a_proved_weakening) {
    const Specification original = original_spec();
    const ParentContext context = context_for(true, &original);
    for (const Specification& parent : {weakened_spec(), original}) {
        const std::size_t steps_before = DirectionGateStats::n_steps.load();
        const OrderTally counts = tally(parent, gated_config(), &context);
        expect(counts.n_weaker == 0,
               "a realizable parent must never yield a child proved weaker");
        if (!(parent == original)) {
            expect(counts.n_stronger > 0 &&
                       DirectionGateStats::n_steps.load() > steps_before,
                   "a realizable parent away from the original should step "
                   "back towards it");
        }
    }
}

TEST(test_no_sideways_share_keeps_only_strengthenings) {
    const Specification original = original_spec();
    const ParentContext context = context_for(true, &original);
    Config cfg = gated_config();
    cfg.p_sideways = 0.0;
    const OrderTally counts = tally(weakened_spec(), cfg, &context);
    expect(counts.n_weaker == 0 && counts.n_incomparable == 0,
           "with no sideways share a realizable parent's children should all "
           "be stronger or equal");
}

// A realizable parent's strengthenings are the steps back towards the
// original. On a parent weaker than the original in every slot, a step never
// passes it, so no child is proved stronger than the original.
TEST(test_realizable_steps_stay_below_the_original) {
    const Specification original = original_spec();
    const ParentContext context = context_for(true, &original);
    Config cfg = gated_config();
    cfg.p_response = 0.0;
    cfg.p_trigger = 0.0;
    cfg.p_stop = 0.0;
    for (std::size_t seed = 0; seed < k_seeds; ++seed) {
        const RandomSource source = make_random_source_from_seed(seed);
        const Specification child =
            mutate_specification(weakened_spec(), source, cfg, &context);
        const MoveOrder past = classify_move(original, child);
        expect(past != MoveOrder::Stronger,
               "a step must not move past the original, seed " +
                   std::to_string(seed));
    }
}

TEST(test_gate_leaves_crossover_offspring_alone) {
    const Specification original = original_spec();
    ParentContext crossed = context_for(false, &original);
    crossed.crossed = true;
    Config ungated = gated_config();
    ungated.direction_gated_mutation = false;
    for (std::size_t seed = 0; seed < k_seeds; ++seed) {
        const RandomSource gated_source = make_random_source_from_seed(seed);
        const RandomSource plain_source = make_random_source_from_seed(seed);
        const Specification gated = mutate_specification(
            weakened_spec(), gated_source, gated_config(), &crossed);
        const Specification plain = mutate_specification(
            weakened_spec(), plain_source, ungated, nullptr);
        expect(
            gated == plain,
            "a crossover offspring should mutate as without the gate, seed " +
                std::to_string(seed));
    }
}

}  // namespace
