#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.hpp"
#include "fitness/syntactic_similarity.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "syntactic_similarity";

Requirement make_req(const std::string& trigger, const std::string& response) {
    return Requirement{Formula(trigger), Formula(response),
                       timing::immediately()};
}

Specification make_spec(
    std::initializer_list<std::pair<const char*, const char*>> reqs) {
    std::vector<Requirement> req_vec;
    req_vec.reserve(reqs.size());
    for (const auto& [trigger, response] : reqs) {
        req_vec.push_back(make_req(trigger, response));
    }
    return Specification({}, std::move(req_vec), {}, {});
}

std::vector<Requirement> make_reqs(
    std::initializer_list<std::pair<const char*, const char*>> reqs) {
    std::vector<Requirement> req_vec;
    req_vec.reserve(reqs.size());
    for (const auto& [trigger, response] : reqs) {
        req_vec.push_back(make_req(trigger, response));
    }
    return req_vec;
}

// --- requirement-level ---

TEST(test_req_similarity_averages_component_scores) {
    const Requirement requirement{Formula("P"), Formula("Q"),
                                  timing::immediately()};
    const Requirement other_requirement{Formula("P"), Formula("P|Q"),
                                        timing::immediately()};

    const double synsim =
        syntactic_similarity(requirement, other_requirement, Config{});
    // condition: P vs P -> 1.0. response: Q vs P|Q -> shared=1, n(Q)=1,
    // n(P|Q)=3, harmonic mean = 2*1*(1/3)/(4/3) = 0.5. timing: identical ->
    // 1.0. scope: both Global -> 1.0. condition type: both Continual -> 1.0.
    // Average: (1.0 + 0.5 + 1.0 + 1.0 + 1.0) / 5 = 0.9.
    expect(std::fabs(synsim - 0.9) < 1e-12,
           "syntactic-similarity: component averaging should produce the "
           "expected score for 'P'/'Q' versus 'P'/'P|Q'");
}

// --- specification-level ---

TEST(test_spec_similarity_identical_single_req) {
    // All components identical → 1.0
    const Specification spec = make_spec({{"p", "q"}});
    const double result = syntactic_similarity(spec, spec, Config{});
    expect(std::fabs(result - 1.0) < 1e-12,
           "spec-similarity: identical single-req specs should score 1.0");
}

TEST(test_spec_similarity_disjoint_atoms) {
    // Triggers share no atoms, responses share no atoms. Both specs are
    // unscoped and share a condition type, so those two terms read 1 alongside
    // the timing term.
    // trigger = 0, response = 0, timing = 1, scope = 1, ctype = 1 → 3/5
    const Specification spec_a = make_spec({{"p", "q"}});
    const Specification spec_b = make_spec({{"r", "s"}});
    const double result = syntactic_similarity(spec_a, spec_b, Config{});
    expect(std::fabs(result - (3.0 / 5.0)) < 1e-12,
           "spec-similarity: fully disjoint single-req specs should score 3/5");
}

TEST(test_spec_similarity_same_trigger_different_response) {
    // trigger = 1, response = 0, timing = 1, scope = 1, ctype = 1 → 4/5
    const Specification spec_a = make_spec({{"p", "q"}});
    const Specification spec_b = make_spec({{"p", "r"}});
    const double result = syntactic_similarity(spec_a, spec_b, Config{});
    expect(
        std::fabs(result - 0.8) < 1e-12,
        "spec-similarity: same trigger, different response should score 4/5");
}

TEST(test_spec_similarity_identical_multi_req) {
    // Identical two-requirement specs → 1.0
    const Specification spec = make_spec({{"p", "q"}, {"r", "s"}});
    const double result = syntactic_similarity(spec, spec, Config{});
    expect(std::fabs(result - 1.0) < 1e-12,
           "spec-similarity: identical multi-req specs should score 1.0");
}

