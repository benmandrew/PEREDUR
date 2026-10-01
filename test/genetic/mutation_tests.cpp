#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "config.hpp"
#include "fixtures.hpp"
#include "genetic/mutation.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "mutation";

TEST(test_mutation_with_false_source_leaves_formula_unchanged) {
    const Formula formula("P & Q");
    // P & Q has 3 subformulae; fallback 1 gives next_index(3) = 1 != 0, so no
    // subformula is selected and the formula is left unchanged.
    const Formula mutated = mutate_formula(formula, {}, make_source({}, 1));
    expect(mutated.to_string() == "(P) & (Q)",
           "mutation: source that never selects a subformula should leave "
           "formula unchanged");
}

TEST(test_mutation_renames_atom_to_one_from_atoms_list) {
    // An atom has three moves, rename (0), negate (1) and graft (2), so a zero
    // source selects the subformula and then the rename branch; atoms = {"Q"}
    // so "P" becomes "Q".
    const Formula formula("P");
    const Formula mutated = mutate_formula(formula, {"Q"}, make_source({}, 0U));
    expect(mutated.to_string() == "Q",
           "mutation: move 0 should mutate the atom to one from the provided "
           "atoms list");
}

// The third move on an atom grows it in place: a drawn anchor is joined to it
// under a connective from {and, or, implies, iff}. Without this the only rule
// that could grow a formula fired at a Not node, so guarding a positive
// literal took three chained mutations (see
// experiments/2026-08-14-aurus-h2h/REPORT.md).
TEST(test_mutation_grafts_an_anchor_onto_an_atom) {
    const Formula formula("P");
    // Draws: select subformula (bound 1), move 2 of 3, anchor index 0 of 1,
    // anchor polarity 0 of 2 (positive), connective 0 of 4 (and), anchor-first
    // 1 of 2.
    const Formula mutated =
        mutate_formula(formula, {"Q"}, make_source({0, 2, 0, 0, 0, 1}, 0U));
    expect(mutated.to_string() == "(Q) & (P)",
           "mutation: the graft branch joins a drawn anchor to the atom");
    // The same draws with a negated anchor give the guard shape a minimal
    // guarantee weakening needs.
    const Formula guarded =
        mutate_formula(formula, {"Q"}, make_source({0, 2, 0, 1, 0, 1}, 0U));
    expect(guarded.to_string() == "(!(Q)) & (P)",
           "mutation: the graft branch can draw a negated anchor");
}

// With no atom pool there is no anchor to draw, so the atom keeps its two
// original moves and the graft case is not reachable.
TEST(test_mutation_without_atoms_never_grafts) {
    const Formula formula("P");
    const Formula mutated =
        mutate_formula(formula, {}, make_source({0, 1}, 0U));
    expect(mutated.to_string() == "!(P)",
           "mutation: an empty atom pool leaves rename and negate only");
}

TEST(test_mutation_atom_unchanged_when_no_atoms_provided) {
    // Select the subformula (0), then move 0, the rename; with no atoms to
    // draw from the name is left alone.
    const Formula formula("P");
    const Formula mutated =
        mutate_formula(formula, {}, make_source({0, 0}, 0U));
    expect(mutated.to_string() == "P",
           "mutation: atom name should be left unchanged when atoms list is "
           "empty");
}

TEST(test_mutation_atom_selected_from_atoms_list) {
    // mutation_function consumes next_index(1) = 0 (subformula selected),
    // mutate_atom_formula consumes next_index(3) = 0 → rename branch,
    // mutate_atom_name consumes next_index(3) = 2 → atoms[2] = "c".
    const Formula formula("x");
    const Formula mutated =
        mutate_formula(formula, {"a", "b", "c"}, make_source({0, 0, 2}, 0));
    expect(mutated.to_string() == "c",
           "mutation: atom should be replaced by the atom at the index chosen "
           "by the random source");
}

struct TimingStep {
    const char* m_name = nullptr;
    Timing m_start;
    Direction m_direction;
    std::vector<std::size_t> m_values;
    Timing m_expected;
};

// With an empty donor pool each start has one outcome for a given draw; the
// draw picks among the branches (step, double or halve, then the third).
TEST(test_timing_mutation_single_steps) {
    const Direction weaken = Direction::Weaken;
    const Direction strengthen = Direction::Strengthen;
    const std::vector<TimingStep> steps = {
        {"weaken next-timepoint to within 1",
         timing::next_timepoint(),
         weaken,
         {},
         timing::within_ticks(1)},
        {"weaken immediately to within 1",
         timing::immediately(),
         weaken,
         {},
         timing::within_ticks(1)},
        {"eventually has no weakening",
         timing::eventually(),
         weaken,
         {},
         timing::eventually()},
        {"weaken within 3 by stepping to within 4",
         timing::within_ticks(3),
         weaken,
         {0},
         timing::within_ticks(4)},
        {"weaken within 3 by doubling to within 6",
         timing::within_ticks(3),
         weaken,
         {1},
         timing::within_ticks(6)},
        {"weaken after 3 to within 4",
         timing::after_ticks(3),
         weaken,
         {},
         timing::within_ticks(4)},
        {"strengthen next-timepoint to for 1",
         timing::next_timepoint(),
         strengthen,
         {},
         timing::for_ticks(1)},
        {"strengthen immediately to for 1",
         timing::immediately(),
         strengthen,
         {},
         timing::for_ticks(1)},
        {"always is the top of the order and has no strengthening",
         timing::always(),
         strengthen,
         {},
         timing::always()},
        {"strengthen for 3 by stepping to for 4",
         timing::for_ticks(3),
         strengthen,
         {0},
         timing::for_ticks(4)},
        {"strengthen for 3 by doubling to for 6",
         timing::for_ticks(3),
         strengthen,
         {1},
         timing::for_ticks(6)},
        {"strengthen for 3 by maximising to always",
         timing::for_ticks(3),
         strengthen,
         {2},
         timing::always()},
        {"strengthen within 5 by stepping to within 4",
         timing::within_ticks(5),
         strengthen,
         {0},
         timing::within_ticks(4)},
        {"strengthen within 5 by halving (rounding up) to within 3",
         timing::within_ticks(5),
         strengthen,
         {1},
         timing::within_ticks(3)},
        {"strengthen within 5 by switching to after 4",
         timing::within_ticks(5),
         strengthen,
         {2},
         timing::after_ticks(4)},
    };
    for (const TimingStep& step : steps) {
        const Timing mutated = mutate_timing(step.m_start, step.m_direction, {},
                                             make_source(step.m_values, 0U));
        const std::string label = std::string("timing: ") + step.m_name +
                                  ", got " + to_string(mutated);
        expect(mutated.index() == step.m_expected.index(), label + " (kind)");
        expect(mutated == step.m_expected, label);
    }
}

