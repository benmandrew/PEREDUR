// Distance-dependent step size: Config::step_scaling scales the share of large
// moves by the parent's status, and Config::bisect_steps moves a realizable
// parent's timing step to the midpoint of its chain.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "fixtures.hpp"
#include "genetic/mutation.hpp"
#include "genetic/random_source.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "step_size";

constexpr std::size_t k_seeds = 200;

std::vector<std::string> inputs() { return {"a", "b"}; }
std::vector<std::string> outputs() { return {"x", "y"}; }

// Only the timing arm fires, at every mutation, under full scaling.
Config timing_only_config() {
    Config cfg;
    cfg.p_response = 0.0;
    cfg.p_trigger = 0.0;
    cfg.p_timing = 1.0;
    cfg.p_stop = 0.0;
    cfg.p_condition_type = 0.0;
    cfg.p_scope = 0.0;
    cfg.p_monotone = 0.0;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 0.0;
    cfg.step_scaling = 1.0;
    return cfg;
}

// A parent at status 0 (as far from realizable as the walk grades) or at 1.
ParentContext context_at(double status) {
    ParentContext context;
    context.status = status;
    context.realizable = status >= 1.0;
    return context;
}

// The timings guarantee @p slot took over k_seeds mutations of @p spec.
std::vector<Timing> timings_taken(const Specification& spec, std::size_t slot,
                                  const Config& cfg,
                                  const ParentContext& context) {
    std::vector<Timing> taken;
    for (std::size_t seed = 0; seed < k_seeds; ++seed) {
        const RandomSource source = make_random_source_from_seed(seed);
        const Specification child =
            mutate_specification(spec, source, cfg, &context);
        if (!(child.m_guarantees[slot].m_timing ==
              spec.m_guarantees[slot].m_timing)) {
            taken.push_back(child.m_guarantees[slot].m_timing);
        }
    }
    return taken;
}

