// Tests over SearchBudget, the run-level termination budget: that the default
// configuration is unchanged by its existence, that an individual cap stops
// breeding between offspring rather than at a generation boundary, and that a
// run reports the criterion AuRUS would report for it.
//
// The draw stream an unbudgeted run must keep is pinned in
// determinism_tests.cpp; what is pinned here is that an inactive budget in the
// call chain does not move it.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "fixtures.hpp"
#include "genetic/generation.hpp"
#include "genetic/pipeline.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "termination";

constexpr std::size_t k_target_size = 4;

// A real generator rather than a fixed sequence: the budget counts an offspring
// only where breeding actually changed it, so a source that makes every
// operator a no-op would make these tests pass without exercising anything.
RandomSource counting_source(std::size_t seed,
                             const std::shared_ptr<std::size_t>& counter) {
    std::mt19937 engine(static_cast<std::uint32_t>(seed));
    return RandomSource([engine, counter](std::size_t bound) mutable {
        ++*counter;
        std::uniform_int_distribution<std::size_t> dist(0, bound - 1);
        return dist(engine);
    });
}

Config individuals_config(std::size_t max_individuals) {
    Config cfg;
    cfg.termination = TerminationMode::Individuals;
    cfg.max_individuals = max_individuals;
    // Every slot recombines and mutates, so a slot that leaves its offspring
    // equal to the parent -- which the budget deliberately does not count -- is
    // rare enough not to decide the assertions below.
    cfg.crossover_rate = 1.0;
    cfg.mutation_rate = 1.0;
    return cfg;
}

// Mirrors the loop both drivers run: check the budget before the generation,
// evolve, count the generation. Returns nothing; the budget carries the result.
void drive(const Config& cfg, SearchBudget& budget, std::size_t seed) {
    const AggregateWeightedFitnessFunction fns = constant_fitness();
    std::vector<ScoredSpecification> pop =
        score_population(cfg, distinct_specs(), fns);
    const auto counter = std::make_shared<std::size_t>(0);
    const RandomSource source = counting_source(seed, counter);
    for (std::size_t gen = 0; gen < cfg.generations; ++gen) {
        if (budget.active() && budget.exhausted()) {
            break;
        }
        pop = evolve_generation(cfg, pop, k_target_size, 0, fns, {}, source,
                                nullptr, nullptr, &budget);
        budget.count_generation();
    }
}

// The default configuration must not be able to end a run early, since every
// archived campaign's config omits these keys and inherits whatever they mean.
TEST(test_default_config_budget_is_inactive) {
    const SearchBudget budget(Config{}, SearchBudget::Clock::now());
    expect(!budget.active(),
           "termination: the default configuration should leave the budget "
           "inactive");
    expect(budget.reason(StopReason::Generations) == StopReason::Generations,
           "termination: an inactive budget should report the fallback reason");
}

// An inactive budget must be invisible to the search. Breeding skips the parent
// comparison and the clock read when the budget cannot fire, so passing one
// must cost exactly what passing none costs -- in draws and in output.
TEST(test_inactive_budget_costs_no_draw) {
    const Config cfg;
    const AggregateWeightedFitnessFunction fns = constant_fitness();
    const std::vector<ScoredSpecification> pop =
        score_population(cfg, distinct_specs(), fns);

    const auto without = std::make_shared<std::size_t>(0);
    const std::vector<ScoredSpecification> no_budget = evolve_generation(
        cfg, pop, k_target_size, 0, fns, {}, counting_source(7, without),
        nullptr, nullptr, nullptr);

    SearchBudget inactive(cfg, SearchBudget::Clock::now());
    const auto with = std::make_shared<std::size_t>(0);
    const std::vector<ScoredSpecification> idle_budget = evolve_generation(
        cfg, pop, k_target_size, 0, fns, {}, counting_source(7, with), nullptr,
        nullptr, &inactive);

    expect(*without == *with, "termination: an inactive budget drew " +
                                  std::to_string(*with) + " times against " +
                                  std::to_string(*without) + " with none");
    expect(no_budget.size() == idle_budget.size(),
           "termination: an inactive budget changed the population size");
    for (std::size_t i = 0; i < no_budget.size(); ++i) {
        expect(no_budget[i].specification == idle_budget[i].specification,
               "termination: an inactive budget changed offspring " +
                   std::to_string(i));
    }
    expect(inactive.bred() == 0,
           "termination: an inactive budget should count no offspring");
}

