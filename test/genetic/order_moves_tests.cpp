#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "genetic/order_moves.hpp"
#include "ltl_oracle.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "order_moves";

Requirement req(const std::string& condition, const std::string& response,
                Timing timing, ConditionType ctype = ConditionType::Continual,
                Scope scope = Scope{}) {
    return Requirement(Formula(condition), Formula(response), std::move(timing),
                       ctype, /*weakenable=*/true, /*removed=*/false,
                       std::move(scope));
}

Requirement locked(const std::string& condition, const std::string& response,
                   Timing timing) {
    return Requirement(Formula(condition), Formula(response), std::move(timing),
                       ConditionType::Continual, /*weakenable=*/false);
}

Requirement removed(Requirement requirement) {
    requirement.m_removed = true;
    return requirement;
}

Specification spec(std::vector<Requirement> assumptions,
                   std::vector<Requirement> guarantees) {
    return Specification(std::move(assumptions), std::move(guarantees),
                         {"a", "b", "c"}, {"x", "y"}, {"m"});
}

Scope in_m() { return Scope{ScopeKind::In, "m"}; }
Scope only_in_m() { return Scope{ScopeKind::OnlyIn, "m"}; }

// The original most tests move away from: one assumption, two guarantees.
Requirement assumption0() { return req("a", "b", timing::eventually()); }
Requirement guarantee0() { return req("a", "x", timing::within_ticks(2)); }
Requirement guarantee1() { return req("b", "y", timing::immediately()); }
Requirement appended_u() { return req("true", "c", timing::eventually()); }
Requirement appended_v() { return req("c", "a", timing::eventually()); }
Requirement appended_w() { return req("true", "b", timing::eventually()); }

Specification original() {
    return spec({assumption0()}, {guarantee0(), guarantee1()});
}

void expect_order(const Specification& parent, const Specification& child,
                  MoveOrder expected, const std::string& what) {
    expect(classify_move(parent, child) == expected, "classify_move: " + what);
}

void expect_requirement(const std::optional<Requirement>& actual,
                        const Requirement& expected, const std::string& what) {
    if (!actual.has_value()) {
        fail(what + ": expected " + expected.to_string() + ", got nothing");
    }
    expect(*actual == expected, what + ": expected " + expected.to_string() +
                                    ", got " + actual->to_string());
}

// --- classify_move -----------------------------------------------------------

TEST(test_classify_move_equal) {
    expect_order(original(), original(), MoveOrder::Equal, "identical");
}

TEST(test_classify_move_guarantee_timing) {
    const Specification looser =
        spec({assumption0()},
             {req("a", "x", timing::within_ticks(3)), guarantee1()});
    expect_order(original(), looser, MoveOrder::Weaker, "within 2 to within 3");
    expect_order(looser, original(), MoveOrder::Stronger,
                 "within 3 to within 2");
}

// A stronger assumption weakens `A -> G`.
TEST(test_classify_move_assumption_side_flips) {
    const Specification tighter =
        spec({req("a", "b & c", timing::eventually())},
             {guarantee0(), guarantee1()});
    expect_order(original(), tighter, MoveOrder::Weaker,
                 "a stronger assumption");
    expect_order(tighter, original(), MoveOrder::Stronger,
                 "a weaker assumption");
}

TEST(test_classify_move_appended_assumption) {
    const Specification appended =
        spec({assumption0(), appended_u()}, {guarantee0(), guarantee1()});
    expect_order(original(), appended, MoveOrder::Weaker, "an appended one");
    expect_order(appended, original(), MoveOrder::Stronger, "a dropped one");
}

// Appended assumptions match by lowering, so dropping the first of two does
// not pair the second against it.
TEST(test_classify_move_drops_first_appended) {
    const Specification both = spec({assumption0(), appended_u(), appended_v()},
                                    {guarantee0(), guarantee1()});
    const Specification second =
        spec({assumption0(), appended_v()}, {guarantee0(), guarantee1()});
    expect_order(both, second, MoveOrder::Stronger, "dropping the first");
}

