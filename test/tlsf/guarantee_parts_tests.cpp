#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "prop_formula.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"
#include "tlsf/guarantee_parts.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace {

constexpr std::string_view k_test_suite = "tlsf_guarantee_parts";

std::vector<std::string> part_strings(
    const std::vector<tlsf::CoreFormula>& parts) {
    std::vector<std::string> out;
    out.reserve(parts.size());
    for (const tlsf::CoreFormula& part : parts) {
        out.push_back(part.formula.to_string());
    }
    return out;
}

tlsf::Specification spec_with_guarantee(const std::string& formula) {
    tlsf::Specification spec;
    spec.m_inputs = {"i"};
    spec.m_outputs = {"o"};
    spec.m_guarantee.emplace_back(Formula(formula));
    return spec;
}

TEST(test_splits_a_top_level_conjunction) {
    const auto parts =
        tlsf::split_guarantee_parts(spec_with_guarantee("a & b"));
    expect(part_strings(parts) == std::vector<std::string>{"a", "b"},
           "guarantee parts: a top-level conjunction should split in two");
}

TEST(test_splits_nested_conjunctions_fully) {
    const auto parts =
        tlsf::split_guarantee_parts(spec_with_guarantee("(a & b) & (c & d)"));
    expect(part_strings(parts) == std::vector<std::string>{"a", "b", "c", "d"},
           "guarantee parts: nested conjunctions should split all the way");
}

TEST(test_distributes_globally_over_conjunction) {
    // The rewrite that matters in practice: a TLSF GUARANTEE is commonly a
    // single G over a wide conjunction, and leaving it whole is what left the
    // detector specification's MRS score flat across every one of its links.
    tlsf::Specification spec;
    spec.m_guarantee.emplace_back(Formula::make_unary(
        Formula::Kind::Globally,
        Formula::make_binary(Formula::Kind::And, Formula("a"), Formula("b"))));
    const auto parts = tlsf::split_guarantee_parts(spec);
    expect(parts.size() == 2,
           "guarantee parts: G over a conjunction should split in two");
    expect(part_strings(parts) == std::vector<std::string>{"G(a)", "G(b)"},
           "guarantee parts: the G should distribute onto each conjunct");
}

TEST(test_leaves_other_operators_alone) {
    // Only conjunction is descended into. Splitting a disjunction or an
    // implication would not preserve the language.
    const auto disjunction =
        tlsf::split_guarantee_parts(spec_with_guarantee("a | b"));
    expect(disjunction.size() == 1,
           "guarantee parts: a disjunction should stay whole");
    const auto implication =
        tlsf::split_guarantee_parts(spec_with_guarantee("a -> b"));
    expect(implication.size() == 1,
           "guarantee parts: an implication should stay whole");
}

TEST(test_covers_the_guarantee_side_sections_in_order) {
    // PRESET, then ASSERT, then GUARANTEE -- the order the MUC extractor
    // enumerates in, so the two agree on what the guarantee side is made of.
    tlsf::Specification spec;
    spec.m_preset.emplace_back(Formula("p"));
    spec.m_assert.emplace_back(Formula("s"));
    spec.m_guarantee.emplace_back(Formula("g"));
    // Environment-side sections must not contribute parts: relaxing an
    // assumption can only make synthesis harder.
    spec.m_assume.emplace_back(Formula("e"));
    spec.m_require.emplace_back(Formula("r"));
    spec.m_initially.emplace_back(Formula("n"));
    expect(part_strings(tlsf::split_guarantee_parts(spec)) ==
               std::vector<std::string>{"p", "s", "g"},
           "guarantee parts: the guarantee side alone, in section order");
}

TEST(test_subset_keeps_the_environment_side_whole) {
    tlsf::Specification spec;
    spec.m_inputs = {"i"};
    spec.m_outputs = {"o"};
    spec.m_assume.emplace_back(Formula("e"));
    spec.m_require.emplace_back(Formula("r"));
    spec.m_initially.emplace_back(Formula("n"));
    spec.m_guarantee.emplace_back(Formula("a & b"));
    const auto parts = tlsf::split_guarantee_parts(spec);
    const tlsf::Specification subset =
        tlsf::build_part_subset(spec, parts, {1});
    expect(subset.m_assume.size() == 1 && subset.m_require.size() == 1 &&
               subset.m_initially.size() == 1,
           "guarantee parts: a subset should carry the environment side whole");
    expect(
        subset.m_inputs == spec.m_inputs && subset.m_outputs == spec.m_outputs,
        "guarantee parts: a subset should keep the atom partition");
    expect(subset.m_guarantee.size() == 1 &&
               subset.m_guarantee[0].m_formula.to_string() == "b",
           "guarantee parts: a subset should hold exactly the named parts");
}

TEST(test_subset_restores_each_part_to_its_own_section) {
    tlsf::Specification spec;
    spec.m_preset.emplace_back(Formula("p"));
    spec.m_assert.emplace_back(Formula("s"));
    spec.m_guarantee.emplace_back(Formula("g"));
    const auto parts = tlsf::split_guarantee_parts(spec);
    const tlsf::Specification subset =
        tlsf::build_part_subset(spec, parts, {0, 1, 2});
    expect(subset.m_preset.size() == 1 && subset.m_assert.size() == 1 &&
               subset.m_guarantee.size() == 1,
           "guarantee parts: each part should return to the section it came "
           "from, since the lowering differs per section");
}

TEST(test_full_subset_reproduces_the_specification) {
    // The walk's last step asks about every part, and that query must be the
    // one the tiered scale would have asked -- otherwise 1.0 would not mean
    // what it means there.
    const tlsf::Specification spec =
        tlsf::parse(std::string("INFO { TITLE: \"t\" DESCRIPTION: \"d\" "
                                "SEMANTICS: Mealy TARGET: Mealy }\n"
                                "MAIN { INPUTS { i; } OUTPUTS { o; }\n"
                                "GUARANTEE { (G ((i) -> (o))); (G (F (o))); }\n"
                                "}\n"));
    const auto parts = tlsf::split_guarantee_parts(spec);
    std::vector<std::size_t> all(parts.size());
    for (std::size_t idx = 0; idx < parts.size(); ++idx) {
        all[idx] = idx;
    }
    expect(tlsf::build_part_subset(spec, parts, all).to_ltl() == spec.to_ltl(),
           "guarantee parts: the full subset should lower to the original LTL");
}

}  // namespace