// With nothing to donate an interior timing, always has no weakening and must
// be left alone rather than acquiring an invented deadline. This was the whole
// of the Always branch until the pool reached it: it weakened to a hard-coded
// `for 10 ticks`, and was frozen rather than given a basis for the count.
TEST(test_timing_weaken_always_without_donor_is_unchanged) {
    const std::vector<Timing> no_donors = {timing::always(),
                                           timing::eventually()};
    for (std::size_t draw = 0; draw < 6; ++draw) {
        const Timing mutated =
            mutate_timing(timing::always(), Direction::Weaken, no_donors,
                          make_source({}, draw));
        expect(std::holds_alternative<timing::Always>(mutated),
               "weaken: always with no donor timing should be unchanged");
    }
    const Timing empty_pool = mutate_timing(timing::always(), Direction::Weaken,
                                            {}, make_source({}, 0U));
    expect(std::holds_alternative<timing::Always>(empty_pool),
           "weaken: always with an empty pool should be unchanged");
}

// The mirror of the Eventually donation, from the other end of the order:
// every quantified donor lends only its tick count, spent as `for n ticks`,
// and Immediately and NextTimepoint lend themselves.
TEST(test_timing_weaken_always_takes_donated_timings) {
    const std::vector<Timing> donors = {
        timing::within_ticks(7),  timing::after_ticks(2), timing::immediately(),
        timing::next_timepoint(), timing::eventually(),   timing::always()};
    std::vector<std::size_t> seen_for_ticks;
    bool seen_immediately = false;
    bool seen_next_timepoint = false;
    for (std::size_t draw = 0; draw < 40; ++draw) {
        const Timing mutated = mutate_timing(
            timing::always(), Direction::Weaken, donors, make_source({}, draw));
        if (const auto* for_ticks = std::get_if<timing::ForTicks>(&mutated)) {
            seen_for_ticks.push_back(for_ticks->m_ticks);
            continue;
        }
        if (std::holds_alternative<timing::Immediately>(mutated)) {
            seen_immediately = true;
            continue;
        }
        if (std::holds_alternative<timing::NextTimepoint>(mutated)) {
            seen_next_timepoint = true;
            continue;
        }
        fail(
            "weaken: always should only take for-ticks, immediately or "
            "next-timepoint from the donor pool");
    }
    for (std::size_t ticks : seen_for_ticks) {
        expect(ticks == 7 || ticks == 2,
               "weaken: a donated tick count must come from the pool");
    }
    expect(!seen_for_ticks.empty() && seen_immediately && seen_next_timepoint,
           "weaken: all three donor kinds should be reachable across 40 draws");
}

// `after n` forbids the response at exactly the ticks Always demands it, so it
// is incomparable rather than weaker and must never be taken whole; the same
// donor's count spent as `for n` is a genuine weakening. `within n` is one too,
// but a donated count keeps a single spelling on both sides of the order.
TEST(test_timing_weaken_always_never_becomes_within_or_after) {
    const std::vector<Timing> donors = {timing::within_ticks(4),
                                        timing::after_ticks(9)};
    for (std::size_t draw = 0; draw < 40; ++draw) {
        const Timing mutated = mutate_timing(
            timing::always(), Direction::Weaken, donors, make_source({}, draw));
        expect(!std::holds_alternative<timing::WithinTicks>(mutated) &&
                   !std::holds_alternative<timing::AfterTicks>(mutated),
               "weaken: always must never take a donor's kind, only its count");
    }
}

// With nothing to donate a tick count, eventually has no strengthening and
// must be left alone rather than acquiring an invented deadline.
TEST(test_timing_strengthen_eventually_without_donor_is_unchanged) {
    const std::vector<Timing> no_donors = {timing::eventually(),
                                           timing::always()};
    for (std::size_t draw = 0; draw < 6; ++draw) {
        const Timing mutated =
            mutate_timing(timing::eventually(), Direction::Strengthen,
                          no_donors, make_source({}, draw));
        expect(std::holds_alternative<timing::Eventually>(mutated),
               "strengthen: eventually with no donor timing should be "
               "unchanged");
    }
}

// Every quantified donor lends only its tick count, spent as `for n ticks` —
// never `within n`. Immediately and NextTimepoint lend themselves.
TEST(test_timing_strengthen_eventually_takes_donated_timings) {
    const std::vector<Timing> donors = {
        timing::within_ticks(7),  timing::after_ticks(2), timing::immediately(),
        timing::next_timepoint(), timing::eventually(),   timing::always()};
    std::vector<std::size_t> seen_for_ticks;
    bool seen_immediately = false;
    bool seen_next_timepoint = false;
    for (std::size_t draw = 0; draw < 40; ++draw) {
        const Timing mutated =
            mutate_timing(timing::eventually(), Direction::Strengthen, donors,
                          make_source({}, draw));
        if (const auto* for_ticks = std::get_if<timing::ForTicks>(&mutated)) {
            seen_for_ticks.push_back(for_ticks->m_ticks);
            continue;
        }
        if (std::holds_alternative<timing::Immediately>(mutated)) {
            seen_immediately = true;
            continue;
        }
        if (std::holds_alternative<timing::NextTimepoint>(mutated)) {
            seen_next_timepoint = true;
            continue;
        }
        fail(
            "strengthen: eventually should only take for-ticks, immediately or "
            "next-timepoint from the donor pool");
    }
    for (std::size_t ticks : seen_for_ticks) {
        expect(ticks == 7 || ticks == 2,
               "strengthen: a donated tick count must come from the pool");
    }
    expect(!seen_for_ticks.empty() && seen_immediately && seen_next_timepoint,
           "strengthen: all three donor kinds should be reachable across 40 "
           "draws");
}

// The whole point of drawing from a pool: a spec with no quantified timing
// anywhere cannot invent one.
TEST(test_timing_strengthen_eventually_never_becomes_within) {
    const std::vector<Timing> donors = {timing::within_ticks(4),
                                        timing::for_ticks(9)};
    for (std::size_t draw = 0; draw < 40; ++draw) {
        const Timing mutated =
            mutate_timing(timing::eventually(), Direction::Strengthen, donors,
                          make_source({}, draw));
        expect(!std::holds_alternative<timing::WithinTicks>(mutated),
               "strengthen: eventually must never become within-ticks, even "
               "when a within-ticks donates the count");
    }
}