TEST(test_classify_move_tombstone) {
    const Specification gutted =
        spec({assumption0()}, {guarantee0(), removed(guarantee1())});
    expect_order(original(), gutted, MoveOrder::Weaker, "a tombstone");
    expect_order(gutted, original(), MoveOrder::Stronger, "a restore");
}

TEST(test_classify_move_combines_slots) {
    const Specification both_weaker =
        spec({assumption0()}, {req("a", "x", timing::within_ticks(3)),
                               req("b", "x | y", timing::immediately())});
    expect_order(original(), both_weaker, MoveOrder::Weaker,
                 "two weaker slots");
    const Specification mixed =
        spec({assumption0()}, {req("a", "x", timing::within_ticks(3)),
                               req("b", "x & y", timing::immediately())});
    expect_order(original(), mixed, MoveOrder::Incomparable,
                 "one weaker and one stronger slot");
    const Specification sideways = spec(
        {assumption0()}, {guarantee0(), req("b", "x", timing::immediately())});
    expect_order(original(), sideways, MoveOrder::Incomparable,
                 "a sideways response");
}

// --- requirement_join and requirement_meet -----------------------------------

TEST(test_bound_of_comparable_pair_is_an_input) {
    const Requirement tight = req("a", "x", timing::within_ticks(2));
    const Requirement loose = req("a", "x", timing::within_ticks(3));
    expect_requirement(requirement_join(loose, tight), tight, "join");
    expect_requirement(requirement_meet(tight, loose), loose, "meet");
}

TEST(test_bound_of_responses) {
    const Requirement lhs = req("a", "x", timing::immediately());
    const Requirement rhs = req("a", "y", timing::immediately());
    expect_requirement(requirement_join(lhs, rhs),
                       req("a", "x & y", timing::immediately()), "join");
    expect_requirement(requirement_meet(lhs, rhs),
                       req("a", "x | y", timing::immediately()), "meet");
}

// The response sits under a negation in an only scope.
TEST(test_bound_of_responses_swaps_under_only_scope) {
    const auto only = [](const std::string& response) {
        return req("a", response, timing::immediately(),
                   ConditionType::Continual, only_in_m());
    };
    expect_requirement(requirement_join(only("x"), only("y")), only("x | y"),
                       "join");
    expect_requirement(requirement_meet(only("x"), only("y")), only("x & y"),
                       "meet");
}

// `after n ticks` holds its response at both polarities.
TEST(test_bound_of_after_responses_is_unproved) {
    const Requirement lhs = req("a", "x", timing::after_ticks(2));
    const Requirement rhs = req("a", "y", timing::after_ticks(2));
    expect(!requirement_join(lhs, rhs).has_value(), "join under after");
    expect(!requirement_meet(lhs, rhs).has_value(), "meet under after");
}

TEST(test_bound_of_continual_conditions) {
    const Requirement lhs = req("a", "x", timing::immediately());
    const Requirement rhs = req("b", "x", timing::immediately());
    expect_requirement(requirement_join(lhs, rhs),
                       req("a | b", "x", timing::immediately()), "join");
    expect_requirement(requirement_meet(lhs, rhs),
                       req("a & b", "x", timing::immediately()), "meet");
}

TEST(test_bound_of_trigger_conditions) {
    const Requirement lhs =
        req("a", "x", timing::immediately(), ConditionType::Trigger);
    const Requirement rhs =
        req("b", "x", timing::immediately(), ConditionType::Trigger);
    expect_requirement(requirement_join(lhs, rhs),
                       req("a | b", "x", timing::immediately()),
                       "join turns continual");
    expect(!requirement_meet(lhs, rhs).has_value(),
           "meet of inequivalent triggers");
}