TEST(test_spec_similarity_partial_match_multi_req) {
    // Spec A = {req(p,q), req(r,s)} — triggers conjoin to (p & r), responses
    // to (q & s).
    // Spec B = {req(p,q), req(t,u)} — triggers conjoin to (p & t), responses
    // to (q & u).
    // (p & r) vs (p & t): 3 nodes each, 1 shared (atom p) → 0.5*(1/3+1/3) =
    // 1/3.
    // (q & s) vs (q & u): same shape → 1/3.
    // Both specs are unscoped and share a condition type, so those two terms
    // read 1 alongside timing = 1, giving (1/3 + 1/3 + 1 + 1 + 1) / 5 =
    // (11/3) / 5 = 11/15.
    const Specification spec_a = make_spec({{"p", "q"}, {"r", "s"}});
    const Specification spec_b = make_spec({{"p", "q"}, {"t", "u"}});
    const double result = syntactic_similarity(spec_a, spec_b, Config{});
    expect(std::fabs(result - (11.0 / 15.0)) < 1e-12,
           "spec-similarity: specs sharing one of two requirements should "
           "score 11/15");
}

// Regression: the p_add_assumption mutation grows a candidate's assumption
// list, so a candidate can be scored against an original with fewer
// assumptions. average_timing_similarity used to index the second spec by the
// first spec's counts, reading out of bounds (an assertion in debug builds,
// undefined behaviour once NDEBUG disables it). The score must stay finite,
// bounded, and symmetric regardless of argument order.
TEST(test_spec_similarity_differing_assumption_counts) {
    const Specification original(make_reqs({{"a", "b"}}),
                                 make_reqs({{"p", "q"}}), {}, {});
    const Specification candidate(make_reqs({{"a", "b"}, {"c", "d"}}),
                                  make_reqs({{"p", "q"}}), {}, {});
    const double candidate_vs_original =
        syntactic_similarity(candidate, original, Config{});
    const double original_vs_candidate =
        syntactic_similarity(original, candidate, Config{});
    expect(std::isfinite(candidate_vs_original) &&
               candidate_vs_original >= 0.0 && candidate_vs_original <= 1.0,
           "spec-similarity: a candidate with an extra assumption must score "
           "a finite, bounded value against the original (no out-of-bounds "
           "read)");
    expect(std::fabs(candidate_vs_original - original_vs_candidate) < 1e-12,
           "spec-similarity: differing assumption counts score the same in "
           "either argument order");
}

// The reason a deleted guarantee is tombstoned rather than erased. Deleting the
// FIRST of three guarantees must leave the other two scoring against the same
// requirements they scored against before; erasing the slot would pair
// candidate guarantee 1 with original guarantee 2, and so on down the list, so
// two untouched requirements would read as changed.
TEST(test_spec_similarity_stays_aligned_across_a_deleted_guarantee) {
    // The timings differ per requirement, which is what makes the pairing
    // observable at all: the trigger and response terms fold each side into one
    // conjunction, so they cannot tell an aligned pairing from a shifted one.
    const std::vector<Requirement> reqs = {
        Requirement{Formula("p"), Formula("q"), timing::always()},
        Requirement{Formula("r"), Formula("s"), timing::immediately()},
        Requirement{Formula("t"), Formula("u"), timing::eventually()}};
    const Specification original({}, reqs, {}, {});

    std::vector<Requirement> guarantees = reqs;
    guarantees[0].m_removed = true;
    const Specification candidate({}, guarantees, {}, {});

    // The same deletion made by erasing the slot: the two survivors are the
    // candidate's two live requirements, but they now sit one position early.
    const Specification shifted({}, {reqs[1], reqs[2]}, {}, {});

    const double tombstoned =
        syntactic_similarity(candidate, original, Config{});
    const double erased = syntactic_similarity(shifted, original, Config{});
    expect(tombstoned > erased,
           "spec-similarity: a tombstoned deletion scores above the same "
           "deletion made by erasing the slot, because the survivors stay "
           "paired with the requirements they came from");

    const Specification untouched({}, reqs, {}, {});
    expect(std::fabs(syntactic_similarity(untouched, original, Config{}) -
                     1.0) < 1e-12,
           "spec-similarity: an untouched specification still scores 1.0");
}

