#include <optional>
#include <string>
#include <string_view>

#include "prop_formula.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "prop_formula_temporal";

Formula atom(const std::string& name) { return Formula::make_atom(name); }

// --- construction + to_string (SPOT syntax) ---

// The boolean constants fold through every temporal operator. Each identity is
// read off the fixpoint expansion, and the two that do not simply annihilate
// -- `phi W false == G phi` and `false R psi == G psi` -- are the ones worth
// getting wrong. Shape only; ltlfilt_runner checks the same list for meaning,
// which is the half that would catch an identity filed backwards.
TEST(test_temporal_constant_folds) {
    const Formula truth = Formula("true");
    const Formula falsity = Formula("false");
    const Formula proposition = Formula::make_atom("p");

    auto folds = [&](Formula::Kind kind, const Formula& child) {
        Formula formula = Formula::make_unary(kind, child);
        formula.simplify();
        return formula.to_string();
    };
    for (const Formula::Kind kind :
         {Formula::Kind::Next, Formula::Kind::Eventually,
          Formula::Kind::Globally}) {
        expect(folds(kind, truth) == "true",
               "prop-formula-temporal: a unary operator over true should fold "
               "to true, got " +
                   folds(kind, truth));
        expect(folds(kind, falsity) == "false",
               "prop-formula-temporal: a unary operator over false should fold "
               "to false, got " +
                   folds(kind, falsity));
    }

    auto binary = [](Formula::Kind kind, const Formula& lhs,
                     const Formula& rhs) {
        Formula formula = Formula::make_binary(kind, lhs, rhs);
        formula.simplify();
        return formula.to_string();
    };
    // phi U true == true; phi U false == false; false U psi == psi;
    // true U psi == F psi.
    expect(binary(Formula::Kind::Until, proposition, truth) == "true",
           "prop-formula-temporal: phi U true should fold to true");
    expect(binary(Formula::Kind::Until, proposition, falsity) == "false",
           "prop-formula-temporal: phi U false should fold to false");
    expect(binary(Formula::Kind::Until, falsity, proposition) == "p",
           "prop-formula-temporal: false U psi should fold to psi");
    expect(binary(Formula::Kind::Until, truth, proposition) == "F(p)",
           "prop-formula-temporal: true U psi should fold to F psi, got " +
               binary(Formula::Kind::Until, truth, proposition));
    // phi W true == true; true W psi == true; false W psi == psi;
    // phi W false == G phi.
    expect(binary(Formula::Kind::WeakUntil, proposition, truth) == "true",
           "prop-formula-temporal: phi W true should fold to true");
    expect(binary(Formula::Kind::WeakUntil, truth, proposition) == "true",
           "prop-formula-temporal: true W psi should fold to true");
    expect(binary(Formula::Kind::WeakUntil, falsity, proposition) == "p",
           "prop-formula-temporal: false W psi should fold to psi");
    expect(binary(Formula::Kind::WeakUntil, proposition, falsity) == "G(p)",
           "prop-formula-temporal: phi W false should fold to G phi, got " +
               binary(Formula::Kind::WeakUntil, proposition, falsity));
    // phi R true == true; phi R false == false; true R psi == psi;
    // false R psi == G psi.
    expect(binary(Formula::Kind::Release, proposition, truth) == "true",
           "prop-formula-temporal: phi R true should fold to true");
    expect(binary(Formula::Kind::Release, proposition, falsity) == "false",
           "prop-formula-temporal: phi R false should fold to false");
    expect(binary(Formula::Kind::Release, truth, proposition) == "p",
           "prop-formula-temporal: true R psi should fold to psi");
    expect(binary(Formula::Kind::Release, falsity, proposition) == "G(p)",
           "prop-formula-temporal: false R psi should fold to G psi, got " +
               binary(Formula::Kind::Release, falsity, proposition));
}

// A fold that fires inside a larger formula, since simplify() walks post-order
// and the constant is usually something the search built further down.
TEST(test_temporal_constant_folds_are_nested) {
    Formula formula = Formula::make_binary(
        Formula::Kind::And, Formula::make_atom("q"),
        Formula::make_unary(
            Formula::Kind::Globally,
            Formula::make_binary(Formula::Kind::WeakUntil, Formula("true"),
                                 Formula("false"))));
    formula.simplify();
    // G (true W false) is G true is true, and `q & true` is q.
    expect(formula.to_string() == "q",
           "prop-formula-temporal: a nested constant should fold out, got " +
               formula.to_string());
}

TEST(test_unary_temporal_to_string) {
    expect(Formula::make_unary(Formula::Kind::Next, atom("p")).to_string() ==
               "X(p)",
           "temporal: Next renders as X(...)");
    expect(
        Formula::make_unary(Formula::Kind::Eventually, atom("p")).to_string() ==
            "F(p)",
        "temporal: Eventually renders as F(...)");
    expect(
        Formula::make_unary(Formula::Kind::Globally, atom("p")).to_string() ==
            "G(p)",
        "temporal: Globally renders as G(...)");
}

TEST(test_binary_temporal_to_string) {
    expect(Formula::make_binary(Formula::Kind::Until, atom("p"), atom("q"))
                   .to_string() == "(p) U (q)",
           "temporal: Until renders as (l) U (r)");
    expect(Formula::make_binary(Formula::Kind::Release, atom("p"), atom("q"))
                   .to_string() == "(p) R (q)",
           "temporal: Release renders as (l) R (r)");
    expect(Formula::make_binary(Formula::Kind::WeakUntil, atom("p"), atom("q"))
                   .to_string() == "(p) W (q)",
           "temporal: WeakUntil renders as (l) W (r)");
}

