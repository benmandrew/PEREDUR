#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "runner/black.hpp"
#include "runner/ltlfilt.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "black_runner";

TEST(test_satisfiable_simple) {
    SatisfiabilityChecker checker;
    checker.set_timeout(k_test_black_timeout);
    const std::optional<bool> result = checker.check_satisfiability("F p");
    expect(result.has_value() && *result,
           "black-runner: F p should be satisfiable");
}

TEST(test_unsatisfiable_contradiction) {
    SatisfiabilityChecker checker;
    checker.set_timeout(k_test_black_timeout);
    const std::optional<bool> result = checker.check_satisfiability("p & !p");
    expect(result.has_value() && !*result,
           "black-runner: p & !p should be unsatisfiable");
}

TEST(test_satisfiable_ltl) {
    SatisfiabilityChecker checker;
    checker.set_timeout(k_test_black_timeout);
    const std::optional<bool> result = checker.check_satisfiability("G F p");
    expect(result.has_value() && *result,
           "black-runner: G F p should be satisfiable");
}

TEST(test_unsatisfiable_ltl) {
    SatisfiabilityChecker checker;
    checker.set_timeout(k_test_black_timeout);
    const std::optional<bool> result =
        checker.check_satisfiability("G !p & F p");
    expect(result.has_value() && !*result,
           "black-runner: G !p & F p should be unsatisfiable");
}

// This codebase spells its boolean constants as atoms named "true"/"false",
// which black parses as free variables — invoked directly on any case below it
// answers SAT, including the four that are unsatisfiable. check_satisfiability
// gets them right two ways over: ltlfilt folds most to a constant before black
// is consulted, and anything reaching black has its constants rewritten to the
// "True"/"False" spelling black reads as constants.
TEST(test_boolean_constants) {
    struct Case {
        const char* formula;
        bool satisfiable;
    };
    const std::array<Case, 11> cases{{
        {"false", false},
        {"!true", false},
        {"G(false)", false},
        {"p & false", false},
        {"true", true},
        {"G(true)", true},
        {"p | true", true},
        // SPOT's own spellings. These reach check_satisfiability whenever a
        // query has been through ltlfilt, which prints constants as 0/1 rather
        // than as the atoms this codebase writes. black rejects them as a
        // syntax error rather than misreading them, so before they were
        // handled a folded query aborted the whole run.
        {"0", false},
        {"G(0)", false},
        {"1", true},
        {"p | 1", true},
    }};
    SatisfiabilityChecker checker;
    checker.set_timeout(k_test_black_timeout);
    for (const Case& test_case : cases) {
        const std::optional<bool> result =
            checker.check_satisfiability(test_case.formula);
        if (!result.has_value()) {
            fail(std::string("black-runner: ") + test_case.formula +
                 " should be decided, not indeterminate");
        }
        expect(*result == test_case.satisfiable,
               std::string("black-runner: ") + test_case.formula +
                   " should be " +
                   (test_case.satisfiable ? "satisfiable" : "unsatisfiable"));
    }
}

// The constants are rewritten to black's "True"/"False" by whole token, so
// atoms that merely contain or abut them must survive untouched. Reading
// "true_count" as "True_count" would silently rename the variable; reading its
// prefix as a constant would corrupt the formula outright.
TEST(test_constant_rewrite_respects_token_boundaries) {
    SatisfiabilityChecker checker;
    checker.set_timeout(k_test_black_timeout);
    // "true_count" and "True_count" are distinct atoms, so asserting one and
    // negating the other is satisfiable. A substring replacement would rewrite
    // the former into the latter, collide them, and report UNSAT.
    const std::array<const char*, 7> formulae{{
        "true_count & !True_count",
        "is_false & !is_False",
        "falsey & X(!falsey)",
        "truth & X(!truth)",
        // The digit spellings need the same discipline, and the corpus is full
        // of atoms that end in one — "g_1" and "r_1" are the arbiter naming
        // convention. A substring rewrite would turn "g_1" into "g_True",
        // collapsing it onto the distinct atom of that name and reporting
        // UNSAT for a satisfiable pair.
        "g_1 & !g_True",
        "r1 & !rTrue",
        "a0 & !aFalse",
    }};
    for (const char* formula : formulae) {
        const std::optional<bool> result =
            checker.check_satisfiability(formula);
        expect(result.has_value() && *result,
               std::string("black-runner: ") + formula +
                   " should be satisfiable — the atom must not be rewritten as "
                   "a boolean constant");
    }
}

