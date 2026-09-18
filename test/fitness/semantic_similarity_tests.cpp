#include <cmath>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.hpp"
#include "fitness/semantic_similarity.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "semantic_similarity";

TEST(test_semantic_similarity_identical_requirements_score_two) {
    const Requirement requirement{Formula("P"), Formula("Q"),
                                  timing::immediately()};
    const double score = semantic_similarity(requirement, requirement, 1);
    expect(std::fabs(score - 1.0) < 1e-12,
           "semantic-similarity: identical requirements should have score 1");
}

TEST(test_semantic_similarity_formula_value_explicit_step_count) {
    const Requirement requirement{Formula("P"), Formula("Q"),
                                  timing::immediately()};
    const Requirement other_requirement{Formula("P"), Formula("P|Q"),
                                        timing::immediately()};
    const double score = semantic_similarity(requirement, other_requirement, 1);
    // (P -> Q) traces are a strict subset of (P -> P|Q) traces, so the
    // conjunction count equals the (P -> Q) count exactly: first == 1.0,
    // second == 0.75, harmonic mean == 2*1.0*0.75/1.75 == 6/7.
    expect(std::fabs(score - (6.0 / 7.0)) < 1e-12,
           "semantic-similarity: expected score 6/7 from formula");
}

TEST(test_semantic_similarity_default_overload_matches_explicit_step_count) {
    const Requirement requirement{Formula("P"), Formula("Q"),
                                  timing::immediately()};
    const Requirement other_requirement{Formula("P"), Formula("P|Q"),
                                        timing::immediately()};
    const double with_default =
        semantic_similarity(requirement, other_requirement, Config{});
    // Pin both the bound and the metric explicitly: the Config overload carries
    // both, so comparing against a bound-only call (which defaults the metric
    // to Direct) would conflate them now that Config defaults the metric to
    // Logarithmic. This isolates the bound-wiring this test is about.
    const double with_explicit = semantic_similarity(
        requirement, other_requirement, Config{}.default_model_counting_bound,
        Config{}.similarity_metric);
    expect(std::fabs(with_default - with_explicit) < 1e-12,
           "semantic-similarity: Config overload should use the config's bound "
           "and metric");
}

TEST(test_semantic_similarity_identical_specifications_score_one) {
    const Specification spec(
        {},
        {Requirement{Formula("P"), Formula("Q"), timing::immediately()},
         Requirement{Formula("P"), Formula("!Q"), timing::immediately()}},
        {"P"}, {"Q"});
    const double score = semantic_similarity(spec, spec, 1);
    expect(std::fabs(score - 1.0) < 1e-12,
           "semantic-similarity: identical specifications should have score 1");
}

// Regression: the p_add_assumption mutation grows a candidate's assumption
// list, so a candidate can be scored against an original with fewer
// assumptions. The specification-level overload used to advance a second
// iterator in lockstep to the first spec's end, running it past the shorter
// spec's assumptions (an assertion in debug builds, undefined behaviour once
// NDEBUG disables it). The score must stay finite, bounded, and independent of
// argument order.
TEST(test_semantic_similarity_differing_assumption_counts) {
    const Requirement req_q{Formula("P"), Formula("Q"), timing::immediately()};
    const Requirement req_not_q{Formula("P"), Formula("!Q"),
                                timing::immediately()};
    const Specification original({req_q}, {req_q}, {"P"}, {"Q"});
    const Specification candidate({req_not_q, req_q}, {req_q}, {"P"}, {"Q"});
    const double candidate_vs_original =
        semantic_similarity(candidate, original, 1);
    const double original_vs_candidate =
        semantic_similarity(original, candidate, 1);
    expect(std::isfinite(candidate_vs_original) &&
               candidate_vs_original >= 0.0 && candidate_vs_original <= 1.0,
           "semantic-similarity: a candidate with an extra assumption must "
           "score a finite, bounded value against the original (no "
           "out-of-bounds read)");
    expect(std::fabs(candidate_vs_original - original_vs_candidate) < 1e-12,
           "semantic-similarity: differing assumption counts score the same in "
           "either argument order");
}

TEST(test_semantic_similarity_specification_averages_requirements) {
    const Requirement req_imm{Formula("P"), Formula("Q"),
                              timing::immediately()};
    const Requirement req_next{Formula("P"), Formula("Q"),
                               timing::next_timepoint()};
    const Requirement req_within{Formula("P"), Formula("Q"),
                                 timing::within_ticks(3)};
    const Requirement req_for{Formula("P"), Formula("Q"), timing::for_ticks(2)};
    const Requirement req_after{Formula("P"), Formula("Q"),
                                timing::after_ticks(2)};
    // req_within is identical in both specs and should be excluded from the
    // average; req_imm/req_next and req_for/req_after differ and should be
    // averaged together.
    const Specification spec1({}, {req_imm, req_within, req_for}, {"P"}, {"Q"});
    const Specification spec2({}, {req_next, req_within, req_after}, {"P"},
                              {"Q"});
    const double score = semantic_similarity(spec1, spec2, 1);
    const double expected = (semantic_similarity(req_imm, req_next, 1) +
                             semantic_similarity(req_for, req_after, 1)) /
                            2.0;
    expect(std::fabs(score - expected) < 1e-12,
           "semantic-similarity: specification score should average only the "
           "requirement pairs that differ, excluding unchanged pairs");
}

