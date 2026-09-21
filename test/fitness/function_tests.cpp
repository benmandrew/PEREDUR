#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fitness/function.hpp"
#include "fixtures.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "fitness_function";

// --- std::hash<Specification> ---

TEST(test_hash_identical_specifications_are_equal) {
    const Specification spec = make_spec("p", "q");
    expect(std::hash<Specification>{}(spec) == std::hash<Specification>{}(spec),
           "hash: identical specifications must hash to the same value");
}

TEST(test_hash_equal_specifications_are_equal) {
    const Specification spec_a = make_spec("p", "q");
    const Specification spec_b = make_spec("p", "q");
    expect(std::hash<Specification>{}(spec_a) ==
               std::hash<Specification>{}(spec_b),
           "hash: structurally equal specifications must hash to the same "
           "value");
}

TEST(test_hash_different_specifications_differ) {
    const Specification spec_a = make_spec("p", "q");
    const Specification spec_b = make_spec("r", "s");
    expect(std::hash<Specification>{}(spec_a) !=
               std::hash<Specification>{}(spec_b),
           "hash: distinct specifications should (almost always) hash "
           "differently");
}

// --- AggregateWeightedFitnessFunction memoisation ---

TEST(test_fitness_function_memoises_repeated_calls) {
    int call_count = 0;
    const AggregateWeightedFitnessFunction fitness_fn(
        {{[&call_count](const Specification&) {
              ++call_count;
              return 0.5;
          },
          1.0, ""}});
    const Specification spec = make_spec("p", "q");
    fitness_fn(spec);
    fitness_fn(spec);
    expect(call_count == 1,
           "fitness: identical specification should be scored only once");
}

TEST(test_fitness_function_scores_distinct_specs_independently) {
    int call_count = 0;
    const AggregateWeightedFitnessFunction fitness_fn(
        {{[&call_count](const Specification&) {
              ++call_count;
              return 0.5;
          },
          1.0, ""}});
    fitness_fn(make_spec("p", "q"));
    fitness_fn(make_spec("r", "s"));
    expect(call_count == 2,
           "fitness: distinct specifications should each be scored once");
}

TEST(test_fitness_function_cached_value_is_correct) {
    const AggregateWeightedFitnessFunction fitness_fn(
        {{[](const Specification&) { return 0.75; }, 1.0, ""}});
    const Specification spec = make_spec("p", "q");
    const double first_result = fitness_fn(spec);
    const double second_result = fitness_fn(spec);
    expect(first_result == second_result,
           "fitness: cached result must equal the originally computed value");
    expect(first_result == 0.75,
           "fitness: cached result must be the correct weighted-average score");
}

// --- per-objective exposure ---

TEST(test_objectives_returns_raw_component_scores_in_order) {
    const AggregateWeightedFitnessFunction fitness_fn(
        {{[](const Specification&) { return 0.25; }, 3.0, "a"},
         {[](const Specification&) { return 0.75; }, 1.0, "b"}});
    const std::vector<double> objectives =
        fitness_fn.objectives(make_spec("p", "q"));
    expect(objectives.size() == 2,
           "objectives: one entry per aggregated function");
    expect(
        objectives[0] == 0.25 && objectives[1] == 0.75,
        "objectives: raw component scores, unweighted, in registration order");
}

TEST(test_operator_is_weighted_average_of_objectives) {
    const AggregateWeightedFitnessFunction fitness_fn(
        {{[](const Specification&) { return 0.25; }, 3.0, "a"},
         {[](const Specification&) { return 0.75; }, 1.0, "b"}});
    // (3*0.25 + 1*0.75) / (3+1) = 1.5 / 4 = 0.375 (exact in binary)
    expect(fitness_fn(make_spec("p", "q")) == 0.375,
           "operator(): weighted average over the raw objective scores");
}

TEST(test_objectives_and_operator_share_one_evaluation) {
    int call_count = 0;
    const AggregateWeightedFitnessFunction fitness_fn(
        {{[&call_count](const Specification&) {
              ++call_count;
              return 0.5;
          },
          1.0, ""}});
    const Specification spec = make_spec("p", "q");
    fitness_fn.objectives(spec);
    fitness_fn(spec);
    fitness_fn.objectives_and_fitness(spec);
    expect(call_count == 1,
           "objectives/operator/objectives_and_fitness: a spec is evaluated "
           "once and served from one shared cache");
}