// --- timing similarity ---

// Identical timings always score 1.0.
TEST(test_timing_identical_immediately) {
    const Requirement req{Formula("p"), Formula("q"), timing::immediately()};
    const double result = syntactic_similarity(req, req, Config{});
    // All three components equal 1.0 → average is 1.0
    expect(std::fabs(result - 1.0) < 1e-12,
           "timing-sim: identical requirements (immediately) should score 1.0");
}

TEST(test_timing_identical_within_ticks) {
    const Requirement req{Formula("p"), Formula("p"), timing::within_ticks(3)};
    const double result = syntactic_similarity(req, req, Config{});
    expect(std::fabs(result - 1.0) < 1e-12,
           "timing-sim: identical within_ticks requirements should score 1.0");
}

// ForTicks{2} > ForTicks{1}: synSim = μ(↓ForTicks{1}) / μ(↓ForTicks{2}).
// With r=0.5, w=0.01:
//   μ(↓ForTicks{1}) = 3*0.01 + 0.5*(2-0.5)/0.5   = 0.03 + 1.5  = 1.53
//   μ(↓ForTicks{2}) = 3*0.01 + 0.5*(2-0.25)/0.5  = 0.03 + 1.75 = 1.78
//   synSim_timing = 1.53 / 1.78
// Both requirements share the same formulas (p/p), are unscoped and share a
// condition type, so every component but the timing one is 1.0.
// Overall = (1.0 + 1.0 + 1.53/1.78 + 1.0 + 1.0) / 5.0
TEST(test_timing_comparable_for_ticks) {
    const Requirement req_strong{Formula("p"), Formula("p"),
                                 timing::for_ticks(2)};
    const Requirement req_weak{Formula("p"), Formula("p"),
                               timing::for_ticks(1)};
    const double timing_sim = 1.53 / 1.78;
    // Both are unscoped and share a condition type, so those terms read 1.0.
    const double expected = (1.0 + 1.0 + timing_sim + 1.0 + 1.0) / 5.0;
    const double result = syntactic_similarity(req_strong, req_weak, Config{});
    expect(std::fabs(result - expected) < 1e-9,
           "timing-sim: for_ticks{2} vs for_ticks{1} should give 1.53/1.78 "
           "timing component");
}

// ForTicks{1} vs Eventually: very different → timing component near 0.
// synSim_timing = μ(↓Eventually) / μ(↓ForTicks{1}) = 0.01 / 1.53
TEST(test_timing_for_ticks_vs_eventually) {
    const Requirement req_strong{Formula("p"), Formula("p"),
                                 timing::for_ticks(1)};
    const Requirement req_weak{Formula("p"), Formula("p"),
                               timing::eventually()};
    const double timing_sim = 0.01 / 1.53;
    // Both are unscoped and share a condition type, so those terms read 1.0.
    const double expected = (1.0 + 1.0 + timing_sim + 1.0 + 1.0) / 5.0;
    const double result = syntactic_similarity(req_strong, req_weak, Config{});
    expect(std::fabs(result - expected) < 1e-9,
           "timing-sim: for_ticks{1} vs eventually should give tiny timing "
           "component");
}