// Regression: the implication check "(from) & !(dest)" reduces to false
// whenever from implies dest, and the genetic algorithm produces such pairs
// constantly with a vacuous G(true) conjunct. Here dest is from plus G(true),
// so from implies dest and the conjunction is unsatisfiable. Asked directly,
// black reads G(true) as a constraint on a free variable and reports SAT.
TEST(test_implication_check_with_vacuous_conjunct) {
    SatisfiabilityChecker checker;
    checker.set_timeout(k_test_black_timeout);
    const std::optional<bool> result = checker.check_satisfiability(
        "((G(a)) & (G(b))) & !(((G(a)) & (G(b))) & (G(true)))");
    expect(result.has_value() && !*result,
           "black-runner: implication check with a vacuous G(true) conjunct "
           "should be unsatisfiable");
}

// black parses "a W b" as weak until, as SPOT writes it, but its
// infinite-trace encoding is unsound on the strong-release its NNF derives
// from a negated one: the loop-closure rules treat only F and U as
// eventualities, so nothing forces M's obligation and black reports SAT for a
// negation that is UNSAT. Invoked directly on any case below it gets them
// wrong. check_satisfiability rewrites W and M away first, so these are the
// cases that pin the rewrite -- each is a validity check, the shape that puts
// a W under exactly one negation.
TEST(test_weak_until_validity) {
    struct Case {
        const char* formula;
        bool satisfiable;
    };
    const std::array<Case, 5> cases{{
        // The three below are guarantees the 2026-08-07 elitism campaign
        // actually wrote out as repairs: each is valid, so each says nothing,
        // and the vacuity screen should have caught all three. ltlfilt's
        // --simplify does not fold any of them to a constant, so they reach
        // black and pin the rewrite rather than the constant-folding path in
        // front of it.
        {"!(F((r_1) W (X(!(r_1)))))", false},
        {"!(G(F((g1) W (X(!(g1))))))", false},
        {"!(F(((g_1) W (!(F(r_1)))) | (!(X(X(g_1))))))", false},
        // A W that genuinely is falsifiable must stay falsifiable: the rewrite
        // has to preserve the answer, not force every weak-until query to
        // UNSAT.
        {"!((a) W (b))", true},
        // G a entails a W b, the weak operator's defining case. ltlfilt folds
        // this one to a constant before black is consulted, so it pins the
        // first line of defence rather than the rewrite -- kept for the same
        // reason test_boolean_constants keeps its folded cases.
        {"!((G(a)) -> ((a) W (b)))", false},
    }};
    SatisfiabilityChecker checker;
    checker.set_timeout(k_test_black_timeout);
    for (const Case& test_case : cases) {
        const std::optional<bool> result =
            checker.check_satisfiability(test_case.formula);
        if (!result.has_value()) {
            fail(std::string("black-runner: ") + test_case.formula +
                 " should be decided, not indeterminate");
            continue;
        }
        expect(*result == test_case.satisfiable,
               std::string("black-runner: ") + test_case.formula +
                   " should be " +
                   (test_case.satisfiable ? "satisfiable" : "unsatisfiable"));
    }
}

// The lexical guard must not fire on atoms that merely contain W or M, or
// every formula over such atoms would pay an ltlfilt exec it does not need.
TEST(test_weak_operator_scan_respects_token_boundaries) {
    expect(!has_weak_operator("(G(WAIT)) & (F(ALARM))"),
           "ltlfilt: W and M inside atom names are not operators");
    expect(!has_weak_operator("(G(m_write)) | (F(w_M))"),
           "ltlfilt: W and M inside underscored atoms are not operators");
    expect(has_weak_operator("(a) W (b)"),
           "ltlfilt: a standalone W is an operator");
    expect(has_weak_operator("(a) M (b)"),
           "ltlfilt: a standalone M is an operator");
}

