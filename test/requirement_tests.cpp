#include <string>
#include <string_view>
#include <vector>

#include "requirement.hpp"
#include "runner/formaliser.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "requirement";

// A malformed requirement makes the CLI's parser write its "Line N: ..."
// message to stderr (not stdout) and still emit an empty line on stdout for
// that request, so PersistentProcess::request never throws on bad input —
// the one-line-in/one-line-out pairing holds, but a rejected line comes back
// as "". Real formalised output is never empty, so that's how a rejection
// is distinguished from a successfully formalised requirement.
void expect_valid_fretish(RequirementFormaliser& formaliser,
                          const Requirement& requirement) {
    const std::string fretish = requirement.to_string();
    const std::string result = formaliser.formalise(fretish);
    expect(
        !result.empty(),
        "Requirement::to_string: formaliser CLI rejected \"" + fretish + "\"");
}

std::string ltl_continual(const Formula& condition, const Formula& response,
                          const Timing& timing) {
    return requirement_to_ltl(
        Requirement(condition, response, timing, ConditionType::Continual));
}

std::string ltl_trigger(const Formula& condition, const Formula& response,
                        const Timing& timing) {
    return requirement_to_ltl(
        Requirement(condition, response, timing, ConditionType::Trigger));
}

// --- Continual semantics (default) ---

TEST(test_immediately) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::immediately());
    expect(result == "G((t) -> (r))",
           "requirement_to_ltl: Immediately should produce G(T -> R)");
}

TEST(test_next_timepoint) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::next_timepoint());
    expect(result == "G((t) -> X(r))",
           "requirement_to_ltl: NextTimepoint should produce G(T -> X R)");
}

TEST(test_eventually) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::eventually());
    expect(result == "G((t) -> F(r))",
           "requirement_to_ltl: Eventually should produce G(T -> F R)");
}

TEST(test_always) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::always());
    expect(result == "G((t) -> G(r))",
           "requirement_to_ltl: Always should produce G(T -> G R)");
}

TEST(test_within_ticks_zero) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::within_ticks(0));
    expect(result == "G((t) -> ((r)))",
           "requirement_to_ltl: WithinTicks(0) should produce G(T -> (R))");
}

TEST(test_within_ticks_one) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::within_ticks(1));
    expect(result == "G((t) -> ((r) | X((r))))",
           "requirement_to_ltl: WithinTicks(1) should expand to R | X(R)");
}

TEST(test_within_ticks_two) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::within_ticks(2));
    expect(
        result == "G((t) -> ((r) | X((r) | X((r)))))",
        "requirement_to_ltl: WithinTicks(2) should expand to R | X(R | X(R))");
}

TEST(test_for_ticks_zero) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::for_ticks(0));
    expect(result == "G((t) -> ((r)))",
           "requirement_to_ltl: ForTicks(0) should produce G(T -> (R))");
}

TEST(test_for_ticks_one) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::for_ticks(1));
    expect(result == "G((t) -> ((r) & X((r))))",
           "requirement_to_ltl: ForTicks(1) should expand to R & X(R)");
}

TEST(test_for_ticks_two) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::for_ticks(2));
    expect(result == "G((t) -> ((r) & X((r) & X((r)))))",
           "requirement_to_ltl: ForTicks(2) should expand to R & X(R & X(R))");
}

TEST(test_after_ticks_zero) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::after_ticks(0));
    expect(result == "G((t) -> (!(r) & X((r))))",
           "requirement_to_ltl: AfterTicks(0) should produce G(T -> (!R & "
           "X(R))), i.e. R must not hold at the condition tick but must hold "
           "at the next one");
}

TEST(test_after_ticks_one) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::after_ticks(1));
    expect(
        result == "G((t) -> (!(r) & X(!(r) & X((r)))))",
        "requirement_to_ltl: AfterTicks(1) should expand to !R & X(!R & X(R))");
}

TEST(test_after_ticks_two) {
    const std::string result =
        ltl_continual(Formula("t"), Formula("r"), timing::after_ticks(2));
    expect(
        result == "G((t) -> (!(r) & X(!(r) & X(!(r) & X((r))))))",
        "requirement_to_ltl: AfterTicks(2) should expand to !R & X(!R & X(!R & "
        "X(R)))");
}

// --- Trigger semantics ---

TEST(test_trigger_immediately) {
    const std::string result =
        ltl_trigger(Formula("c"), Formula("r"), timing::immediately());
    expect(result == "G((!(c) & X(c)) -> X((r))) & ((c) -> (r))",
           "requirement_to_ltl trigger: Immediately should produce "
           "G((!C & XC) -> X(R)) & (C -> R)");
}

