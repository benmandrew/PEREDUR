#include <string>
#include <string_view>
#include <vector>

#include "formula_key.hpp"
#include "prop_formula.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "prop_formula_canonical";

// The asserting constructor is right for a caller that built its own input.
// try_parse is for a string from outside, and under NDEBUG the asserts are
// gone -- the walk used to run past the end of the text and throw out of
// std::string::compare, which is what this pins.
TEST(test_try_parse_reports_a_malformed_string) {
    const std::vector<std::string> malformed = {
        "G(", "(p", "p)", "", "&", "G(p", "(p) & ",
    };
    for (const std::string& text : malformed) {
        expect(!Formula::try_parse(text).has_value(),
               "prop-formula-canonical: try_parse should report \"" + text +
                   "\" as malformed rather than assert on it");
    }
}

TEST(test_try_parse_accepts_what_the_renderer_emits) {
    const std::vector<std::string> valid = {
        "p", "!(p)", "(p) & (q)", "G((p) -> (X(q)))", "(p) W (q)",
    };
    for (const std::string& text : valid) {
        const auto parsed = Formula::try_parse(text);
        expect(parsed.has_value() && parsed->to_string() == text,
               "prop-formula-canonical: try_parse should round-trip " + text);
    }
}

// The renderer emitted the temporal operators long before the parser could
// read them back, so every cache key built from a rendered formula was a
// string nothing in-process could reparse. Canonicalisation depends on that
// round trip, so it is pinned first.
TEST(test_parser_round_trips_temporal_operators) {
    const std::vector<std::string> formulae = {
        "G(p)",
        "F(X(p))",
        "(p) U (q)",
        "(p) R (q)",
        "(p) W (q)",
        "G((p) -> (X(F(q))))",
        "((p) U (q)) & (G(!(r)))",
        "(G(p)) <-> (F((q) W (r)))",
    };
    for (const std::string& text : formulae) {
        const Formula parsed(text);
        expect(parsed.to_string() == text, "prop-formula-canonical: " + text +
                                               " should round-trip, got " +
                                               parsed.to_string());
    }
}

// An operator spelled as a letter is only an operator when what follows cannot
// continue an identifier, or every atom beginning with G, F, X, U, R or W
// lexes as an operator applied to its own tail.
TEST(test_operator_letters_do_not_eat_identifiers) {
    const Formula grant("Grant");
    expect(grant.kind() == Formula::Kind::Atom &&
               grant.atom_name().value_or("") == "Grant",
           "prop-formula-canonical: Grant should lex as one atom");
    const Formula globally("G(rant)");
    expect(globally.kind() == Formula::Kind::Globally,
           "prop-formula-canonical: G(rant) should lex as the operator");
    const Formula until("(Until) & (Water)");
    expect(until.kind() == Formula::Kind::And,
           "prop-formula-canonical: atoms named for operators should lex");
}

TEST(test_commutative_operands_are_ordered) {
    const std::string left =
        Formula("(a) & ((b) & (c))").canonical().to_string();
    const std::string right =
        Formula("((c) & (b)) & (a)").canonical().to_string();
    expect(left == right,
           "prop-formula-canonical: & should flatten and order, "
           "got " +
               left + " against " + right);
    const std::string disjunction_a =
        Formula("(q) | (p)").canonical().to_string();
    const std::string disjunction_b =
        Formula("(p) | (q)").canonical().to_string();
    expect(disjunction_a == disjunction_b,
           "prop-formula-canonical: | should order its operands");
}

TEST(test_repeated_operands_are_dropped) {
    const std::string once = Formula("(a) & (b)").canonical().to_string();
    const std::string twice =
        Formula("((a) & (b)) & (a)").canonical().to_string();
    expect(once == twice, "prop-formula-canonical: & is idempotent, got " +
                              once + " against " + twice);
}

TEST(test_double_negation_is_dropped) {
    const std::string plain = Formula("p").canonical().to_string();
    const std::string negated = Formula("!(!(p))").canonical().to_string();
    expect(
        plain == negated,
        "prop-formula-canonical: !!p should canonicalise to p, got " + negated);
}

// Iff is commutative and associative but not idempotent: `a <-> a` is the
// constant true, so deduplicating its operands would change what it says.
TEST(test_iff_is_ordered_but_not_deduplicated) {
    const std::string forward = Formula("(a) <-> (b)").canonical().to_string();
    const std::string backward = Formula("(b) <-> (a)").canonical().to_string();
    expect(forward == backward,
           "prop-formula-canonical: <-> should order its operands");
    const Formula self = Formula("(a) <-> (a)").canonical();
    expect(self.kind() == Formula::Kind::Iff,
           "prop-formula-canonical: a <-> a must stay a biconditional");
}

