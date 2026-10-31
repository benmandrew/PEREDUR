// Golden-seed regression tests over the generation loop's use of randomness.
//
// These exist to protect seed reproducibility across refactors of
// evolve_generation_generic. The pre-existing determinism test
// (test_evolve_generation_nsga2_apportion_is_deterministic) drives evolution
// from a *scripted* RandomSource, so it only shows that equal inputs give equal
// outputs: reorder the draws and both sides reorder alike and it still passes.
// The tests here instead record the real mt19937 draw stream and pin it, so a
// change to the order or count of draws fails loudly.
//
// Randomness is consumed only while breeding (the two probability_check calls
// in breed_offspring, plus crossover and mutation); filtering, scoring,
// padding, ordering and NSGA-II selection draw nothing. Evolution is therefore
// driven here without filters, which keeps the test hermetic -- the production
// filters need black/ltlsynt -- without narrowing what the draw golden covers.
//
// The pinned values are recorded from current behaviour rather than derived
// independently; their purpose is to detect change, not to be correct in their
// own right. They no longer depend on the standard library: bounded_uniform is
// vendored precisely so that the stream is the same under libstdc++ and
// libc++, which is what lets a macOS run reproduce a Linux campaign. Each
// assertion still reports the value it actually saw, so a deliberate change to
// the breeding order is mechanical to re-pin.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.hpp"
#include "fitness/status.hpp"
#include "fixtures.hpp"
#include "genetic/generation.hpp"
#include "genetic/mutation.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "determinism";

constexpr std::size_t k_seed = 20260730;
constexpr std::size_t k_generations = 3;
constexpr std::size_t k_target_size = 4;
constexpr std::size_t k_elitism_size = 1;

// FNV-1a. Hand-rolled rather than std::hash because a pinned constant must
// survive a standard-library upgrade that rehashes std::hash<std::string>.
constexpr std::uint64_t k_fnv_offset_basis = 14695981039346656037ULL;
constexpr std::uint64_t k_fnv_prime = 1099511628211ULL;

std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = k_fnv_offset_basis;
    for (const char character : text) {
        hash ^=
            static_cast<std::uint64_t>(static_cast<unsigned char>(character));
        hash *= k_fnv_prime;
    }
    return hash;
}

/// Every (upper_bound, value) pair drawn from a recording RandomSource, in
/// order. The bound is recorded alongside the value because a refactor can
/// reorder calls without changing the values they happen to return; the bound
/// sequence still shifts.
struct DrawTrace {
    std::vector<std::pair<std::size_t, std::size_t>> draws;
};

/// A RandomSource that appends to @p trace on every draw. Mirrors
/// make_random_source_from_seed exactly -- same generator, same per-call
/// distribution construction -- so the recorded stream is the production
/// stream. test_recording_source_matches_production_stream guards that.
RandomSource make_recording_source(std::size_t seed,
                                   const std::shared_ptr<DrawTrace>& trace) {
    std::mt19937 rng(seed);
    auto generator = [rng, trace](std::size_t upper_bound) mutable {
        const auto value = static_cast<std::size_t>(
            bounded_uniform(rng, static_cast<std::uint32_t>(upper_bound)));
        trace->draws.emplace_back(upper_bound, value);
        return value;
    };
    return {generator, seed};
}

std::string render_trace(const DrawTrace& trace) {
    std::string text;
    for (const auto& draw : trace.draws) {
        text += std::to_string(draw.first) + ":" + std::to_string(draw.second) +
                ";";
    }
    return text;
}