// A requirement weakened to a tautology (trigger entails response trivially,
// so it accepts every trace) trivially contains the original's accepting
// set, making the "recall" ratio exactly 1 regardless of how small the
// original's footprint is. Under the old arithmetic mean of the two
// directional ratios this floored the score at 0.5 no matter how vacuous the
// weakening was; the harmonic mean lets a near-zero "precision" ratio pull
// the score down toward 0 instead.
TEST(test_semantic_similarity_tautology_scores_near_zero) {
    const Requirement original{Formula("P"), Formula("Q"),
                               timing::after_ticks(1)};
    const Requirement tautology{Formula("Q"), Formula("Q"),
                                timing::within_ticks(2)};
    const double score = semantic_similarity(tautology, original, 20);
    expect(score < 0.01,
           "semantic-similarity: a requirement weakened to a tautology "
           "should score near 0, not float at the old 0.5 floor");
}

// Two distinct eventually-timing requirements. Their conjunction produces a
// generalized-Buchi automaton; the old HOA parser over-counted accepting states
// and returned similarity > 1. Verifies the result stays in [0, 1].
TEST(test_semantic_similarity_liveness_in_range) {
    const Requirement req_a{Formula("P"), Formula("Q"), timing::eventually()};
    // A, B avoid SPOT's reserved LTL operator letters (e.g. R for Release).
    const Requirement req_b{Formula("A"), Formula("B"), timing::eventually()};
    const double identical = semantic_similarity(req_a, req_a, 4);
    expect(std::fabs(identical - 1.0) < 1e-12,
           "semantic-similarity: identical eventually reqs must score 1");
    const double cross = semantic_similarity(req_a, req_b, 4);
    expect(cross >= 0.0 && cross <= 1.0,
           "semantic-similarity: eventually cross-score must lie in [0, 1]");
}

// A bounded timing (WithinTicks/AfterTicks/...) compiles to a safety automaton
// -- ltl2tgba emits "acc-name: all", and a violating trace is rejected by
// running out of transitions. Eventually compiles to a Buchi automaton, which
// is complete: every trace has a run, and rejection is carried entirely by the
// accepting mask. Counting k-step traces asks the same question of both only
// once k reaches the bounded operand's horizon (ticks + 1). Below it the safety
// automaton has not yet reached its stuck transition, so it still counts traces
// that already missed the deadline -- more than the Buchi side admits for the
// weaker formula it implies. The containment ratio then exceeds 1.
TEST(test_semantic_similarity_bounded_timing_vs_eventually_in_range) {
    const Requirement within5{Formula("true"), Formula("lift_off"),
                              timing::within_ticks(5), ConditionType::Trigger};
    const Requirement eventually{Formula("true"), Formula("lift_off"),
                                 timing::eventually(), ConditionType::Trigger};
    // Spans both sides of the horizon (6): 3-5 are below it, 6-10 at or above.
    for (const std::size_t step_count : {3, 4, 5, 6, 7, 10}) {
        const double score =
            semantic_similarity(within5, eventually, step_count);
        expect(score >= 0.0 && score <= 1.0,
               "semantic-similarity: WithinTicks(5) vs Eventually must stay in "
               "[0, 1] at bound " +
                   std::to_string(step_count) +
                   ", including below the horizon where the safety automaton "
                   "has not yet enforced its deadline");
    }
}

// The same defect at the shipped default_bound of 10: mutate_timing can raise a
// requirement's tick count to 10 or beyond, putting its horizon (11) above the
// bound. Guards against treating the defect as a small-bound curiosity.
TEST(test_semantic_similarity_deep_ticks_at_default_bound_in_range) {
    const Requirement within10{Formula("true"), Formula("lift_off"),
                               timing::within_ticks(10),
                               ConditionType::Trigger};
    const Requirement eventually{Formula("true"), Formula("lift_off"),
                                 timing::eventually(), ConditionType::Trigger};
    const double score = semantic_similarity(
        within10, eventually, Config{}.default_model_counting_bound);
    expect(score >= 0.0 && score <= 1.0,
           "semantic-similarity: a WithinTicks(10) mutant scored against "
           "Eventually at the default bound must stay in [0, 1]");
}

