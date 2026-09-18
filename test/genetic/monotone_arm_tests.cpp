#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "genetic/mutation.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "fretish_monotone";

const std::vector<std::string>& atom_pool() {
    static const std::vector<std::string> pool = {"a", "b", "c"};
    return pool;
}

// Whether `from` implies `dest` over two lowered requirements, asked as the
// unsatisfiability of `from & !dest`. An ExpectUnsat query is never escalated
// to black, so nullopt here means SPOT ran out of budget; the callers count
// what was answered rather than folding an unanswered query into a verdict,
// since a monotonicity assertion that passes on one asserts nothing.
std::optional<bool> implies(const std::string& from, const std::string& dest) {
    const std::optional<bool> sat = global_sat_checker().check_satisfiability(
        "(" + from + ") & !(" + dest + ")", QueryPolarity::ExpectUnsat);
    if (!sat.has_value()) {
        return std::nullopt;
    }
    return !*sat;
}

// Only the response arm, so a firing draw is unambiguously that one, and the
// monotone rewrite rather than the general one every time.
Config response_only() {
    Config cfg;
    cfg.p_response = 1.0;
    cfg.p_trigger = 0.0;
    cfg.p_timing = 0.0;
    cfg.p_condition_type = 0.0;
    cfg.p_scope = 0.0;
    cfg.p_stop = 0.0;
    cfg.p_monotone = 1.0;
    return cfg;
}

// The same isolation for the stop arm.
Config stop_only() {
    Config cfg = response_only();
    cfg.p_response = 0.0;
    cfg.p_stop = 1.0;
    return cfg;
}

Requirement subject(const Timing& timing, ConditionType condition_type,
                    const Scope& scope) {
    return Requirement(Formula("a & b"), Formula("c"), timing, condition_type,
                       true, false, scope);
}

// The arm's claim is that a rewrite of the response or the condition moves the
// *requirement* the way the caller asked, which rests on a polarity table
// rather than on the rewrite alone: scope_wrap places its argument positively
// in all eight branches, both condition wrappers place the body positively, an
// "only" scope carries the negation of the timing's obligation, and
// `after n ticks` holds its response at both polarities. Checking the table
// against a solver is what separates it being right from it being plausible.
void check_direction(const Requirement& original, Direction direction,
                     const Config& cfg, const std::string& label,
                     std::size_t& answered) {
    for (std::size_t seed = 0; seed < 4; ++seed) {
        const RandomSource rng = make_random_source_from_seed(seed);
        const Requirement mutated = mutate_requirement(
            original, atom_pool(), atom_pool(), direction, {}, {}, rng, cfg);
        if (mutated.m_ltl == original.m_ltl) {
            continue;
        }
        const std::optional<bool> held =
            direction == Direction::Weaken
                ? implies(original.m_ltl, mutated.m_ltl)
                : implies(mutated.m_ltl, original.m_ltl);
        if (!held.has_value()) {
            continue;
        }
        ++answered;
        expect(*held, label);
    }
}

TEST(test_response_arm_moves_the_requirement) {
    const std::vector<Timing> timings = {timing::immediately(),
                                         timing::next_timepoint(),
                                         timing::always(),
                                         timing::eventually(),
                                         timing::within_ticks(2),
                                         timing::until(Formula("b")),
                                         timing::before(Formula("b"))};
    const Config cfg = response_only();
    std::size_t answered = 0;
    for (const Timing& timing : timings) {
        for (const ConditionType condition_type :
             {ConditionType::Continual, ConditionType::Trigger}) {
            const Requirement original =
                subject(timing, condition_type, global_scope());
            check_direction(original, Direction::Weaken, cfg,
                            "monotone arm: the original requirement must imply "
                            "its weakened rewrite",
                            answered);
            check_direction(original, Direction::Strengthen, cfg,
                            "monotone arm: the strengthened rewrite must imply "
                            "the original requirement",
                            answered);
        }
    }
    expect(answered > 20,
           "monotone arm: the implication oracle settled too few of the "
           "queries to have asserted anything, got " +
               std::to_string(answered));
}