TEST(test_timing_strengthen_within_one_tick_becomes_qualitative) {
    // within 1 tick has no numeric room: it steps up to the qualitative pair.
    const Timing one =
        mutate_timing(timing::within_ticks(1), Direction::Strengthen, {},
                      make_source({}, 0U));
    expect(
        std::holds_alternative<timing::Immediately>(one) ||
            std::holds_alternative<timing::NextTimepoint>(one),
        "strengthen: within 1 tick should become immediately/next-timepoint");
}

// `after n` pins the response to exactly tick n+1 and forbids it before, so
// `after n-1` and `always` contradict it rather than strengthen it. It has no
// strengthening and must be returned unchanged.
TEST(test_timing_strengthen_after_ticks_is_unchanged) {
    for (std::size_t ticks : {std::size_t{1}, std::size_t{5}}) {
        for (std::size_t draw = 0; draw < 6; ++draw) {
            const Timing mutated =
                mutate_timing(timing::after_ticks(ticks), Direction::Strengthen,
                              {}, make_source({}, draw));
            const auto* after = std::get_if<timing::AfterTicks>(&mutated);
            expect(after != nullptr && after->m_ticks == ticks,
                   "strengthen: after-ticks has no strengthening and should be "
                   "unchanged");
        }
    }
}

// Neither direction may cross to the opposite extreme of the order: only a
// spec that already sits at an extreme may come back out of the mutator still
// sitting there. Sweeping the random source over both an empty and a populated
// donor pool exercises every branch of both directions, the two extremes moving
// only when the pool has something to lend them.
TEST(test_timing_mutation_directions_are_monotone) {
    const std::vector<Timing> starts = {timing::immediately(),
                                        timing::next_timepoint(),
                                        timing::always(),
                                        timing::eventually(),
                                        timing::for_ticks(1),
                                        timing::for_ticks(4),
                                        timing::within_ticks(1),
                                        timing::within_ticks(4),
                                        timing::after_ticks(1),
                                        timing::after_ticks(4),
                                        timing::until(Formula("s")),
                                        timing::before(Formula("t"))};
    const std::vector<std::vector<Timing>> pools = {{}, starts};
    for (const std::vector<Timing>& pool : pools) {
        for (const Timing& start : starts) {
            for (std::size_t draw = 0; draw < 12; ++draw) {
                const Timing stronger = mutate_timing(
                    start, Direction::Strengthen, pool, make_source({}, draw));
                expect(!std::holds_alternative<timing::Eventually>(stronger) ||
                           std::holds_alternative<timing::Eventually>(start),
                       "strengthen: no branch may fall to the bottom of the "
                       "order");
                expect(!std::holds_alternative<timing::Eventually>(start) ||
                           timing_stop(stronger) == nullptr,
                       "strengthen: eventually must not move to a stop timing, "
                       "which does not imply it");
                const Timing weaker = mutate_timing(
                    start, Direction::Weaken, pool, make_source({}, draw));
                expect(!std::holds_alternative<timing::Always>(weaker) ||
                           std::holds_alternative<timing::Always>(start),
                       "weaken: no branch may rise to the top of the order");
            }
        }
    }
}

// `until s` strengthens to Always, the one kind above it, and weakens only
// through its stop; `before s` is incomparable with every kind. Always weakens
// into `until s` for any stop the pool carries, after the candidates a
// stop-free pool already lends, so the index a draw lands on is unchanged for
// those.
TEST(test_timing_stop_edges) {
    const Timing until_s = timing::until(Formula("s"));
    const Timing before_t = timing::before(Formula("t"));
    for (std::size_t draw = 0; draw < 4; ++draw) {
        expect(std::holds_alternative<timing::Always>(mutate_timing(
                   until_s, Direction::Strengthen, {}, make_source({}, draw))),
               "strengthen: until s should move to always");
        expect(mutate_timing(until_s, Direction::Weaken, {until_s},
                             make_source({}, draw)) == until_s,
               "weaken: until s has no weaker kind and should be unchanged");
        for (const Direction direction :
             {Direction::Strengthen, Direction::Weaken}) {
            expect(mutate_timing(before_t, direction, {before_t},
                                 make_source({}, draw)) == before_t,
                   "timing: before t is incomparable with every kind and "
                   "should be unchanged");
        }
    }
    const std::vector<Timing> pool = {until_s, before_t, timing::for_ticks(2)};
    const std::vector<Timing> expected = {timing::for_ticks(2), until_s,
                                          timing::until(Formula("t"))};
    for (std::size_t draw = 0; draw < expected.size(); ++draw) {
        expect(mutate_timing(timing::always(), Direction::Weaken, pool,
                             make_source({draw}, 0)) == expected[draw],
               "weaken: always should draw from for 2, then until s and until "
               "t, in that order");
        expect(mutate_timing(timing::eventually(), Direction::Strengthen, pool,
                             make_source({draw}, 0)) == timing::for_ticks(2),
               "strengthen: eventually should draw only the stop-free for 2");
    }
}

// A two-guarantee specification with p_remove_guarantee forced to 1: the first
// action tombstones a guarantee. The slot must survive, because everything
// comparing this candidate against the original pairs requirements by position.
TEST(test_remove_guarantee_tombstones_in_place) {
    const Specification spec(
        {},
        {Requirement(Formula("a"), Formula("b"), timing::immediately()),
         Requirement(Formula("c"), Formula("d"), timing::immediately())},
        {"a", "c"}, {"b", "d"});
    Config cfg;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 1.0;
    const Specification result =
        mutate_specification(spec, make_source({}, 0), cfg);
    expect(result.m_guarantees.size() == spec.m_guarantees.size(),
           "remove-guarantee: the slot is kept, so the list does not shrink");
    expect(count_live(result.m_guarantees) == 1,
           "remove-guarantee: exactly one guarantee is deleted");
    expect(
        result.m_guarantees[0].m_removed && !result.m_guarantees[1].m_removed,
        "remove-guarantee: a zero-yielding source deletes the first slot");
    expect(
        result.m_guarantees[0].m_condition == spec.m_guarantees[0].m_condition,
        "remove-guarantee: the deleted requirement keeps its content");
    expect(result.m_guarantees[1] == spec.m_guarantees[1],
           "remove-guarantee: later guarantees do not shift");
}