// An atom-rich pair whose horizon (21) sits above the ceiling a 128-bit Count
// imposed: max_representable_step_count clamped to 127/10 == 12, so the bound
// landed below the horizon and reopened the ratio defect documented on
// effective_step_count. long double's 16384-bit exponent range lifts the clamp
// to 1638, letting the bound actually reach the horizon.
TEST(test_semantic_similarity_atom_rich_deep_horizon_in_range) {
    const Formula condition("a0 & a1 & a2 & a3 & a4");
    const Requirement wide{condition, Formula("a5 | a6 | a7 | a8 | a9"),
                           timing::within_ticks(20), ConditionType::Trigger};
    const Requirement narrow{condition, Formula("a5 | a6"),
                             timing::within_ticks(20), ConditionType::Trigger};
    const double self_score = semantic_similarity(wide, wide, 1);
    expect(std::fabs(self_score - 1.0) < 1e-12,
           "semantic-similarity: an atom-rich requirement scored against "
           "itself must be exactly 1 even once the horizon drives the bound "
           "past the old 128-bit clamp");
    const double cross_score = semantic_similarity(wide, narrow, 1);
    expect(cross_score >= 0.0 && cross_score <= 1.0,
           "semantic-similarity: a 10-atom pair with a horizon of 21 must stay "
           "in [0, 1] (got " +
               std::to_string(cross_score) + ")");
}

// A conjunction count is mathematically at most either individual count, but
// once Count is a float and the counts climb past the 64-bit mantissa, a
// rounding error larger than double's precision can leave it slightly greater
// -- driving a directional ratio above 1. Feeding that straight in checks the
// [0, 1] clamp in semantic_similarity_from_counts: without it the harmonic mean
// would exceed 1. The overshoot (2^30 against a 2^70 count) is deliberately
// coarser than double epsilon so it survives ratio_or_throw's cast to double,
// which a mere ulp of long double would not.
TEST(test_semantic_similarity_from_counts_clamps_rounding_overshoot) {
    const Count base = std::ldexp(1.0L, 70);
    // base + 2^30 is exact and strictly greater in long double's 64-bit
    // mantissa (the two differ by 40 bits), so the conjunction count exceeds
    // the individual counts and the directional ratios land just above 1.
    const Count overshoot = base + std::ldexp(1.0L, 30);
    const SemanticSimilarityCounts counts{base, base, overshoot};
    const double score = semantic_similarity_from_counts(counts);
    expect(score >= 0.0 && score <= 1.0,
           "semantic-similarity: a conjunction count rounded above the "
           "individual counts must still yield a score in [0, 1] (got " +
               std::to_string(score) + ")");
    // Both ratios clamp to exactly 1, so the harmonic mean is exactly 1 -- the
    // clamp lands on the boundary, it does not merely bound from above.
    expect(std::fabs(score - 1.0) < 1e-12,
           "semantic-similarity: clamped ratios of 1 must give exactly 1");
}

// Checks that all timing variants produce similarity in [0, 1] when compared
// cross-requirement. This exercises both safety and liveness automaton paths.
TEST(test_semantic_similarity_all_timings_in_range) {
    const Requirement other{Formula("P"), Formula("A"), timing::immediately()};
    const std::vector<std::pair<std::string, Requirement>> cases = {
        {"immediately",
         Requirement(Formula("P"), Formula("Q"), timing::immediately())},
        {"next-timepoint",
         Requirement(Formula("P"), Formula("Q"), timing::next_timepoint())},
        {"within-ticks",
         Requirement(Formula("P"), Formula("Q"), timing::within_ticks(2))},
        {"for-ticks",
         Requirement(Formula("P"), Formula("Q"), timing::for_ticks(2))},
        {"after-ticks",
         Requirement(Formula("P"), Formula("Q"), timing::after_ticks(1))},
        {"eventually",
         Requirement(Formula("P"), Formula("Q"), timing::eventually())},
    };
    for (const auto& [label, req] : cases) {
        const double self_score = semantic_similarity(req, req, 2);
        expect(
            std::fabs(self_score - 1.0) < 1e-12,
            "semantic-similarity: " + label + " identical reqs must score 1");
        const double cross_score = semantic_similarity(req, other, 2);
        expect(
            cross_score >= 0.0 && cross_score <= 1.0,
            "semantic-similarity: " + label + " cross score must be in [0, 1]");
    }
}

