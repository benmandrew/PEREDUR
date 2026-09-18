#include <optional>
#include <string>
#include <string_view>

#include "prop_formula.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "prop_formula_rewrite";

TEST(test_rewrite_post_order_identity) {
    const Formula formula = Formula::make_binary(
        Formula::Kind::And, Formula::make_atom("P"), Formula::make_atom("Q"));
    const Formula rewritten = formula.rewrite_post_order(
        [](const Formula&) -> std::optional<Formula> { return std::nullopt; });
    expect(rewritten.to_string() == "(P) & (Q)",
           "prop-formula-rewrite: identity rewrite should preserve formula");
}

TEST(test_rewrite_post_order_rewrites_children_before_parent) {
    const Formula formula = Formula::make_binary(
        Formula::Kind::And, Formula::make_atom("P"), Formula::make_atom("Q"));
    const Formula rewritten = formula.rewrite_post_order(
        [](const Formula& subtree) -> std::optional<Formula> {
            if (subtree.atom_name().has_value() &&
                subtree.atom_name().value() == "P") {
                return Formula::make_atom("R");
            }
            if (subtree.atom_name().has_value() &&
                subtree.atom_name().value() == "Q") {
                return Formula::make_atom("S");
            }
            if (subtree.to_string() == "(R) & (S)") {
                return Formula::make_atom("T");
            }
            return std::nullopt;
        });
    expect(
        rewritten.to_string() == "T",
        "prop-formula-rewrite: parent callback should see rewritten children");
}

TEST(test_simplify_idempotence) {
    Formula fml = Formula::make_binary(
        Formula::Kind::And, Formula::make_atom("A"), Formula::make_atom("A"));
    fml.simplify();
    expect(fml.to_string() == "A", "simplify: A & A -> A");

    fml = Formula::make_binary(Formula::Kind::Or, Formula::make_atom("A"),
                               Formula::make_atom("A"));
    fml.simplify();
    expect(fml.to_string() == "A", "simplify: A | A -> A");
}

TEST(test_simplify_tautology) {
    Formula fml =
        Formula::make_binary(Formula::Kind::Implies, Formula::make_atom("A"),
                             Formula::make_atom("A"));
    fml.simplify();
    expect(fml.to_string() == "true", "simplify: A -> A -> true");

    fml = Formula::make_binary(Formula::Kind::Iff, Formula::make_atom("A"),
                               Formula::make_atom("A"));
    fml.simplify();
    expect(fml.to_string() == "true", "simplify: A <-> A -> true");
}

TEST(test_simplify_excluded_middle) {
    Formula fml = Formula::make_binary(
        Formula::Kind::Or, Formula::make_atom("A"),
        Formula::make_unary(Formula::Kind::Not, Formula::make_atom("A")));
    fml.simplify();
    expect(fml.to_string() == "true", "simplify: A | !A -> true");

    fml = Formula::make_binary(
        Formula::Kind::Or,
        Formula::make_unary(Formula::Kind::Not, Formula::make_atom("A")),
        Formula::make_atom("A"));
    fml.simplify();
    expect(fml.to_string() == "true", "simplify: !A | A -> true");
}

TEST(test_simplify_with_true) {
    const Formula tru;

    Formula fml =
        Formula::make_binary(Formula::Kind::And, Formula::make_atom("A"), tru);
    fml.simplify();
    expect(fml.to_string() == "A", "simplify: A & true -> A");

    fml =
        Formula::make_binary(Formula::Kind::And, tru, Formula::make_atom("A"));
    fml.simplify();
    expect(fml.to_string() == "A", "simplify: true & A -> A");

    fml = Formula::make_binary(Formula::Kind::Or, Formula::make_atom("A"), tru);
    fml.simplify();
    expect(fml.to_string() == "true", "simplify: A | true -> true");

    fml = Formula::make_binary(Formula::Kind::Implies, tru,
                               Formula::make_atom("A"));
    fml.simplify();
    expect(fml.to_string() == "A", "simplify: true -> A -> A");

    fml = Formula::make_binary(Formula::Kind::Implies, Formula::make_atom("A"),
                               tru);
    fml.simplify();
    expect(fml.to_string() == "true", "simplify: A -> true -> true");

    fml =
        Formula::make_binary(Formula::Kind::Iff, Formula::make_atom("A"), tru);
    fml.simplify();
    expect(fml.to_string() == "A", "simplify: A <-> true -> A");
}

TEST(test_simplify_double_negation) {
    Formula fml = Formula::make_unary(
        Formula::Kind::Not,
        Formula::make_unary(Formula::Kind::Not, Formula::make_atom("A")));
    fml.simplify();
    expect(fml.to_string() == "A", "simplify: !!A -> A");
}

