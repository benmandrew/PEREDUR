#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "prop_formula.hpp"
#include "runner/ltlfilt.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "ltlfilt_runner";

// simplify() folds the boolean constants through the temporal operators, and
// prop_formula_temporal pins the shape each identity produces. That check is
// against what the identities were written to do, so it cannot catch one
// written backwards. This asks ltlfilt whether the output still means the
// input, over every temporal operator crossed with a constant in each operand
// position, which is where an identity filed the wrong way round shows up.
//
// ltl_equivalent reports an inconclusive answer as equivalent, so this can
// fail but never falsely fail.
TEST(test_temporal_constant_folds_preserve_meaning) {
    const std::vector<std::string> unary = {"X", "F", "G"};
    const std::vector<std::string> binary = {"U", "W", "R"};
    const std::vector<std::string> constants = {"true", "false"};
    std::vector<std::string> subjects;
    const auto join = [](std::initializer_list<std::string_view> parts) {
        std::string text;
        for (const std::string_view part : parts) {
            text.append(part);
        }
        return text;
    };
    for (const std::string& oper : unary) {
        for (const std::string& constant : constants) {
            subjects.push_back(join({oper, "(", constant, ")"}));
        }
    }
    for (const std::string& oper : binary) {
        for (const std::string& constant : constants) {
            subjects.push_back(join({"(p) ", oper, " (", constant, ")"}));
            subjects.push_back(join({"(", constant, ") ", oper, " (p)"}));
        }
    }
    // The nested case: a constant the search built under an operator that is
    // itself an operand, which is where the post-order walk has to fire twice.
    subjects.emplace_back("G((true) W (false))");
    subjects.emplace_back("((p) U (false)) | (G(q))");

    for (const std::string& text : subjects) {
        Formula formula(text);
        formula.simplify();
        const std::string folded = formula.to_string();
        expect(ltl_equivalent(text, folded),
               join({"ltlfilt-runner: simplify() changed the meaning of ", text,
                     ", which became ", folded}));
    }
}

}  // namespace
