#include <stdexcept>
#include <string>
#include <string_view>

#include "prop_formula.hpp"
#include "runner/ltlfilt.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"
#include "tlsf_fixtures.hpp"

namespace {

constexpr std::string_view k_test_suite = "tlsf_parser";

bool equiv(const std::string& lhs, const std::string& rhs) {
    return ltl_equivalent(lhs, rhs);
}

// Confirms the ltlfilt oracle is actually live (not silently short-circuiting
// to "true"); every equivalence assertion below relies on this.
TEST(test_oracle_is_live) {
    expect(equiv("a", "a"), "oracle: a is equivalent to itself");
    expect(!equiv("a", "X(a)"), "oracle: a is not equivalent to X a");
}

TEST(test_info_fields) {
    const tlsf::Specification spec = tlsf::parse(
        "INFO {\n"
        "  TITLE: \"Arbiter\";\n"
        "  DESCRIPTION: \"a demo\";\n"
        "  SEMANTICS: Mealy,standard;\n"
        "  TARGET: Mealy;\n"
        "  TAGS: \"x\", \"y\";\n"
        "  VERSION: \"1.0\";\n"
        "}\n"
        "MAIN { INPUTS { r; } OUTPUTS { g; } GUARANTEE { g; } }");
    expect(spec.m_title == "Arbiter", "info: title parsed");
    expect(spec.m_description == "a demo", "info: description parsed");
    expect(spec.m_semantics == tlsf::Semantics::MealyStandard,
           "info: semantics parsed, extra keys ignored");
    expect(spec.m_inputs.size() == 1 && spec.m_inputs[0] == "r",
           "info: inputs parsed");
    expect(spec.m_outputs.size() == 1 && spec.m_outputs[0] == "g",
           "info: outputs parsed");
}

TEST(test_semantics_variants) {
    expect(tlsf::parse(tlsf_doc("GUARANTEE { g; }", "Mealy")).m_semantics ==
               tlsf::Semantics::MealyStandard,
           "semantics: bare Mealy defaults to standard");
    expect(tlsf::parse(tlsf_doc("GUARANTEE { g; }", "Moore")).m_semantics ==
               tlsf::Semantics::MooreStandard,
           "semantics: bare Moore defaults to standard");
    expect(
        tlsf::parse(tlsf_doc("GUARANTEE { g; }", "Mealy,Strict")).m_semantics ==
            tlsf::Semantics::MealyStrict,
        "semantics: Mealy,Strict");
    expect(
        tlsf::parse(tlsf_doc("GUARANTEE { g; }", "Moore,strict")).m_semantics ==
            tlsf::Semantics::MooreStrict,
        "semantics: Moore,strict (case-insensitive mode)");
}

TEST(test_finite_rejected) {
    expect_throws<std::invalid_argument>(
        [&] { tlsf::parse(tlsf_doc("GUARANTEE { g; }", "Mealy,finite")); },
        "semantics: finite semantics is rejected", "finite");
}

TEST(test_all_sections_and_aliases) {
    const tlsf::Specification spec =
        tlsf::parse(tlsf_doc("INPUTS { a; } OUTPUTS { b; }\n"
                             "INITIALLY { a; }\n"
                             "PRESET { b; }\n"
                             "REQUIRE { a; }\n"
                             "ASSUMPTIONS { a; }\n"
                             "INVARIANTS { b; }\n"
                             "GUARANTEES { b; }\n"));
    expect(spec.m_initially.size() == 1, "sections: INITIALLY");
    expect(spec.m_preset.size() == 1, "sections: PRESET");
    expect(spec.m_require.size() == 1, "sections: REQUIRE");
    expect(spec.m_assume.size() == 1, "sections: ASSUMPTIONS alias");
    expect(spec.m_assert.size() == 1, "sections: INVARIANTS alias");
    expect(spec.m_guarantee.size() == 1, "sections: GUARANTEES alias");

    const tlsf::Specification spec2 =
        tlsf::parse(tlsf_doc("ASSUME { a; } ASSERT { b; } GUARANTEE { b; } "
                             "REQUIREMENTS { a; } INVARIANTS { a; }"));
    expect(spec2.m_assume.size() == 1, "sections: ASSUME singular");
    expect(spec2.m_assert.size() == 2, "sections: ASSERT + INVARIANTS merge");
    expect(spec2.m_guarantee.size() == 1, "sections: GUARANTEE singular");
    expect(spec2.m_require.size() == 1, "sections: REQUIREMENTS alias");
}

// Real-world basic TLSF (as emitted by syfco): INFO entries have no `;`
// terminator and boolean connectives use the doubled `&&`/`||`.
TEST(test_real_format_no_semicolons_and_double_ops) {
    const tlsf::Specification spec = tlsf::parse(
        "INFO {\n"
        "  TITLE:       \"TLSF - Test Specification\"\n"
        "  DESCRIPTION: \"Test Test Test\"\n"
        "  SEMANTICS:   Mealy\n"
        "  TARGET:      Mealy\n"
        "}\n"
        "MAIN {\n"
        "  INPUTS { methane; high_water; }\n"
        "  OUTPUTS { pump_on; }\n"
        "  ASSUMPTIONS {\n"
        "    G (pump_on -> (!high_water || X(!high_water || "
        "X(!high_water))));\n"
        "  }\n"
        "  GUARANTEES {\n"
        "    G (high_water -> X (pump_on));\n"
        "    G (methane -> X (!pump_on));\n"
        "  }\n"
        "}\n");
    expect(spec.m_title == "TLSF - Test Specification",
           "real: TITLE parsed without a semicolon terminator");
    expect(spec.m_description == "Test Test Test",
           "real: DESCRIPTION parsed without a semicolon terminator");
    expect(spec.m_semantics == tlsf::Semantics::MealyStandard,
           "real: SEMANTICS parsed without a semicolon terminator");
    expect(spec.m_inputs.size() == 2, "real: two inputs");
    expect(spec.m_outputs.size() == 1, "real: one output");
    expect(spec.m_assume.size() == 1, "real: ASSUMPTIONS with || parsed");
    expect(spec.m_guarantee.size() == 2, "real: two GUARANTEES parsed");
}

TEST(test_double_operators) {
    auto first = [](const std::string& body) {
        return tlsf::parse(
                   tlsf_doc("OUTPUTS { a; b; c; } GUARANTEE { " + body + "; }"))
            .m_guarantee.front()
            .m_formula.to_string();
    };
    // `&&`/`||` are the TLSF connectives; they parse identically to `&`/`|`.
    expect(first("a && b || c") == first("a & b | c"),
           "operators: && / || match & / |");
    expect(first("a && b || c") == "((a) & (b)) | (c)",
           "operators: && binds tighter than ||");
}

TEST(test_precedence_and_associativity) {
    auto first = [](const std::string& body) {
        return tlsf::parse(
                   tlsf_doc("OUTPUTS { a; b; c; } GUARANTEE { " + body + "; }"))
            .m_guarantee.front()
            .m_formula.to_string();
    };
    expect(first("a -> b -> c") == "(a) -> ((b) -> (c))",
           "precedence: -> is right-associative");
    expect(first("a & b | c") == "((a) & (b)) | (c)",
           "precedence: & binds tighter than |");
    expect(first("G a & b") == "(G(a)) & (b)",
           "precedence: G binds tighter than &");
    expect(first("a U b U c") == "(a) U ((b) U (c))",
           "precedence: U is right-associative");
    expect(first("a <-> b -> c") == "(a) <-> ((b) -> (c))",
           "precedence: -> binds tighter than <->");
}

TEST(test_bounded_expansion) {
    auto first = [](const std::string& body) {
        return tlsf::parse(
                   tlsf_doc("OUTPUTS { p; } GUARANTEE { " + body + "; }"))
            .m_guarantee.front()
            .m_formula.to_string();
    };
    expect(equiv(first("X[2] p"), "X X p"), "bounded: X[2] p");
    expect(equiv(first("X[0] p"), "p"), "bounded: X[0] p is p");
    expect(equiv(first("F[0..2] p"), "p | X p | X X p"), "bounded: F[0..2] p");
    expect(equiv(first("G[1..2] p"), "X p & X X p"), "bounded: G[1..2] p");

    expect_throws<std::invalid_argument>([&] { first("F[0..65] p"); },
                                         "bounded: bound over 64 throws");
}

TEST(test_comments_and_multistatement) {
    const tlsf::Specification spec =
        tlsf::parse(tlsf_doc("OUTPUTS { a; b; }\n"
                             "// a line comment\n"
                             "GUARANTEE {\n"
                             "  a; /* inline */ b;\n"
                             "}\n"));
    expect(spec.m_guarantee.size() == 2,
           "comments: two statements survive comments");
}

void expect_reject(const std::string& text, const std::string& mentions,
                   const std::string& msg) {
    expect_throws<std::invalid_argument>([&] { tlsf::parse(text); }, msg,
                                         mentions);
}

TEST(test_error_cases) {
    expect_reject(
        "INFO { SEMANTICS: Mealy; }\n"
        "GLOBAL { }\n"
        "MAIN { INPUTS { } OUTPUTS { g; } GUARANTEE { g; } }",
        "GLOBAL", "reject: GLOBAL block");
    expect_reject(tlsf_doc("PARAMETERS { n = 2; } GUARANTEE { g; }"),
                  "PARAMETERS", "reject: PARAMETERS section");
    expect_reject(tlsf_doc("DEFINITIONS { d = g; } GUARANTEE { g; }"),
                  "DEFINITIONS", "reject: DEFINITIONS section");
    expect_reject(tlsf_doc("INPUTS { bus[4]; } GUARANTEE { g; }"), "bus",
                  "reject: bus declaration");
    expect_reject(tlsf_doc("INPUTS { col { red, green }; } GUARANTEE { g; }"),
                  "enumeration", "reject: enumeration declaration");
    expect_reject(tlsf_doc("OUTPUTS { g; } GUARANTEE { &&[i <- 0..2] g; }"),
                  "loop aggregate", "reject: loop aggregate");
    expect_reject(tlsf_doc("OUTPUTS { g; } GUARANTEE { g@0; }"),
                  "primed/bus-access", "reject: bus-access syntax");
    // Singular INVARIANT is not a TLSF keyword (only the plural INVARIANTS);
    // syfco rejects it, so we do too.
    expect_reject(tlsf_doc("OUTPUTS { g; } INVARIANT { g; } GUARANTEE { g; }"),
                  "INVARIANT", "reject: non-standard singular INVARIANT");

    // Genuine syntax errors throw invalid_argument rather than crashing.
    expect_throws<std::invalid_argument>(
        [&] { tlsf::parse(tlsf_doc("OUTPUTS { g; } GUARANTEE { g }")); },
        "reject: missing ';' is a syntax error");

    expect_throws<std::invalid_argument>(
        [&] { tlsf::parse("not a tlsf file at all"); },
        "reject: garbage input throws, never crashes");
}

TEST(test_to_ltl_standard_lowering) {
    // GR(1)-style arbiter.
    const tlsf::Specification arbiter =
        tlsf::parse(tlsf_doc("INPUTS { req; } OUTPUTS { grant; }\n"
                             "ASSUME { G F req; }\n"
                             "GUARANTEE { G (req -> F grant); }"));
    expect(equiv(arbiter.to_ltl(), "(G F req) -> G(req -> F grant)"),
           "to_ltl: arbiter lowering");

    // Guarantee-only: no assumption term, result is the guarantee conjunction.
    const tlsf::Specification guar_only =
        tlsf::parse(tlsf_doc("OUTPUTS { p; } GUARANTEE { G p; }"));
    expect(equiv(guar_only.to_ltl(), "G p"),
           "to_ltl: guarantee-only has no implication");

    // Initial states nest around the invariant implication (TLSF §3.2):
    // θ_e -> (θ_s & ((G ψ_e & φ_e) -> (G ψ_s & φ_s))), here with φ_e/φ_s empty.
    const tlsf::Specification invariants =
        tlsf::parse(tlsf_doc("INPUTS { a; b; } OUTPUTS { c; d; }\n"
                             "INITIALLY { !a; }\n"
                             "REQUIRE { a -> b; }\n"
                             "PRESET { d; }\n"
                             "ASSERT { c; }"));
    expect(equiv(invariants.to_ltl(), "(!a) -> (d & (G(a -> b) -> G(c)))"),
           "to_ltl: initially/require/preset/assert lowering");

    // Multiple verbatim terms conjoined on each side.
    const tlsf::Specification multi =
        tlsf::parse(tlsf_doc("INPUTS { a; b; } OUTPUTS { c; d; }\n"
                             "ASSUME { a; b; }\n"
                             "GUARANTEE { c; d; }"));
    expect(equiv(multi.to_ltl(), "(a & b) -> (c & d)"),
           "to_ltl: multi-statement conjunction on both sides");
}

TEST(test_to_ltl_strict_lowering) {
    // Strict semantics move the system invariant ψ_s (ASSERT) into a weak-until
    // guard (ψ_s W ¬ψ_e) and drop it from the consequent:
    //   θ_e -> (θ_s & (ψ_s W ¬ψ_e) & ((G ψ_e & φ_e) -> φ_s))
    const tlsf::Specification strict = tlsf::parse(
        "INFO { SEMANTICS: Mealy,Strict; }\n"
        "MAIN { INPUTS { a; } OUTPUTS { c; }\n"
        "       REQUIRE { a; } ASSERT { c; } GUARANTEE { F c; } }");
    expect(equiv(strict.to_ltl(), "(c W !a) & ((G a) -> (F c))"),
           "to_ltl: strict pulls ASSERT into a weak-until guard");

    // The standard counterpart keeps ASSERT as a G-conjunct on the guarantee
    // side, with no weak-until guard.
    const tlsf::Specification standard = tlsf::parse(
        "INFO { SEMANTICS: Mealy; }\n"
        "MAIN { INPUTS { a; } OUTPUTS { c; }\n"
        "       REQUIRE { a; } ASSERT { c; } GUARANTEE { F c; } }");
    expect(equiv(standard.to_ltl(), "(G a) -> ((G c) & (F c))"),
           "to_ltl: standard keeps ASSERT as a G-conjunct");
}

}  // namespace
