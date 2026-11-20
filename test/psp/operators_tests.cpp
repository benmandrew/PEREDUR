// The PSP operators: the order moves (steps_towards, the count chain,
// with_slot_count), the per-slot mutator, crossover, the gutting flag on the
// output, and the determinism of a short run of the breeding operators.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "config.hpp"
#include "fitness/function.hpp"
#include "genetic/order_step.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "prop_formula.hpp"
#include "psp/fitness.hpp"
#include "psp/implication.hpp"
#include "psp/mutation.hpp"
#include "psp/order_moves.hpp"
#include "psp/ordered_language.hpp"
#include "psp/pipeline.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "psp_operators";

using psp::PspScopeKind;
using psp::Requirement;
using psp::RequirementField;
using psp::Specification;

Requirement bounded_response(std::size_t count, const std::string& target,
                             const std::string& opener = "start",
                             const std::string& closer = "stop") {
    return {psp::pattern::BoundedResponse{count},
            PspScopeKind::AfterUntil,
            Formula("req"),
            Formula(target),
            Formula(opener),
            Formula(closer)};
}

// examples/psp-arbiter: two bounded responses that conflict under one
// assumption.
Specification arbiter() {
    return Specification(
        {Requirement(psp::pattern::Universality{}, PspScopeKind::Globally,
                     std::nullopt, Formula("!(start & stop)"))},
        {bounded_response(1, "grant"), bounded_response(1, "!grant")},
        {"req", "start", "stop"}, {"grant"});
}

Specification with_guarantee(const Specification& spec, std::size_t slot,
                             Requirement req) {
    Specification out = spec;
    out.m_guarantees[slot] = std::move(req);
    return Specification(out.m_assumptions, out.m_guarantees, out.m_in_atoms,
                         out.m_out_atoms);
}

// A RandomSource that counts its draws and answers 0 to each.
RandomSource counting_source(std::size_t& n_draws) {
    return RandomSource([&n_draws](std::size_t /*upper_bound*/) {
        ++n_draws;
        return std::size_t{0};
    });
}

Config psp_arms_off() {
    Config cfg;
    cfg.psp_p_pattern = 0.0;
    cfg.psp_p_scope_kind = 0.0;
    cfg.psp_p_count = 0.0;
    cfg.psp_p_p = 0.0;
    cfg.psp_p_s = 0.0;
    cfg.psp_p_q = 0.0;
    cfg.psp_p_r = 0.0;
    return cfg;
}

TEST(psp_count_chain_runs_from_one_count_to_the_other) {
    expect(psp::count_chain(1, 4) == std::vector<std::size_t>{2, 3, 4},
           "count_chain: ascending, the destination last");
    expect(psp::count_chain(4, 1) == std::vector<std::size_t>{3, 2, 1},
           "count_chain: descending, the destination last");
    expect(psp::count_chain(2, 2).empty(), "count_chain: empty when equal");
}

TEST(psp_steps_towards_move_a_count_back) {
    const Specification original = arbiter();
    // Bounded response weakens as its count grows, so count 3 is weaker.
    const Specification current =
        with_guarantee(original, 0, bounded_response(3, "grant"));
    const std::vector<psp::Step> steps = psp::steps_towards(current, original);
    expect(!steps.empty(), "steps_towards: a weakened count has a step back");
    for (const psp::Step& step : steps) {
        expect(
            psp::classify_move(current, step.m_result) == MoveOrder::Stronger,
            "steps_towards: every step is proved stronger");
    }
    const auto pattern_step =
        std::find_if(steps.begin(), steps.end(), [](const psp::Step& step) {
            return step.m_field == RequirementField::Pattern;
        });
    expect(pattern_step != steps.end(),
           "steps_towards: the count moves as a pattern step");
    if (pattern_step != steps.end()) {
        expect(psp::count_step_chain(current, *pattern_step) ==
                   std::vector<std::size_t>{2, 1},
               "count_step_chain: the counts the step crosses, from 3 to 1");
        const std::optional<Specification> halfway = psp::with_slot_count(
            current, StepSide::Guarantee, pattern_step->m_slot, 2);
        expect(halfway.has_value() &&
                   halfway->m_guarantees[0] == bounded_response(2, "grant"),
               "with_slot_count: sets one count, proved stronger");
    }
    expect(
        !psp::with_slot_count(original, StepSide::Guarantee, 0, 3).has_value(),
        "with_slot_count: refuses a count that weakens");
}