TEST(test_kind_reports_temporal) {
    expect(Formula::make_unary(Formula::Kind::Globally, atom("p")).kind() ==
               Formula::Kind::Globally,
           "temporal: kind() reports Globally");
    expect(Formula::make_binary(Formula::Kind::Until, atom("p"), atom("q"))
                   .kind() == Formula::Kind::Until,
           "temporal: kind() reports Until");
}

// --- nesting and mixed propositional/temporal ---

TEST(test_globally_over_propositional) {
    const Formula inner =
        Formula::make_binary(Formula::Kind::And, atom("a"), atom("b"));
    const Formula glob = Formula::make_unary(Formula::Kind::Globally, inner);
    expect(glob.to_string() == "G((a) & (b))",
           "temporal: G over a propositional conjunction");
    expect(glob.n_subformulae() == inner.n_subformulae() + 1,
           "temporal: G adds exactly one node over its child");
}

TEST(test_nested_temporal) {
    const Formula fml = Formula::make_unary(
        Formula::Kind::Globally,
        Formula::make_binary(
            Formula::Kind::Implies, atom("req"),
            Formula::make_unary(Formula::Kind::Eventually, atom("ack"))));
    expect(fml.to_string() == "G((req) -> (F(ack)))",
           "temporal: nested G(req -> F ack) request-response shape");
}

// --- is_propositional ---

TEST(test_is_propositional) {
    expect(atom("p").is_propositional(),
           "temporal: bare atom is propositional");
    expect(Formula::make_binary(Formula::Kind::And, atom("a"), atom("b"))
               .is_propositional(),
           "temporal: conjunction is propositional");
    expect(
        !Formula::make_unary(Formula::Kind::Next, atom("p")).is_propositional(),
        "temporal: X(p) is not propositional");
    expect(!Formula::make_unary(
                Formula::Kind::Globally,
                Formula::make_binary(Formula::Kind::And, atom("a"), atom("b")))
                .is_propositional(),
           "temporal: G(a & b) is not propositional");
}

// --- extraction round-trips + arena consistency ---

TEST(test_unary_child_extraction) {
    const Formula inner =
        Formula::make_binary(Formula::Kind::Or, atom("a"), atom("b"));
    const Formula glob = Formula::make_unary(Formula::Kind::Globally, inner);
    const auto child = glob.unary_child();
    expect(child.has_value(), "temporal: unary_child of G returns a child");
    expect(child.has_value() && child->to_string() == inner.to_string(),
           "temporal: extracted child string matches");
    // The extracted propositional subtree must be identical (hash + equality)
    // to the standalone one, so dedup / caching stay consistent.
    expect(child.has_value() && *child == inner,
           "temporal: extracted propositional subtree equals the original");
    expect(child.has_value() && child->hash() == inner.hash(),
           "temporal: extracted subtree hashes identically");
}

TEST(test_binary_children_extraction) {
    const Formula left =
        Formula::make_binary(Formula::Kind::And, atom("a"), atom("b"));
    const Formula right = atom("c");
    const Formula unt = Formula::make_binary(Formula::Kind::Until, left, right);
    const auto children = unt.binary_children();
    expect(children.has_value(),
           "temporal: binary_children of U returns children");
    expect(children.has_value() && children->first == left &&
               children->second == right,
           "temporal: U children extract back to the operands");
}

TEST(test_propositional_extraction_unchanged) {
    // Propositional binary_children still round-trips (regression guard for the
    // untouched string path).
    const Formula fml =
        Formula::make_binary(Formula::Kind::And, atom("p"), atom("q"));
    const auto children = fml.binary_children();
    expect(children.has_value() && children->first == atom("p") &&
               children->second == atom("q"),
           "temporal: propositional binary_children unaffected");
    expect(!Formula::make_unary(Formula::Kind::Globally, atom("p"))
                .binary_children()
                .has_value(),
           "temporal: binary_children of a unary temporal node is empty");
}

// --- similarity ---

TEST(test_similarity_identical_temporal) {
    const Formula lhs = Formula::make_unary(Formula::Kind::Globally, atom("p"));
    const Formula rhs = Formula::make_unary(Formula::Kind::Globally, atom("p"));
    expect(lhs.syntactic_similarity(rhs) == 1.0,
           "temporal: identical temporal formulae are fully similar");
}

TEST(test_similarity_distinguishes_operator) {
    const Formula glob =
        Formula::make_unary(Formula::Kind::Globally, atom("p"));
    const Formula fml =
        Formula::make_unary(Formula::Kind::Eventually, atom("p"));
    const double sim = glob.syntactic_similarity(fml);
    expect(sim > 0.0 && sim < 1.0,
           "temporal: G(p) vs F(p) share the atom but differ at the operator");
}

// --- rewrite over temporal preserves skeleton, rewrites prop subtrees ---

TEST(test_rewrite_preserves_temporal_skeleton) {
    const Formula fml = Formula::make_unary(
        Formula::Kind::Globally,
        Formula::make_binary(Formula::Kind::And, atom("a"), atom("b")));
    const Formula rewritten = fml.rewrite_post_order(
        [](const Formula& subtree) -> std::optional<Formula> {
            if (subtree.atom_name() == "a") {
                return Formula::make_atom("z");
            }
            return std::nullopt;
        });
    expect(rewritten.to_string() == "G((z) & (b))",
           "temporal: rewrite replaces a propositional leaf under G, keeping "
           "the temporal skeleton");
}

}  // namespace