/// Pins every Config field the breeding path reads, so the goldens below track
/// the code rather than the config defaults. A default that changes the draw
/// stream in production is a real event, but it should not surface here as an
/// unexplained golden break.
Config golden_config() {
    Config cfg;
    // Re-pinned from Nsga2Truncate when that scheme was removed on 2026-10-30.
    cfg.selection_scheme = SelectionScheme::Nsga2Apportion;
    // Pinned rather than left to the default, the same discipline
    // p_remove_guarantee follows below: this is the one Config field the
    // breeding path reads whose default has moved before, and a later move
    // must fail this suite rather than re-record itself silently.
    cfg.elitism_rate = 0.1;
    // Strictly between 0 and 1: probability_check short-circuits without
    // drawing at exactly 0.0 or 1.0, which would leave its own draws untested.
    cfg.crossover_rate = 0.75;
    cfg.mutation_rate = 0.75;
    cfg.p_trigger = 0.5;
    cfg.p_response = 0.5;
    cfg.p_timing = 0.15;
    cfg.p_add_assumption = 0.05;
    // Pinned explicitly rather than left to the struct default, like every
    // other field here: the goldens below record the draw stream this operator
    // takes part in, so a later change to the default must fail this suite
    // rather than silently re-record it.
    cfg.p_remove_guarantee = 0.05;
    cfg.p_conditional_assumption = 0.25;
    // Pinned at 0 rather than their 0.15 default: at 0 each arm returns before
    // touching the RandomSource, which keeps the counts below equal to the ones
    // recorded before scopes existed.
    cfg.p_condition_type = 0.0;
    cfg.p_scope = 0.0;
    // The same, for the monotone arm and its 0.25 default: at 0 rewrite_field
    // returns to mutate_formula without drawing, so the goldens hold what they
    // held before the arm existed.
    cfg.p_monotone = 0.0;
    // At its default: no golden specification carries a stop timing, so the
    // arm returns before the RandomSource at any value. Pinned so a golden that
    // gains one fails here rather than re-recording.
    cfg.p_stop = 0.15;
    // Off, its default. The golden fitness has no status objective, so the
    // arm would leave every list alone even on; pinned so that a golden
    // gaining one fails here rather than re-recording.
    cfg.blame_targeted_slots = false;
    // Off, its default: selection then runs no proof, so the goldens hold what
    // they held before the rule existed.
    cfg.maximality_demotion = false;
    // At its default of 0 the order crossover returns before reading a verdict
    // or drawing. The golden fitness has no status objective, so no parent
    // here reads realizable at any value; pinned so that stays a decision.
    cfg.p_order_crossover = 0.0;
    // Off, its default, with the sideways share at its default of 1, where it
    // draws nothing even with the gate on. The golden fitness has no status
    // objective, so the gate would stay shut even on; both are pinned so that
    // a golden gaining one fails here rather than re-recording.
    cfg.direction_gated_mutation = false;
    cfg.p_sideways = 1.0;
    // Off, their defaults. At 0 the scale reads no status and the tick-count
    // steps draw as they did before it existed; bisection draws nothing even
    // on and acts only under the gate. Pinned so that a golden gaining a
    // status objective fails here rather than re-recording.
    cfg.step_scaling = 0.0;
    cfg.bisect_steps = false;
    // Off, its default. The stage draws nothing even on, and the golden
    // fitness has no status objective to record origins against; pinned so
    // that a golden gaining one fails here rather than re-recording.
    cfg.parent_verdict_filter = false;
    cfg.parallel = 1;
    return cfg;
}

/// A population with varied timings, varied assumption/guarantee counts, and a
/// spec with no assumptions, so breeding reaches both the assumption and
/// guarantee arms of mutate_specification and a non-trivial timing pool.
std::vector<Specification> golden_population() {
    const std::vector<std::string> k_in_atoms = {"a", "b", "c"};
    const std::vector<std::string> k_out_atoms = {"x", "y"};
    return {
        Specification({make_req("a", "x", timing::immediately())},
                      {make_req("b", "y", timing::next_timepoint())},
                      k_in_atoms, k_out_atoms),
        Specification({make_req("b", "y", timing::immediately())},
                      {make_req("a", "x", timing::within_ticks(3)),
                       make_req("c", "y", timing::immediately())},
                      k_in_atoms, k_out_atoms),
        Specification({}, {make_req("c", "x", timing::for_ticks(2))},
                      k_in_atoms, k_out_atoms),
        Specification({make_req("a", "y", timing::always())},
                      {make_req("b", "x", timing::eventually()),
                       make_req("c", "y", timing::after_ticks(4))},
                      k_in_atoms, k_out_atoms),
    };
}

double spec_size(const Specification& spec) {
    return static_cast<double>(spec.to_string().size());
}

/// Two objectives with a genuine trade-off -- one rewards compact
/// specifications, the other larger ones -- so NSGA-II ordering is
/// non-degenerate and the population golden is sensitive to selection changes.
/// Stubs rather than the real fitness functions, which need external tools.
AggregateWeightedFitnessFunction golden_fitness() {
    constexpr double k_compact_scale = 100.0;
    constexpr double k_expressive_scale = 400.0;
    return AggregateWeightedFitnessFunction(
        {{[](const Specification& spec) {
              return 1.0 / (1.0 + (spec_size(spec) / k_compact_scale));
          },
          1.0, "compact"},
         {[](const Specification& spec) {
              const double scaled = spec_size(spec) / k_expressive_scale;
              return scaled > 1.0 ? 1.0 : scaled;
          },
          1.0, "expressive"}});
}