TEST(psp_steps_towards_restore_and_drop) {
    const Specification original = arbiter();
    Specification removed = original;
    removed.m_guarantees[1].m_removed = true;
    const std::vector<psp::Step> restore =
        psp::steps_towards(removed, original);
    expect(std::any_of(restore.begin(), restore.end(),
                       [&original](const psp::Step& step) {
                           return step.m_kind == StepKind::Restore &&
                                  step.m_result == original;
                       }),
           "steps_towards: a tombstoned guarantee is restored");

    Specification added = original;
    added.m_assumptions.emplace_back(psp::pattern::Universality{},
                                     PspScopeKind::Globally, std::nullopt,
                                     Formula("!req"));
    const std::vector<psp::Step> drop = psp::steps_towards(added, original);
    expect(std::any_of(drop.begin(), drop.end(),
                       [&original](const psp::Step& step) {
                           return step.m_kind == StepKind::DropAssumption &&
                                  step.m_result == original;
                       }),
           "steps_towards: an appended assumption is dropped");
}

TEST(psp_mutator_draws_nothing_with_every_arm_off) {
    const Specification spec = arbiter();
    const psp::MutationPoolData pools = psp::mutation_pool_data(spec);
    std::size_t n_draws = 0;
    const RandomSource source = counting_source(n_draws);
    const Config cfg = psp_arms_off();
    bool free_rewrite = false;
    const Requirement out =
        psp::mutate_requirement(spec.m_guarantees[0], pools, Direction::Weaken,
                                source, cfg, &free_rewrite);
    expect(n_draws == 0, "mutate_requirement: an arm at 0 costs no draw");
    expect(out == spec.m_guarantees[0],
           "mutate_requirement: no arm on leaves the slot alone");
    expect(!free_rewrite, "mutate_requirement: no arm on rewrites nothing");
}

TEST(psp_mutator_count_arm_weakens_by_count) {
    const Specification spec = arbiter();
    const psp::MutationPoolData pools = psp::mutation_pool_data(spec);
    Config cfg = psp_arms_off();
    cfg.psp_p_count = 1.0;
    for (std::size_t seed = 0; seed < 8; ++seed) {
        const RandomSource source = make_random_source_from_seed(seed);
        const Requirement out =
            psp::mutate_requirement(spec.m_guarantees[0], pools,
                                    Direction::Weaken, source, cfg, nullptr);
        expect(psp::requirement_implies(spec.m_guarantees[0], out),
               "mutate_requirement: a weakening count move is implied");
        expect(psp::pattern_count(out.m_pattern).value_or(0) > 1,
               "mutate_requirement: bounded response weakens upwards");
    }
}

TEST(psp_mutator_monotone_rewrite_follows_polarity) {
    const Specification spec = arbiter();
    const psp::MutationPoolData pools = psp::mutation_pool_data(spec);
    Config cfg = psp_arms_off();
    cfg.psp_p_s = 1.0;
    cfg.p_monotone = 1.0;
    std::size_t n_moved = 0;
    for (std::size_t seed = 0; seed < 16; ++seed) {
        const RandomSource source = make_random_source_from_seed(seed);
        const Requirement out =
            psp::mutate_requirement(spec.m_guarantees[0], pools,
                                    Direction::Weaken, source, cfg, nullptr);
        expect(out.m_pattern == spec.m_guarantees[0].m_pattern &&
                   out.m_scope == spec.m_guarantees[0].m_scope,
               "mutate_requirement: the S arm moves S alone");
        if (!(out == spec.m_guarantees[0])) {
            ++n_moved;
            // formula_implies counts the constants as atoms, so a rewrite to
            // `true` is weaker without the rule proving it.
            if (out.m_s.has_value() &&
                out.m_s->atom_name() != std::optional<std::string>("true")) {
                expect(psp::requirement_implies(spec.m_guarantees[0], out),
                       "mutate_requirement: a monotone S rewrite weakens: " +
                           out.to_string());
            }
        }
    }
    expect(n_moved > 0, "mutate_requirement: the S arm moves S");
}