TEST(test_spot_satisfiable_decides_both_ways) {
    const auto budget = std::chrono::milliseconds(5000);
    expect(spot_satisfiable("F p", budget) == std::optional<bool>(true),
           "spot: F p should be satisfiable");
    expect(spot_satisfiable("(G !p) & (F p)", budget) ==
               std::optional<bool>(false),
           "spot: (G !p) & (F p) should be unsatisfiable");
    // Exit status 2, which must read as no answer rather than as UNSAT: a
    // parse error reported as unsatisfiable would drop a candidate silently.
    expect(!spot_satisfiable("a &&& b", budget).has_value(),
           "spot: a formula SPOT cannot parse yields no answer");
}

// SPOT's own blowup case, and the reason the first stage carries a budget at
// all. A chain of `<->` over `G F` terms builds an automaton large enough that
// construction runs unboundedly: left without a deadline one of these reached
// 4.5GB resident and was still growing 24 minutes later. What is pinned here
// is that the deadline is honoured -- execute_and_capture kills the process
// group, so nothing survives the call -- and that the result reads as no
// answer rather than as UNSAT, which would drop a candidate over a budget.
TEST(test_spot_blowup_is_bounded_not_answered) {
    std::string formula = "G(F(p0))";
    for (int term = 1; term < 12; ++term) {
        formula.append(" <-> G(F(p").append(std::to_string(term)).append("))");
    }
    const auto start = std::chrono::steady_clock::now();
    const std::optional<bool> result =
        spot_satisfiable(formula, std::chrono::milliseconds(200));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    expect(!result.has_value(),
           "spot: an automaton-hostile formula yields no answer rather than "
           "a verdict");
    expect(elapsed < std::chrono::seconds(5),
           "spot: the budget is honoured rather than run to completion");
}

// The case the routing exists for. black cannot decide this within any budget
// the engine gives it -- at an X-chain depth of 40 it takes seconds, and the
// depth grows with every generation the search runs -- while SPOT answers by
// automaton emptiness in milliseconds regardless of depth. Before SPOT took
// the first stage this returned nullopt, and the implication filter kept every
// candidate it could not judge.
TEST(test_deep_nested_x_implication_is_decided) {
    const auto x_chain = [](int depth) {
        std::string chain = "q";
        for (int step = 0; step < depth; ++step) {
            std::string wrapped = "(q | X(";
            wrapped.append(chain).append("))");
            chain = std::move(wrapped);
        }
        return chain;
    };
    const std::string chain = x_chain(60);
    const std::string wider = x_chain(61);
    SatisfiabilityChecker checker;
    checker.set_timeout(k_test_black_timeout);
    // "within 60" implies "within 61", so the implication holds and the query
    // asking whether it fails is unsatisfiable.
    const std::optional<bool> sat = checker.check_satisfiability(
        "(G(p -> " + chain + ")) & !(G(p -> " + wider + "))",
        QueryPolarity::ExpectUnsat);
    expect(sat.has_value() && !*sat,
           "routing: a deep nested-X implication is decided unsatisfiable");
}

// Polarity governs escalation alone. Both spellings must agree on every
// formula either backend can decide, or the filters would disagree with one
// another over the same candidate.
TEST(test_polarity_does_not_change_the_answer) {
    const std::array<std::pair<const char*, bool>, 4> cases{
        {{"F p", true},
         {"p & !p", false},
         {"G F p", true},
         {"(G !p) & (F p)", false}}};
    for (const auto& [formula, satisfiable] : cases) {
        SatisfiabilityChecker sat_checker;
        SatisfiabilityChecker unsat_checker;
        sat_checker.set_timeout(k_test_black_timeout);
        unsat_checker.set_timeout(k_test_black_timeout);
        const std::optional<bool> as_sat =
            sat_checker.check_satisfiability(formula, QueryPolarity::ExpectSat);
        const std::optional<bool> as_unsat = unsat_checker.check_satisfiability(
            formula, QueryPolarity::ExpectUnsat);
        std::string message(
            "routing: polarity must not change the answer for ");
        message.append(formula);
        expect(as_sat == std::optional<bool>(satisfiable) && as_sat == as_unsat,
               message);
    }
}

}  // namespace