TEST(test_simplify_negated_constants) {
    Formula fml = Formula::make_unary(Formula::Kind::Not, Formula{});
    fml.simplify();
    expect(fml.to_string() == "false", "simplify: !true -> false");

    fml = Formula::make_unary(Formula::Kind::Not, Formula::make_atom("false"));
    fml.simplify();
    expect(fml.to_string() == "true", "simplify: !false -> true");
}

TEST(test_simplify_compound) {
    // (A & A) -> A  =>  A -> A  =>  true
    Formula inner = Formula::make_binary(
        Formula::Kind::And, Formula::make_atom("A"), Formula::make_atom("A"));
    Formula fml = Formula::make_binary(Formula::Kind::Implies, inner,
                                       Formula::make_atom("A"));
    fml.simplify();
    expect(fml.to_string() == "true", "simplify: (A & A) -> A -> true");
}

TEST(test_simplify_contradiction) {
    Formula fml = Formula::make_binary(
        Formula::Kind::And, Formula::make_atom("A"),
        Formula::make_unary(Formula::Kind::Not, Formula::make_atom("A")));
    fml.simplify();
    expect(fml.to_string() == "false", "simplify: A & !A -> false");

    fml = Formula::make_binary(
        Formula::Kind::And,
        Formula::make_unary(Formula::Kind::Not, Formula::make_atom("A")),
        Formula::make_atom("A"));
    fml.simplify();
    expect(fml.to_string() == "false", "simplify: !A & A -> false");

    fml = Formula::make_binary(
        Formula::Kind::Iff, Formula::make_atom("A"),
        Formula::make_unary(Formula::Kind::Not, Formula::make_atom("A")));
    fml.simplify();
    expect(fml.to_string() == "false", "simplify: A <-> !A -> false");
}

TEST(test_simplify_with_false) {
    const Formula fls("false");

    Formula fml =
        Formula::make_binary(Formula::Kind::And, Formula::make_atom("A"), fls);
    fml.simplify();
    expect(fml.to_string() == "false", "simplify: A & false -> false");

    fml = Formula::make_binary(Formula::Kind::Or, Formula::make_atom("A"), fls);
    fml.simplify();
    expect(fml.to_string() == "A", "simplify: A | false -> A");

    fml = Formula::make_binary(Formula::Kind::Or, fls, Formula::make_atom("A"));
    fml.simplify();
    expect(fml.to_string() == "A", "simplify: false | A -> A");

    fml = Formula::make_binary(Formula::Kind::Implies, fls,
                               Formula::make_atom("A"));
    fml.simplify();
    expect(fml.to_string() == "true", "simplify: false -> A -> true");

    fml = Formula::make_binary(Formula::Kind::Implies, Formula::make_atom("A"),
                               fls);
    fml.simplify();
    expect(fml.to_string() == "!(A)", "simplify: A -> false -> !A");

    fml =
        Formula::make_binary(Formula::Kind::Iff, Formula::make_atom("A"), fls);
    fml.simplify();
    expect(fml.to_string() == "!(A)", "simplify: A <-> false -> !A");
}

TEST(test_simplify_pointless_weakening) {
    // A guarantee p weakened to !(!p <-> p) is semantically true, a pointless
    // weakening that the simplifier must fold to the literal true.
    Formula iff = Formula::make_binary(
        Formula::Kind::Iff,
        Formula::make_unary(Formula::Kind::Not, Formula::make_atom("p")),
        Formula::make_atom("p"));
    Formula fml = Formula::make_unary(Formula::Kind::Not, iff);
    fml.simplify();
    expect(fml.to_string() == "true", "simplify: !(!p <-> p) -> true");
}

TEST(test_simplify_cascade) {
    // A single post-order pass collapses a subformula to a constant and lets
    // that constant ripple through every ancestor: (A & !A) reduces to false
    // and (B | !B) to true, so (false <-> true) is false and the outer negation
    // is true.
    Formula contradiction = Formula::make_binary(
        Formula::Kind::And, Formula::make_atom("A"),
        Formula::make_unary(Formula::Kind::Not, Formula::make_atom("A")));
    Formula tautology = Formula::make_binary(
        Formula::Kind::Or, Formula::make_atom("B"),
        Formula::make_unary(Formula::Kind::Not, Formula::make_atom("B")));
    Formula fml = Formula::make_unary(
        Formula::Kind::Not,
        Formula::make_binary(Formula::Kind::Iff, contradiction, tautology));
    fml.simplify();
    expect(fml.to_string() == "true",
           "simplify: !((A & !A) <-> (B | !B)) -> true");

    // A rule whose output is itself reducible (A -> false yields !A) must not
    // leave a residual double negation: ((A -> false) -> false) is A.
    Formula inner = Formula::make_binary(
        Formula::Kind::Implies, Formula::make_atom("A"), Formula("false"));
    Formula outer =
        Formula::make_binary(Formula::Kind::Implies, inner, Formula("false"));
    outer.simplify();
    expect(outer.to_string() == "A", "simplify: (A -> false) -> false -> A");
}

}  // namespace
