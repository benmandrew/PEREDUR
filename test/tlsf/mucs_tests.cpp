#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "prop_formula.hpp"
#include "runner/spot.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"
#include "tlsf/mucs.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace {

constexpr std::string_view k_test_suite = "tlsf_mucs";

// Whether any guarantee-side section of `spec` contains the named atom. The
// fake oracles below phrase their (un)realizability verdict over these.
bool has_atom(const tlsf::Specification& spec, const std::string& atom) {
    auto contains = [&atom](const tlsf::Section& section) {
        return std::any_of(section.begin(), section.end(),
                           [&atom](const tlsf::SectionEntry& entry) {
                               return entry.m_formula.to_string() == atom;
                           });
    };
    return contains(spec.m_preset) || contains(spec.m_assert) ||
           contains(spec.m_guarantee);
}

std::set<std::string> core_atoms(const tlsf::MinimalUnrealizableCore& muc) {
    std::set<std::string> atoms;
    for (const tlsf::CoreFormula& entry : muc.formulae) {
        atoms.insert(entry.formula.to_string());
    }
    return atoms;
}

// The two-guarantee conflict {a, b} sits among four guarantees; QuickXplain
// must return exactly {a, b}.
TEST(test_extracts_minimal_pair) {
    tlsf::Specification spec;
    spec.m_inputs = {"x"};
    spec.m_outputs = {"y"};
    spec.m_guarantee = {Formula::make_atom("a"), Formula::make_atom("b"),
                        Formula::make_atom("c"), Formula::make_atom("d")};
    const tlsf::RealizabilityOracle oracle =
        [](const tlsf::Specification& probe) {
            return !(has_atom(probe, "a") && has_atom(probe, "b"));
        };

    const tlsf::MinimalUnrealizableCore muc = tlsf::extract_muc(spec, oracle);
    expect(core_atoms(muc) == std::set<std::string>({"a", "b"}),
           "core is exactly {a, b}");
    expect(has_atom(muc.spec, "a") && has_atom(muc.spec, "b"),
           "rebuilt core spec keeps the culprits");
    expect(!has_atom(muc.spec, "c") && !has_atom(muc.spec, "d"),
           "rebuilt core spec drops the innocent guarantees");
}

// The conflict is on the last two candidates; confirms the result is a genuine
// minimal set, not just a prefix of the input order.
TEST(test_conflict_at_tail) {
    tlsf::Specification spec;
    spec.m_outputs = {"y"};
    spec.m_guarantee = {Formula::make_atom("a"), Formula::make_atom("b"),
                        Formula::make_atom("c"), Formula::make_atom("d")};
    const tlsf::RealizabilityOracle oracle =
        [](const tlsf::Specification& probe) {
            return !(has_atom(probe, "c") && has_atom(probe, "d"));
        };

    const tlsf::MinimalUnrealizableCore muc = tlsf::extract_muc(spec, oracle);
    expect(core_atoms(muc) == std::set<std::string>({"c", "d"}),
           "core is exactly {c, d}");
}

// A single-formula core.
TEST(test_singleton_core) {
    tlsf::Specification spec;
    spec.m_outputs = {"y"};
    spec.m_guarantee = {Formula::make_atom("a"), Formula::make_atom("b")};
    const tlsf::RealizabilityOracle oracle =
        [](const tlsf::Specification& probe) { return !has_atom(probe, "a"); };

    const tlsf::MinimalUnrealizableCore muc = tlsf::extract_muc(spec, oracle);
    expect(muc.formulae.size() == 1, "core has one formula");
    expect(core_atoms(muc) == std::set<std::string>({"a"}), "core is {a}");
}

// The culprits span two different sections; the core must tag each with the
// right section id so the rebuilt spec places them correctly.
TEST(test_core_spans_sections) {
    tlsf::Specification spec;
    spec.m_outputs = {"y"};
    spec.m_preset = {Formula::make_atom("p")};
    spec.m_guarantee = {Formula::make_atom("g"), Formula::make_atom("h")};
    const tlsf::RealizabilityOracle oracle =
        [](const tlsf::Specification& probe) {
            return !(has_atom(probe, "p") && has_atom(probe, "g"));
        };

    const tlsf::MinimalUnrealizableCore muc = tlsf::extract_muc(spec, oracle);
    expect(core_atoms(muc) == std::set<std::string>({"p", "g"}),
           "core is {p, g} across preset and guarantee");
    bool preset_tagged = false;
    bool guarantee_tagged = false;
    for (const tlsf::CoreFormula& entry : muc.formulae) {
        if (entry.formula.to_string() == "p") {
            preset_tagged = entry.section_id == 1;
        }
        if (entry.formula.to_string() == "g") {
            guarantee_tagged = entry.section_id == 5;
        }
    }
    expect(preset_tagged, "p is tagged as the PRESET section");
    expect(guarantee_tagged, "g is tagged as the GUARANTEE section");
    expect(muc.spec.m_preset.size() == 1 && muc.spec.m_guarantee.size() == 1,
           "rebuilt spec places each culprit in its own section");
}

