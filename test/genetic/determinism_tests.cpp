// Golden-seed regression tests over the generation loop's use of randomness.
//
// These exist to protect seed reproducibility across refactors of
// evolve_generation_generic. The pre-existing determinism test
// (test_evolve_generation_nsga2_truncate_is_deterministic) drives evolution
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
#include <utility>
#include <vector>

#include "config.hpp"
#include "genetic/generation.hpp"
#include "genetic/mutation.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "test_suite.hpp"
#include "test_support.hpp"

namespace {

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

const std::vector<std::string> k_in_atoms = {"a", "b", "c"};
const std::vector<std::string> k_out_atoms = {"x", "y"};

Requirement make_req(const std::string& condition, const std::string& response,
                     Timing timing) {
    return Requirement{Formula(condition), Formula(response),
                       std::move(timing)};
}

/// Pins every Config field the breeding path reads, so the goldens below track
/// the code rather than the config defaults. A default that changes the draw
/// stream in production is a real event, but it should not surface here as an
/// unexplained golden break.
Config golden_config() {
    Config cfg;
    cfg.selection_scheme = SelectionScheme::Nsga2Truncate;
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
    cfg.parallel = 1;
    return cfg;
}

/// A population with varied timings, varied assumption/guarantee counts, and a
/// spec with no assumptions, so breeding reaches both the assumption and
/// guarantee arms of mutate_specification and a non-trivial timing pool.
std::vector<Specification> golden_population() {
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
              return 1.0 / (1.0 + spec_size(spec) / k_compact_scale);
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

void test_recording_source_matches_production_stream() {
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
void test_trace_hash_distinguishes_order_and_bounds() {
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
void test_new_arms_cost_no_draw_at_zero() {
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
}

void test_generation_draw_sequence_is_pinned() {
    constexpr std::size_t k_expected_draws = 187;
    constexpr std::uint64_t k_expected_hash = 6958607742440809774ULL;

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

void test_evolved_population_is_pinned() {
    constexpr std::uint64_t k_expected_hash = 6487454080779250071ULL;

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

void test_same_seed_reproduces_evolution() {
    const std::string first =
        render_population(run_golden_evolution().population);
    const std::string second =
        render_population(run_golden_evolution().population);
    expect(first == second,
           "golden population: two runs from the same seed should produce an "
           "identical population");
}

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
void test_golden_bounded_uniform() {
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

}  // namespace

void run_determinism_tests() {
    test_golden_bounded_uniform();
    test_recording_source_matches_production_stream();
    test_trace_hash_distinguishes_order_and_bounds();
    test_generation_draw_sequence_is_pinned();
    test_new_arms_cost_no_draw_at_zero();
    test_evolved_population_is_pinned();
    test_same_seed_reproduces_evolution();
}