// The response sits under a negation in an "only" scope, so the direction the
// rewrite takes has to flip with it. Getting this backwards is silent: the
// rewrite still runs and still returns a comparable formula, pointing the
// wrong way.
TEST(test_only_scope_flips_the_response) {
    const Scope only_in{ScopeKind::OnlyIn, "m"};
    const Config cfg = response_only();
    std::size_t answered = 0;
    for (const Timing& timing :
         {timing::always(), timing::eventually(), timing::until(Formula("b")),
          timing::before(Formula("b"))}) {
        const Requirement original =
            subject(timing, ConditionType::Continual, only_in);
        check_direction(original, Direction::Weaken, cfg,
                        "monotone arm: an only-scoped requirement must imply "
                        "its weakened rewrite",
                        answered);
        check_direction(original, Direction::Strengthen, cfg,
                        "monotone arm: the strengthened rewrite must imply its "
                        "only-scoped original",
                        answered);
    }
    expect(answered > 4,
           "monotone arm: the implication oracle settled too few of the "
           "only-scope queries, got " +
               std::to_string(answered));
}

// The stop occurs positively in `until` and negatively in `before`, and an
// "only" scope swaps the two, so the stop arm's direction depends on both. A
// sign error in either is silent in the same way as the response's.
TEST(test_stop_arm_moves_the_requirement) {
    const Config cfg = stop_only();
    std::size_t answered = 0;
    for (const Scope& scope : {global_scope(), Scope{ScopeKind::OnlyIn, "m"}}) {
        for (const Timing& timing :
             {timing::until(Formula("a")), timing::before(Formula("a"))}) {
            for (const ConditionType condition_type :
                 {ConditionType::Continual, ConditionType::Trigger}) {
                const Requirement original =
                    subject(timing, condition_type, scope);
                check_direction(original, Direction::Weaken, cfg,
                                "stop arm: the original requirement must imply "
                                "its weakened rewrite",
                                answered);
                check_direction(original, Direction::Strengthen, cfg,
                                "stop arm: the strengthened rewrite must imply "
                                "the original requirement",
                                answered);
            }
        }
    }
    expect(answered > 10,
           "stop arm: the implication oracle settled too few of the queries "
           "to have asserted anything, got " +
               std::to_string(answered));
}

// A field whose polarity is indeterminate has to be sat out: no rewrite of it
// says anything about the requirement. expand_after emits
// `!r & X(!r & X(... & X(r)))`, holding the response at both polarities, and a
// trigger's rising edge `(!c & Xc)` does the same to the condition. The check
// is that the general rewriter runs in the arm's place, which it does by
// drawing the stream the arm's absence would draw -- the probability being
// read before the RandomSource is touched, an ineligible field never reaches
// that draw.
void expect_general_rewrite(const Requirement& original, const Config& cfg,
                            Formula Requirement::* field,
                            const std::string& label) {
    const RandomSource rng = make_random_source_from_seed(0);
    const Requirement mutated =
        mutate_requirement(original, atom_pool(), atom_pool(),
                           Direction::Weaken, {}, {}, rng, cfg);

    Config general = cfg;
    general.p_monotone = 0.0;
    const RandomSource same = make_random_source_from_seed(0);
    const Requirement expected =
        mutate_requirement(original, atom_pool(), atom_pool(),
                           Direction::Weaken, {}, {}, same, general);
    expect(mutated.*field == expected.*field, label);
}

TEST(test_after_ticks_declines_the_arm) {
    expect_general_rewrite(
        subject(timing::after_ticks(1), ConditionType::Continual,
                global_scope()),
        response_only(), &Requirement::m_response,
        "monotone arm: `after n ticks` holds its response at both polarities, "
        "so the arm must decline it and the general rewrite must run in its "
        "place");
}

TEST(test_trigger_condition_declines_the_arm) {
    Config cfg = response_only();
    cfg.p_response = 0.0;
    cfg.p_trigger = 1.0;
    expect_general_rewrite(
        subject(timing::always(), ConditionType::Trigger, global_scope()), cfg,
        &Requirement::m_condition,
        "monotone arm: a trigger's rising edge holds its condition at both "
        "polarities, so the arm must decline it");
}

}  // namespace