bool all_equal_to(const std::vector<Timing>& taken,
                  const std::vector<Timing>& allowed) {
    for (const Timing& timing : taken) {
        bool found = false;
        for (const Timing& candidate : allowed) {
            found = found || timing == candidate;
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

// At status 0 and full scaling every tick-count weakening jumps; at status 1
// every one takes its single edge.
TEST(test_tick_steps_follow_the_distance) {
    const Specification spec({},
                             {make_req("a", "x", timing::for_ticks(8)),
                              make_req("b", "y", timing::within_ticks(4))},
                             inputs(), outputs());
    const Config cfg = timing_only_config();

    const std::vector<Timing> far_for =
        timings_taken(spec, 0, cfg, context_at(0.0));
    const std::vector<Timing> far_within =
        timings_taken(spec, 1, cfg, context_at(0.0));
    expect(!far_for.empty() && all_equal_to(far_for, {timing::for_ticks(4)}),
           "far from realizable, `for 8` should always halve");
    expect(!far_within.empty() &&
               all_equal_to(far_within,
                            {timing::within_ticks(8), timing::eventually()}),
           "far from realizable, `within 4` should always jump");

    const std::vector<Timing> near_for =
        timings_taken(spec, 0, cfg, context_at(1.0));
    const std::vector<Timing> near_within =
        timings_taken(spec, 1, cfg, context_at(1.0));
    expect(!near_for.empty() && all_equal_to(near_for, {timing::for_ticks(7)}),
           "near realizable, `for 8` should always step one tick");
    expect(!near_within.empty() &&
               all_equal_to(near_within, {timing::within_ticks(5)}),
           "near realizable, `within 4` should always step one tick");
}

// Removal's rate doubles at status 0 under full scaling and falls to 0 at
// status 1.
TEST(test_removal_rate_follows_the_distance) {
    const Specification spec({},
                             {make_req("a", "x"), make_req("b", "y"),
                              make_req("a", "y", timing::eventually())},
                             inputs(), outputs());
    Config cfg = timing_only_config();
    cfg.p_remove_guarantee = 0.5;
    std::size_t far_removed = 0;
    std::size_t near_removed = 0;
    for (std::size_t seed = 0; seed < k_seeds; ++seed) {
        const RandomSource far_source = make_random_source_from_seed(seed);
        const RandomSource near_source = make_random_source_from_seed(seed);
        const ParentContext far = context_at(0.0);
        const ParentContext near = context_at(1.0);
        if (count_live(mutate_specification(spec, far_source, cfg, &far)
                           .m_guarantees) < 3) {
            far_removed++;
        }
        if (count_live(mutate_specification(spec, near_source, cfg, &near)
                           .m_guarantees) < 3) {
            near_removed++;
        }
    }
    expect(far_removed == k_seeds,
           "far from realizable, removal at 0.5 scaled by 2 should always "
           "fire, got " +
               std::to_string(far_removed));
    expect(near_removed == 0,
           "near realizable, removal should never fire, got " +
               std::to_string(near_removed));
}

// A move off Always has no single edge, so the timing arm's rate scales:
// never near realizable, and at the capped rate far from it.
TEST(test_move_off_always_follows_the_distance) {
    const Specification spec({},
                             {make_req("a", "x", timing::always()),
                              make_req("b", "y", timing::within_ticks(3))},
                             inputs(), outputs());
    Config cfg = timing_only_config();
    cfg.p_timing = 0.5;
    expect(timings_taken(spec, 0, cfg, context_at(1.0)).empty(),
           "near realizable, Always should never move");
    const std::vector<Timing> far =
        timings_taken(spec, 0, cfg, context_at(0.0));
    expect(!far.empty() && all_equal_to(far, {timing::for_ticks(3)}),
           "far from realizable, Always should move into the donated timing");
}

// A crossover offspring is unscaled.
TEST(test_crossover_offspring_is_unscaled) {
    const Specification spec({},
                             {make_req("a", "x", timing::for_ticks(8)),
                              make_req("b", "y", timing::within_ticks(4))},
                             inputs(), outputs());
    ParentContext crossed = context_at(0.0);
    crossed.crossed = true;
    Config off = timing_only_config();
    off.step_scaling = 0.0;
    for (std::size_t seed = 0; seed < k_seeds; ++seed) {
        const RandomSource scaled_source = make_random_source_from_seed(seed);
        const RandomSource plain_source = make_random_source_from_seed(seed);
        const Specification scaled = mutate_specification(
            spec, scaled_source, timing_only_config(), &crossed);
        const Specification plain =
            mutate_specification(spec, plain_source, off, nullptr);
        expect(scaled == plain,
               "a crossover offspring should mutate unscaled, seed " +
                   std::to_string(seed));
    }
}

// Under the gate a realizable parent at `within 8`, whose original says
// `within 2`, steps to `within 2`; with bisection it steps to the midpoint of
// the chain `within 7` .. `within 2`, which is `within 5`.
TEST(test_bisected_step_lands_on_the_midpoint) {
    const Specification original(
        {}, {make_req("a", "x"), make_req("b", "y", timing::within_ticks(2))},
        inputs(), outputs());
    const Specification spec(
        {}, {make_req("a", "x"), make_req("b", "y", timing::within_ticks(8))},
        inputs(), outputs());
    ParentContext context = context_at(1.0);
    context.original = &original;
    Config cfg;
    cfg.direction_gated_mutation = true;

    expect(all_equal_to(timings_taken(spec, 1, cfg, context),
                        {timing::within_ticks(2)}),
           "without bisection the step should go to the original's timing");

    cfg.bisect_steps = true;
    const std::size_t before = DirectionGateStats::n_bisected.load();
    const std::vector<Timing> bisected = timings_taken(spec, 1, cfg, context);
    expect(
        !bisected.empty() && all_equal_to(bisected, {timing::within_ticks(5)}),
        "with bisection the step should go to `within 5`");
    expect(DirectionGateStats::n_bisected.load() >= before + bisected.size(),
           "each bisected step taken should be counted");

    // One tick from the original there is nothing between, so the step goes
    // all the way.
    const Specification adjacent(
        {}, {make_req("a", "x"), make_req("b", "y", timing::within_ticks(3))},
        inputs(), outputs());
    expect(all_equal_to(timings_taken(adjacent, 1, cfg, context),
                        {timing::within_ticks(2)}),
           "an adjacent step should go to the original's timing");
}

}  // namespace
