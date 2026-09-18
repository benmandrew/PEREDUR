#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.hpp"
#include "filter/vacuity.hpp"
#include "genetic/generation.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "vacuity_filter";

Requirement continual(const std::string& response, const Timing& tim) {
    return Requirement(Formula("true"), Formula(response), tim);
}

Requirement conditional(const std::string& condition,
                        const std::string& response) {
    return Requirement(Formula(condition), Formula(response),
                       timing::immediately());
}

Specification with_assumptions(std::vector<Requirement> assumptions) {
    return Specification(std::move(assumptions),
                         {continual("grant", timing::immediately())}, {"req"},
                         {"grant"});
}

Specification with_guarantees(std::vector<Requirement> guarantees) {
    return Specification({}, std::move(guarantees), {"req"}, {"grant"});
}

TEST(test_no_assumptions_is_not_vacuous) {
    SatisfiabilityChecker checker;
    const Specification spec = with_assumptions({});
    expect(!specification_has_unsatisfiable_assumptions(spec, checker),
           "vacuity: a spec with no assumptions is not vacuous");
}

TEST(test_satisfiable_assumption_is_not_vacuous) {
    SatisfiabilityChecker checker;
    const Specification spec =
        with_assumptions({continual("req", timing::within_ticks(6))});
    expect(!specification_has_unsatisfiable_assumptions(spec, checker),
           "vacuity: 'within 6 ticks' is satisfiable and not vacuous");
}

// G(!R & X(!R & ... & X R)) asserts R at tick n+1 relative to every timepoint,
// and !R at that same tick relative to the timepoint n+1 later. This is the
// candidate the strengthening direction reaches via within n -> after n-1.
TEST(test_after_ticks_under_continual_true_is_vacuous) {
    SatisfiabilityChecker checker;
    const Specification spec =
        with_assumptions({continual("req", timing::after_ticks(5))});
    expect(specification_has_unsatisfiable_assumptions(spec, checker),
           "vacuity: 'after 5 ticks' under a continual true condition is "
           "unsatisfiable");
}

// Each assumption is satisfiable alone; only their conjunction contradicts.
// This is why the assumption side stays one joint query while the guarantee
// side splits per formula: validity distributes over conjunction and
// unsatisfiability does not.
TEST(test_jointly_unsatisfiable_assumptions_are_vacuous) {
    SatisfiabilityChecker checker;
    const Requirement always_req = continual("req", timing::always());
    const Requirement never_req = continual("!req", timing::always());
    expect(checker.check_satisfiability(always_req.m_ltl).value_or(false) &&
               checker.check_satisfiability(never_req.m_ltl).value_or(false),
           "vacuity: 'G req' and 'G !req' are each satisfiable alone");
    const Specification spec = with_assumptions({always_req, never_req});
    expect(specification_has_unsatisfiable_assumptions(spec, checker),
           "vacuity: 'G req' and 'G !req' are jointly unsatisfiable");
}

TEST(test_filter_drops_only_the_vacuous_spec) {
    SatisfiabilityChecker checker;
    FilterFunction filter = make_vacuity_filter(checker);
    const Specification good =
        with_assumptions({continual("req", timing::within_ticks(6))});
    const Specification vacuous =
        with_assumptions({continual("req", timing::after_ticks(5))});
    const auto survivors = filter({good, vacuous});
    expect(survivors.size() == 1,
           "vacuity filter: exactly one of the two specs should survive");
    expect(!specification_has_unsatisfiable_assumptions(survivors[0], checker),
           "vacuity filter: the survivor should be the satisfiable spec");
}

// --- the syntactic screen ---

// The condition sits only in the antecedent of the lowered implication, so a
// false one is vacuous under every timing. Both specs below have satisfiable
// assumptions, so the solver would keep them: the screen is what rejects them,
// and it runs first.
TEST(test_false_condition_is_trivially_vacuous) {
    SatisfiabilityChecker checker;
    const Specification in_guarantee =
        with_guarantees({conditional("false", "grant")});
    expect(specification_is_trivially_vacuous(in_guarantee),
           "vacuity: a false condition in a guarantee is trivially vacuous");
    expect(specification_is_vacuous(in_guarantee, checker),
           "vacuity: the syntactic screen rejects what the assumption check "
           "would keep");

    const Specification in_assumption = Specification(
        {conditional("false", "req")}, {continual("grant", timing::always())},
        {"req"}, {"grant"});
    expect(specification_is_trivially_vacuous(in_assumption),
           "vacuity: a false condition in an assumption is trivially vacuous");
}

// A true response is still rejected under an ordinary timing -- but by the
// semantic check reading the lowered formula, not by the screen. There is
// deliberately no syntactic test for it; see the AfterTicks case below.
TEST(test_true_response_guarantee_is_rejected_semantically) {
    SatisfiabilityChecker checker;
    for (const Timing& tim : {timing::immediately(), timing::always()}) {
        const Specification spec =
            with_guarantees({Requirement(Formula("req"), Formula("true"), tim),
                             conditional("req", "grant")});
        expect(!specification_is_trivially_vacuous(spec),
               "vacuity: a true response is not caught by the syntactic "
               "screen");
        expect(specification_has_valid_guarantee(spec, checker),
               "vacuity: a true response under an ordinary timing is a valid "
               "guarantee");
        expect(specification_is_vacuous(spec, checker),
               "vacuity: a true response under an ordinary timing is "
               "rejected");
    }
}