TEST(test_remove_guarantee_keeps_the_last_live_one) {
    const Specification spec(
        {}, {Requirement(Formula("a"), Formula("b"), timing::immediately())},
        {"a"}, {"b"});
    Config cfg;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 1.0;
    const Specification result =
        mutate_specification(spec, make_source({}, 0), cfg);
    expect(count_live(result.m_guarantees) == 1,
           "remove-guarantee: the only guarantee is never deleted");
}

// The floor counts live guarantees rather than removable ones, so the sole
// weakenable guarantee may go while a locked one still holds the specification
// up.
TEST(test_remove_guarantee_may_take_the_only_weakenable_one) {
    const Specification spec(
        {},
        {Requirement(Formula("a"), Formula("b"), timing::immediately(),
                     ConditionType::Continual, /*weakenable=*/false),
         Requirement(Formula("c"), Formula("d"), timing::immediately())},
        {"a", "c"}, {"b", "d"});
    Config cfg;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 1.0;
    const Specification result =
        mutate_specification(spec, make_source({}, 0), cfg);
    expect(
        !result.m_guarantees[0].m_removed && result.m_guarantees[1].m_removed,
        "remove-guarantee: a locked guarantee is never the one deleted");
}

TEST(test_remove_guarantee_never_deletes_a_locked_guarantee) {
    const Specification spec(
        {},
        {Requirement(Formula("a"), Formula("b"), timing::immediately(),
                     ConditionType::Continual, /*weakenable=*/false),
         Requirement(Formula("c"), Formula("d"), timing::immediately(),
                     ConditionType::Continual, /*weakenable=*/false)},
        {"a", "c"}, {"b", "d"});
    Config cfg;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 1.0;
    const Specification result =
        mutate_specification(spec, make_source({}, 0), cfg);
    expect(count_live(result.m_guarantees) == 2,
           "remove-guarantee: nothing is deleted when every guarantee is "
           "locked");
}

// A zero-yielding source makes every probability test pass, so this pins that
// the guard is on the configured probability rather than on the draw. The
// operator drawing nothing at all when off is what keeps the determinism
// goldens valid, and those cover it.
TEST(test_remove_guarantee_disabled_by_zero_probability) {
    const Specification spec(
        {},
        {Requirement(Formula("a"), Formula("b"), timing::immediately()),
         Requirement(Formula("c"), Formula("d"), timing::immediately())},
        {"a", "c"}, {"b", "d"});
    Config cfg;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 0.0;
    const Specification result =
        mutate_specification(spec, make_source({}, 0), cfg);
    expect(count_live(result.m_guarantees) == 2,
           "remove-guarantee: none deleted when p_remove_guarantee is zero");
}

TEST(test_mutation_all_locked_is_noop) {
    const Specification spec(
        {},
        {Requirement(Formula("a"), Formula("b"), timing::immediately(),
                     ConditionType::Continual, false)},
        {"a"}, {"b"});
    Config cfg;
    cfg.p_add_assumption = 0.0;  // isolate the requirement-rewrite path
    cfg.p_remove_guarantee = 0.0;
    const Specification result =
        mutate_specification(spec, make_source({}, 0), cfg);
    expect(result == spec,
           "mutation: a spec whose only requirement is non-weakenable is "
           "returned unchanged");
}

TEST(test_mutation_skips_non_weakenable_requirement) {
    // guarantees[0] is locked, guarantees[1] is weakenable. Only index 1 is
    // eligible, so the forced timing mutation must land on the weakenable
    // requirement and leave the locked one untouched.
    const Requirement locked(Formula("a"), Formula("b"), timing::immediately(),
                             ConditionType::Continual, false);
    const Requirement weak(Formula("c"), Formula("d"), timing::immediately(),
                           ConditionType::Continual, true);
    const Specification spec({}, {locked, weak}, {"a", "c"}, {"b", "d"});
    Config cfg;
    cfg.p_add_assumption = 0.0;  // isolate the requirement-rewrite path
    cfg.p_remove_guarantee = 0.0;
    cfg.p_response = 0.0;
    cfg.p_trigger = 0.0;
    cfg.p_timing = 1.0;
    const Specification result =
        mutate_specification(spec, make_source({}, 0), cfg);
    expect(result.m_guarantees.size() == 2,
           "mutation: guarantee count should be preserved");
    expect(std::holds_alternative<timing::Immediately>(
               result.m_guarantees[0].m_timing) &&
               !result.m_guarantees[0].m_weakenable,
           "mutation: the non-weakenable requirement must be left untouched");
    const auto* within =
        std::get_if<timing::WithinTicks>(&result.m_guarantees[1].m_timing);
    expect(within != nullptr && within->m_ticks == 1,
           "mutation: the weakenable requirement must be the one mutated");
}

// The direction is chosen per requirement list, not per specification: an
// assumption and a guarantee mutated in the same run must move opposite ways.
// `within 4 ticks` is the discriminator — weakening only ever grows the
// deadline or drops to `eventually`, strengthening only ever shrinks it, moves
// to `after`, or rises to the qualitative timings.
TEST(test_assumption_and_guarantee_timings_move_opposite_ways) {
    const Requirement req(Formula("true"), Formula("a"),
                          timing::within_ticks(4), ConditionType::Continual,
                          true);
    Config cfg;
    cfg.p_response = 0.0;
    cfg.p_trigger = 0.0;
    cfg.p_timing = 1.0;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 0.0;
    std::size_t n_assumption_moves = 0;
    std::size_t n_guarantee_moves = 0;
    for (std::size_t seed = 0; seed < 200; ++seed) {
        const RandomSource source = make_random_source_from_seed(seed);
        const Specification assumption_side({req}, {}, {"a"}, {"b"});
        const Timing mutated_assumption =
            mutate_specification(assumption_side, source, cfg)
                .m_assumptions[0]
                .m_timing;
        if (!(mutated_assumption == req.m_timing)) {
            ++n_assumption_moves;
            const auto* within =
                std::get_if<timing::WithinTicks>(&mutated_assumption);
            expect(
                within == nullptr || within->m_ticks < 4,
                "direction split: an assumption's within-ticks deadline must "
                "only shrink");
            expect(
                !std::holds_alternative<timing::Eventually>(mutated_assumption),
                "direction split: an assumption must never weaken to "
                "eventually");
        }
        const Specification guarantee_side({}, {req}, {"a"}, {"b"});
        const Timing mutated_guarantee =
            mutate_specification(guarantee_side, source, cfg)
                .m_guarantees[0]
                .m_timing;
        if (!(mutated_guarantee == req.m_timing)) {
            ++n_guarantee_moves;
            const auto* within =
                std::get_if<timing::WithinTicks>(&mutated_guarantee);
            expect(within == nullptr || within->m_ticks > 4,
                   "direction split: a guarantee's within-ticks deadline must "
                   "only grow");
            expect(
                !std::holds_alternative<timing::AfterTicks>(mutated_guarantee),
                "direction split: a guarantee must never move to after-ticks "
                "(only strengthening reaches it)");
        }
    }
    expect(n_assumption_moves > 0 && n_guarantee_moves > 0,
           "direction split: both sides should have been mutated at least once "
           "across 200 seeds");
}

