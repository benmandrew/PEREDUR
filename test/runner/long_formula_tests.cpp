// Formulas longer than one argv string may be. Linux caps a single argument
// at MAX_ARG_STRLEN (131072 bytes), and a runner that passed its formula with
// `-f` failed the exec with E2BIG: the tool never ran, and its empty output
// surfaced as a parse error (a 68-guarantee FRETISH import aborted `realize`
// this way). Every runner now hands the formula over on stdin, and each is
// driven here past the limit to a real verdict.

#include <chrono>
#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "runner/black.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/spot.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "long_formula_runner";

constexpr std::size_t k_max_arg_strlen = 131072;
constexpr std::size_t k_atom_count = 140;
constexpr std::chrono::milliseconds k_generous_timeout{60'000};

// Distinct, lowercase atoms padded to about 1 KB each, so a conjunction of
// k_atom_count of them clears the limit with room to spare while staying a
// trivial query for every tool. Few long atoms rather than many short ones,
// like the lowered specification that hit the limit: thousands of conjuncts
// stall the in-process canonicalisation behind the cache keys long before any
// tool runs.
std::vector<std::string> long_atoms() {
    std::vector<std::string> atoms;
    atoms.reserve(k_atom_count);
    const std::string padding(1000, 'x');
    for (std::size_t idx = 0; idx < k_atom_count; ++idx) {
        atoms.push_back("p" + padding + std::to_string(idx));
    }
    return atoms;
}

std::string conjunction(const std::vector<std::string>& atoms) {
    std::string formula;
    for (const std::string& atom : atoms) {
        if (!formula.empty()) {
            formula += " & ";
        }
        formula += atom;
    }
    expect(formula.size() > k_max_arg_strlen,
           "long-formula: the test formula must exceed MAX_ARG_STRLEN, got " +
               std::to_string(formula.size()) + " bytes");
    return formula;
}

TEST(test_ltlsynt_decides_a_formula_over_the_argv_limit) {
    const std::vector<std::string> outputs = long_atoms();
    const std::string formula = "G(" + conjunction(outputs) + ")";
    RealizabilityChecker checker;
    std::optional<bool> realizable;
    std::optional<bool> forced_input;
    try {
        realizable = checker.check_realizability_ltl(formula, {"inp"}, outputs);
        forced_input = checker.check_realizability_ltl(formula + " & G(inp)",
                                                       {"inp"}, outputs);
    } catch (const std::exception& error) {
        fail(std::string("long-formula: ltlsynt threw instead of deciding: ") +
             error.what());
        return;
    }
    expect(realizable == std::optional<bool>(true),
           "long-formula: G(outputs) should be realizable");
    expect(forced_input == std::optional<bool>(false),
           "long-formula: requiring G(input) should be unrealizable");
}

TEST(test_ltl2tgba_translates_a_formula_over_the_argv_limit) {
    const std::string formula = "F(" + conjunction(long_atoms()) + ")";
    std::string hoa;
    try {
        hoa = run_ltl2tgba_for_counting(formula);
    } catch (const std::exception& error) {
        fail(std::string("long-formula: ltl2tgba threw instead of "
                         "translating: ") +
             error.what());
        return;
    }
    expect(hoa.find("HOA: v1") != std::string::npos &&
               hoa.find("--END--") != std::string::npos,
           "long-formula: ltl2tgba should return a whole automaton");
}

TEST(test_ltlfilt_decides_a_formula_over_the_argv_limit) {
    const std::vector<std::string> atoms = long_atoms();
    const std::string formula = conjunction(atoms);
    expect(spot_satisfiable(formula, k_generous_timeout) ==
               std::optional<bool>(true),
           "long-formula: a conjunction of atoms should be satisfiable");
    expect(spot_satisfiable(formula + " & !" + atoms.front(),
                            k_generous_timeout) == std::optional<bool>(false),
           "long-formula: a conjunction with one atom negated beside it should "
           "be unsatisfiable");
    // Only one side is long: it goes on stdin and the short side stays on
    // --equivalent-to.
    expect(!ltl_equivalent(formula, atoms.front()),
           "long-formula: a long conjunction is not equivalent to one atom");
    // Both sides are long, so the query becomes one exclusive or on stdin.
    std::string reordered;
    for (auto atom = atoms.rbegin(); atom != atoms.rend(); ++atom) {
        reordered += reordered.empty() ? *atom : " & " + *atom;
    }
    expect(ltl_equivalent(formula, reordered),
           "long-formula: a conjunction is equivalent to its reversal");
    expect(!ltl_equivalent(formula, reordered + " & q"),
           "long-formula: an extra conjunct breaks equivalence");
    expect(!simplify_ltl(formula).empty(),
           "long-formula: simplify_ltl should return a formula");
    const std::optional<std::string> rewritten =
        rewrite_weak_operators("(" + formula + ") W q");
    expect(rewritten.has_value() && !has_weak_operator(*rewritten),
           "long-formula: --remove-wm should rewrite a long formula");
}

TEST(test_black_decides_a_formula_over_the_argv_limit) {
    const std::vector<std::string> atoms = long_atoms();
    const std::string formula = conjunction(atoms);
    SatisfiabilityChecker checker;
    checker.set_timeout(k_generous_timeout);
    // A SPOT budget no exec can meet and no simplification pass, so each query
    // falls through to black itself.
    checker.set_spot_budget(std::chrono::milliseconds{1});
    checker.set_simplify(false);
    const std::size_t calls_before = SatisfiabilityChecker::n_black_calls;
    std::optional<bool> sat;
    std::optional<bool> unsat;
    try {
        sat = checker.check_satisfiability("G(" + formula + ")");
        unsat = checker.check_satisfiability("G(" + formula + ") & F(!" +
                                             atoms.back() + ")");
    } catch (const std::exception& error) {
        fail(std::string("long-formula: black threw instead of deciding: ") +
             error.what());
        return;
    }
    expect(SatisfiabilityChecker::n_black_calls == calls_before + 2,
           "long-formula: both queries should have reached black");
    expect(sat == std::optional<bool>(true),
           "long-formula: black should find G(conjunction) satisfiable");
    expect(unsat == std::optional<bool>(false),
           "long-formula: black should find the contradiction unsatisfiable");
}

}  // namespace
