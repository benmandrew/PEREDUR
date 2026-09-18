// Tests over the generation pipeline as an observable list of stages: that a
// consumer sees every stage without knowing which stages exist, that adding a
// filter adds a stage, and that the reported population sizes and distinct
// counts track the real ones. The draw stream the pipeline must preserve is
// pinned separately, in determinism_tests.cpp.

#include <algorithm>
#include <cstddef>
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

constexpr std::string_view k_test_suite = "pipeline";

constexpr std::size_t k_target_size = 4;
constexpr std::size_t k_elitism_size = 1;

std::vector<StageObservation> observe_generation(
    const std::vector<FilterFunction>& filters,
    const std::vector<Specification>& specs = distinct_specs(),
    std::size_t elitism_size = k_elitism_size) {
    const Config cfg;
    const AggregateWeightedFitnessFunction fns = constant_fitness();
    const std::vector<ScoredSpecification> pop =
        score_population(cfg, specs, fns);
    std::vector<StageObservation> seen;
    evolve_generation(
        cfg, pop, k_target_size, elitism_size, fns, filters, make_source({}, 0),
        nullptr, [&seen](const StageObservation& obs) { seen.push_back(obs); });
    return seen;
}

std::vector<std::string> stage_names(
    const std::vector<StageObservation>& observations) {
    std::vector<std::string> names;
    names.reserve(observations.size());
    for (const StageObservation& obs : observations) {
        names.push_back(obs.name);
    }
    return names;
}

bool contains(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::size_t index_of(const std::vector<std::string>& names,
                     const std::string& name) {
    const auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end()) {
        fail("pipeline: expected a stage named " + name);
    }
    return static_cast<std::size_t>(std::distance(names.begin(), found));
}

const StageObservation& stage(const std::vector<StageObservation>& observations,
                              const std::string& name) {
    return observations[index_of(stage_names(observations), name)];
}

TEST(test_pipeline_reports_every_fixed_stage) {
    const std::vector<std::string> names = stage_names(observe_generation({}));
    for (const char* const expected :
         {"order-parents", "breed", "filter-fallback", "restore-elites", "pad",
          "score", "select"}) {
        expect(contains(names, expected),
               "pipeline: the observer should see the " +
                   std::string(expected) + " stage");
    }
    expect(names.size() == 7,
           "pipeline: a generation with no filters should report exactly the "
           "seven fixed stages");
}

TEST(test_pipeline_stages_run_in_dependency_order) {
    const std::vector<std::string> names = stage_names(observe_generation({}));
    expect(index_of(names, "order-parents") < index_of(names, "breed"),
           "pipeline: parents must be ordered before breeding selects from "
           "them");
    expect(index_of(names, "restore-elites") < index_of(names, "pad"),
           "pipeline: elites must be restored before padding, so padding "
           "cannot duplicate its way past target_size first");
    expect(index_of(names, "pad") < index_of(names, "score"),
           "pipeline: the population must be padded before it is scored");
    expect(index_of(names, "score") < index_of(names, "select"),
           "pipeline: selection ranks scored individuals");
}

TEST(test_pipeline_derives_one_stage_per_filter) {
    const std::vector<FilterFunction> filters = {
        make_predicate_filter("keep-all",
                              [](const Specification&) { return true; }),
        make_predicate_filter("also-keep-all",
                              [](const Specification&) { return true; })};
    const std::vector<std::string> names =
        stage_names(observe_generation(filters));
    expect(names.size() == 9,
           "pipeline: two filters should add two stages to the seven fixed "
           "ones, so a filter appears in the stage list without the pipeline "
           "being told about it");
    expect(contains(names, "keep-all") && contains(names, "also-keep-all"),
           "pipeline: each filter stage should carry its filter's name");
    expect(index_of(names, "breed") < index_of(names, "keep-all") &&
               index_of(names, "also-keep-all") <
                   index_of(names, "filter-fallback"),
           "pipeline: filter stages should sit between breeding and the "
           "fallback, in their registration order");
}

TEST(test_pipeline_sizes_track_the_population) {
    const std::vector<StageObservation> seen = observe_generation({});
    // target_size 4 with elitism 1 breeds 3 offspring, restores 1 elite, and
    // pads back to 4.
    expect(stage(seen, "breed").n_out == 3,
           "pipeline: breeding should report the offspring count it produced");
    expect(stage(seen, "restore-elites").n_out == 4,
           "pipeline: restoring one elite should grow the population by one");
    expect(stage(seen, "pad").n_out == k_target_size,
           "pipeline: padding should report the target size");
    expect(stage(seen, "select").n_out == k_target_size,
           "pipeline: selection should report exactly target_size survivors");
}