// With no guarantee-side formulae there is nothing to minimise: the core is
// empty even when the oracle reports unrealizable.
TEST(test_empty_guarantee_side) {
    tlsf::Specification spec;
    spec.m_inputs = {"x"};
    spec.m_outputs = {"y"};
    const tlsf::RealizabilityOracle oracle = [](const tlsf::Specification&) {
        return false;
    };

    const tlsf::MinimalUnrealizableCore muc = tlsf::extract_muc(spec, oracle);
    expect(muc.formulae.empty(), "empty guarantee side yields an empty core");
}

// The concurrent singleton pre-screen must not change the answer. A singleton
// conflict sitting third is found by the screen rather than by QuickXplain's
// walk, and the core is the same either way.
TEST(test_screen_finds_singleton) {
    tlsf::Specification spec;
    spec.m_outputs = {"y"};
    spec.m_guarantee = {Formula::make_atom("a"), Formula::make_atom("b"),
                        Formula::make_atom("c"), Formula::make_atom("d")};
    const tlsf::RealizabilityOracle oracle =
        [](const tlsf::Specification& probe) { return !has_atom(probe, "c"); };

    const tlsf::MinimalUnrealizableCore screened =
        tlsf::extract_muc(spec, oracle, 4);
    expect(core_atoms(screened) == std::set<std::string>({"c"}),
           "the pre-screen returns the singleton core {c}");
    expect(core_atoms(screened) == core_atoms(tlsf::extract_muc(spec, oracle)),
           "screened and serial extraction agree");
}

// Two formulae are each unrealizable alone. The screen runs its probes
// concurrently, so the tie is broken by index rather than by which probe
// finishes first, or the core stops being reproducible.
TEST(test_screen_picks_lowest_index) {
    tlsf::Specification spec;
    spec.m_outputs = {"y"};
    spec.m_guarantee = {Formula::make_atom("a"), Formula::make_atom("b"),
                        Formula::make_atom("c"), Formula::make_atom("d")};
    const tlsf::RealizabilityOracle oracle =
        [](const tlsf::Specification& probe) {
            return !(has_atom(probe, "b") || has_atom(probe, "d"));
        };

    for (int repeat = 0; repeat < 8; ++repeat) {
        const tlsf::MinimalUnrealizableCore muc =
            tlsf::extract_muc(spec, oracle, 4);
        expect(core_atoms(muc) == std::set<std::string>({"b"}),
               "the earlier of two singleton conflicts wins every time");
    }
}

// With no singleton conflict the screen finds nothing and QuickXplain answers,
// with the probes it already ran served from the memo.
TEST(test_screen_falls_through_to_quickxplain) {
    tlsf::Specification spec;
    spec.m_outputs = {"y"};
    spec.m_guarantee = {Formula::make_atom("a"), Formula::make_atom("b"),
                        Formula::make_atom("c"), Formula::make_atom("d")};
    const tlsf::RealizabilityOracle oracle =
        [](const tlsf::Specification& probe) {
            return !(has_atom(probe, "c") && has_atom(probe, "d"));
        };

    const tlsf::MinimalUnrealizableCore muc =
        tlsf::extract_muc(spec, oracle, 4);
    expect(core_atoms(muc) == std::set<std::string>({"c", "d"}),
           "a pairwise conflict still comes back as {c, d}");
}

// End-to-end against ltlsynt on the unrealizable arbiter fixture: the core
// must be a strict, still-unrealizable subset, minimal in that dropping any
// one member restores realizability.
const char* const k_unrealizable_arbiter =
    "INFO { SEMANTICS: Mealy; }\n"
    "MAIN {\n"
    "  INPUTS { r0; r1; }\n"
    "  OUTPUTS { g0; g1; }\n"
    "  GUARANTEE {\n"
    "    G (g0 -> r0);\n"
    "    G (g1 -> r1);\n"
    "    G !(g0 & g1);\n"
    "    G F g0;\n"
    "    G F g1;\n"
    "  }\n"
    "}\n";

bool is_realizable(const tlsf::Specification& spec) {
    // No timeout is set in the tests, so every query is decided; value_or's
    // argument is unreachable rather than a policy choice.
    return global_real_checker()
        .check_realizability_ltl(spec.to_ltl(), spec.m_inputs, spec.m_outputs)
        .value_or(false);
}