TEST(test_trigger_next_timepoint) {
    const std::string result =
        ltl_trigger(Formula("c"), Formula("r"), timing::next_timepoint());
    expect(result == "G((!(c) & X(c)) -> X(X(r))) & ((c) -> X(r))",
           "requirement_to_ltl trigger: NextTimepoint should produce "
           "G((!C & XC) -> X(X(R))) & (C -> X(R))");
}

TEST(test_trigger_eventually) {
    const std::string result =
        ltl_trigger(Formula("c"), Formula("r"), timing::eventually());
    expect(result == "G((!(c) & X(c)) -> X(F(r))) & ((c) -> F(r))",
           "requirement_to_ltl trigger: Eventually should produce "
           "G((!C & XC) -> X(F(R))) & (C -> F(R))");
}

TEST(test_trigger_always) {
    const std::string result =
        ltl_trigger(Formula("c"), Formula("r"), timing::always());
    expect(result == "G((!(c) & X(c)) -> X(G(r))) & ((c) -> G(r))",
           "requirement_to_ltl trigger: Always should produce "
           "G((!C & XC) -> X(G(R))) & (C -> G(R))");
}

TEST(test_trigger_for_ticks_one) {
    const std::string result =
        ltl_trigger(Formula("c"), Formula("r"), timing::for_ticks(1));
    expect(result ==
               "G((!(c) & X(c)) -> X(((r) & X((r))))) & "
               "((c) -> ((r) & X((r))))",
           "requirement_to_ltl trigger: ForTicks(1) should produce "
           "G((!C & XC) -> X((R & X(R)))) & (C -> (R & X(R)))");
}

TEST(test_trigger_within_ticks_one) {
    const std::string result =
        ltl_trigger(Formula("c"), Formula("r"), timing::within_ticks(1));
    expect(result ==
               "G((!(c) & X(c)) -> X(((r) | X((r))))) & "
               "((c) -> ((r) | X((r))))",
           "requirement_to_ltl trigger: WithinTicks(1) should produce "
           "G((!C & XC) -> X((R | X(R)))) & (C -> (R | X(R)))");
}

TEST(test_trigger_after_ticks_one) {
    const std::string result =
        ltl_trigger(Formula("c"), Formula("r"), timing::after_ticks(1));
    expect(result ==
               "G((!(c) & X(c)) -> X((!(r) & X(!(r) & X((r)))))) & "
               "((c) -> (!(r) & X(!(r) & X((r)))))",
           "requirement_to_ltl trigger: AfterTicks(1) should produce "
           "G((!C & XC) -> X((!R & X(!R & X(R))))) & (C -> (!R & X(!R & "
           "X(R))))");
}

// --- specification_has_false_condition ---

TEST(test_specification_has_false_condition_detects_assumption) {
    const Specification spec(
        {Requirement(Formula("false"), Formula("r"), timing::immediately())},
        {Requirement(Formula("t"), Formula("r"), timing::immediately())}, {"t"},
        {"r"});
    expect(specification_has_false_condition(spec),
           "specification_has_false_condition: should detect a false condition "
           "in an assumption");
}

TEST(test_specification_has_false_condition_detects_guarantee) {
    const Specification spec(
        {},
        {Requirement(Formula("false"), Formula("r"), timing::immediately())},
        {"t"}, {"r"});
    expect(specification_has_false_condition(spec),
           "specification_has_false_condition: should detect a false condition "
           "in a guarantee");
}

TEST(test_specification_has_false_condition_false_for_normal_spec) {
    const Specification spec(
        {}, {Requirement(Formula("t"), Formula("r"), timing::immediately())},
        {"t"}, {"r"});
    expect(!specification_has_false_condition(spec),
           "specification_has_false_condition: should not flag a normal "
           "condition");
}

// --- Requirement::to_string against the real FRET formaliser CLI ---

TEST(test_to_string_is_valid_fretish_for_all_timings_and_condition_types) {
    RequirementFormaliser formaliser(formaliser_command());
    const std::vector<Timing> timings = {
        timing::immediately(),
        timing::next_timepoint(),
        timing::within_ticks(3),
        timing::for_ticks(2),
        timing::after_ticks(1),
        timing::eventually(),
        timing::always(),
        timing::until(Formula("stop")),
        timing::before(Formula("stop & other")),
    };
    for (const Timing& tim : timings) {
        expect_valid_fretish(formaliser,
                             Requirement(Formula("cond"), Formula("resp"), tim,
                                         ConditionType::Continual));
        expect_valid_fretish(formaliser,
                             Requirement(Formula("cond"), Formula("resp"), tim,
                                         ConditionType::Trigger));
    }
}