// Immediately vs NextTimepoint are incomparable in the partial order.
// ↓I ∩ ↓N = WithinTicks{k≥1} ∪ {Eventually} → μ = 0.01 + 1.0 = 1.01
// μ(↓I) = μ(↓N) = 0.01 + 0.01 + 1.0 = 1.02
// μ(∪) = 1.02 + 1.02 - 1.01 = 1.03
// synSim_timing = 1.01 / 1.03
TEST(test_timing_immediately_vs_next_timepoint) {
    const Requirement req_i{Formula("p"), Formula("p"), timing::immediately()};
    const Requirement req_n{Formula("p"), Formula("p"),
                            timing::next_timepoint()};
    const double timing_sim = 1.01 / 1.03;
    // Both are unscoped and share a condition type, so those terms read 1.0.
    const double expected = (1.0 + 1.0 + timing_sim + 1.0 + 1.0) / 5.0;
    const double result = syntactic_similarity(req_i, req_n, Config{});
    expect(std::fabs(result - expected) < 1e-9,
           "timing-sim: immediately vs next_timepoint should give 1.01/1.03 "
           "timing component");
}

TEST(test_timing_identical_always) {
    const Requirement req{Formula("p"), Formula("p"), timing::always()};
    const double result = syntactic_similarity(req, req, Config{});
    expect(std::fabs(result - 1.0) < 1e-12,
           "timing-sim: identical always requirements should score 1.0");
}

// Always is the top of the order; Eventually is the bottom, so they are
// maximally dissimilar: synSim_timing = μ(↓Eventually) / μ(↓Always) = 0.01
// / 2.04.
//   μ(↓Always) = 4*0.01 + 2*(0.5/0.5) = 0.04 + 2.0 = 2.04
TEST(test_timing_always_vs_eventually) {
    const Requirement req_strong{Formula("p"), Formula("p"), timing::always()};
    const Requirement req_weak{Formula("p"), Formula("p"),
                               timing::eventually()};
    const double timing_sim = 0.01 / 2.04;
    // Both are unscoped and share a condition type, so those terms read 1.0.
    const double expected = (1.0 + 1.0 + timing_sim + 1.0 + 1.0) / 5.0;
    const double result = syntactic_similarity(req_strong, req_weak, Config{});
    expect(std::fabs(result - expected) < 1e-9,
           "timing-sim: always vs eventually should give tiny timing "
           "component");
}

// Only the timing differs, so the score is (4 + t) / 5 for a timing similarity
// of t.
double timing_term(const Timing& lhs, const Timing& rhs) {
    const Requirement left{Formula("p"), Formula("q"), lhs};
    const Requirement right{Formula("p"), Formula("q"), rhs};
    return (5.0 * syntactic_similarity(left, right, Config{})) - 4.0;
}

// A stop timing is scored outside the downset measure, so no stop-free pair
// moves. `until s` lies below Always alone, sharing one element of weight 0.01
// with ↓Always: 0.01 / (2.04 + 0.01). `before s` lies nowhere in the order.
// Within one stop kind the term averages the kind match with the stops'
// similarity.
TEST(test_timing_stop_timings) {
    const Timing until_s = timing::until(Formula("s"));
    const Timing before_s = timing::before(Formula("s"));
    expect(std::fabs(timing_term(until_s, until_s) - 1.0) < 1e-12,
           "timing-sim: until s should score 1.0 against itself");
    const double below_always = 0.01 / 2.05;
    expect(std::fabs(timing_term(timing::always(), until_s) - below_always) <
                   1e-9 &&
               std::fabs(timing_term(until_s, timing::always()) -
                         below_always) < 1e-9,
           "timing-sim: always vs until s should share one element, both ways");
    expect(std::fabs(timing_term(timing::eventually(), until_s)) < 1e-9,
           "timing-sim: until s shares nothing with eventually");
    expect(std::fabs(timing_term(timing::always(), before_s)) < 1e-9,
           "timing-sim: before s shares nothing with always");
    expect(std::fabs(timing_term(until_s, before_s)) < 1e-9,
           "timing-sim: until s and before s share nothing");
    const double stops = Formula("s").syntactic_similarity(Formula("t"));
    expect(std::fabs(timing_term(until_s, timing::until(Formula("t"))) -
                     ((1.0 + stops) / 2.0)) < 1e-9,
           "timing-sim: two untils should average the kind match with the "
           "stops' similarity");
}