TEST(test_bound_of_timings) {
    const Requirement lhs = req("a", "x", timing::immediately());
    const Requirement rhs = req("a", "y", timing::next_timepoint());
    expect_requirement(requirement_join(lhs, rhs),
                       req("a", "x & y", timing::for_ticks(1)), "join");
    expect_requirement(requirement_meet(lhs, rhs),
                       req("a", "x | y", timing::within_ticks(1)), "meet");
}

// `after 2` and `after 3` are incomparable and nothing implies both, but both
// imply `within 4`, one tick past the larger count.
TEST(test_bound_of_after_timings) {
    const Requirement lhs = req("a", "x", timing::after_ticks(2));
    const Requirement rhs = req("a", "x", timing::after_ticks(3));
    expect(!requirement_join(lhs, rhs).has_value(), "join of two afters");
    expect_requirement(requirement_meet(lhs, rhs),
                       req("a", "x", timing::within_ticks(4)), "meet");
}

TEST(test_bound_of_condition_types) {
    const Requirement lhs =
        req("a", "x", timing::immediately(), ConditionType::Trigger);
    const Requirement rhs = req("a", "y", timing::immediately());
    expect_requirement(requirement_join(lhs, rhs),
                       req("a", "x & y", timing::immediately()), "join");
    expect_requirement(
        requirement_meet(lhs, rhs),
        req("a", "x | y", timing::immediately(), ConditionType::Trigger),
        "meet");
}

TEST(test_bound_of_scopes) {
    const Requirement lhs = req("a", "x", timing::immediately());
    const Requirement rhs =
        req("a", "y", timing::immediately(), ConditionType::Continual, in_m());
    expect_requirement(requirement_join(lhs, rhs),
                       req("a", "x & y", timing::immediately()), "join");
    expect_requirement(requirement_meet(lhs, rhs),
                       req("a", "x | y", timing::immediately(),
                           ConditionType::Continual, in_m()),
                       "meet");
}

TEST(test_bound_of_stops) {
    const Requirement lhs = req("a", "x", timing::until(Formula("s")));
    const Requirement rhs = req("a", "x", timing::until(Formula("t")));
    expect_requirement(requirement_join(lhs, rhs),
                       req("a", "x", timing::until(Formula("s & t"))), "join");
    expect_requirement(requirement_meet(lhs, rhs),
                       req("a", "x", timing::until(Formula("s | t"))), "meet");
}

// `x & (x | y)` is `x`, and the bound keeps the simpler spelling.
TEST(test_bound_keeps_simpler_field) {
    const Requirement lhs = req("a", "x", timing::immediately());
    const Requirement rhs = req("a", "x | y", timing::next_timepoint());
    expect_requirement(requirement_join(lhs, rhs),
                       req("a", "x", timing::for_ticks(1)), "join");
    expect_requirement(requirement_meet(lhs, rhs),
                       req("a", "x | y", timing::within_ticks(1)), "meet");
}

TEST(test_bound_of_locked_requirements) {
    const Requirement lhs = locked("a", "x", timing::immediately());
    const Requirement rhs = locked("a", "y", timing::immediately());
    expect(!requirement_join(lhs, rhs).has_value(), "join of two locked");
    expect(!requirement_meet(lhs, rhs).has_value(), "meet of two locked");
    expect_requirement(requirement_join(lhs, lhs), lhs, "join of one locked");
}

// --- specification_join and specification_meet -------------------------------

TEST(test_specification_bound_tombstones) {
    const Specification lhs =
        spec({assumption0()}, {guarantee0(), removed(guarantee1())});
    const Specification rhs = original();
    const SlotwiseBound join = specification_join(lhs, rhs, 1);
    const SlotwiseBound meet = specification_meet(lhs, rhs, 1);
    expect_requirement(join.m_guarantees[1], guarantee1(), "join keeps live");
    expect_requirement(meet.m_guarantees[1], removed(guarantee1()),
                       "meet keeps the tombstone");
}