TEST(psp_crossover_swaps_one_field) {
    const Specification first = arbiter();
    const Specification second =
        with_guarantee(first, 0, bounded_response(4, "grant"));
    bool swapped = false;
    for (std::size_t seed = 0; seed < 32; ++seed) {
        const RandomSource source = make_random_source_from_seed(seed);
        const Specification child =
            psp::crossover_specifications(first, second, source);
        std::size_t n_changed = 0;
        for (std::size_t slot = 0; slot < first.m_guarantees.size(); ++slot) {
            n_changed +=
                child.m_guarantees[slot] == first.m_guarantees[slot] ? 0 : 1;
        }
        n_changed += child.m_assumptions == first.m_assumptions ? 0 : 1;
        expect(n_changed <= 1, "crossover: one slot changes at most");
        swapped = swapped || child.m_guarantees[0] == second.m_guarantees[0];
    }
    expect(swapped, "crossover: some draw takes the donor's count");
}

TEST(psp_empty_scope_is_detected) {
    expect(psp::empties_scope(bounded_response(1, "grant", "start", "start")),
           "empties_scope: after-until with Q implying R never opens");
    expect(psp::empties_scope(
               bounded_response(1, "grant", "start & !start", "stop")),
           "empties_scope: an unsatisfiable Q never opens");
    expect(psp::empties_scope(bounded_response(1, "grant", "false", "stop")),
           "empties_scope: Q rewritten to false never opens");
    expect(!psp::empties_scope(bounded_response(1, "grant")),
           "empties_scope: the arbiter's scope opens");
}

TEST(psp_gutted_repair_is_flagged_not_refused) {
    const TempDir dir("psp_gutting");
    const Specification original = arbiter();
    const Specification gutted = with_guarantee(
        original, 1, bounded_response(1, "!grant", "start & stop", "stop"));
    expect(
        psp::gutted_guarantees(original, gutted) == std::vector<std::size_t>{1},
        "gutted_guarantees: names the slot whose scope was emptied");
    expect(psp::gutted_guarantees(original, original).empty(),
           "gutted_guarantees: the original guts nothing");

    Scored<Specification> scored;
    scored.specification = gutted;
    const AggregateWeightedFitnessFunctionT<Specification> no_objectives(
        std::vector<WeightedFitnessFunctionT<Specification>>{});
    const std::size_t n_gutted = psp::write_repairs(
        {scored}, original, no_objectives, dir.path().string());
    expect(n_gutted == 1, "write_repairs: counts the gutted repair");
    expect(std::filesystem::exists(dir.path() / "repair_0.psp"),
           "write_repairs: a gutted repair is still written");
    const nlohmann::json fitness =
        nlohmann::json::parse(read_text(dir.path() / "repair_0.fitness.json"));
    expect(fitness.at("gutted") == nlohmann::json::array({1}),
           "write_repairs: the fitness record flags the gutted slot");
}

// The breeding operators over a few generations, twice from one seed: the
// same seed must give the same offspring.
std::vector<Specification> short_run(std::size_t seed) {
    Config cfg;
    cfg.p_add_assumption = 0.2;
    cfg.p_remove_guarantee = 0.2;
    cfg.p_monotone = 0.5;
    const RandomSource source = make_random_source_from_seed(seed);
    std::vector<Specification> pop{arbiter()};
    for (std::size_t generation = 0; generation < 6; ++generation) {
        std::vector<Specification> next;
        for (std::size_t i = 0; i < pop.size(); ++i) {
            const Specification& partner = pop[source.next_index(pop.size())];
            Specification child =
                psp::crossover_specifications(pop[i], partner, source);
            next.push_back(psp::mutate_specification(child, source, cfg));
            next.push_back(pop[i]);
        }
        pop = std::move(next);
    }
    return pop;
}

TEST(psp_short_run_is_deterministic) {
    const std::vector<Specification> first = short_run(11);
    const std::vector<Specification> second = short_run(11);
    expect(first == second, "psp operators: one seed breeds the same twice");
    expect(std::any_of(
               first.begin(), first.end(),
               [](const Specification& spec) { return !(spec == arbiter()); }),
           "psp operators: a short run moves something");
}

}  // namespace