TEST(test_to_string_is_valid_fretish_for_true_condition) {
    RequirementFormaliser formaliser(formaliser_command());
    // A literal "true" condition collapses condition_to_string() to "", so
    // this exercises the branch that omits the condition clause entirely.
    expect_valid_fretish(
        formaliser,
        Requirement(Formula::true_formula, Formula("resp"),
                    timing::immediately(), ConditionType::Continual));
    expect_valid_fretish(
        formaliser, Requirement(Formula::true_formula, Formula("resp"),
                                timing::eventually(), ConditionType::Trigger));
}

TEST(test_to_string_is_valid_fretish_for_compound_formulae) {
    RequirementFormaliser formaliser(formaliser_command());
    expect_valid_fretish(
        formaliser,
        Requirement(Formula("a & b"), Formula("c | !d"),
                    timing::within_ticks(2), ConditionType::Continual));
}

TEST(test_add_atom_prefix_tags_atoms) {
    const Requirement req(Formula("c"), Formula("GF"), timing::immediately(),
                          ConditionType::Continual);
    const Requirement prefixed = add_atom_prefix(req);
    expect(prefixed.m_condition.to_string() == "iap_c",
           "add_atom_prefix: condition atom should be tagged");
    expect(prefixed.m_response.to_string() == "iap_GF",
           "add_atom_prefix: response atom should be tagged");
    expect(prefixed.m_ltl.find("iap_GF") != std::string::npos,
           "add_atom_prefix: derived LTL should contain the tagged atom");
}

TEST(test_add_atom_prefix_preserves_constants) {
    const Requirement req(Formula("true"), Formula("r"), timing::immediately(),
                          ConditionType::Trigger);
    const Requirement prefixed = add_atom_prefix(req);
    expect(prefixed.m_condition == Formula::true_formula,
           "add_atom_prefix: the 'true' constant must not be tagged");
    expect(prefixed.m_response.to_string() == "iap_r",
           "add_atom_prefix: ordinary response atom should be tagged");
}

TEST(test_strip_atom_prefix_is_inverse) {
    const Requirement req(Formula("a & GF"), Formula("Uu | b"),
                          timing::within_ticks(2), ConditionType::Continual);
    const Requirement round = strip_atom_prefix(add_atom_prefix(req));
    expect(round == req,
           "strip_atom_prefix: should invert add_atom_prefix exactly");
}

// Both functions rebuild the requirement field by field, so a field they forget
// silently reverts to its default. For the removal tombstone that means a
// deleted guarantee coming back to life on a round trip through the atom
// prefix, which every specification takes at load and at output.
TEST(test_atom_prefix_preserves_locked_and_removed_flags) {
    Requirement req(Formula("a"), Formula("b"), timing::immediately(),
                    ConditionType::Continual, /*weakenable=*/false);
    req.m_removed = true;
    const Requirement prefixed = add_atom_prefix(req);
    expect(prefixed.m_removed,
           "add_atom_prefix: a deleted requirement stays deleted");
    expect(!prefixed.m_weakenable,
           "add_atom_prefix: a locked requirement stays locked");
    expect(strip_atom_prefix(prefixed) == req,
           "strip_atom_prefix: the round trip preserves both flags");
}

TEST(test_strip_atom_prefix_defensive_on_untagged) {
    const Requirement req(Formula("a"), Formula("b"), timing::immediately());
    const Requirement stripped = strip_atom_prefix(req);
    expect(stripped == req,
           "strip_atom_prefix: untagged requirement should be unchanged");
}

TEST(test_add_atom_prefix_ltl_regression_gf) {
    // 'GF' previously lexed as two temporal operators by SPOT, diverging from
    // black and crashing realize. After tagging, the LTL carries 'iap_GF' and
    // no bare 'GF' operator token remains.
    const Requirement req(Formula("c"), Formula("GF"), timing::eventually(),
                          ConditionType::Continual);
    const std::string ltl = add_atom_prefix(req).m_ltl;
    expect(ltl.find("iap_GF") != std::string::npos,
           "regression: tagged LTL should contain iap_GF");
    auto has_bare_gf = [](const std::string& str) {
        for (std::size_t pos = str.find("GF"); pos != std::string::npos;
             pos = str.find("GF", pos + 1)) {
            if (pos < 4 || str.compare(pos - 4, 4, "iap_") != 0) {
                return true;
            }
        }
        return false;
    };
    expect(!has_bare_gf(ltl),
           "regression: LTL should contain no bare 'GF' operator token");
}

}  // namespace