struct GoldenRun {
    std::shared_ptr<DrawTrace> trace;
    std::vector<ScoredSpecification> population;
};

GoldenRun run_golden_evolution() {
    const Config cfg = golden_config();
    const AggregateWeightedFitnessFunction fns = golden_fitness();
    auto trace = std::make_shared<DrawTrace>();
    const RandomSource source = make_recording_source(k_seed, trace);

    std::vector<ScoredSpecification> population =
        score_population(cfg, golden_population(), fns);
    for (std::size_t gen = 0; gen < k_generations; ++gen) {
        population = evolve_generation(cfg, population, k_target_size,
                                       k_elitism_size, fns, {}, source);
    }
    return {std::move(trace), std::move(population)};
}

std::string render_population(
    const std::vector<ScoredSpecification>& population) {
    std::string text;
    for (const ScoredSpecification& scored : population) {
        text += scored.specification.to_string() + "\n--\n";
    }
    return text;
}

// --- the goldens ---

// bounded_uniform is the one piece of the draw stream PEREDUR owns rather than
// inherits, and the reason it is owned is that std::uniform_int_distribution
// gave a different answer under libc++ than under libstdc++ -- so a seed
// reproduced a run only within one standard library, and a macOS run could not
// reproduce a Linux campaign at all.
//
// These values were recorded from libstdc++'s uniform_int_distribution, which
// is what every archived campaign was drawn against, so the vendored reduction
// is a drop-in there and only macOS moves. Pinning them here is what stops a
// future edit, or a libstdc++ change, drifting the stream back apart in
// silence. A bound of 1 leads deliberately: it returns the only legal answer
// but still spends an engine word, and an implementation that skips that draw
// passes every other case here while shifting the whole stream by one.
TEST(test_golden_bounded_uniform) {
    // The golden values below are this seed's stream.
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937 rng(20260820);
    const std::vector<std::uint32_t> bounds = {1,  2,  3,    4,      7,
                                               12, 60, 1000, 1000000};
    const std::vector<std::uint32_t> expected = {
        0, 0, 1, 3, 2, 9, 35, 181, 896636, 0, 0, 2, 1, 2, 1, 36, 243, 703569,
        0, 0, 1, 1, 2, 8, 7,  348, 673845, 0, 1, 2, 0, 3, 1, 25, 736, 972698};

    std::vector<std::uint32_t> actual;
    for (int round = 0; round < 4; ++round) {
        for (const std::uint32_t bound : bounds) {
            actual.push_back(bounded_uniform(rng, bound));
        }
    }
    expect(actual.size() == expected.size(),
           "golden bounded_uniform: the fixture should draw once per bound");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        expect(actual[i] == expected[i],
               "golden bounded_uniform: draw " + std::to_string(i) +
                   " for bound " + std::to_string(bounds[i % bounds.size()]) +
                   " gave " + std::to_string(actual[i]) + ", pinned " +
                   std::to_string(expected[i]) +
                   "; the bounded draw no longer matches the stream every "
                   "archived seed was drawn against");
    }
}

TEST(test_recording_source_matches_production_stream) {
    // Includes 1000000, the bound next_real() uses, and 2, the bound
    // next_bool() uses.
    const std::vector<std::size_t> bounds = {4, 2, 3, 1000000, 7, 2, 2, 5};
    const RandomSource production = make_random_source_from_seed(k_seed);
    auto trace = std::make_shared<DrawTrace>();
    const RandomSource recording = make_recording_source(k_seed, trace);
    for (const std::size_t bound : bounds) {
        const std::size_t expected = production.next_index(bound);
        const std::size_t actual = recording.next_index(bound);
        expect(expected == actual,
               "recording source: draw for bound " + std::to_string(bound) +
                   " diverged from make_random_source_from_seed (expected " +
                   std::to_string(expected) + ", got " +
                   std::to_string(actual) +
                   "); the trace no longer reflects the production stream");
    }
    expect(trace->draws.size() == bounds.size(),
           "recording source: should record exactly one entry per draw");
}