TEST(test_specification_bound_flips_assumptions) {
    const Requirement tighter = req("a", "b & c", timing::eventually());
    const Specification lhs = spec({tighter}, {guarantee0(), guarantee1()});
    const SlotwiseBound join = specification_join(lhs, original(), 1);
    const SlotwiseBound meet = specification_meet(lhs, original(), 1);
    expect_requirement(join.m_assumptions[0], assumption0(),
                       "join takes the weaker assumption");
    expect_requirement(meet.m_assumptions[0], tighter,
                       "meet takes the stronger assumption");
}

TEST(test_specification_bound_appended_assumptions) {
    const Specification lhs = spec({assumption0(), appended_u(), appended_v()},
                                   {guarantee0(), guarantee1()});
    const Specification rhs = spec({assumption0(), appended_v(), appended_w()},
                                   {guarantee0(), guarantee1()});
    const SlotwiseBound join = specification_join(lhs, rhs, 1);
    expect(join.m_appended.size() == 1 && join.m_appended[0] == appended_v(),
           "join keeps only the shared appended assumption");
    const SlotwiseBound meet = specification_meet(lhs, rhs, 1);
    expect(meet.m_appended.size() == 3 && meet.m_appended[0] == appended_u() &&
               meet.m_appended[1] == appended_v() &&
               meet.m_appended[2] == appended_w(),
           "meet keeps the union");
    const std::optional<Specification> assembled = assemble(meet, lhs);
    expect(assembled.has_value() && assembled->m_assumptions.size() == 4,
           "assemble: the meet is complete");
}

TEST(test_specification_bound_unproved_slot) {
    const Specification lhs = spec(
        {assumption0()}, {req("a", "x", timing::after_ticks(2)), guarantee1()});
    const Specification rhs = spec(
        {assumption0()}, {req("a", "y", timing::after_ticks(2)), guarantee1()});
    const SlotwiseBound join = specification_join(lhs, rhs, 1);
    expect(!join.m_guarantees[0].has_value(), "the after slot is unproved");
    expect_requirement(join.m_guarantees[1], guarantee1(),
                       "the equal slot is proved");
    expect(!join.is_complete(), "is_complete: an unproved slot");
    expect(!assemble(join, lhs).has_value(), "assemble: an unproved slot");
}

// Two slots whose joins coincide would merge in the constructor and shift
// every later slot.
TEST(test_assemble_refuses_a_merge) {
    const Requirement tight = req("a", "x", timing::within_ticks(2));
    const Requirement loose = req("a", "x", timing::within_ticks(3));
    const Specification lhs = spec({}, {tight, loose});
    const Specification rhs = spec({}, {loose, tight});
    const SlotwiseBound join = specification_join(lhs, rhs, 0);
    expect(join.is_complete(), "both slots are proved");
    expect(!assemble(join, lhs).has_value(), "assemble: two equal slots");
}

// --- steps_towards -----------------------------------------------------------

TEST(test_steps_towards_every_kind) {
    const Specification current =
        spec({req("a", "b & c", timing::eventually()), appended_u()},
             {req("a", "x", timing::within_ticks(4)), removed(guarantee1())});
    const std::vector<Step> steps = steps_towards(current, original());
    expect(steps.size() == 4,
           "steps_towards: four steps, got " + std::to_string(steps.size()));
    expect(steps[0].m_kind == StepKind::Field &&
               steps[0].m_side == StepSide::Assumption &&
               steps[0].m_slot == 0 &&
               steps[0].m_field == RequirementField::Response &&
               steps[0].m_result.m_assumptions[0] == assumption0(),
           "steps_towards: the assumption weakens back");
    expect(steps[1].m_kind == StepKind::Field &&
               steps[1].m_side == StepSide::Guarantee && steps[1].m_slot == 0 &&
               steps[1].m_field == RequirementField::Timing &&
               steps[1].m_result.m_guarantees[0] == guarantee0(),
           "steps_towards: the timing tightens back");
    expect(steps[2].m_kind == StepKind::Restore && steps[2].m_slot == 1 &&
               steps[2].m_result.m_guarantees[1] == guarantee1(),
           "steps_towards: the tombstone is restored");
    expect(steps[3].m_kind == StepKind::DropAssumption &&
               steps[3].m_slot == 1 &&
               steps[3].m_result.m_assumptions.size() == 1,
           "steps_towards: the appended assumption is dropped");
    for (const Step& step : steps) {
        expect(classify_move(current, step.m_result) == MoveOrder::Stronger,
               "steps_towards: every step strengthens");
    }
}