// Regression. `after n ticks` is the one timing where a true response is not a
// no-op: expand_after negates the response, so the requirement lowers to
// G(req -> (!true & X(...))) == G(!req) -- the strongest guarantee expressible,
// not an empty one. A syntactic test on the response would reject this exactly
// backwards. The semantic check reads the lowered formula, finds its negation
// satisfiable, and keeps the candidate.
TEST(test_true_response_under_after_ticks_is_kept) {
    SatisfiabilityChecker checker;
    const Specification spec = with_guarantees(
        {Requirement(Formula("req"), Formula("true"), timing::after_ticks(2))});
    expect(!specification_is_trivially_vacuous(spec),
           "vacuity: a true response under after_ticks is not trivially "
           "vacuous");
    expect(!specification_has_valid_guarantee(spec, checker),
           "vacuity: a true response under after_ticks lowers to G(!req), "
           "which is falsifiable");
    expect(!specification_is_vacuous(spec, checker),
           "vacuity: a true response under after_ticks is kept, not rejected");
}

// Guarantees only. A valid assumption neither weakens what the system must do
// nor makes the implication a tautology, so the scan never looks at one.
TEST(test_true_assumption_response_is_not_vacuous) {
    SatisfiabilityChecker checker;
    const Specification spec = with_assumptions({conditional("req", "true")});
    expect(!specification_has_valid_guarantee(spec, checker),
           "vacuity: a true response in an assumption is not a valid "
           "guarantee");
    expect(!specification_is_vacuous(spec, checker),
           "vacuity: a true response in an assumption is kept");
}

TEST(test_filter_drops_the_vacuous_specs) {
    SatisfiabilityChecker checker;
    FilterFunction filter = make_vacuity_filter(checker);
    const Specification good = with_guarantees({conditional("req", "grant")});
    const auto survivors =
        filter({good, with_guarantees({conditional("false", "grant")}),
                with_guarantees({conditional("req", "true")})});
    expect(survivors.size() == 1 && survivors[0] == good,
           "vacuity filter: the false condition and the true guarantee are "
           "both dropped");
}

// --- the semantic guarantee half ---

// `grant | !grant` is valid but its root is an Or, so the root-only syntactic
// test cannot see it. The same holds for a condition of `req & !req`: the
// requirement lowers to G((req & !req) -> grant), valid because the antecedent
// never holds. Neither Formula nor Requirement simplifies on construction, so
// this pins that the semantic verdict does not depend on simplify() having run.
TEST(test_valid_guarantee_is_caught_only_semantically) {
    SatisfiabilityChecker checker;
    const Specification tautological_response =
        with_guarantees({conditional("req", "grant | !grant")});
    expect(!specification_is_trivially_vacuous(tautological_response),
           "vacuity: a tautological response is not syntactically trivial");
    expect(specification_has_valid_guarantee(tautological_response, checker),
           "vacuity: a tautological response is a valid guarantee");

    const Specification contradictory_condition =
        with_guarantees({conditional("req & !req", "grant")});
    expect(!specification_is_trivially_vacuous(contradictory_condition),
           "vacuity: an unsimplified contradictory condition is not "
           "syntactically trivial");
    expect(specification_has_valid_guarantee(contradictory_condition, checker),
           "vacuity: an unsimplified contradictory condition still yields a "
           "valid guarantee");
}

TEST(test_substantive_guarantee_is_not_valid) {
    SatisfiabilityChecker checker;
    expect(!specification_has_valid_guarantee(
               with_guarantees({conditional("req", "grant")}), checker),
           "vacuity: G(req -> grant) is falsifiable and demands something");
}

// Per guarantee, not over the conjunction: one gutted conjunct is enough, and
// the verdict does not depend on where in the list it sits -- the scan returns
// on the first valid guarantee it reaches.
TEST(test_one_valid_guarantee_among_substantive_ones_rejects) {
    SatisfiabilityChecker checker;
    const Requirement valid = conditional("req", "grant | !grant");
    const Requirement substantive = conditional("req", "grant");
    expect(specification_has_valid_guarantee(
               with_guarantees({valid, substantive}), checker),
           "vacuity: a valid guarantee is caught when it comes first");
    expect(specification_has_valid_guarantee(
               with_guarantees({substantive, valid}), checker),
           "vacuity: a valid guarantee is caught when it comes last, so the "
           "early exit does not change the verdict");
}

// A non-answer keeps the candidate, matching the assumption side. 1ms cannot
// spawn a subprocess, and `!(G(req -> grant))` does not fold to a constant, so
// the query reaches the deadline rather than being decided ahead of it.
TEST(test_guarantee_timeout_keeps_the_candidate) {
    SatisfiabilityChecker checker;
    checker.set_timeout(std::chrono::milliseconds(1));
    const std::size_t before = SatisfiabilityChecker::n_timeouts;
    expect(!specification_has_valid_guarantee(
               with_guarantees({conditional("req", "grant")}), checker),
           "vacuity: a timed-out guarantee query keeps the candidate");
    expect(SatisfiabilityChecker::n_timeouts == before + 1,
           "vacuity: the guarantee query did time out, so the keep was the "
           "timeout policy and not an answer");
}

// --- the flag gates the whole filter, syntactic half included ---

bool chain_has_vacuity_stage(const std::vector<FilterFunction>& filters) {
    return std::any_of(filters.begin(), filters.end(),
                       [](const FilterFunction& filter) {
                           return filter.name() == "vacuity";
                       });
}

TEST(test_chain_always_carries_the_vacuity_stage) {
    SatisfiabilityChecker checker;
    const Specification original =
        with_guarantees({conditional("req", "grant")});
    const Specification vacuous = with_guarantees({conditional("req", "true")});

    const std::vector<FilterFunction> filters =
        get_filter_functions(original, checker);
    expect(chain_has_vacuity_stage(filters),
           "vacuity: the chain always carries a vacuity stage");
    expect(filter_population({original, vacuous}, filters).size() == 1,
           "vacuity: the vacuous candidate is dropped per generation");
}

}  // namespace