// The draw-count assertion below cannot catch a reordering that preserves the
// number of draws, so the trace hash carries that weight. Pin the two
// properties it needs: sensitivity to order, and to the bounds as well as the
// values.
TEST(test_trace_hash_distinguishes_order_and_bounds) {
    const DrawTrace baseline{{{4, 1}, {2, 0}, {1000000, 750}}};
    const DrawTrace reordered{{{2, 0}, {4, 1}, {1000000, 750}}};
    const DrawTrace rebounded{{{4, 1}, {3, 0}, {1000000, 750}}};

    expect(fnv1a(render_trace(baseline)) != fnv1a(render_trace(reordered)),
           "trace hash: should distinguish two traces that differ only in the "
           "order of their draws, since that is the reordering the draw count "
           "cannot detect");
    expect(fnv1a(render_trace(baseline)) != fnv1a(render_trace(rebounded)),
           "trace hash: should distinguish two traces that differ only in an "
           "upper bound, since a call can be re-pointed without changing the "
           "value it returns");
    expect(fnv1a(render_trace(baseline)) == fnv1a(render_trace(baseline)),
           "trace hash: should be stable for an unchanged trace");
}

TEST(test_generation_draw_sequence_is_pinned) {
    constexpr std::size_t k_expected_draws = 161;
    // Re-pinned, with the population golden below, when golden_config() moved
    // from Nsga2Truncate to Nsga2Apportion: selection draws nothing, but it
    // picks the parents later generations breed from, so the stream moves.
    constexpr std::uint64_t k_expected_hash = 13845064172576639202ULL;

    const GoldenRun run = run_golden_evolution();
    const std::uint64_t hash = fnv1a(render_trace(*run.trace));

    expect(run.trace->draws.size() == k_expected_draws,
           "golden draw sequence: the generation loop drew " +
               std::to_string(run.trace->draws.size()) + " times, expected " +
               std::to_string(k_expected_draws) +
               "; breeding no longer consumes randomness in the same quantity, "
               "so a fixed seed no longer reproduces earlier runs");
    expect(hash == k_expected_hash,
           "golden draw sequence: trace hash " + std::to_string(hash) +
               " != pinned " + std::to_string(k_expected_hash) +
               "; the order of RNG draws in breeding has changed, so a fixed "
               "seed no longer reproduces earlier runs");
}