// A slot that moved sideways steps towards the join, which implies both it and
// the original.
TEST(test_steps_towards_sideways_slot) {
    const Specification current =
        spec({assumption0()},
             {req("a", "y", timing::within_ticks(2)), guarantee1()});
    const std::vector<Step> steps = steps_towards(current, original());
    expect(steps.size() == 1 && steps[0].m_field == RequirementField::Response,
           "steps_towards: one response step");
    expect(steps[0].m_result.m_guarantees[0] ==
               req("a", "y & x", timing::within_ticks(2)),
           "steps_towards: the response moves to the join, got " +
               steps[0].m_result.m_guarantees[0].to_string());
}

TEST(test_steps_towards_original_is_empty) {
    expect(steps_towards(original(), original()).empty(),
           "steps_towards: nothing to do at the original");
}

// --- timing_chain ------------------------------------------------------------

void expect_chain(const std::vector<Timing>& actual,
                  const std::vector<Timing>& expected,
                  const std::string& what) {
    std::string rendered;
    for (const Timing& timing : actual) {
        rendered += to_string(timing) + "; ";
    }
    // An element loop, since vector's operator== would reach std's variant
    // comparison rather than the Timing one.
    bool same = actual.size() == expected.size();
    for (std::size_t idx = 0; same && idx < actual.size(); ++idx) {
        same = actual[idx] == expected[idx];
    }
    expect(same, "timing_chain: " + what + ", got " + rendered);
}

TEST(test_timing_chain) {
    expect_chain(timing_chain(timing::within_ticks(5), timing::within_ticks(2),
                              ScopeKind::Global),
                 {timing::within_ticks(4), timing::within_ticks(3),
                  timing::within_ticks(2)},
                 "within 5 to within 2");
    expect_chain(timing_chain(timing::within_ticks(2), timing::within_ticks(5),
                              ScopeKind::Global),
                 {timing::within_ticks(3), timing::within_ticks(4),
                  timing::within_ticks(5)},
                 "within 2 to within 5");
    // Long enough that ranking inside the sort's comparator misordered it.
    expect_chain(timing_chain(timing::within_ticks(9), timing::within_ticks(1),
                              ScopeKind::Global),
                 {timing::within_ticks(8), timing::within_ticks(7),
                  timing::within_ticks(6), timing::within_ticks(5),
                  timing::within_ticks(4), timing::within_ticks(3),
                  timing::within_ticks(2), timing::within_ticks(1)},
                 "within 9 to within 1");
    expect_chain(timing_chain(timing::within_ticks(3), timing::for_ticks(2),
                              ScopeKind::Global),
                 {timing::within_ticks(2), timing::within_ticks(1),
                  timing::for_ticks(1), timing::for_ticks(2)},
                 "within 3 to for 2");
    expect_chain(
        timing_chain(timing::for_ticks(1), timing::for_ticks(4),
                     ScopeKind::Global),
        {timing::for_ticks(2), timing::for_ticks(3), timing::for_ticks(4)},
        "for 1 to for 4");
    // The only scopes reverse the order.
    expect_chain(timing_chain(timing::within_ticks(2), timing::within_ticks(4),
                              ScopeKind::OnlyIn),
                 {timing::within_ticks(3), timing::within_ticks(4)},
                 "within 2 to within 4 under only in");
    expect_chain(
        timing_chain(timing::for_ticks(2), timing::always(), ScopeKind::Global),
        {timing::always()}, "for 2 to always");
    expect_chain(timing_chain(timing::after_ticks(2), timing::always(),
                              ScopeKind::Global),
                 {}, "incomparable");
    expect_chain(timing_chain(timing::eventually(), timing::eventually(),
                              ScopeKind::Global),
                 {}, "equal");
}