// Propositionally-equivalent responses that differ syntactically must produce
// the same semantic similarity when scored against the same original.
// Regression: repair_2 and repair_13 from the takeoff-unfixed-1 run had
// divergent stored scores because the automata happened to differ in an older
// version of requirement_to_ltl; the current code must agree.
TEST(test_semantic_similarity_propequiv_responses_score_equal) {
    // repair_2.req2: response = !((!tr & lo) -> tr)  ≡  !tr & lo
    const Requirement repair2_req2{
        Formula("true"),
        Formula("!(((!(takeoff_roll)) & (lift_off)) -> (takeoff_roll))"),
        timing::within_ticks(5), ConditionType::Trigger};
    // repair_13.req2: response = !tr & lo  (same formula, simpler string)
    const Requirement repair13_req2{
        Formula("true"), Formula("(!(takeoff_roll)) & (lift_off)"),
        timing::within_ticks(5), ConditionType::Trigger};
    // original req2
    const Requirement original_req2{
        Formula("true"), Formula("!(takeoff_roll) & (lift_off)"),
        timing::within_ticks(7), ConditionType::Trigger};
    const double score2 = semantic_similarity(
        repair2_req2, original_req2, Config{}.default_model_counting_bound);
    const double score13 = semantic_similarity(
        repair13_req2, original_req2, Config{}.default_model_counting_bound);
    expect(std::fabs(score2 - score13) < 1e-12,
           "semantic-similarity: propositionally-equivalent responses must "
           "score identically against the same original (got " +
               std::to_string(score2) + " vs " + std::to_string(score13) + ")");
}

// Direct and logarithmic metrics agree on identical languages but diverge on a
// strict subset. With 2^10 shared traces out of 2^20 the direct metric returns
// the count ratio 2^-10, while the logarithmic metric returns
// log(2^10)/log(2^20) == 1/2 -- the ratio of growth rates, which is what keeps
// it stable as the counting bound (and hence the counts' magnitude) grows.
// Exercises the metric branch through semantic_similarity_from_counts, without
// invoking the model counter.
TEST(test_semantic_similarity_metric_direct_vs_logarithmic) {
    const Count whole = std::ldexp(1.0L, 20);
    const Count shared = std::ldexp(1.0L, 10);
    const SemanticSimilarityCounts counts{whole, whole, shared};

    const double direct =
        semantic_similarity_from_counts(counts, SimilarityMetric::Direct);
    expect(std::fabs(direct - std::ldexp(1.0, -10)) < 1e-12,
           "semantic-similarity: the direct metric must return the count ratio "
           "2^-10 for a 2^10-of-2^20 overlap");

    const double logarithmic =
        semantic_similarity_from_counts(counts, SimilarityMetric::Logarithmic);
    expect(std::fabs(logarithmic - 0.5) < 1e-12,
           "semantic-similarity: the logarithmic metric must return the "
           "growth-rate ratio 1/2 for a 2^10-of-2^20 overlap");

    const SemanticSimilarityCounts identical{whole, whole, whole};
    expect(std::fabs(semantic_similarity_from_counts(
                         identical, SimilarityMetric::Logarithmic) -
                     1.0) < 1e-12,
           "semantic-similarity: the logarithmic metric must score identical "
           "counts exactly 1");
}

// The split scoring path dispatches one term per changed slot and takes their
// mean, so the two have to agree by construction rather than by inspection.
TEST(test_semantic_similarity_terms_mean_matches_the_score) {
    const Requirement req_imm{Formula("P"), Formula("Q"),
                              timing::immediately()};
    const Requirement req_next{Formula("P"), Formula("Q"),
                               timing::next_timepoint()};
    const Requirement req_within{Formula("P"), Formula("Q"),
                                 timing::within_ticks(3)};
    const Requirement req_for{Formula("P"), Formula("Q"), timing::for_ticks(2)};
    const Requirement req_after{Formula("P"), Formula("Q"),
                                timing::after_ticks(2)};
    const Specification spec1({}, {req_imm, req_within, req_for}, {"P"}, {"Q"});
    const Specification spec2({}, {req_next, req_within, req_after}, {"P"},
                              {"Q"});
    const std::vector<std::function<double()>> terms =
        semantic_similarity_terms(spec1, spec2, 1, SimilarityMetric::Direct);
    expect(terms.size() == 2,
           "semantic-similarity: one term per changed slot, and the unchanged "
           "slot should contribute none");
    double total = 0.0;
    for (const std::function<double()>& term : terms) {
        total += term();
    }
    const double mean = total / static_cast<double>(terms.size());
    expect(
        std::fabs(mean - semantic_similarity(spec1, spec2, 1,
                                             SimilarityMetric::Direct)) < 1e-12,
        "semantic-similarity: the mean of the terms must equal the score "
        "the same arguments return, or the split scoring path grades "
        "differently from the serial one");
}

TEST(test_semantic_similarity_terms_are_empty_when_nothing_differs) {
    const Requirement req{Formula("P"), Formula("Q"), timing::immediately()};
    const Specification spec({}, {req}, {"P"}, {"Q"});
    expect(semantic_similarity_terms(spec, spec, 1, SimilarityMetric::Direct)
               .empty(),
           "semantic-similarity: identical specifications should yield no "
           "terms, which the fold reads as a perfect match");
}

}  // namespace