TEST(test_pipeline_reports_distinct_alongside_size) {
    const std::vector<StageObservation> seen = observe_generation({});
    for (const StageObservation& obs : seen) {
        expect(obs.distinct <= obs.n_out,
               "pipeline: a stage cannot hold more distinct specifications "
               "than it holds specifications");
    }
    expect(stage(seen, "order-parents").distinct == 4,
           "pipeline: four distinct parents should be reported as four");
}

TEST(test_pipeline_distinct_sees_through_a_duplicated_population) {
    // The real generation 0: original_population seeds the run with
    // population_size byte-identical copies of the input specification, so
    // every size the pipeline reports is 4 while the population holds one
    // specification. distinct is the only field that can tell those apart.
    const std::vector<StageObservation> seen = observe_generation(
        {}, std::vector<Specification>(4, distinct_specs().front()));
    expect(stage(seen, "order-parents").n_out == 4 &&
               stage(seen, "order-parents").distinct == 1,
           "pipeline: a population of four copies of one specification should "
           "report size 4 and one distinct individual");
}

FilterFunction reject_all(const std::string& name, FilterKind kind) {
    return make_predicate_filter(
        name, [](const Specification&) { return false; }, 1, kind);
}

TEST(test_pipeline_filter_fallback_restores_preference_rejects) {
    const std::vector<FilterFunction> filters = {
        reject_all("prefer-none", FilterKind::Preference)};
    const std::vector<StageObservation> seen = observe_generation(filters);
    expect(stage(seen, "prefer-none").n_out == 0,
           "pipeline: the filter should reject every offspring");
    expect(stage(seen, "filter-fallback").n_in == 0 &&
               stage(seen, "filter-fallback").n_out == 3,
           "pipeline: the fallback should restore the offspring a preference "
           "filter emptied, since nothing about them is unfit to breed from");
}

TEST(test_pipeline_filter_fallback_withholds_correctness_rejects) {
    const std::vector<FilterFunction> filters = {
        reject_all("not-correct", FilterKind::Correctness)};
    const std::vector<StageObservation> seen = observe_generation(filters);
    expect(stage(seen, "filter-fallback").n_out == 0,
           "pipeline: the fallback must not re-admit offspring a correctness "
           "filter rejected, which is what it exists to enforce");
    expect(stage(seen, "restore-elites").n_out == k_elitism_size &&
               stage(seen, "pad").n_out == k_target_size,
           "pipeline: with no correct offspring the elites should carry the "
           "generation on their own, and padding should still reach the "
           "target size");
}

TEST(test_pipeline_filter_fallback_rescreens_the_rescued_offspring) {
    // The preference filter empties the population before the correctness
    // filter judges a single candidate, so every call it records is one the
    // rescue made.
    std::size_t calls = 0;
    const std::vector<FilterFunction> filters = {
        reject_all("prefer-none", FilterKind::Preference),
        make_predicate_filter(
            "correct-all",
            [&calls](const Specification&) {
                ++calls;
                return true;
            },
            1, FilterKind::Correctness)};
    const std::vector<StageObservation> seen = observe_generation(filters);
    expect(stage(seen, "correct-all").n_in == 0,
           "pipeline: the correctness filter should see nothing in the main "
           "pass, the preference filter having emptied the population first");
    expect(calls == 3,
           "pipeline: the rescue should re-apply the correctness filter to "
           "each of the three unfiltered offspring, which the chain never "
           "judged");
    expect(stage(seen, "filter-fallback").n_out == 3,
           "pipeline: offspring that pass the re-applied correctness filter "
           "should be restored");
}

TEST(test_pipeline_filter_fallback_restores_offspring_without_elites) {
    const std::vector<FilterFunction> filters = {
        reject_all("not-correct", FilterKind::Correctness)};
    const std::vector<StageObservation> seen =
        observe_generation(filters, distinct_specs(), 0);
    expect(stage(seen, "filter-fallback").n_out == k_target_size,
           "pipeline: with no elites to fall back on, an empty rescue must "
           "still restore the offspring, an empty population being one the "
           "run cannot continue from");
}

}  // namespace