// The pool is collected across the whole specification, so a guarantee's tick
// count can rescue an assumption stuck at eventually — the case that motivates
// drawing from a pool at all, since add_assumption seeds every new assumption
// with eventually.
TEST(test_eventually_assumption_escapes_using_a_guarantee_tick_count) {
    const Requirement assumption(Formula("true"), Formula("a"),
                                 timing::eventually(), ConditionType::Continual,
                                 true);
    const Requirement guarantee(Formula("a"), Formula("b"),
                                timing::within_ticks(6),
                                ConditionType::Continual, false);
    const Specification spec({assumption}, {guarantee}, {"a"}, {"b"});
    Config cfg;
    cfg.p_response = 0.0;
    cfg.p_trigger = 0.0;
    cfg.p_timing = 1.0;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 0.0;
    expect_some_seed(
        200,
        [&](std::size_t seed) {
            const Timing mutated =
                mutate_specification(spec, make_random_source_from_seed(seed),
                                     cfg)
                    .m_assumptions[0]
                    .m_timing;
            const auto* for_ticks = std::get_if<timing::ForTicks>(&mutated);
            if (for_ticks == nullptr) {
                return false;
            }
            expect(
                for_ticks->m_ticks == 6,
                "pool: the only tick count in the spec is the guarantee's 6");
            return true;
        },
        "pool: an eventually assumption should be able to take the "
        "guarantee's tick count as 'for 6 ticks'");
}

// A specification containing no quantified timing anywhere donates nothing, so
// its eventually assumption stays put rather than inventing a deadline.
TEST(test_eventually_assumption_stays_put_without_a_donor) {
    const Requirement assumption(Formula("true"), Formula("a"),
                                 timing::eventually(), ConditionType::Continual,
                                 true);
    const Requirement guarantee(Formula("a"), Formula("b"), timing::always(),
                                ConditionType::Continual, false);
    const Specification spec({assumption}, {guarantee}, {"a"}, {"b"});
    Config cfg;
    cfg.p_response = 0.0;
    cfg.p_trigger = 0.0;
    cfg.p_timing = 1.0;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 0.0;
    for (std::size_t seed = 0; seed < 100; ++seed) {
        const Timing mutated =
            mutate_specification(spec, make_random_source_from_seed(seed), cfg)
                .m_assumptions[0]
                .m_timing;
        expect(std::holds_alternative<timing::Eventually>(mutated),
               "pool: with no donor the assumption must stay at eventually");
    }
}

TEST(test_condition_mutation_never_introduces_output_atom) {
    // Inputs and outputs are disjoint and distinctly named. With p_trigger = 1
    // every mutation rewrites the trigger; across many seeds this exercises
    // both atom renaming and new-atom introduction, none of which may pull an
    // output atom ("B"/"D") into the condition.
    const Requirement guar(Formula("a & c"), Formula("B"), timing::always(),
                           ConditionType::Trigger, true);
    const Specification spec({}, {guar}, {"a", "c"}, {"B", "D"});
    Config cfg;
    cfg.p_response = 0.0;
    cfg.p_trigger = 1.0;
    cfg.p_timing = 0.0;
    for (std::size_t seed = 0; seed < 200; ++seed) {
        const Specification result =
            mutate_specification(spec, make_random_source_from_seed(seed), cfg);
        const std::string condition =
            result.m_guarantees[0].m_condition.to_string();
        expect(condition.find('B') == std::string::npos &&
                   condition.find('D') == std::string::npos,
               "mutation: trigger mutation must never introduce an output atom "
               "into a condition");
    }
}

TEST(test_add_assumption_appends_environment_assumption) {
    // p_add_assumption forced to 1 with a zero-yielding source: the first
    // action appends a fairness assumption over the first input (no negation,
    // since next_bool() is false). next_real() returns 0, which is below the
    // default p_conditional_assumption, so the condition is a drawn atom rather
    // than `true`. It draws the same `a` as the response, so its polarity is
    // flipped to keep `G(a -> F a)` out. The guarantees are left untouched.
    const Specification spec(
        {},
        {Requirement(Formula("a"), Formula("B"), timing::always(),
                     ConditionType::Trigger, true)},
        {"a", "c"}, {"B"});
    Config cfg;
    cfg.p_add_assumption = 1.0;
    const Specification result =
        mutate_specification(spec, make_source({}, 0), cfg);
    expect(result.m_assumptions.size() == 1,
           "add-assumption: a new environment assumption is appended");
    expect(result.m_guarantees.size() == 1 &&
               result.m_guarantees == spec.m_guarantees,
           "add-assumption: guarantees are left untouched");
    const Requirement& added = result.m_assumptions.front();
    expect(added.m_weakenable,
           "add-assumption: the added assumption is weakenable");
    expect(std::holds_alternative<timing::Eventually>(added.m_timing),
           "add-assumption: fairness assumption uses Eventually timing");
    expect(added.m_response.to_string() == "a",
           "add-assumption: response is drawn from the input atoms");
    expect(added.m_condition.to_string() == "!(a)",
           "add-assumption: a condition equal to the response is negated");
    expect(added.m_ltl.find('B') == std::string::npos,
           "add-assumption: an output atom never enters an added assumption");
}