// The arms cost no draw at 0, which is what keeps the goldens above where they
// were. A guard written that way passes just as
// happily when the arm is dead, so this counts the draws of a single
// mutate_requirement call with each arm off and then on.
//
// One call rather than a whole evolution, because the evolution's draw count is
// not monotone in the arm: a different condition type changes which offspring
// deduplicate away, which changes how much later generations draw. Turning
// p_condition_type on over the golden run reads 195 draws against 187, and
// neither number says anything about whether the probability was read.
TEST(test_new_arms_cost_no_draw_at_zero) {
    const Requirement req(Formula("a"), Formula("x"), timing::immediately(),
                          ConditionType::Trigger);
    const std::vector<std::string> atoms = {"a", "x"};
    const std::vector<Timing> timings = {timing::immediately()};
    const std::vector<std::string> modes = {"m"};

    const auto count_draws = [&](const Config& cfg) {
        auto trace = std::make_shared<DrawTrace>();
        const RandomSource source = make_recording_source(k_seed, trace);
        mutate_requirement(req, atoms, atoms, Direction::Weaken, timings, modes,
                           source, cfg);
        return trace->draws.size();
    };

    Config off;
    off.p_response = 0.0;
    off.p_trigger = 0.0;
    off.p_timing = 0.0;
    off.p_condition_type = 0.0;
    off.p_scope = 0.0;
    off.p_stop = 0.0;
    off.p_monotone = 0.0;
    const std::size_t baseline = count_draws(off);

    Config condition_type_on = off;
    condition_type_on.p_condition_type = 0.5;
    expect(count_draws(condition_type_on) == baseline + 1,
           "p_condition_type: the arm must cost exactly one draw when on and "
           "none at 0, got " +
               std::to_string(count_draws(condition_type_on)) + " against " +
               std::to_string(baseline));

    Config scope_on = off;
    scope_on.p_scope = 0.5;
    expect(count_draws(scope_on) == baseline + 1,
           "p_scope: the arm must cost exactly one draw when on and none at 0, "
           "got " +
               std::to_string(count_draws(scope_on)) + " against " +
               std::to_string(baseline));

    // The stop arm is gated on the requirement holding a stop as well, so on
    // this one it costs nothing even when certain to fire.
    Config stop_on = off;
    stop_on.p_stop = 1.0;
    expect(count_draws(stop_on) == baseline,
           "p_stop: the arm must cost no draw on a requirement without a stop, "
           "got " +
               std::to_string(count_draws(stop_on)) + " against " +
               std::to_string(baseline));
    const Requirement stopped(Formula("a"), Formula("x"),
                              timing::until(Formula("a")),
                              ConditionType::Trigger);
    const auto count_stop_draws = [&](const Config& cfg) {
        auto trace = std::make_shared<DrawTrace>();
        const RandomSource source = make_recording_source(k_seed, trace);
        mutate_requirement(stopped, atoms, atoms, Direction::Weaken, timings,
                           modes, source, cfg);
        return trace->draws.size();
    };
    expect(count_stop_draws(off) == baseline &&
               count_stop_draws(stop_on) > baseline,
           "p_stop: the arm must draw on a requirement with a stop when on, "
           "and not at 0");

    // The monotone arm is checked on the rendered stream rather than on a
    // draw count. It sits *inside* the response arm rather than beside it, so
    // it is only observable with that arm on, and it replaces one rewrite with
    // another rather than appending draws to the end -- two rewrites of one
    // field can draw the same number of times and take different branches
    // doing it.
    const auto render_draws = [&](const Config& cfg) {
        auto trace = std::make_shared<DrawTrace>();
        const RandomSource source = make_recording_source(k_seed, trace);
        mutate_requirement(req, atoms, atoms, Direction::Weaken, timings, modes,
                           source, cfg);
        return render_trace(*trace);
    };

    Config response_on = off;
    response_on.p_response = 1.0;
    Config monotone_on = response_on;
    monotone_on.p_monotone = 1.0;
    expect(render_draws(monotone_on) != render_draws(response_on),
           "p_monotone: turning the arm on must change the draw stream, or "
           "the zero-cost guard above is passing over a dead arm");

    // The order crossover sits inside breeding, after the partner draw, and
    // replaces the graft rather than adding to it, so it too is compared on
    // the rendered stream. Every parent scores a full status, so every pair
    // has verdicts and the arm can fire whenever its probability allows.
    const auto render_breeding = [&](const Config& cfg, double status) {
        std::vector<ScoredSpecification> parents;
        for (Specification& specification : golden_population()) {
            ScoredSpecification entry;
            entry.specification = std::move(specification);
            entry.objectives = {0.5, 0.5, status};
            parents.push_back(std::move(entry));
        }
        auto trace = std::make_shared<DrawTrace>();
        const RandomSource source = make_recording_source(k_seed, trace);
        (void)generation_detail::breed_offspring(
            cfg, parents, parents.size(), parents.size(), source, nullptr);
        return render_trace(*trace);
    };
    Config breeding = golden_config();
    breeding.crossover_rate = 1.0;
    breeding.mutation_rate = 0.0;
    Config order_on = breeding;
    order_on.p_order_crossover = 1.0;
    expect(render_breeding(breeding, k_status_realizable) ==
               render_breeding(order_on, k_status_unrealizable),
           "p_order_crossover: at 0, or on parents with no verdict, the arm "
           "must draw nothing");
    expect(render_breeding(order_on, k_status_realizable) !=
               render_breeding(breeding, k_status_realizable),
           "p_order_crossover: turning the arm on must change the draw "
           "stream, or the zero-cost guard above is passing over a dead arm");
}