// --- scope ---

// Only the scope differs, so the other four components read 1.0 and the score
// is (4 + s) / 5 for a scope similarity of s. That is what lets each case below
// be stated as the scope term alone.
double scope_term(const Scope& lhs, const Scope& rhs) {
    const Requirement left(Formula("p"), Formula("q"), timing::immediately(),
                           ConditionType::Continual, true, false, lhs);
    const Requirement right(Formula("p"), Formula("q"), timing::immediately(),
                            ConditionType::Continual, true, false, rhs);
    const double mean = syntactic_similarity(left, right, Config{});
    return (5.0 * mean) - 4.0;
}

TEST(test_scope_identical_is_one) {
    expect(std::fabs(scope_term(Scope{}, Scope{}) - 1.0) < 1e-12,
           "scope-similarity: two Global scopes should score 1.0, so the term "
           "is inert on a specification that uses no scopes");
    const Scope in_mode{ScopeKind::In, "m"};
    expect(std::fabs(scope_term(in_mode, in_mode) - 1.0) < 1e-12,
           "scope-similarity: a scope should score 1.0 against itself");
}

// Global applies over the whole trace and `in m` over one of its four regions,
// so the regions overlap by a quarter; the modes differ (Global names none),
// so that half of the term is zero.
TEST(test_scope_global_versus_in) {
    const double term = scope_term(Scope{}, Scope{ScopeKind::In, "m"});
    expect(
        std::fabs(term - 0.125) < 1e-12,
        "scope-similarity: Global versus `in m` should score (0.25 + 0) / 2");
}

// `before m` is contained in `except in m`: the points strictly before the mode
// first holds are points where it does not hold. One of that scope's three
// regions is shared, over the same mode.
TEST(test_scope_notin_versus_before) {
    const double term =
        scope_term(Scope{ScopeKind::NotIn, "m"}, Scope{ScopeKind::Before, "m"});
    expect(std::fabs(term - (((1.0 / 3.0) + 1.0) / 2.0)) < 1e-12,
           "scope-similarity: `except in m` versus `before m` should score "
           "(1/3 + 1) / 2");
}

// The two make opposite claims about the same timepoints, so sharing every
// region earns nothing. Without the polarity tag they would score as the most
// similar pair of distinct scopes there is.
TEST(test_scope_notin_versus_onlyin_shares_nothing) {
    const double term =
        scope_term(Scope{ScopeKind::NotIn, "m"}, Scope{ScopeKind::OnlyIn, "m"});
    expect(std::fabs(term - 0.5) < 1e-12,
           "scope-similarity: `except in m` versus `only in m` should earn no "
           "region credit, leaving only the shared mode");
}

TEST(test_scope_same_kind_different_mode) {
    const double term =
        scope_term(Scope{ScopeKind::In, "m"}, Scope{ScopeKind::In, "n"});
    expect(std::fabs(term - 0.5) < 1e-12,
           "scope-similarity: the same kind over two modes should keep its "
           "region credit and lose the mode credit");
}

TEST(test_scope_similarity_is_symmetric_and_bounded) {
    const std::vector<Scope> scopes = {Scope{},
                                       Scope{ScopeKind::In, "m"},
                                       Scope{ScopeKind::NotIn, "m"},
                                       Scope{ScopeKind::Before, "m"},
                                       Scope{ScopeKind::After, "m"},
                                       Scope{ScopeKind::OnlyIn, "m"},
                                       Scope{ScopeKind::OnlyBefore, "m"},
                                       Scope{ScopeKind::OnlyAfter, "n"}};
    for (const Scope& lhs : scopes) {
        for (const Scope& rhs : scopes) {
            const double forward = scope_term(lhs, rhs);
            // The reversed argument order is the point of a symmetry check.
            // NOLINTNEXTLINE(readability-suspicious-call-argument)
            const double reversed = scope_term(rhs, lhs);
            expect(std::fabs(forward - reversed) < 1e-12,
                   "scope-similarity: should be symmetric");
            expect(forward >= -1e-12 && forward <= 1.0 + 1e-12,
                   "scope-similarity: should stay within [0, 1]");
        }
    }
}

