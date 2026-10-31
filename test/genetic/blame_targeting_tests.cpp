// The refused guarantees the MRS walk reports, the fitness cache that carries
// them to breeding, and the slot choice Config::blame_targeted_slots makes
// from them.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.hpp"
#include "fitness/function.hpp"
#include "fitness/status.hpp"
#include "fixtures.hpp"
#include "genetic/generation.hpp"
#include "genetic/mutation.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "blame_targeting";

// Slot 0 is a tombstone, so walk positions and slots disagree by one. Of the
// three live guarantees the first two are jointly realizable and the third
// breaks them:
//   G(i -> o)  -- mirror the input
//   G F o      -- o must hold infinitely often
//   G(o -> i)  -- o only where i already holds
// The environment plays i false forever, so the walk keeps slots 1 and 2 and
// refuses slot 3.
Specification conflicting_spec() {
    Requirement removed(Formula("i"), Formula("!o"), timing::immediately());
    removed.m_removed = true;
    return Specification(
        {},
        {removed,
         Requirement(Formula("i"), Formula("o"), timing::immediately()),
         Requirement(Formula("true"), Formula("o"), timing::eventually()),
         Requirement(Formula("o"), Formula("i"), timing::immediately())},
        {"i"}, {"o"});
}

// `spec` with only the guarantees at `slots` live.
Specification keeping(const Specification& spec,
                      const std::vector<std::size_t>& slots) {
    std::vector<Requirement> guarantees = spec.m_guarantees;
    for (std::size_t slot = 0; slot < guarantees.size(); ++slot) {
        if (std::find(slots.begin(), slots.end(), slot) == slots.end()) {
            guarantees[slot].m_removed = true;
        }
    }
    return Specification(spec.m_assumptions, std::move(guarantees),
                         spec.m_in_atoms, spec.m_out_atoms, spec.m_modes);
}

// --- the walk's refused set ---

TEST(test_mrs_reports_the_complement_of_its_kept_set) {
    // Part 2 conflicts with part 0, so the walk keeps 0, 1 and 3. Under the
    // reversed order it admits 3, 2 and 1 first and refuses 0 instead.
    SatisfiabilityChecker sat;
    const auto oracle = [](const std::vector<std::size_t>& indices) {
        const bool has_zero =
            std::find(indices.begin(), indices.end(), 0) != indices.end();
        const bool has_two =
            std::find(indices.begin(), indices.end(), 2) != indices.end();
        return !(has_zero && has_two);
    };
    // No components, so the tier passes without asking black, whose timeout
    // under a loaded machine would read as an unsatisfiable component.
    std::vector<std::size_t> refused;
    const double score =
        status_score_mrs({}, 4, sat, oracle, {}, {}, 0, &refused);
    std::string seen;
    for (const std::size_t part : refused) {
        seen += std::to_string(part) + " ";
    }
    expect(score == 0.75 && refused == std::vector<std::size_t>{2},
           "mrs refused: index order should refuse exactly part 2, got " +
               std::to_string(score) + " refusing " + seen);
    const double reversed =
        status_score_mrs({}, 4, sat, oracle, {3, 2, 1, 0}, {}, 0, &refused);
    expect(reversed == 0.75 && refused == std::vector<std::size_t>{0},
           "mrs refused: the reversed order should refuse exactly part 0");

    refused = {7};
    status_score_mrs({"p & !p"}, 4, sat, oracle, {}, {}, 0, &refused);
    expect(refused.empty(),
           "mrs refused: a failed component runs no walk, so nothing is "
           "refused");
}

TEST(test_mrs_refused_slots_match_the_kept_set) {
    // Checked against the kept set rather than against a pinned answer: the
    // kept guarantees together are realizable, and each refused one added to
    // them is not, which down-closure turns into a sound test of the walk.
    SatisfiabilityChecker sat;
    RealizabilityChecker real;
    const Specification spec = conflicting_spec();
    std::vector<std::size_t> refused;
    const double score =
        specification_status(spec, sat, real, StatusGrading::Mrs, {},
                             ComponentCheck::Included, 0, &refused);
    expect(refused == std::vector<std::size_t>{3},
           "refused slots: the walk should refuse slot 3, named as a slot of "
           "m_guarantees rather than as walk position 2");

    std::vector<std::size_t> kept;
    for (const std::size_t slot : live_indices(spec.m_guarantees)) {
        if (std::find(refused.begin(), refused.end(), slot) == refused.end()) {
            kept.push_back(slot);
        }
    }
    expect(score == static_cast<double>(kept.size()) / 3.0,
           "refused slots: the kept set should be the fraction the score "
           "reports");
    expect(specification_status(keeping(spec, kept), sat, real) ==
               k_status_realizable,
           "refused slots: the kept guarantees should be realizable together");
    for (const std::size_t slot : refused) {
        std::vector<std::size_t> grown = kept;
        grown.push_back(slot);
        expect(specification_status(keeping(spec, grown), sat, real) <
                   k_status_realizable,
               "refused slots: a refused guarantee added to the kept set "
               "should leave it unrealizable");
    }

    refused = {7};
    specification_status(spec, sat, real, StatusGrading::Aurus, {},
                         ComponentCheck::Included, 0, &refused);
    expect(refused.empty(), "refused slots: the AuRUS ladder reports none");
}

// --- the fitness cache ---

