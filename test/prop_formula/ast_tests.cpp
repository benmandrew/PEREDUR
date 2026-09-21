#include <string>
#include <string_view>

#include "prop_formula.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "prop_formula_ast";

TEST(test_make_atom_and_inspect) {
    const Formula formula = Formula::make_atom("P");
    expect(formula.kind() == Formula::Kind::Atom,
           "prop-formula-ast: atom should report Kind::Atom");
    const auto name = formula.atom_name();
    expect(name.has_value(), "prop-formula-ast: atom should expose its name");
    expect(name.has_value() && *name == "P",
           "prop-formula-ast: atom name should round-trip");
    expect(formula.to_string() == "P",
           "prop-formula-ast: atom should stringify to itself");
}

TEST(test_make_unary_and_inspect) {
    const Formula child = Formula::make_atom("P");
    const Formula formula = Formula::make_unary(Formula::Kind::Not, child);
    expect(formula.kind() == Formula::Kind::Not,
           "prop-formula-ast: unary should report Kind::Not");
    const auto unary_child = formula.unary_child();
    expect(unary_child.has_value(),
           "prop-formula-ast: unary should expose its child");
    expect(unary_child.has_value() && unary_child->to_string() == "P",
           "prop-formula-ast: unary child should round-trip");
    expect(formula.to_string() == "!(P)",
           "prop-formula-ast: unary should stringify canonically");
}

TEST(test_make_binary_and_inspect) {
    const Formula left = Formula::make_atom("P");
    const Formula right = Formula::make_atom("Q");
    const Formula formula =
        Formula::make_binary(Formula::Kind::And, left, right);
    expect(formula.kind() == Formula::Kind::And,
           "prop-formula-ast: binary should report Kind::And");
    const auto children = formula.binary_children();
    expect(children.has_value(),
           "prop-formula-ast: binary should expose its children");
    expect(children.has_value() && children->first.to_string() == "P",
           "prop-formula-ast: left child should round-trip");
    expect(children.has_value() && children->second.to_string() == "Q",
           "prop-formula-ast: right child should round-trip");
    expect(formula.to_string() == "(P) & (Q)",
           "prop-formula-ast: binary should stringify canonically");
}

}  // namespace
