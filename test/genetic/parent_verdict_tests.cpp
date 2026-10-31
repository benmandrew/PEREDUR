// The parent-verdict stage (Config::parent_verdict_filter): the origins
// breeding records, the memo lookup that proves a parent unrealizable without
// a query, and the stage that drops proved strengthenings of such a parent.

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.hpp"
#include "filter/implication.hpp"
#include "fixtures.hpp"
#include "genetic/generation.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "runner/spot.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "parent_verdict";

std::vector<std::string> inputs() { return {"a", "b"}; }
std::vector<std::string> outputs() { return {"x", "y"}; }

Specification spec_of(std::vector<Requirement> guarantees) {
    return Specification({}, std::move(guarantees), inputs(), outputs());
}

ScoredSpecification scored(Specification specification, double status) {
    ScoredSpecification entry;
    entry.specification = std::move(specification);
    entry.objectives = {status};
    return entry;
}

// A context holding two parents and five bred offspring, with their origins
// written in by hand so the stage is tested apart from breeding.
struct StageFixture {
    Config cfg;
    std::vector<ScoredSpecification> parents;
    GenerationContext<Specification> ctx;

    StageFixture()
        : parents{scored(spec_of({make_req("a", "x")}), 0.0),
                  scored(spec_of({make_req("b", "y")}), 0.0)},
          ctx(cfg, parents, 2, 0, make_random_source_from_seed(0), nullptr,
              nullptr) {
        ctx.m_sorted_parents = parents;
        const std::vector<Specification> bred = {
            spec_of({make_req("a", "x"), make_req("b", "x")}),    // 0, Stronger
            spec_of({make_req("a", "x", timing::eventually())}),  // 0, Weaker
            spec_of({make_req("b", "y"), make_req("a", "y")}),    // 1, Stronger
            spec_of({make_req("a", "y")}),  // 0, Incomparable
            spec_of({make_req("a", "x"), make_req("b", "x")}),  // repeat of 0
        };
        ctx.m_pre_filter = bred;
        ctx.m_origins = {{0, MoveOrder::Stronger},
                         {0, MoveOrder::Weaker},
                         {1, MoveOrder::Stronger},
                         {0, MoveOrder::Incomparable},
                         {0, MoveOrder::Stronger}};
        // Dedup has already run.
        ctx.m_candidates = {bred[0], bred[1], bred[2], bred[3]};
    }
};

TEST(test_stage_drops_exactly_proved_strengthenings_of_unrealizable_parents) {
    StageFixture fixture;
    // Parent 0 has a decided unrealizable verdict, parent 1 none.
    const Specification unrealizable = fixture.parents[0].specification;
    std::size_t n_asked = 0;
    const ParentVerdictLookup<Specification> verdict =
        [&](const Specification& parent) -> std::optional<bool> {
        n_asked++;
        if (parent == unrealizable) {
            return false;
        }
        return std::nullopt;
    };
    const std::size_t dropped_before = ParentVerdictStats::n_dropped.load();
    const std::size_t unproved_before = ParentVerdictStats::n_unproved.load();
    pipeline_detail::stage_parent_verdict(fixture.ctx, verdict);
    const std::vector<Specification> expected = {fixture.ctx.m_pre_filter[1],
                                                 fixture.ctx.m_pre_filter[2],
                                                 fixture.ctx.m_pre_filter[3]};
    expect(fixture.ctx.m_candidates == expected,
           "the stage should drop the one proved strengthening of the "
           "unrealizable parent and nothing else");
    expect(n_asked == 2, "each parent should be asked at most once, got " +
                             std::to_string(n_asked));
    expect(ParentVerdictStats::n_dropped.load() == dropped_before + 1 &&
               ParentVerdictStats::n_unproved.load() == unproved_before + 1,
           "one drop and one unproved strengthening should be counted");
}

TEST(test_stage_keeps_everything_without_a_decided_false_verdict) {
    for (const std::optional<bool> answer :
         {std::optional<bool>(true), std::optional<bool>()}) {
        StageFixture fixture;
        const std::vector<Specification> before = fixture.ctx.m_candidates;
        pipeline_detail::stage_parent_verdict<Specification>(
            fixture.ctx, [answer](const Specification&) { return answer; });
        expect(fixture.ctx.m_candidates == before,
               "a realizable or undecided parent proves nothing about its "
               "strengthenings");
    }
}