TEST(test_cache_hit_restores_the_refused_set) {
    Config cfg;
    cfg.fitness_weight_semantic = 0.0;
    const Specification spec = conflicting_spec();
    // A guarantee whose response can never hold scores 0 at the component
    // tier, before any walk on the serial path. The split path runs its walk
    // anyway, and must report nothing to match.
    const Specification unsatisfiable(
        {},
        {Requirement(Formula("i"), Formula("o & !o"), timing::immediately()),
         Requirement(Formula("true"), Formula("o"), timing::eventually())},
        {"i"}, {"o"});

    const AggregateWeightedFitnessFunction split =
        get_fitness_function(spec, cfg);
    const std::vector<ScoredSpecification> fresh =
        score_population(cfg, {spec, unsatisfiable}, split);
    const std::vector<ScoredSpecification> cached =
        score_population(cfg, {spec, unsatisfiable}, split);
    expect(fresh.size() == 2 && cached.size() == 2,
           "cache hit: both candidates should score");
    expect(fresh[0].refused_guarantees == std::vector<std::size_t>{3},
           "cache hit: a fresh split-path score should carry the refused slot");
    expect(cached[0].refused_guarantees == fresh[0].refused_guarantees &&
               cached[1].refused_guarantees == fresh[1].refused_guarantees,
           "cache hit: a cached score should restore the refused set a fresh "
           "walk gave");
    expect(fresh[1].refused_guarantees.empty(),
           "cache hit: a candidate failing a component should carry no "
           "refused slot on the split path");

    // A second function shares no cache with the first, so this is the serial
    // path's own walk.
    const AggregateWeightedFitnessFunction serial =
        get_fitness_function(spec, cfg);
    expect(serial.record_and_fitness(spec).first.refused ==
                   fresh[0].refused_guarantees &&
               serial.record_and_fitness(unsatisfiable).first.refused.empty(),
           "cache hit: the serial path should report what the split path "
           "does");
}

// --- slot choice ---

// The original has two assumptions and three guarantees. The candidate
// rewrote assumption 1 and guarantee 0 and appended an assumption, so its
// global slots run 0 to 2 over assumptions and 3 to 5 over guarantees.
struct SlotFixture {
    Specification original;
    Specification candidate;
};

SlotFixture slot_fixture() {
    const std::vector<std::string> inputs = {"a", "b"};
    const std::vector<std::string> outputs = {"x", "y"};
    const Specification original(
        {make_req("a", "b", timing::eventually()),
         make_req("b", "a", timing::eventually())},
        {make_req("a", "x"), make_req("b", "y"), make_req("a", "y")}, inputs,
        outputs);
    const Specification candidate({make_req("a", "b", timing::eventually()),
                                   make_req("b", "!a", timing::eventually()),
                                   make_req("true", "a", timing::eventually())},
                                  {make_req("a", "x", timing::within_ticks(2)),
                                   make_req("b", "y"), make_req("a", "y")},
                                  inputs, outputs);
    return {original, candidate};
}

TEST(test_targeted_slots_follow_the_parent_status) {
    const SlotFixture fixture = slot_fixture();
    const std::vector<std::size_t> weakenable = {0, 1, 2, 3, 4, 5};

    ParentContext unrealizable;
    unrealizable.status = 2.0 / 3.0;
    unrealizable.refused_guarantees = {1};
    expect(
        targeted_mutation_slots(fixture.candidate, weakenable, unrealizable) ==
            std::vector<std::size_t>{0, 1, 2, 4},
        "targeted slots: an unrealizable parent should keep every "
        "assumption and the refused guarantee");

    ParentContext realizable;
    realizable.status = k_status_realizable;
    realizable.realizable = true;
    realizable.original = &fixture.original;
    expect(targeted_mutation_slots(fixture.candidate, weakenable, realizable) ==
               std::vector<std::size_t>{1, 2, 3},
           "targeted slots: a realizable parent should keep the slots that "
           "differ from the original, an appended assumption among them");

    expect(
        targeted_mutation_slots(fixture.candidate, {0, 2, 3, 5},
                                unrealizable) == std::vector<std::size_t>{0, 2},
        "targeted slots: a refused guarantee that is not weakenable should "
        "not be drawn");

    ParentContext crossed = unrealizable;
    crossed.crossed = true;
    ParentContext unscored;
    unscored.refused_guarantees = {1};
    ParentContext ungraded = unrealizable;
    ungraded.refused_guarantees.clear();
    ParentContext no_original = realizable;
    no_original.original = nullptr;
    for (const ParentContext& context :
         {crossed, unscored, ungraded, no_original}) {
        expect(targeted_mutation_slots(fixture.candidate, weakenable, context)
                   .empty(),
               "targeted slots: a crossover offspring, a parent without a "
               "status, one the walk never graded, or a realizable one "
               "without an original should leave the list alone");
    }
}

TEST(test_mutation_draws_only_targeted_slots) {
    const SlotFixture fixture = slot_fixture();
    Config cfg;
    cfg.blame_targeted_slots = true;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 0.0;
    ParentContext parent;
    parent.status = 2.0 / 3.0;
    parent.refused_guarantees = {2};
    // Every guarantee slot but 2 is outside the targeted list, so a draw
    // landing on any other guarantee would show the filter was ignored.
    for (std::size_t seed = 0; seed < 40; ++seed) {
        const Specification mutated = mutate_specification(
            fixture.candidate, make_random_source_from_seed(seed), cfg,
            &parent);
        expect(mutated.m_guarantees[0] == fixture.candidate.m_guarantees[0] &&
                   mutated.m_guarantees[1] == fixture.candidate.m_guarantees[1],
               "targeted mutation: seed " + std::to_string(seed) +
                   " rewrote a guarantee the walk kept");
    }
}

}  // namespace