// --- Soundness against SPOT --------------------------------------------------

// A family with an implication chain and an incomparable value in every
// field, over which the bounds and the classifier are checked against SPOT.
std::vector<Requirement> requirement_family() {
    const std::vector<std::string> conditions = {"a", "b", "a & b"};
    const std::vector<std::string> responses = {"x", "y", "x | y"};
    const std::vector<Timing> timings = {timing::immediately(),
                                         timing::next_timepoint(),
                                         timing::within_ticks(2),
                                         timing::for_ticks(2),
                                         timing::after_ticks(1),
                                         timing::eventually(),
                                         timing::always(),
                                         timing::until(Formula("s")),
                                         timing::until(Formula("t")),
                                         timing::before(Formula("s"))};
    const std::vector<Scope> scopes = {Scope{}, in_m(), only_in_m()};
    std::vector<Requirement> family;
    for (const std::string& condition : conditions) {
        for (const std::string& response : responses) {
            for (const Timing& timing : timings) {
                for (const ConditionType ctype :
                     {ConditionType::Continual, ConditionType::Trigger}) {
                    for (const Scope& scope : scopes) {
                        family.push_back(
                            req(condition, response, timing, ctype, scope));
                    }
                }
            }
        }
    }
    return family;
}

// How many of the five fields two requirements differ in.
int distance(const Requirement& lhs, const Requirement& rhs) {
    return static_cast<int>(!(lhs.m_condition == rhs.m_condition)) +
           static_cast<int>(!(lhs.m_response == rhs.m_response)) +
           static_cast<int>(!(lhs.m_timing == rhs.m_timing)) +
           static_cast<int>(lhs.m_condition_type != rhs.m_condition_type) +
           static_cast<int>(!(lhs.m_scope == rhs.m_scope));
}

class Claims {
   public:
    void add(const std::string& stronger, const std::string& weaker,
             const std::string& where) {
        m_implications.push_back({stronger, weaker});
        m_wheres.push_back(where);
    }

    [[nodiscard]] std::size_t size() const { return m_wheres.size(); }

    void check(const std::string& what) const {
        const std::vector<bool> holds = ltl_implications(m_implications);
        for (std::size_t idx = 0; idx < m_wheres.size(); ++idx) {
            expect(holds[idx],
                   what + " claimed a non-implication: " + m_wheres[idx]);
        }
    }

   private:
    std::vector<Implication> m_implications;
    std::vector<std::string> m_wheres;
};

// Every pair within two fields of a sampled requirement. A bound is claimed
// to lie above or below both inputs.
TEST(test_requirement_bounds_are_sound) {
    const std::vector<Requirement> family = requirement_family();
    Claims claims;
    std::size_t asked = 0;
    std::size_t proved = 0;
    for (std::size_t i = 0; i < family.size(); i += 13) {
        const Requirement& lhs = family[i];
        for (const Requirement& rhs : family) {
            const int apart = distance(lhs, rhs);
            if (apart == 0 || apart > 2) {
                continue;
            }
            ++asked;
            const std::string pair =
                lhs.to_string() + " and " + rhs.to_string();
            if (const std::optional<Requirement> join =
                    requirement_join(lhs, rhs)) {
                ++proved;
                claims.add(join->m_ltl, lhs.m_ltl, "join of " + pair);
                claims.add(join->m_ltl, rhs.m_ltl, "join of " + pair);
            }
            if (const std::optional<Requirement> meet =
                    requirement_meet(lhs, rhs)) {
                ++proved;
                claims.add(lhs.m_ltl, meet->m_ltl, "meet of " + pair);
                claims.add(rhs.m_ltl, meet->m_ltl, "meet of " + pair);
            }
        }
    }
    claims.check("requirement_join or requirement_meet");
    expect(proved > 0 && proved < 2 * asked,
           "the sample must prove some bounds, not all");
}