// Recording origins draws nothing and changes no offspring, and each record
// is the parent's index with the classification of what it bred.
TEST(test_breeding_records_origins_without_moving_the_stream) {
    Config cfg;
    cfg.crossover_rate = 0.5;
    cfg.mutation_rate = 1.0;
    std::vector<ScoredSpecification> pop;
    const std::vector<Specification> specs = distinct_specs();
    pop.reserve(specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        // The last parent is realizable by its status, so it is not
        // classified.
        pop.push_back(scored(specs[i], i + 1 == specs.size() ? 1.0 : 0.5));
    }
    constexpr std::size_t k_seed = 7;
    const std::vector<Specification> plain =
        generation_detail::breed_offspring<Specification>(
            cfg, pop, pop.size(), pop.size(),
            make_random_source_from_seed(k_seed), nullptr, nullptr,
            std::size_t{0});
    std::vector<OffspringOrigin> origins;
    const std::vector<Specification> recorded =
        generation_detail::breed_offspring<Specification>(
            cfg, pop, pop.size(), pop.size(),
            make_random_source_from_seed(k_seed), nullptr, nullptr,
            std::size_t{0}, &origins);
    expect(plain == recorded,
           "recording origins must not change what breeding produces");
    expect(origins.size() == recorded.size(),
           "every offspring should have an origin");
    for (std::size_t k = 0; k < origins.size(); ++k) {
        expect(origins[k].parent == k, "offspring k descends from parent k");
        if (k + 1 == origins.size()) {
            expect(!origins[k].order.has_value(),
                   "a parent realizable by its status is not classified");
        } else {
            expect(origins[k].order ==
                       classify_move(pop[k].specification, recorded[k]),
                   "the origin should hold classify_move of the offspring");
        }
    }
}

std::vector<std::string> stage_names(const Config& cfg) {
    const AggregateWeightedFitnessFunction fns = constant_fitness();
    const std::vector<ScoredSpecification> pop =
        score_population(cfg, distinct_specs(), fns);
    std::vector<std::string> seen;
    evolve_generation(
        cfg, pop, 4, 1, fns, {make_dedup_filter<Specification>()},
        make_random_source_from_seed(0), nullptr,
        [&seen](const StageObservation& obs) { seen.push_back(obs.name); });
    return seen;
}

TEST(test_stage_runs_after_dedup_only_when_on) {
    Config cfg;
    const std::vector<std::string> off = stage_names(cfg);
    expect(
        std::find(off.begin(), off.end(), k_parent_verdict_stage) == off.end(),
        "off, the stage should not be in the pipeline");
    cfg.parent_verdict_filter = true;
    const std::vector<std::string> with_stage = stage_names(cfg);
    const auto dedup = std::find(with_stage.begin(), with_stage.end(), "dedup");
    expect(dedup != with_stage.end() && dedup + 1 != with_stage.end() &&
               *(dedup + 1) == k_parent_verdict_stage,
           "on, the stage should run directly after dedup");
}

// Slot 2 conflicts with slot 1 (see blame_targeting_tests.cpp): the
// environment holds i low forever, so G(i -> o), G F o and G(o -> i) cannot
// hold together.
Specification conflicting(bool with_extra) {
    std::vector<Requirement> guarantees = {
        Requirement(Formula("i"), Formula("o"), timing::immediately()),
        Requirement(Formula("true"), Formula("o"), timing::eventually()),
        Requirement(Formula("o"), Formula("i"), timing::immediately())};
    if (with_extra) {
        guarantees.emplace_back(Formula("true"), Formula("o | !o"),
                                timing::immediately());
    }
    return Specification({}, std::move(guarantees), {"i"}, {"o"});
}

TEST(test_parent_verdict_lookup_launches_no_query) {
    RealizabilityChecker checker;
    expect(!checker.proved_unrealizable(conflicting(true)),
           "nothing is proved before any query");
    expect(checker.check_realizability(conflicting(false)) ==
               std::optional<bool>(false),
           "the fixture should be unrealizable");
    const std::size_t misses = RealizabilityChecker::n_cache_misses;
    const std::size_t subsumed = RealizabilityChecker::n_subsumed;
    expect(checker.proved_unrealizable(conflicting(false)),
           "the decided query should be read back");
    expect(checker.proved_unrealizable(conflicting(true)),
           "a strengthening of a decided unrealizable query should be settled "
           "by inclusion");
    expect(RealizabilityChecker::n_cache_misses == misses &&
               RealizabilityChecker::n_subsumed == subsumed,
           "the lookup must neither query nor count");
}

}  // namespace