// Counted per offspring and tested between them, so the run stops on the cap
// rather than overshooting it by the rest of the generation.
TEST(test_individual_cap_stops_the_run) {
    Config cfg = individuals_config(2);
    cfg.generations = 6;
    SearchBudget budget(cfg, SearchBudget::Clock::now());
    drive(cfg, budget, 11);

    expect(
        budget.bred() == 2,
        "termination: the cap should stop breeding at 2 offspring, counted " +
            std::to_string(budget.bred()));
    expect(budget.exhausted(),
           "termination: a spent individual cap should read as exhausted");
    expect(budget.reason(StopReason::Generations) == StopReason::Individuals,
           "termination: a spent individual cap should report Individuals");
    expect(budget.generations() < cfg.generations,
           "termination: the run should stop before its generation cap, ran " +
               std::to_string(budget.generations()));
}

// A slot whose operators left the offspring equal to its parent is free, which
// is what AuRUS counts: its mutation arm increments only on
// !chromosome.equals(mutated).
TEST(test_offspring_equal_to_parent_is_not_counted) {
    Config cfg = individuals_config(4);
    cfg.generations = 3;
    // Neither operator fires, so every slot returns its parent verbatim.
    cfg.crossover_rate = 0.0;
    cfg.mutation_rate = 0.0;
    SearchBudget budget(cfg, SearchBudget::Clock::now());
    drive(cfg, budget, 3);

    expect(budget.bred() == 0,
           "termination: offspring identical to their parents should not "
           "count, counted " +
               std::to_string(budget.bred()));
    expect(!budget.exhausted(),
           "termination: a budget nothing counted against should not be spent");
    expect(budget.generations() == cfg.generations,
           "termination: the run should have used its whole generation cap");
}

// Honoured whatever the termination mode is: the deadline is about the clock,
// not about how the search budget is counted.
TEST(test_deadline_stops_the_run_under_either_mode) {
    for (const TerminationMode mode :
         {TerminationMode::Generations, TerminationMode::Individuals}) {
        Config cfg = individuals_config(1000);
        cfg.termination = mode;
        cfg.generations = 4;
        cfg.max_wall_s = 1;
        // Started in the past, so the deadline is already behind the run at its
        // first check rather than the test having to wait out a real second.
        SearchBudget budget(
            cfg, SearchBudget::Clock::now() - std::chrono::seconds(30));
        drive(cfg, budget, 5);

        expect(budget.exhausted(),
               "termination: a passed deadline should read as exhausted");
        expect(budget.reason(StopReason::Generations) == StopReason::Deadline,
               "termination: a passed deadline should report Deadline");
        expect(budget.generations() == 0,
               "termination: a run past its deadline should evolve nothing, "
               "ran " +
                   std::to_string(budget.generations()));
    }
}

// AuRUS's checkTermination() tests the individual count first and the timeout
// second, so a run that trips both reports the same criterion on both sides.
TEST(test_individuals_reported_before_deadline) {
    Config cfg = individuals_config(1);
    cfg.generations = 4;
    cfg.max_wall_s = 1;
    SearchBudget budget(cfg,
                        SearchBudget::Clock::now() - std::chrono::seconds(30));
    budget.count_offspring();
    expect(budget.reason(StopReason::Generations) == StopReason::Individuals,
           "termination: a run tripping both should report Individuals");
}

// Zero is off rather than an instantly-passed deadline, which is what an
// archived config omitting the key has to keep meaning.
TEST(test_zero_deadline_never_fires) {
    Config cfg;
    cfg.max_wall_s = 0;
    const SearchBudget budget(
        cfg, SearchBudget::Clock::now() - std::chrono::seconds(3600));
    expect(!budget.active(),
           "termination: max_wall_s = 0 should leave the budget inactive");
    expect(!budget.exhausted(),
           "termination: max_wall_s = 0 should never read as exhausted");
}

// The manifest reports the count on every run, so it has to be maintained
// whether or not a budget is capable of firing.
TEST(test_generations_counted_without_a_budget) {
    Config cfg;
    cfg.generations = 3;
    SearchBudget budget(cfg, SearchBudget::Clock::now());
    drive(cfg, budget, 2);
    expect(budget.generations() == 3,
           "termination: an unbudgeted run should still count its generations, "
           "counted " +
               std::to_string(budget.generations()));
}

}  // namespace
