// The status-gated mutation direction (Config::direction_gated_mutation): an
// unrealizable parent never yields a child proved stronger, and a realizable
// one never a child proved weaker.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "fitness/status.hpp"
#include "fixtures.hpp"
#include "genetic/mutation.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
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

// The step flag is set exactly when a step is counted, and settle counts a
// held offspring only where the scored generation rates it realizable.
TEST(test_step_offspring_are_settled_by_their_score) {
    const Specification original = original_spec();
    const ParentContext context = context_for(true, &original);
    const Config cfg = gated_config();
    std::vector<Specification> stepped_children;
    for (std::size_t seed = 0; seed < k_seeds; ++seed) {
        const RandomSource source = make_random_source_from_seed(seed);
        const std::size_t steps_before = DirectionGateStats::n_steps.load();
        bool stepped = true;
        const Specification child = mutate_specification(
            weakened_spec(), source, cfg, &context, &stepped);
        expect(
            stepped == (DirectionGateStats::n_steps.load() == steps_before + 1),
            "the step flag should match the step count, seed " +
                std::to_string(seed));
        if (stepped) {
            stepped_children.push_back(child);
        }
        DirectionGateStats::note_offspring(stepped, child);
    }
    expect(!stepped_children.empty(), "some mutation should take a step");

    // Only the first stepped child scores realizable; status sits at index 0.
    std::vector<Scored<Specification>> scored;
    Scored<Specification> realizable;
    realizable.specification = stepped_children.front();
    realizable.objectives = {k_status_realizable};
    scored.push_back(realizable);
    std::size_t n_same = 0;
    for (const Specification& child : stepped_children) {
        n_same += child == stepped_children.front() ? 1 : 0;
    }
    const std::size_t before = DirectionGateStats::n_steps_realizable.load();
    DirectionGateStats::settle(scored, 0);
    expect(DirectionGateStats::n_steps_realizable.load() == before + n_same,
           "settle should count each held offspring the generation scored "
           "realizable");
    DirectionGateStats::settle(scored, 0);
    expect(DirectionGateStats::n_steps_realizable.load() == before + n_same,
           "settle should forget what it has counted");
}

// Only the timing arm, always firing, so the one field a rewrite moves is
// the timing and each mode's moves are easy to read off.
Config timing_only(GateRealizable mode) {
    Config cfg = gated_config();
    cfg.p_response = 0.0;
    cfg.p_trigger = 0.0;
    cfg.p_stop = 0.0;
    cfg.p_condition_type = 0.0;
    cfg.p_scope = 0.0;
    cfg.p_timing = 1.0;
    cfg.gate_realizable = mode;
    return cfg;
}

// Under Free a realizable parent at the original strengthens a slot past it,
// which Towards never does, since its only strengthenings are steps back.
TEST(test_free_gate_moves_a_slot_above_the_original) {
    const Specification original = original_spec();
    const ParentContext context = context_for(true, &original);
    std::size_t above_free = 0;
    std::size_t above_towards = 0;
    for (std::size_t seed = 0; seed < k_seeds; ++seed) {
        const Specification free_child =
            mutate_specification(original, make_random_source_from_seed(seed),
                                 timing_only(GateRealizable::Free), &context);
        const Specification towards_child = mutate_specification(
            original, make_random_source_from_seed(seed),
            timing_only(GateRealizable::Towards), &context);
        expect(classify_move(original, free_child) != MoveOrder::Weaker,
               "free gate: a realizable parent must not yield a child proved "
               "weaker, seed " +
                   std::to_string(seed));
        if (classify_move(original, free_child) == MoveOrder::Stronger) {
            above_free++;
        }
        if (classify_move(original, towards_child) == MoveOrder::Stronger) {
            above_towards++;
        }
    }
    expect(above_free > 0,
           "free gate: some child of the original should be proved stronger "
           "than it");
    expect(above_towards == 0,
           "towards gate: no child of the original should pass it");
}

// Under Exchange a realizable parent that differs from the original in one
// guarantee steps that guarantee back and weakens a slot still equal to the
// original's in the same mutation. Towards never edits an untouched slot here,
// since its rewrites run with the timing arm off.
TEST(test_exchange_gate_steps_back_and_weakens_an_untouched_slot) {
    const Specification original = original_spec();
    const Specification parent(
        {make_req("true", "a", timing::eventually())},
        {make_req("a", "x"), make_req("b", "y", timing::within_ticks(4)),
         make_req("a", "y", timing::eventually())},
        inputs(), outputs());
    const ParentContext context = context_for(true, &original);
    const auto touched_elsewhere = [&original](const Specification& child) {
        return !(child.m_assumptions[0] == original.m_assumptions[0]) ||
               !(child.m_guarantees[0] == original.m_guarantees[0]) ||
               !(child.m_guarantees[2] == original.m_guarantees[2]);
    };
    std::size_t exchanged = 0;
    for (std::size_t seed = 0; seed < k_seeds; ++seed) {
        const Specification child = mutate_specification(
            parent, make_random_source_from_seed(seed),
            timing_only(GateRealizable::Exchange), &context);
        expect(child.m_guarantees.size() == 3 &&
                   child.m_guarantees[1] == original.m_guarantees[1],
               "exchange gate: the differing guarantee should step back to "
               "the original, seed " +
                   std::to_string(seed));
        if (touched_elsewhere(child)) {
            exchanged++;
        }
        const Specification towards_child = mutate_specification(
            parent, make_random_source_from_seed(seed),
            timing_only(GateRealizable::Towards), &context);
        expect(!touched_elsewhere(towards_child),
               "towards gate: an untouched slot should stay as the original "
               "has it, seed " +
                   std::to_string(seed));
    }
    expect(exchanged > 0,
           "exchange gate: some child should also weaken a slot equal to the "
           "original's");
}

}  // namespace