// The condition varies over the atom pool, and `true` stays in the draw so the
// unconditional fairness assumption G F <input> is still reachable.
TEST(test_add_assumption_condition_varies_over_inputs_and_true) {
    const Specification spec(
        {},
        {Requirement(Formula("a"), Formula("B"), timing::always(),
                     ConditionType::Trigger, true)},
        {"a", "c"}, {"B"});
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.p_conditional_assumption = 0.5;
    std::vector<std::string> seen_conditions;
    for (std::size_t seed = 0; seed < 200; ++seed) {
        const Specification result =
            mutate_specification(spec, make_random_source_from_seed(seed), cfg);
        const Requirement& added = result.m_assumptions.front();
        const std::string condition = added.m_condition.to_string();
        expect(condition != "false",
               "add-assumption: the condition must never be false, which would "
               "make the assumption vacuous");
        if (std::find(seen_conditions.begin(), seen_conditions.end(),
                      condition) == seen_conditions.end()) {
            seen_conditions.push_back(condition);
        }
    }
    const auto saw = [&seen_conditions](const std::string& want) {
        return std::find(seen_conditions.begin(), seen_conditions.end(),
                         want) != seen_conditions.end();
    };
    expect(saw("true"),
           "add-assumption: unconditional fairness must stay reachable");
    expect(saw("a") || saw("c"),
           "add-assumption: a plain input condition should be reachable");
    expect(saw("!(a)") || saw("!(c)"),
           "add-assumption: a negated input condition should be reachable");
}

// The condition pool includes outputs, so an added assumption can be guarded
// on an output atom: conditioning on system behaviour adds no obligation the
// system can dodge.
TEST(test_add_assumption_can_reference_output) {
    const Specification spec(
        {},
        {Requirement(Formula("a"), Formula("B"), timing::always(),
                     ConditionType::Trigger, true)},
        {"a", "c"}, {"B", "D"});
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.p_conditional_assumption = 0.5;
    expect_some_seed(
        200,
        [&](std::size_t seed) {
            const std::string ltl =
                mutate_specification(spec, make_random_source_from_seed(seed),
                                     cfg)
                    .m_assumptions.front()
                    .m_ltl;
            return ltl.find('B') != std::string::npos ||
                   ltl.find('D') != std::string::npos;
        },
        "add-assumption: an added assumption can reference an output "
        "atom");
}

// A rewrite draws from the same wider pool as the add, so an existing
// assumption can acquire an output atom.
TEST(test_assumption_rewrite_can_reference_output) {
    const Specification spec(
        {Requirement(Formula("a"), Formula("c"), timing::always(),
                     ConditionType::Trigger, /*weakenable=*/true)},
        {Requirement(Formula("a"), Formula("B"), timing::always(),
                     ConditionType::Trigger, /*weakenable=*/false)},
        {"a", "c"}, {"B", "D"});
    Config cfg;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 0.0;
    cfg.p_response = 1.0;
    cfg.p_trigger = 1.0;
    cfg.p_timing = 0.0;
    expect_some_seed(
        200,
        [&](std::size_t seed) {
            const std::string ltl =
                mutate_specification(spec, make_random_source_from_seed(seed),
                                     cfg)
                    .m_assumptions.front()
                    .m_ltl;
            return ltl.find('B') != std::string::npos ||
                   ltl.find('D') != std::string::npos;
        },
        "assumption rewrite: a rewrite of an existing assumption can "
        "introduce an output atom");
}

// The response of an added assumption is always an input literal. An
// assumption obliging an output, `G(c -> F <output>)`, is one the system
// defeats by never raising the output, and well-separation passes the guarded
// form.
TEST(test_add_assumption_response_is_always_input) {
    const Specification spec(
        {},
        {Requirement(Formula("a"), Formula("B"), timing::always(),
                     ConditionType::Trigger, true)},
        {"a", "c"}, {"B", "D"});
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.p_conditional_assumption = 0.5;
    for (std::size_t seed = 0; seed < 300; ++seed) {
        const Specification result =
            mutate_specification(spec, make_random_source_from_seed(seed), cfg);
        expect(result.m_assumptions.size() == 1,
               "add-assumption: one assumption is appended");
        const std::string response =
            result.m_assumptions.front().m_response.to_string();
        expect(response == "a" || response == "c" || response == "!(a)" ||
                   response == "!(c)",
               "add-assumption: the response must be an input literal, got " +
                   response);
    }
}

// Condition and response are drawn independently, but the tautology
// `G(l -> F l)` never comes out: a condition equal to the response is negated.
TEST(test_add_assumption_never_tautological) {
    const Specification spec(
        {},
        {Requirement(Formula("a"), Formula("B"), timing::always(),
                     ConditionType::Trigger, true)},
        {"a"}, {"B"});
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.p_conditional_assumption = 1.0;
    bool saw_flip_partner = false;
    for (std::size_t seed = 0; seed < 300; ++seed) {
        const Specification result =
            mutate_specification(spec, make_random_source_from_seed(seed), cfg);
        const Requirement& added = result.m_assumptions.front();
        expect(!(added.m_condition == added.m_response),
               "add-assumption: condition and response must differ, got " +
                   added.m_ltl);
        const std::string condition = added.m_condition.to_string();
        const std::string response = added.m_response.to_string();
        saw_flip_partner = saw_flip_partner ||
                           (condition == "!(a)" && response == "a") ||
                           (condition == "a" && response == "!(a)");
    }
    expect(saw_flip_partner, "add-assumption: `G(!l -> F l)` stays reachable");
}

// With no input there is nothing the environment alone can be obliged to do,
// so no assumption is added.
TEST(test_add_assumption_needs_an_input) {
    const Specification spec(
        {},
        {Requirement(Formula("B"), Formula("D"), timing::always(),
                     ConditionType::Trigger, true)},
        {}, {"B", "D"});
    Config cfg;
    cfg.p_add_assumption = 1.0;
    cfg.p_remove_guarantee = 0.0;
    for (std::size_t seed = 0; seed < 50; ++seed) {
        const Specification result =
            mutate_specification(spec, make_random_source_from_seed(seed), cfg);
        expect(result.m_assumptions.empty(),
               "add-assumption: nothing is added to a spec without inputs");
    }
}

TEST(test_add_assumption_disabled_by_zero_probability) {
    const Specification spec(
        {}, {Requirement(Formula("a"), Formula("b"), timing::immediately())},
        {"a"}, {"b"});
    Config cfg;
    cfg.p_add_assumption = 0.0;
    cfg.p_remove_guarantee = 0.0;
    const Specification result =
        mutate_specification(spec, make_source({}, 0), cfg);
    expect(result.m_assumptions.empty(),
           "add-assumption: none added when p_add_assumption is zero");
}

// --- ordered_fields = "uniform" -------------------------------------------

/// Only the arms given a probability fire, and they redraw uniformly.
Config uniform_config(double p_timing, double p_condition_type,
                      double p_scope) {
    Config cfg;
    cfg.ordered_fields = OrderedFieldMutation::Uniform;
    cfg.p_response = 0.0;
    cfg.p_trigger = 0.0;
    cfg.p_stop = 0.0;
    cfg.p_monotone = 0.0;
    cfg.p_timing = p_timing;
    cfg.p_condition_type = p_condition_type;
    cfg.p_scope = p_scope;
    return cfg;
}