// Slot targeting is a filter on the list the slot draw indexes, so it never
// adds a draw. Off, a parent context must change nothing at all; on, the same
// draw must index a shorter list, or the off check passes over a dead arm.
TEST(test_slot_targeting_costs_no_draw_when_off) {
    const std::vector<std::string> inputs = {"a", "b", "c"};
    const std::vector<std::string> outputs = {"x", "y"};
    const Specification spec({make_req("a", "b", timing::eventually())},
                             {make_req("a", "x"), make_req("b", "y"),
                              make_req("c", "x", timing::within_ticks(2))},
                             inputs, outputs);
    ParentContext parent;
    parent.status = 1.0 / 3.0;
    parent.refused_guarantees = {1};

    struct Outcome {
        std::string trace;
        std::size_t slot_bound = 0;
        Specification mutated;
    };
    const auto mutate = [&spec](const Config& cfg,
                                const ParentContext* context) {
        auto trace = std::make_shared<DrawTrace>();
        const RandomSource source = make_recording_source(k_seed, trace);
        Outcome outcome;
        outcome.mutated = mutate_specification(spec, source, cfg, context);
        outcome.trace = render_trace(*trace);
        // The add-assumption arm's next_real comes first, then the slot.
        outcome.slot_bound =
            trace->draws.size() > 1 ? trace->draws[1].first : 0;
        return outcome;
    };

    Config off = golden_config();
    off.p_remove_guarantee = 0.0;
    const Outcome without = mutate(off, nullptr);
    const Outcome with = mutate(off, &parent);
    expect(with.trace == without.trace && with.mutated == without.mutated,
           "blame_targeted_slots: off, a parent context must change neither "
           "the draw stream nor the offspring");
    expect(without.slot_bound == 4,
           "blame_targeted_slots: off, the slot draw should index all four "
           "live weakenable slots, got " +
               std::to_string(without.slot_bound));

    Config targeting_on = off;
    targeting_on.blame_targeted_slots = true;
    const std::size_t before = SlotTargetingStats::n_targeted.load();
    const Outcome targeted = mutate(targeting_on, &parent);
    expect(targeted.slot_bound == 2,
           "blame_targeted_slots: on, the slot draw should index the "
           "assumption and the refused guarantee alone, got " +
               std::to_string(targeted.slot_bound));
    expect(SlotTargetingStats::n_targeted.load() == before + 1,
           "blame_targeted_slots: a narrowed draw should be counted once");
}

// The direction gate reads its key before anything else, so off, a parent
// context in either state must change neither the stream nor the offspring.
// On, a realizable parent must skip the add-assumption draw, or the off check
// passes over a dead arm.
TEST(test_direction_gate_costs_no_draw_when_off) {
    const std::vector<std::string> inputs = {"a", "b", "c"};
    const std::vector<std::string> outputs = {"x", "y"};
    const Specification original({make_req("a", "b", timing::eventually())},
                                 {make_req("a", "x"), make_req("b", "y"),
                                  make_req("c", "x", timing::within_ticks(2))},
                                 inputs, outputs);
    const Specification spec({make_req("a", "b", timing::eventually())},
                             {make_req("a", "x"), make_req("b", "y"),
                              make_req("c", "x", timing::within_ticks(4))},
                             inputs, outputs);
    ParentContext unrealizable;
    unrealizable.status = 1.0 / 3.0;
    unrealizable.original = &original;
    ParentContext realizable = unrealizable;
    realizable.status = 1.0;
    realizable.realizable = true;

    const auto mutate = [&spec](const Config& cfg,
                                const ParentContext* context) {
        auto trace = std::make_shared<DrawTrace>();
        const RandomSource source = make_recording_source(k_seed, trace);
        Specification mutated =
            mutate_specification(spec, source, cfg, context);
        return std::make_pair(*trace, std::move(mutated));
    };

    const Config off = golden_config();
    const auto without = mutate(off, nullptr);
    for (const ParentContext* context : {&unrealizable, &realizable}) {
        const auto with = mutate(off, context);
        expect(render_trace(with.first) == render_trace(without.first) &&
                   with.second == without.second,
               "direction_gated: off, a parent context must change neither "
               "the draw stream nor the offspring");
    }

    Config gate_on = off;
    gate_on.direction_gated_mutation = true;
    const DrawTrace strengthened = mutate(gate_on, &realizable).first;
    // Without the gate the add-assumption arm's next_real opens the stream;
    // with it a realizable parent opens on the draw over steps and slots.
    constexpr std::size_t k_real_bound = 1000000;
    expect(!without.first.draws.empty() &&
               without.first.draws[0].first == k_real_bound &&
               !strengthened.draws.empty() &&
               strengthened.draws[0].first != k_real_bound,
           "direction_gated: on, a realizable parent should skip the "
           "add-assumption draw");

    // The sideways share draws only strictly between 0 and 1.
    constexpr std::size_t k_seeds = 64;
    for (std::size_t seed = 0; seed < k_seeds; ++seed) {
        const auto stream = [&](double p_sideways,
                                const ParentContext& context) {
            Config cfg = gate_on;
            cfg.p_sideways = p_sideways;
            auto trace = std::make_shared<DrawTrace>();
            const RandomSource source = make_recording_source(seed, trace);
            static_cast<void>(
                mutate_specification(spec, source, cfg, &context));
            return render_trace(*trace);
        };
        for (const ParentContext* context : {&unrealizable, &realizable}) {
            expect(stream(0.0, *context) == stream(1.0, *context),
                   "p_sideways: at 0 and at 1 the share must cost no draw, "
                   "seed " +
                       std::to_string(seed));
        }
    }
}