// Specifications one move from a small original: each slot replaced by a
// neighbour from the family, a tombstone, or an appended assumption.
std::vector<Specification> specification_family() {
    const std::vector<Requirement> family = requirement_family();
    const Specification base = original();
    std::vector<Specification> variants = {base};
    // Every second neighbour one field away.
    const auto neighbours = [&](const Requirement& slot) {
        std::vector<Requirement> near;
        bool take = true;
        for (const Requirement& member : family) {
            if (distance(slot, member) == 1) {
                if (take) {
                    near.push_back(member);
                }
                take = !take;
            }
        }
        return near;
    };
    for (const Requirement& near : neighbours(base.m_assumptions[0])) {
        variants.push_back(spec({near}, base.m_guarantees));
    }
    for (std::size_t slot = 0; slot < base.m_guarantees.size(); ++slot) {
        for (const Requirement& near : neighbours(base.m_guarantees[slot])) {
            std::vector<Requirement> guarantees = base.m_guarantees;
            guarantees[slot] = near;
            variants.push_back(spec(base.m_assumptions, guarantees));
        }
        std::vector<Requirement> guarantees = base.m_guarantees;
        guarantees[slot].m_removed = true;
        variants.push_back(spec(base.m_assumptions, guarantees));
    }
    variants.push_back(spec({assumption0(), appended_u()}, base.m_guarantees));
    variants.push_back(spec({assumption0(), appended_v()}, base.m_guarantees));
    return variants;
}

// Every pair of the family: each classified order, and each complete join and
// meet, against SPOT over the whole-specification lowering.
TEST(test_specification_moves_are_sound) {
    const std::vector<Specification> family = specification_family();
    Claims claims;
    std::size_t n_ordered = 0;
    for (const Specification& lhs : family) {
        for (const Specification& rhs : family) {
            const std::string pair =
                lhs.to_string() + "\n  and\n" + rhs.to_string();
            const MoveOrder order = classify_move(lhs, rhs);
            if (order == MoveOrder::Equal || order == MoveOrder::Stronger) {
                claims.add(rhs.to_ltl(), lhs.to_ltl(), "stronger: " + pair);
            }
            if (order == MoveOrder::Equal || order == MoveOrder::Weaker) {
                claims.add(lhs.to_ltl(), rhs.to_ltl(), "weaker: " + pair);
            }
            n_ordered +=
                static_cast<std::size_t>(order != MoveOrder::Incomparable);
            if (const std::optional<Specification> join =
                    assemble(specification_join(lhs, rhs, 1), lhs)) {
                claims.add(join->to_ltl(), lhs.to_ltl(), "join of " + pair);
                claims.add(join->to_ltl(), rhs.to_ltl(), "join of " + pair);
            }
            if (const std::optional<Specification> meet =
                    assemble(specification_meet(lhs, rhs, 1), lhs)) {
                claims.add(lhs.to_ltl(), meet->to_ltl(), "meet of " + pair);
                claims.add(rhs.to_ltl(), meet->to_ltl(), "meet of " + pair);
            }
        }
    }
    claims.check("classify_move or a specification bound");
    expect(n_ordered > family.size(), "the sample must order some pairs");
}

// Every step from every member of the family towards every other. Any member
// can stand in for the original, since all of them pair slot by slot.
TEST(test_steps_towards_are_sound) {
    const std::vector<Specification> family = specification_family();
    Claims claims;
    for (const Specification& current : family) {
        for (const Specification& target : family) {
            for (const Step& step : steps_towards(current, target)) {
                claims.add(step.m_result.to_ltl(), current.to_ltl(),
                           "step from " + current.to_string() + "\n  to\n" +
                               step.m_result.to_string());
            }
        }
    }
    claims.check("steps_towards");
    expect(claims.size() > 0, "the family must offer some steps");
}

}  // namespace