TEST(test_non_commutative_operators_keep_their_order) {
    const std::vector<std::string> kinds = {"U", "R", "W", "->"};
    for (const std::string& kind : kinds) {
        const std::string forward =
            Formula("(a) " + kind + " (b)").canonical().to_string();
        const std::string backward =
            Formula("(b) " + kind + " (a)").canonical().to_string();
        expect(forward != backward,
               "prop-formula-canonical: " + kind +
                   " must not have its operands reordered");
    }
}

TEST(test_canonical_form_is_idempotent) {
    const Formula once =
        Formula("((c) & (b)) & ((a) | ((b) | (a)))").canonical();
    const Formula twice = once.canonical();
    expect(once.to_string() == twice.to_string(),
           "prop-formula-canonical: canonical() should be idempotent, got " +
               once.to_string() + " against " + twice.to_string());
    // The rebuilt chain must also survive the parser it is rendered for, or a
    // key computed from a key would differ from the key.
    const Formula reparsed(once.to_string());
    expect(reparsed.canonical().to_string() == once.to_string(),
           "prop-formula-canonical: a canonical rendering should reparse to "
           "itself");
}

TEST(test_renaming_collapses_alpha_equivalent_formulae) {
    expect(formula_key::renamed("G((p) -> (X(q)))") ==
               formula_key::renamed("G((r) -> (X(s)))"),
           "formula-key: alpha-equivalent formulae should share a key, got " +
               formula_key::renamed("G((p) -> (X(q)))") + " against " +
               formula_key::renamed("G((r) -> (X(s)))"));
    expect(formula_key::renamed("G((p) -> (X(p)))") !=
               formula_key::renamed("G((p) -> (X(q)))"),
           "formula-key: a formula reusing one atom is not alpha-equivalent to "
           "one using two");
}

// The renamed key is unusable wherever the cached value names atoms, because
// reading it back would mean renaming inside a tool's own output and SPOT
// prints a unary operator hard against its operand. The canonical key exists
// for those caches, so it must leave the atom names alone.
TEST(test_canonical_key_preserves_atom_names) {
    const std::string key = formula_key::canonical("G((high_water) -> (pump))");
    expect(key.find("high_water") != std::string::npos &&
               key.find("pump") != std::string::npos,
           "formula-key: canonical() must keep the caller's atom names, got " +
               key);
}

TEST(test_constants_are_never_renamed) {
    const std::string key = formula_key::renamed("(true) & ((p) | (false))");
    expect(key.find("true") != std::string::npos &&
               key.find("false") != std::string::npos,
           "formula-key: the boolean constants are atoms by convention and "
           "renaming one makes it a free variable, got " +
               key);
}

// Realizability is invariant under a bijection on the atoms only when that
// bijection preserves the input/output partition, so an input and an output
// must never collapse onto one another.
TEST(test_realizability_key_preserves_the_partition) {
    const std::vector<std::string> inputs = {"req"};
    const std::vector<std::string> outputs = {"grant"};
    const std::string forward =
        formula_key::realizability("G((req) -> (X(grant)))", inputs, outputs);
    const std::string swapped =
        formula_key::realizability("G((grant) -> (X(req)))", inputs, outputs);
    expect(forward != swapped,
           "formula-key: swapping an input for an output must change the key");
    const std::vector<std::string> two_inputs = {"req", "cancel"};
    const std::string renamed_input = formula_key::realizability(
        "G((cancel) -> (X(grant)))", two_inputs, outputs);
    const std::string first_input = formula_key::realizability(
        "G((req) -> (X(grant)))", two_inputs, outputs);
    expect(renamed_input == first_input,
           "formula-key: two inputs in the same position should share a key");
}

// A declared signal the formula never mentions is still part of the alphabet
// the synthesiser plays over, so the key has to count them.
TEST(test_realizability_key_counts_unmentioned_signals) {
    const std::vector<std::string> outputs = {"grant"};
    const std::string narrow =
        formula_key::realizability("G(grant)", {"req"}, outputs);
    const std::string wide =
        formula_key::realizability("G(grant)", {"req", "cancel"}, outputs);
    expect(narrow != wide,
           "formula-key: an unmentioned input widens the alphabet and must "
           "change the key");
}

}  // namespace