// The step scale reads its key before the parent's status, so at 0 a context in
// either state must change neither the stream nor the offspring. On, the
// tick-count steps must draw differently, or the off check passes over a dead
// arm. Bisection replaces a step's result and draws nothing, so with the gate
// on its stream must match the gate's alone.
TEST(test_step_scaling_costs_no_draw_when_off) {
    const std::vector<std::string> inputs = {"a", "b", "c"};
    const std::vector<std::string> outputs = {"x", "y"};
    const Specification original({make_req("a", "b", timing::eventually())},
                                 {make_req("a", "x"), make_req("b", "y"),
                                  make_req("c", "x", timing::within_ticks(2))},
                                 inputs, outputs);
    const Specification spec(
        {make_req("a", "b", timing::eventually())},
        {make_req("a", "x", timing::for_ticks(4)), make_req("b", "y"),
         make_req("c", "x", timing::within_ticks(8))},
        inputs, outputs);
    ParentContext unrealizable;
    unrealizable.status = 0.0;
    unrealizable.original = &original;
    ParentContext realizable = unrealizable;
    realizable.status = 1.0;
    realizable.realizable = true;

    const auto mutate = [&spec](const Config& cfg, const ParentContext* context,
                                std::size_t seed) {
        auto trace = std::make_shared<DrawTrace>();
        const RandomSource source = make_recording_source(seed, trace);
        Specification mutated =
            mutate_specification(spec, source, cfg, context);
        return std::make_pair(render_trace(*trace), std::move(mutated));
    };

    Config off = golden_config();
    off.p_timing = 1.0;
    off.bisect_steps = true;
    Config gate_on = off;
    gate_on.direction_gated_mutation = true;
    Config gate_unbisected = gate_on;
    gate_unbisected.bisect_steps = false;
    Config scaled = off;
    scaled.step_scaling = 1.0;
    constexpr std::size_t k_seeds = 64;
    const std::size_t scaled_before = StepScalingStats::n_scaled.load();
    bool scaled_moved = false;
    for (std::size_t seed = 0; seed < k_seeds; ++seed) {
        const auto without = mutate(off, nullptr, seed);
        for (const ParentContext* context : {&unrealizable, &realizable}) {
            const auto with = mutate(off, context, seed);
            expect(with.first == without.first && with.second == without.second,
                   "step_scaling: at 0, a parent context must change neither "
                   "the draw stream nor the offspring, seed " +
                       std::to_string(seed));
            expect(mutate(gate_on, context, seed).first ==
                       mutate(gate_unbisected, context, seed).first,
                   "bisect_steps: under the gate bisection must cost no draw, "
                   "seed " +
                       std::to_string(seed));
        }
        const bool differs =
            mutate(scaled, &unrealizable, seed).first != without.first;
        scaled_moved = scaled_moved || differs;
    }
    expect(StepScalingStats::n_scaled.load() == scaled_before + k_seeds,
           "step_scaling: on, each mutation with a status should be scaled "
           "once, and none at 0");
    expect(scaled_moved,
           "step_scaling: on, some tick-count step should draw differently");
}

TEST(test_evolved_population_is_pinned) {
    constexpr std::uint64_t k_expected_hash = 13850672673615569937ULL;

    const GoldenRun run = run_golden_evolution();
    const std::uint64_t hash = fnv1a(render_population(run.population));

    expect(run.population.size() == k_target_size,
           "golden population: evolution should return target_size survivors");
    expect(hash == k_expected_hash,
           "golden population: hash " + std::to_string(hash) + " != pinned " +
               std::to_string(k_expected_hash) +
               "; the evolved population has changed. If the draw-sequence "
               "golden still passes, the change is in scoring, ordering, "
               "elitism, padding or selection rather than in breeding");
}

TEST(test_same_seed_reproduces_evolution) {
    const std::string first =
        render_population(run_golden_evolution().population);
    const std::string second =
        render_population(run_golden_evolution().population);
    expect(first == second,
           "golden population: two runs from the same seed should produce an "
           "identical population");
}

}  // namespace