/// The distinct values of @p field over @p n_seeds seeded mutations, sorted.
template <typename Field>
std::vector<std::string> redraws(const Requirement& req, Direction direction,
                                 const std::vector<Timing>& pool,
                                 const std::vector<std::string>& modes,
                                 const Config& cfg, Field field,
                                 std::size_t n_seeds = 400) {
    std::vector<std::string> seen;
    for (std::size_t seed = 0; seed < n_seeds; ++seed) {
        const Requirement mutated =
            mutate_requirement(req, {"a", "x"}, {"a"}, direction, pool, modes,
                               make_random_source_from_seed(seed), cfg);
        const std::string value = field(mutated);
        if (std::find(seen.begin(), seen.end(), value) == seen.end()) {
            seen.push_back(value);
        }
    }
    std::sort(seen.begin(), seen.end());
    return seen;
}

std::string render_timing(const Requirement& req) {
    return to_string(req.m_timing);
}

std::string render_scope(const Requirement& req) {
    return to_string(req.m_scope);
}

std::vector<std::string> rendered(const std::vector<Timing>& timings) {
    std::vector<std::string> out;
    out.reserve(timings.size());
    for (const Timing& tim : timings) {
        out.push_back(to_string(tim));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool contains(const std::vector<std::string>& seen, const std::string& want) {
    return std::find(seen.begin(), seen.end(), want) != seen.end();
}

// The redraw reaches exactly the timings the specification can instantiate:
// the qualitative kinds, the quantified kinds at its one count, and `until`
// and `before` at its one stop, less the current value. No count or stop
// appears that the specification does not already use.
TEST(test_uniform_timing_redraws_over_the_specification_vocabulary) {
    const Requirement req(Formula("a"), Formula("x"), timing::within_ticks(3),
                          ConditionType::Continual, true);
    const std::vector<Timing> pool = {timing::within_ticks(3),
                                      timing::until(Formula("a"))};
    const std::vector<std::string> seen =
        redraws(req, Direction::Weaken, pool, {}, uniform_config(1.0, 0.0, 0.0),
                render_timing);
    const std::vector<std::string> want = rendered(
        {timing::immediately(), timing::next_timepoint(), timing::eventually(),
         timing::always(), timing::for_ticks(3), timing::after_ticks(3),
         timing::until(Formula("a")), timing::before(Formula("a"))});
    expect(seen == want,
           "uniform timing: the redraw must reach every instantiable timing "
           "but the current one, and nothing else");
}

// Immediately and NextTimepoint lend count 1, the count the directed arm's
// one-step moves off them reach. The qualitative kinds lend nothing else.
TEST(test_uniform_timing_count_one_comes_from_immediately) {
    const Requirement req(Formula("a"), Formula("x"), timing::eventually(),
                          ConditionType::Continual, true);
    const std::vector<std::string> with_immediately =
        redraws(req, Direction::Weaken, {timing::immediately()}, {},
                uniform_config(1.0, 0.0, 0.0), render_timing);
    expect(with_immediately ==
               rendered({timing::immediately(), timing::next_timepoint(),
                         timing::always(), timing::within_ticks(1),
                         timing::for_ticks(1), timing::after_ticks(1)}),
           "uniform timing: an immediate timing in the pool lends count 1");
    const std::vector<std::string> qualitative_only =
        redraws(req, Direction::Weaken, {timing::always()}, {},
                uniform_config(1.0, 0.0, 0.0), render_timing);
    expect(qualitative_only ==
               rendered({timing::immediately(), timing::next_timepoint(),
                         timing::always()}),
           "uniform timing: with no count or stop in the specification only "
           "the qualitative kinds are reachable");
}

// A guarantee's directed arms only weaken; the uniform ones move it either way
// along each field's order.
TEST(test_uniform_arm_moves_a_guarantee_both_ways) {
    const Requirement timed(Formula("a"), Formula("x"), timing::within_ticks(3),
                            ConditionType::Continual, true);
    const std::vector<std::string> timings =
        redraws(timed, Direction::Weaken, {timing::within_ticks(3)}, {},
                uniform_config(1.0, 0.0, 0.0), render_timing);
    expect(contains(timings, to_string(timing::eventually())) &&
               contains(timings, to_string(timing::always())),
           "uniform timing: a guarantee must reach both a weaker (eventually) "
           "and a stronger (always) timing");

    // Continual implies Trigger, so a guarantee going Trigger -> Continual
    // strengthens, which the directed arm never does.
    Requirement trigger = timed;
    trigger.m_condition_type = ConditionType::Trigger;
    expect(mutate_requirement(trigger, {"a", "x"}, {"a"}, Direction::Weaken, {},
                              {}, make_source({}, 0),
                              uniform_config(0.0, 1.0, 0.0))
                   .m_condition_type == ConditionType::Continual,
           "uniform condition type: a guarantee's trigger must flip to "
           "continual");
    expect(mutate_requirement(timed, {"a", "x"}, {"a"}, Direction::Strengthen,
                              {}, {}, make_source({}, 0),
                              uniform_config(0.0, 1.0, 0.0))
                   .m_condition_type == ConditionType::Trigger,
           "uniform condition type: an assumption's continual must flip to "
           "trigger");

    // `global` implies `except in m`, which implies `before m`.
    Requirement scoped = timed;
    scoped.m_timing = timing::immediately();
    scoped.m_scope = Scope{ScopeKind::NotIn, "m"};
    const std::vector<std::string> scopes =
        redraws(scoped, Direction::Weaken, {}, {"m"},
                uniform_config(0.0, 0.0, 1.0), render_scope);
    expect(contains(scopes, to_string(Scope{})) &&
               contains(scopes, to_string(Scope{ScopeKind::Before, "m"})),
           "uniform scope: a guarantee must reach both a stronger (global) "
           "and a weaker (before) scope");
}

// Every kind, over the declared modes only, less the current value: an `in m1`
// requirement can move to `in m2` but never to a mode nobody declared.
TEST(test_uniform_scope_redraws_over_declared_modes) {
    Requirement req(Formula("a"), Formula("x"), timing::immediately(),
                    ConditionType::Continual, true);
    req.m_scope = Scope{ScopeKind::In, "m1"};
    const std::vector<std::string> modes = {"m1", "m2"};
    const std::vector<std::string> seen =
        redraws(req, Direction::Weaken, {}, modes,
                uniform_config(0.0, 0.0, 1.0), render_scope, 1000);
    std::vector<std::string> want = {to_string(Scope{})};
    for (const ScopeKind kind :
         {ScopeKind::In, ScopeKind::NotIn, ScopeKind::Before, ScopeKind::After,
          ScopeKind::OnlyIn, ScopeKind::OnlyBefore, ScopeKind::OnlyAfter}) {
        for (const std::string& mode : modes) {
            if (kind != ScopeKind::In || mode != "m1") {
                want.push_back(to_string(Scope{kind, mode}));
            }
        }
    }
    std::sort(want.begin(), want.end());
    expect(seen == want,
           "uniform scope: the redraw must reach every scope over the "
           "declared modes but the current one, and nothing else");
}

// With no declared mode a Global requirement has nowhere to go: the scope
// stays Global and the arm spends only its probability draw.
TEST(test_uniform_scope_without_modes_is_unchanged_at_no_draw) {
    const Requirement req(Formula("a"), Formula("x"), timing::immediately(),
                          ConditionType::Continual, true);
    const auto count_draws = [&req](const Config& cfg) {
        std::size_t n_draws = 0;
        const RandomSource source([&n_draws](std::size_t upper_bound) {
            ++n_draws;
            return (n_draws * 7) % upper_bound;
        });
        const Requirement mutated = mutate_requirement(
            req, {"a", "x"}, {"a"}, Direction::Weaken, {}, {}, source, cfg);
        expect(mutated.m_scope.is_global(),
               "uniform scope: with no declared mode the scope stays global");
        return n_draws;
    };
    expect(count_draws(uniform_config(0.0, 0.0, 1.0)) ==
               count_draws(uniform_config(0.0, 0.0, 0.0)) + 1,
           "uniform scope: an empty candidate set must cost no draw beyond "
           "the arm's probability check");
}

// --- ordered_fields = "mixed" ---------------------------------------------

Config ordered_config(OrderedFieldMutation rule, double p_timing,
                      double p_condition_type, double p_scope) {
    Config cfg = uniform_config(p_timing, p_condition_type, p_scope);
    cfg.ordered_fields = rule;
    return cfg;
}

std::string render_condition_type(const Requirement& req) {
    return req.m_condition_type == ConditionType::Trigger ? "trigger"
                                                          : "continual";
}

// A guarantee's directed condition-type arm leaves a trigger a trigger, since
// it can only weaken; the uniform arm flips it to continual. Under mixed both
// outcomes occur across seeds, while each pure rule gives only its own.
TEST(test_mixed_condition_type_takes_both_rules) {
    Requirement req(Formula("a"), Formula("x"), timing::within_ticks(3),
                    ConditionType::Trigger, true);
    const auto seen = [&req](OrderedFieldMutation rule) {
        return redraws(req, Direction::Weaken, {}, {},
                       ordered_config(rule, 0.0, 1.0, 0.0),
                       render_condition_type);
    };
    expect(seen(OrderedFieldMutation::Directed) ==
               std::vector<std::string>{"trigger"},
           "directed condition type: a guarantee's trigger stays a trigger");
    expect(seen(OrderedFieldMutation::Uniform) ==
               std::vector<std::string>{"continual"},
           "uniform condition type: a guarantee's trigger becomes continual");
    expect(seen(OrderedFieldMutation::Mixed) ==
               std::vector<std::string>{"continual", "trigger"},
           "mixed condition type: both the directed and the uniform outcome "
           "should occur across seeds");
}

// The directed timing arm only weakens a guarantee, so `always` is reachable
// from `within 3` only by the uniform rule; mixed reaches it, and also reaches
// every value the directed arm does.
TEST(test_mixed_timing_reaches_both_rules_values) {
    const Requirement req(Formula("a"), Formula("x"), timing::within_ticks(3),
                          ConditionType::Continual, true);
    const std::vector<Timing> pool = {timing::within_ticks(3)};
    const auto seen = [&req, &pool](OrderedFieldMutation rule) {
        return redraws(req, Direction::Weaken, pool, {},
                       ordered_config(rule, 1.0, 0.0, 0.0), render_timing);
    };
    const std::vector<std::string> directed =
        seen(OrderedFieldMutation::Directed);
    const std::vector<std::string> mixed = seen(OrderedFieldMutation::Mixed);
    const std::string always = to_string(timing::always());
    expect(!contains(directed, always) && contains(mixed, always),
           "mixed timing: a guarantee should reach the uniform-only `always`");
    bool covers_directed = true;
    for (const std::string& value : directed) {
        covers_directed = covers_directed && contains(mixed, value);
    }
    expect(covers_directed,
           "mixed timing: every directed outcome should also occur under "
           "mixed");
}

// The coin costs one draw per fired arm under mixed and none otherwise, so the
// directed and uniform streams are the ones they drew before mixed existed.
TEST(test_mixed_coin_is_drawn_only_under_mixed) {
    const Requirement req(Formula("a"), Formula("x"), timing::within_ticks(3),
                          ConditionType::Continual, true);
    const auto count_draws = [&req](const Config& cfg) {
        std::size_t n_draws = 0;
        const RandomSource source([&n_draws](std::size_t upper_bound) {
            ++n_draws;
            return (n_draws * 7) % upper_bound;
        });
        static_cast<void>(mutate_requirement(
            req, {"a", "x"}, {"a"}, Direction::Weaken, {}, {}, source, cfg));
        return n_draws;
    };
    // The condition-type arm spends no draw under either rule, so only its
    // probability check and, under mixed, the coin are counted.
    const std::size_t idle =
        count_draws(ordered_config(OrderedFieldMutation::Directed, 0, 0, 0));
    expect(count_draws(ordered_config(OrderedFieldMutation::Directed, 0, 1,
                                      0)) == idle + 1 &&
               count_draws(ordered_config(OrderedFieldMutation::Uniform, 0, 1,
                                          0)) == idle + 1,
           "ordered fields: directed and uniform should spend no coin");
    expect(count_draws(ordered_config(OrderedFieldMutation::Mixed, 0, 1, 0)) ==
               idle + 2,
           "mixed ordered fields: a fired arm should spend exactly one coin");
    expect(count_draws(ordered_config(OrderedFieldMutation::Mixed, 0, 0, 0)) ==
               idle,
           "mixed ordered fields: an arm that does not fire spends no coin");
}

}  // namespace