TEST(test_objectives_and_fitness_matches_separate_calls) {
    const AggregateWeightedFitnessFunction fitness_fn(
        {{[](const Specification&) { return 0.25; }, 3.0, "a"},
         {[](const Specification&) { return 0.75; }, 1.0, "b"}});
    const Specification spec = make_spec("p", "q");
    const auto [objectives, fitness] = fitness_fn.objectives_and_fitness(spec);
    expect(objectives == fitness_fn.objectives(spec),
           "objectives_and_fitness: vector matches objectives()");
    expect(fitness == fitness_fn(spec),
           "objectives_and_fitness: scalar matches operator()");
    expect(fitness_fn.n_objectives() == 2,
           "n_objectives: reports the number of aggregated functions");
}

// --- plan / store, the split scoring path ---

// The whole split rests on this: whatever an objective decomposes into, folding
// its parts must give the number the serial call gives.
TEST(test_plan_and_fold_matches_the_serial_score) {
    const AggregateWeightedFitnessFunction fitness_fn(
        {{[](const Specification&) { return 0.25; }, 3.0, "whole"},
         {[](const Specification&) { return 0.75; }, 1.0, "split",
          [](const Specification&) {
              ObjectiveWork work;
              work.parts.push_back({[] { return 0.5; }, 2.0});
              work.parts.push_back({[] { return 1.0; }, 1.0});
              work.combine = [](const std::vector<double>& values) {
                  return (values[0] + values[1]) / 2.0;
              };
              return work;
          }}});
    const Specification spec = make_spec("p", "q");

    const std::vector<ObjectiveWork> work = fitness_fn.plan(spec);
    expect(work.size() == 2, "plan: one entry per registered objective");
    expect(work[0].parts.size() == 1,
           "plan: an objective with no decomposition should contribute one "
           "part wrapping its whole function");
    expect(work[1].parts.size() == 2,
           "plan: a decomposed objective should contribute its own parts");

    std::vector<double> objectives;
    for (const ObjectiveWork& objective : work) {
        std::vector<double> values;
        // Reverse order, since a part may run whenever the pool gets to it.
        values.resize(objective.parts.size());
        for (std::size_t i = objective.parts.size(); i > 0; --i) {
            values[i - 1] = objective.parts[i - 1].run();
        }
        objectives.push_back(objective.combine(values));
    }
    auto [stored, fitness] = fitness_fn.store(spec, objectives);
    expect(stored == objectives, "store: returns the vector it recorded");
    expect(std::fabs(fitness - (((0.25 * 3.0) + (0.75 * 1.0)) / 4.0)) < 1e-12,
           "store: the scalar should be the weighted average of the folded "
           "objectives");
    expect(std::fabs(fitness_fn(spec) - fitness) < 1e-12,
           "store: a stored score must be what the serial path answers for the "
           "same specification");
}

TEST(test_cached_objectives_answers_only_after_scoring) {
    const AggregateWeightedFitnessFunction fitness_fn(
        {{[](const Specification&) { return 0.5; }, 1.0, ""}});
    const Specification spec = make_spec("p", "q");
    expect(!fitness_fn.cached_objectives(spec).has_value(),
           "cached_objectives: an unscored specification has no entry");
    fitness_fn(spec);
    const std::optional<std::vector<double>> cached =
        fitness_fn.cached_objectives(spec);
    if (!cached.has_value()) {
        fail(
            "cached_objectives: a scored specification should answer from "
            "the cache");
        return;
    }
    expect(cached->size() == 1 && std::fabs(cached->front() - 0.5) < 1e-12,
           "cached_objectives: the cached vector should be the objectives the "
           "serial path computed");
    expect(std::fabs(fitness_fn.scalar(*cached) - 0.5) < 1e-12,
           "scalar: pairs a cached vector with the same weighted average the "
           "serial path computes");
}

}  // namespace