// The specification-level term pairs by index, as the timing term does, so a
// scope change in slot i is scored against slot i of the original.
TEST(test_spec_scope_similarity_pairs_by_index) {
    const Requirement plain(Formula("p"), Formula("q"), timing::immediately());
    const Requirement scoped(Formula("p"), Formula("q"), timing::immediately(),
                             ConditionType::Continual, true, false,
                             Scope{ScopeKind::In, "m"});
    const Specification original({}, {plain, plain}, {"p"}, {"q"});
    const Specification moved({}, {plain, scoped}, {"p"}, {"q"}, {"m"});
    const double both_plain =
        syntactic_similarity(original, original, Config{});
    const double one_moved = syntactic_similarity(original, moved, Config{});
    expect(one_moved < both_plain,
           "scope-similarity: a scope change in one slot must lower the "
           "specification-level score, or the search has no gradient on the "
           "field the p_scope arm moves");
}

// --- condition type ---

// The same trick as scope_term: every other component reads 1.0, so the score
// isolates the condition-type one.
double condition_type_term(ConditionType lhs, ConditionType rhs) {
    const Requirement left(Formula("p"), Formula("q"), timing::immediately(),
                           lhs);
    const Requirement right(Formula("p"), Formula("q"), timing::immediately(),
                            rhs);
    const double mean = syntactic_similarity(left, right, Config{});
    return (5.0 * mean) - 4.0;
}

// Jaccard on downsets in the two-element implication order: Continual implies
// Trigger, so down-Continual holds both values and down-Trigger holds itself
// alone. Nothing is chosen here, which is the point of deriving it rather than
// picking a penalty.
TEST(test_condition_type_similarity_follows_the_implication_order) {
    expect(std::fabs(condition_type_term(ConditionType::Continual,
                                         ConditionType::Continual) -
                     1.0) < 1e-12,
           "condition-type-similarity: equal values should score 1.0");
    expect(std::fabs(condition_type_term(ConditionType::Trigger,
                                         ConditionType::Trigger) -
                     1.0) < 1e-12,
           "condition-type-similarity: equal values should score 1.0");
    expect(std::fabs(condition_type_term(ConditionType::Continual,
                                         ConditionType::Trigger) -
                     0.5) < 1e-12,
           "condition-type-similarity: a differing pair should score 1/2, the "
           "Jaccard overlap of their downsets");
    expect(std::fabs(condition_type_term(ConditionType::Continual,
                                         ConditionType::Trigger) -
                     condition_type_term(ConditionType::Trigger,
                                         ConditionType::Continual)) < 1e-12,
           "condition-type-similarity: should be symmetric");
}

// Without this the p_condition_type arm would move a field the syntactic
// objective cannot see, so a candidate that flipped one requirement's
// condition type would score identical to its parent on that objective.
TEST(test_spec_condition_type_similarity_pairs_by_index) {
    const Requirement continual(Formula("p"), Formula("q"),
                                timing::immediately(),
                                ConditionType::Continual);
    const Requirement trigger(Formula("p"), Formula("q"), timing::immediately(),
                              ConditionType::Trigger);
    const Specification original({}, {continual, continual}, {"p"}, {"q"});
    const Specification moved({}, {continual, trigger}, {"p"}, {"q"});
    expect(syntactic_similarity(original, moved, Config{}) <
               syntactic_similarity(original, original, Config{}),
           "condition-type-similarity: a condition-type change in one slot "
           "must lower the specification-level score");
}

}  // namespace