tlsf::Specification without(const tlsf::Specification& base,
                            const tlsf::CoreFormula& entry) {
    tlsf::Specification reduced = base;
    auto erase_one = [&entry](tlsf::Section& section) {
        for (auto it = section.begin(); it != section.end(); ++it) {
            if (it->m_formula.to_string() == entry.formula.to_string()) {
                section.erase(it);
                return;
            }
        }
    };
    switch (entry.section_id) {
        case 1:
            erase_one(reduced.m_preset);
            break;
        case 4:
            erase_one(reduced.m_assert);
            break;
        default:
            erase_one(reduced.m_guarantee);
            break;
    }
    return reduced;
}

// non_core_formulae returns the guarantee-side formulae NOT in the core, with
// multiset semantics (one occurrence removed per core member).
TEST(test_non_core_formulae) {
    tlsf::Specification spec;
    spec.m_preset = {Formula::make_atom("p")};
    spec.m_guarantee = {Formula::make_atom("a"), Formula::make_atom("a"),
                        Formula::make_atom("b"), Formula::make_atom("c")};
    const std::vector<tlsf::CoreFormula> core = {{5, Formula::make_atom("a")},
                                                 {5, Formula::make_atom("c")}};

    const std::vector<tlsf::CoreFormula> rest =
        tlsf::non_core_formulae(spec, core);
    // One 'a' consumed by the core, the other survives; 'b' survives; 'p'
    // (preset, never in this core) survives; 'c' consumed.
    std::vector<std::string> names;
    names.reserve(rest.size());
    for (const tlsf::CoreFormula& entry : rest) {
        names.push_back(entry.formula.to_string());
    }
    std::sort(names.begin(), names.end());
    expect(names == std::vector<std::string>({"a", "b", "p"}),
           "non-core keeps the untouched guarantees with multiset semantics");
}

// reintegrate appends the carried-over non-core formulae back onto a repaired
// sub-specification, each into its tagged section.
TEST(test_reintegrate) {
    tlsf::Specification repaired;
    repaired.m_outputs = {"y"};
    repaired.m_guarantee = {Formula::make_atom("a_repaired")};
    const std::vector<tlsf::CoreFormula> non_core = {
        {1, Formula::make_atom("p")}, {5, Formula::make_atom("b")}};

    const tlsf::Specification whole = tlsf::reintegrate(repaired, non_core);
    expect(whole.m_preset.size() == 1 &&
               whole.m_preset[0].m_formula.to_string() == "p",
           "reintegrate restores the preset formula");
    expect(whole.m_guarantee.size() == 2, "reintegrate keeps repaired + b");
    expect(whole.m_guarantee[0].m_formula.to_string() == "a_repaired" &&
               whole.m_guarantee[1].m_formula.to_string() == "b",
           "reintegrate appends non-core after the repaired core");
}

TEST(test_arbiter_end_to_end) {
    const tlsf::Specification spec = tlsf::parse(k_unrealizable_arbiter);
    expect(!is_realizable(spec), "fixture is unrealizable");

    const tlsf::MinimalUnrealizableCore muc = tlsf::extract_muc(spec);
    expect(!muc.formulae.empty(), "arbiter core is non-empty");
    expect(muc.formulae.size() < spec.m_guarantee.size(),
           "core is a strict subset of the guarantees");
    expect(!is_realizable(muc.spec), "core spec is still unrealizable");
    for (const tlsf::CoreFormula& entry : muc.formulae) {
        expect(is_realizable(without(muc.spec, entry)),
               "dropping any one core formula restores realizability");
    }
}

// n_undecided counts only probes the oracle left undecided. An oracle that
// answers every query -- as every fixture oracle here does, and as ltlsynt
// does with no timeout set -- must leave it at zero, or the MUC loop reports
// a sound core as provisional on every iteration.
TEST(test_undecided_count_is_zero_when_decided) {
    tlsf::Specification spec;
    spec.m_outputs = {"y"};
    spec.m_guarantee = {Formula::make_atom("a"), Formula::make_atom("b")};
    const tlsf::RealizabilityOracle oracle =
        [](const tlsf::Specification& probe) { return !has_atom(probe, "a"); };

    expect(tlsf::extract_muc(spec, oracle).n_undecided == 0,
           "a decided oracle leaves no undecided probes");
    const tlsf::Specification arbiter = tlsf::parse(k_unrealizable_arbiter);
    expect(tlsf::extract_muc(arbiter).n_undecided == 0,
           "ltlsynt decides every arbiter probe when no timeout is set");
}

}  // namespace
