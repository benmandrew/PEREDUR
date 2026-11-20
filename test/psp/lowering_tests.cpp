// The PSP lowering against its two references: the Dwyer web table, and the
// body-and-wrapper lowering in scripts/psp_field_orders.py that measured the
// PSP orders. The script's lowering of every pattern x scope at counts 1 to 4
// is the "lowering" table of docs/dev/psp-orders.json, which the script wrote;
// scripts/test_psp_field_orders.py checks that table still equals what the
// script lowers, so the fixture cannot drift from the script.

#include <array>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <functional>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ltl_oracle.hpp"
#include "prop_formula.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"
#include "requirement.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/process.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "psp_lowering";

using psp::PspScopeKind;

constexpr std::array<PspScopeKind, 5> k_scopes = {
    PspScopeKind::Globally, PspScopeKind::Before, PspScopeKind::After,
    PspScopeKind::Between, PspScopeKind::AfterUntil};

// Every pattern the script measures, in its order: the five base patterns,
// then each bounded one at counts 1 to 4.
std::vector<psp::Pattern> measured_patterns() {
    std::vector<psp::Pattern> out = {
        psp::pattern::Absence{}, psp::pattern::Universality{},
        psp::pattern::Existence{}, psp::pattern::Precedence{},
        psp::pattern::Response{}};
    for (std::size_t count = 1; count <= 4; ++count) {
        out.emplace_back(psp::pattern::BoundedResponse{count});
    }
    for (std::size_t count = 1; count <= 4; ++count) {
        out.emplace_back(psp::pattern::BoundedInvariance{count});
    }
    for (std::size_t count = 1; count <= 4; ++count) {
        out.emplace_back(psp::pattern::BoundedExistence{count});
    }
    for (std::size_t count = 1; count <= 4; ++count) {
        out.emplace_back(psp::pattern::BoundedRecurrence{count});
    }
    return out;
}

// The requirement over the given field formulas, each present where the
// pattern or scope reads it.
psp::Requirement over(const psp::Pattern& pattern, PspScopeKind scope,
                      const std::string& field_p = "p",
                      const std::string& field_s = "s",
                      const std::string& field_q = "q",
                      const std::string& field_r = "r") {
    const auto field = [](bool used, const std::string& text) {
        return used ? std::optional<Formula>(Formula(text)) : std::nullopt;
    };
    return {pattern,
            scope,
            field(psp::pattern_uses_p(pattern), field_p),
            field(psp::pattern_uses_s(pattern), field_s),
            field(psp::scope_uses_q(scope), field_q),
            field(psp::scope_uses_r(scope), field_r)};
}

// The node's name as docs/dev/psp-orders.json keys it: "bounded-response
// 2/between".
std::string node_name(const psp::Pattern& pattern, PspScopeKind scope) {
    std::string out = psp::pattern_name(pattern);
    if (const std::optional<std::size_t> count = psp::pattern_count(pattern)) {
        out += " " + std::to_string(*count);
    }
    return out + "/" + psp::scope_name(scope);
}

nlohmann::json orders_json() {
    const std::string path =
        std::string(PEREDUR_SOURCE_DIR) + "/docs/dev/psp-orders.json";
    std::ifstream file(path);
    expect(static_cast<bool>(file), "psp: cannot read " + path);
    return nlohmann::json::parse(file);
}

// Both directions in one ltlfilt run, which fails the test when it cannot
// decide, where ltl_equivalent would read a failure as a match.
std::vector<bool> equivalences(
    const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::vector<Implication> queries;
    queries.reserve(2 * pairs.size());
    for (const auto& [lhs, rhs] : pairs) {
        queries.push_back({lhs, rhs});
        queries.push_back({rhs, lhs});
    }
    const std::vector<bool> holds = ltl_implications(queries);
    std::vector<bool> out;
    out.reserve(pairs.size());
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        out.push_back(holds[2 * i] && holds[(2 * i) + 1]);
    }
    return out;
}

bool accepts(const std::string& formula, const std::string& word) {
    const ProcessResult result = execute_and_capture(
        {ltlfilt_path(), "-f", formula, "--accept-word=" + word},
        std::chrono::milliseconds{60'000});
    expect(result.m_exit_code == 0 || result.m_exit_code == 1,
           "psp: ltlfilt --accept-word failed: " + result.m_output);
    return result.m_exit_code == 0;
}

// Dwyer, Avrunin and Corbett's rows as the pattern website prints them
// (matthewbdwyer.github.io/psp/patterns/ltl.html), with Dwyer's single
// proposition renamed to s in universality and existence and to p in absence
// and bounded existence. Bounded existence is the published instance k = 2,
// with its parentheses balanced. The same table as DWYER in
// scripts/psp_field_orders.py.
struct DwyerRow {
    const char* m_pattern = nullptr;
    PspScopeKind m_scope = PspScopeKind::Globally;
    const char* m_published = nullptr;
};

const std::vector<DwyerRow>& dwyer_rows() {
    static const std::vector<DwyerRow> rows = {
        {"absence", PspScopeKind::Globally, "G(!p)"},
        {"absence", PspScopeKind::Before, "F r -> (!p U r)"},
        {"absence", PspScopeKind::After, "G(q -> G(!p))"},
        {"absence", PspScopeKind::Between, "G((q & !r & F r) -> (!p U r))"},
        {"absence", PspScopeKind::AfterUntil, "G((q & !r) -> (!p W r))"},
        {"existence", PspScopeKind::Globally, "F(s)"},
        {"existence", PspScopeKind::Before, "!r W (s & !r)"},
        {"existence", PspScopeKind::After, "G(!q) | F(q & F(s))"},
        {"existence", PspScopeKind::Between, "G((q & !r) -> (!r W (s & !r)))"},
        {"existence", PspScopeKind::AfterUntil,
         "G((q & !r) -> (!r U (s & !r)))"},
        {"universality", PspScopeKind::Globally, "G(s)"},
        {"universality", PspScopeKind::Before, "F r -> (s U r)"},
        {"universality", PspScopeKind::After, "G(q -> G(s))"},
        {"universality", PspScopeKind::Between, "G((q & !r & F r) -> (s U r))"},
        {"universality", PspScopeKind::AfterUntil, "G((q & !r) -> (s W r))"},
        {"precedence", PspScopeKind::Globally, "!p W s"},
        {"precedence", PspScopeKind::Before, "F r -> (!p U (s | r))"},
        {"precedence", PspScopeKind::After, "G(!q) | F(q & (!p W s))"},
        {"precedence", PspScopeKind::Between,
         "G((q & !r & F r) -> (!p U (s | r)))"},
        {"precedence", PspScopeKind::AfterUntil,
         "G((q & !r) -> (!p W (s | r)))"},
        {"response", PspScopeKind::Globally, "G(p -> F s)"},
        {"response", PspScopeKind::Before,
         "F r -> ((p -> (!r U (s & !r))) U r)"},
        {"response", PspScopeKind::After, "G(q -> G(p -> F s))"},
        {"response", PspScopeKind::Between,
         "G((q & !r & F r) -> ((p -> (!r U (s & !r))) U r))"},
        {"response", PspScopeKind::AfterUntil,
         "G((q & !r) -> ((p -> (!r U (s & !r))) W r))"},
        {"bounded-existence", PspScopeKind::Globally,
         "!p W (p W (!p W (p W G(!p))))"},
        {"bounded-existence", PspScopeKind::Before,
         "F r -> ((!p & !r) U (r | ((p & !r) U (r | ((!p & !r) U (r | "
         "((p & !r) U (r | (!p U r)))))))))"},
        {"bounded-existence", PspScopeKind::After,
         "F q -> (!q U (q & (!p W (p W (!p W (p W G(!p)))))))"},
        {"bounded-existence", PspScopeKind::Between,
         "G((q & F r) -> ((!p & !r) U (r | ((p & !r) U (r | ((!p & !r) U "
         "(r | ((p & !r) U (r | (!p U r))))))))))"},
        {"bounded-existence", PspScopeKind::AfterUntil,
         "G(q -> ((!p & !r) U (r | ((p & !r) U (r | ((!p & !r) U (r | "
         "((p & !r) U (r | (!p W r) | G(p))))))))))"},
    };
    return rows;
}

psp::Pattern dwyer_pattern(const std::string& name) {
    for (const psp::Pattern& pattern : measured_patterns()) {
        const std::optional<std::size_t> count = psp::pattern_count(pattern);
        if (name == psp::pattern_name(pattern) &&
            (!count.has_value() || *count == 2)) {
            return pattern;
        }
    }
    fail("psp: no pattern named " + name);
}

// --- The oracle is live ----------------------------------------------------

// ltl_equivalent reads an ltlfilt failure as a match, so first check that it
// tells two formulas apart.
TEST(test_psp_oracle_is_live) {
    expect(ltl_equivalent("a", "a"), "oracle: a is equivalent to itself");
    expect(!ltl_equivalent("a", "X(a)"), "oracle: a is not equivalent to X a");
    expect(accepts("G(a)", "cycle{a}") && !accepts("G(a)", "!a; cycle{a}"),
           "oracle: --accept-word tells a word in G a from one outside it");
}

// --- The reference lowering -------------------------------------------------

TEST(test_psp_lowering_matches_the_reference_script) {
    const nlohmann::json lowering = orders_json().at("lowering");
    const std::vector<psp::Pattern> patterns = measured_patterns();
    expect(lowering.size() == patterns.size() * k_scopes.size(),
           "psp: the fixture lowers every pattern under every scope");
    std::vector<std::pair<std::string, std::string>> pairs;
    std::vector<std::string> names;
    for (const psp::Pattern& pattern : patterns) {
        for (const PspScopeKind scope : k_scopes) {
            const std::string name = node_name(pattern, scope);
            expect(lowering.contains(name), "psp: the fixture lacks " + name);
            pairs.emplace_back(over(pattern, scope).m_ltl,
                               lowering.at(name).get<std::string>());
            names.push_back(name);
        }
    }
    const std::vector<bool> same = equivalences(pairs);
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        expect(same[i], "psp: " + names[i] + " lowers to " + pairs[i].first +
                            ", not equivalent to the script's " +
                            pairs[i].second);
        expect(ltl_equivalent(pairs[i].first, pairs[i].second),
               "psp: ltl_equivalent agrees on " + names[i]);
    }
}

// A compound field sits in parentheses wherever the lowering puts it, so
// substituting it into the script's atom lowering gives the same formula.
TEST(test_psp_lowering_parenthesises_compound_fields) {
    const nlohmann::json lowering = orders_json().at("lowering");
    const std::array<std::pair<const char*, const char*>, 4> substitutions = {
        {{"p", "(a | b)"},
         {"s", "(c & !d)"},
         {"q", "(e -> f)"},
         {"r", "!(g <-> h)"}}};
    std::vector<std::pair<std::string, std::string>> pairs;
    std::vector<std::string> names;
    for (const psp::Pattern& pattern : measured_patterns()) {
        if (psp::pattern_count(pattern).value_or(1) > 2) {
            continue;
        }
        for (const PspScopeKind scope : k_scopes) {
            std::string expected =
                lowering.at(node_name(pattern, scope)).get<std::string>();
            for (const auto& [atom, formula] : substitutions) {
                expected = std::regex_replace(
                    expected, std::regex(std::string("\\b") + atom + "\\b"),
                    std::string("(") + formula + ")");
            }
            pairs.emplace_back(
                over(pattern, scope, "a | b", "c & !d", "e -> f", "!(g <-> h)")
                    .m_ltl,
                expected);
            names.push_back(node_name(pattern, scope));
        }
    }
    const std::vector<bool> same = equivalences(pairs);
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        expect(same[i], "psp: with compound fields, " + names[i] +
                            " lowers to " + pairs[i].first);
    }
}

// The bodies of the bounded patterns stay X chains SPOT and black both read:
// no bounded operator and no unparenthesised X operand.
TEST(test_psp_lowering_expands_bounded_operators) {
    for (const psp::Pattern& pattern : measured_patterns()) {
        for (const PspScopeKind scope : k_scopes) {
            const std::string ltl = over(pattern, scope).m_ltl;
            expect(ltl.find('[') == std::string::npos,
                   "psp: " + node_name(pattern, scope) +
                       " carries no bounded operator: " + ltl);
            expect(!std::regex_search(ltl, std::regex("X[^(]")),
                   "psp: every X operand is parenthesised in " + ltl);
            expect(Formula::try_parse(ltl).has_value(),
                   "psp: the lowering of " + node_name(pattern, scope) +
                       " reads back through Formula");
        }
    }
}

// --- The Dwyer web table
// ------------------------------------------------------

// 24 of the 25 base rows are equivalent to the lowering. Precedence after Q is
// the D4 departure: the lowering opens the scope at the first q only, so it is
// strictly stronger than the published row, which lets any q open it.
TEST(test_psp_lowering_matches_the_dwyer_table) {
    std::vector<Implication> queries;
    std::vector<std::string> names;
    for (const DwyerRow& row : dwyer_rows()) {
        if (std::string(row.m_pattern) == "bounded-existence") {
            continue;
        }
        const std::string lowered =
            over(dwyer_pattern(row.m_pattern), row.m_scope).m_ltl;
        queries.push_back({lowered, row.m_published});
        queries.push_back({row.m_published, lowered});
        names.push_back(std::string(row.m_pattern) + "/" +
                        psp::scope_name(row.m_scope));
    }
    const std::vector<bool> holds = ltl_implications(queries);
    std::size_t equivalent = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const bool forward = holds[2 * i];
        const bool backward = holds[(2 * i) + 1];
        if (names[i] == "precedence/after") {
            expect(forward && !backward,
                   "psp: precedence after Q is strictly stronger than its "
                   "published row");
            continue;
        }
        expect(forward && backward,
               "psp: " + names[i] + " is equivalent to its published row");
        ++equivalent;
    }
    expect(equivalent == 24, "psp: 24 base rows are equivalent");

    // The counter-trace of docs/dev/psp-orders.json: the second q would open
    // the published row's scope, after a p that no s preceded.
    const std::string word =
        "q&!p&!r&!s; p&!q&!r&!s; q&!p&!r&!s; cycle{!p&!q&!r&!s}";
    expect(orders_json().at("counter_trace").at("word") == word,
           "psp: the pinned counter-trace is the measured one");
    const std::string lowered =
        over(psp::pattern::Precedence{}, PspScopeKind::After).m_ltl;
    expect(accepts("G(!q) | F(q & (!p W s))", word),
           "psp: the published precedence-after row accepts the trace");
    expect(!accepts(lowered, word),
           "psp: the lowering of precedence after Q rejects the trace");
}

// P0 found the five published bounded-existence rows (k = 2) equivalent to the
// lowering, although they keep the old guards (`Q & F R`, or `Q` alone, with
// no `!R`): a body that starts where R holds is true at once.
TEST(test_psp_bounded_existence_matches_the_dwyer_table) {
    std::vector<std::pair<std::string, std::string>> pairs;
    std::vector<std::string> names;
    for (const DwyerRow& row : dwyer_rows()) {
        if (std::string(row.m_pattern) != "bounded-existence") {
            continue;
        }
        pairs.emplace_back(
            over(psp::pattern::BoundedExistence{2}, row.m_scope).m_ltl,
            row.m_published);
        names.emplace_back(psp::scope_name(row.m_scope));
    }
    expect(pairs.size() == 5, "psp: five bounded-existence rows");
    const std::vector<bool> same = equivalences(pairs);
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        expect(same[i], "psp: bounded existence 2 under " + names[i] +
                            " is equivalent to its published row");
    }
    // The published rows are the fixture's too, and P0 recorded each as
    // equivalent.
    const nlohmann::json dwyer = orders_json().at("dwyer");
    for (const std::string& name : names) {
        expect(dwyer.at("bounded-existence/" + name).at("lowering") ==
                   "equivalent",
               "psp: P0 found bounded existence under " + name + " equivalent");
    }
}

// --- Fixed requirements
// -------------------------------------------------------

TEST(test_psp_fixed_requirement_is_raw_ltl) {
    const psp::Requirement fixed =
        psp::Requirement::fixed(Formula("G(a -> F(b))"));
    expect(fixed.is_fixed() && !fixed.m_weakenable,
           "psp: a fixed requirement is never weakenable");
    expect(ltl_equivalent(fixed.m_ltl, "G(a -> F b)"),
           "psp: a fixed requirement lowers to its own formula");
    expect(fixed.to_string().rfind("Fixed: ", 0) == 0,
           "psp: a fixed requirement prints as such");
}

// --- Structured English
// ---------------------------------------------------------

TEST(test_psp_prints_structured_english) {
    expect(over(psp::pattern::Response{}, PspScopeKind::Between).to_string() ==
               "Between q and r, it is always the case that if p holds, then "
               "s eventually holds.",
           "psp: response between Q and R");
    expect(over(psp::pattern::Absence{}, PspScopeKind::Globally, "a | b")
                   .to_string() ==
               "Globally, it is never the case that ((a) | (b)) holds.",
           "psp: a compound field prints in parentheses");
    expect(over(psp::pattern::BoundedResponse{3}, PspScopeKind::AfterUntil)
                   .to_string() ==
               "After q until r, it is always the case that if p holds, then "
               "s holds after at most 3 time units.",
           "psp: bounded response after Q until R");
    expect(over(psp::pattern::BoundedExistence{2}, PspScopeKind::Before)
                   .to_string() ==
               "Before r, transitions to states in which p holds occur at "
               "most 2 times.",
           "psp: bounded existence before R");
    expect(over(psp::pattern::BoundedRecurrence{4}, PspScopeKind::After)
                   .to_string() ==
               "After q, it is always the case that s holds at least every 4 "
               "time units.",
           "psp: bounded recurrence after Q");
}

// --- Order and hash
// ---------------------------------------------------------------

TEST(test_psp_order_and_hash_see_every_field) {
    const psp::Requirement base =
        over(psp::pattern::BoundedResponse{2}, PspScopeKind::Between);
    const std::vector<psp::Requirement> variants = {
        over(psp::pattern::BoundedResponse{3}, PspScopeKind::Between),
        over(psp::pattern::BoundedInvariance{2}, PspScopeKind::Between),
        over(psp::pattern::BoundedResponse{2}, PspScopeKind::AfterUntil),
        over(psp::pattern::BoundedResponse{2}, PspScopeKind::Between, "x"),
        over(psp::pattern::BoundedResponse{2}, PspScopeKind::Between, "p", "x"),
        over(psp::pattern::BoundedResponse{2}, PspScopeKind::Between, "p", "s",
             "x"),
        over(psp::pattern::BoundedResponse{2}, PspScopeKind::Between, "p", "s",
             "q", "x"),
        psp::Requirement(base.m_pattern, base.m_scope, base.m_p, base.m_s,
                         base.m_q, base.m_r, false),
        psp::Requirement(base.m_pattern, base.m_scope, base.m_p, base.m_s,
                         base.m_q, base.m_r, true, true),
    };
    const std::hash<psp::Requirement> hasher;
    for (const psp::Requirement& other : variants) {
        expect(!(base == other) && ((base < other) != (other < base)),
               "psp: a requirement differing in one field orders apart: " +
                   other.to_string());
        expect(hasher(base) != hasher(other),
               "psp: a requirement differing in one field hashes apart: " +
                   other.to_string());
    }
    const psp::Requirement copy =
        over(psp::pattern::BoundedResponse{2}, PspScopeKind::Between);
    expect(base == copy, "psp: equal requirements compare equal");
    expect(!(base < copy) && !(copy < base),
           "psp: equal requirements order neither way");
    expect(hasher(base) == hasher(copy), "psp: equal requirements hash equal");
}

// --- The .psp format
// ----------------------------------------------------------------

const char* const k_spec = R"JSON({
  "assumptions": [
    { "fixed": "G(!(start & stop))" },
    { "pattern": "bounded-recurrence", "count": 3, "s": "stop",
      "weakenable": false }
  ],
  "guarantees": [
    { "pattern": "bounded-response", "count": 2, "scope": "afteruntil",
      "p": "req", "s": "grant | busy", "q": "start", "r": "stop" },
    { "pattern": "absence", "scope": "before", "p": "grant & !req",
      "r": "stop" },
    { "pattern": "precedence", "scope": "after", "p": "grant", "s": "req",
      "q": "start" }
  ],
  "in_atoms": ["req", "start", "stop"],
  "out_atoms": ["grant", "busy"]
})JSON";

TEST(test_psp_format_round_trips) {
    const psp::Specification spec = psp::parse_specification(k_spec);
    expect(spec.m_assumptions.size() == 2 && spec.m_guarantees.size() == 3,
           "psp: every requirement loads");
    expect(spec.m_assumptions[0].is_fixed() &&
               !spec.m_assumptions[1].m_weakenable &&
               spec.m_assumptions[1].m_scope == PspScopeKind::Globally,
           "psp: a fixed entry, a lock and an omitted scope load as written");
    const std::string written = psp::write_specification(spec);
    const psp::Specification reread = psp::parse_specification(written);
    expect(reread == spec, "psp: a written specification parses back equal");
    expect(reread.to_ltl() == spec.to_ltl(),
           "psp: and lowers to the same formula");
    expect(psp::write_specification(reread) == written,
           "psp: writing is a fixed point after one round trip");
    expect(std::hash<psp::Specification>{}(reread) ==
               std::hash<psp::Specification>{}(spec),
           "psp: and hashes the same");

    // The lowering of the loaded file is the lowering of the same requirement
    // built by hand, under the atom prefix.
    const psp::Specification bare = psp::strip_atom_prefix(spec);
    const psp::Requirement by_hand =
        over(psp::pattern::BoundedResponse{2}, PspScopeKind::AfterUntil, "req",
             "grant | busy", "start", "stop");
    expect(bare.m_guarantees[0] == by_hand,
           "psp: a loaded requirement equals the one built by hand");
    expect(psp::add_atom_prefix(by_hand) == spec.m_guarantees[0],
           "psp: the loader prefixes as add_atom_prefix does");
}

TEST(test_psp_format_prefixes_atoms) {
    const psp::Specification spec = psp::parse_specification(k_spec);
    expect(spec.m_in_atoms.front() == std::string(k_atom_prefix) + "req",
           "psp: the atom lists carry the prefix in memory");
    expect(spec.m_guarantees[0].m_ltl.find("iap_grant") != std::string::npos &&
               spec.m_guarantees[0].m_ltl.find("(grant") == std::string::npos,
           "psp: the lowering names the prefixed atoms");
    expect(spec.m_assumptions[0].m_ltl.find("iap_start") != std::string::npos,
           "psp: a fixed formula's atoms are prefixed too");
    expect(psp::write_specification(spec).find("iap_") == std::string::npos,
           "psp: the written file shows the user's names");
    const psp::Specification with_constant = psp::parse_specification(R"JSON({
      "assumptions": [],
      "guarantees": [{ "pattern": "universality", "s": "true | grant" }],
      "in_atoms": [], "out_atoms": ["grant"]})JSON");
    expect(with_constant.m_guarantees[0].m_ltl.find("iap_true") ==
               std::string::npos,
           "psp: the constants are not prefixed");
}

TEST(test_psp_format_rejects_unsafe_atom_names) {
    const auto spec_with = [](const std::string& ins, const std::string& outs) {
        return R"({"assumptions": [], "guarantees": [{"pattern": "response",
            "p": "a", "s": "b"}], "in_atoms": )" +
               ins + R"(, "out_atoms": )" + outs + "}";
    };
    expect_throws<std::invalid_argument>(
        [&] {
            psp::parse_specification(spec_with(R"(["a", "Req"])", R"(["b"])"));
        },
        "psp: an uppercase input is rejected", "uppercase");
    expect_throws<std::invalid_argument>(
        [&] {
            psp::parse_specification(spec_with(R"(["a", ""])", R"(["b"])"));
        },
        "psp: an empty atom name is rejected", "non-empty");
    // As on the FRETISH path: the prefix keeps an output named after an
    // operator whole, and an uppercase output is never passed to ltlsynt.
    const psp::Specification safe = psp::parse_specification(
        spec_with(R"(["a"])", R"(["b", "Fail", "GRANT"])"));
    expect(safe.m_out_atoms.size() == 3,
           "psp: uppercase and operator-led outputs load");
    expect(!psp::first_format_error(spec_with(R"(["A"])", R"(["b"])")).empty(),
           "psp: first_format_error reports the unsafe name");
}

TEST(test_psp_format_rejects_malformed_documents) {
    const auto guarantee = [](const std::string& req) {
        return R"({"assumptions": [], "guarantees": [)" + req +
               R"(], "in_atoms": ["a"], "out_atoms": ["b", "c"]})";
    };
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"{", "JSON parse error"},
        {R"({"assumptions": [], "guarantees": []})", "in_atoms"},
        {guarantee(R"({"pattern": "chain", "p": "a", "s": "b"})"),
         "guarantees[0].pattern: unknown value"},
        {guarantee(R"({"pattern": "response", "p": "a"})"),
         "guarantees[0].s: missing"},
        {guarantee(R"({"pattern": "response", "p": "a", "s": "b", "q": "c"})"),
         "guarantees[0].q: not read"},
        {guarantee(R"({"pattern": "absence", "s": "b", "p": "a"})"),
         "guarantees[0].s: not read"},
        {guarantee(R"({"pattern": "response", "p": "a", "s": "b",
                       "scope": "between", "q": "c"})"),
         "guarantees[0].r: missing"},
        {guarantee(R"({"pattern": "response", "p": "a", "s": "b",
                       "scope": "during"})"),
         "guarantees[0].scope"},
        {guarantee(R"({"pattern": "bounded-response", "p": "a", "s": "b"})"),
         "guarantees[0].count"},
        {guarantee(R"({"pattern": "bounded-response", "count": 0, "p": "a",
                       "s": "b"})"),
         "guarantees[0].count"},
        {guarantee(R"({"pattern": "bounded-response", "count": 65, "p": "a",
                       "s": "b"})"),
         "guarantees[0].count"},
        {guarantee(R"({"pattern": "response", "count": 2, "p": "a",
                       "s": "b"})"),
         "takes no count"},
        {guarantee(R"J({"pattern": "response", "p": "F(a)", "s": "b"})J"),
         "guarantees[0].p: must be propositional"},
        {guarantee(R"({"pattern": "response", "p": "a &", "s": "b"})"),
         "guarantees[0].p: not a formula"},
        {guarantee(R"({"pattern": "response", "p": "a", "s": "b",
                       "weakenable": "no"})"),
         "weakenable"},
        {guarantee(R"({"pattern": "response", "p": "a", "s": "b",
                       "extra": 1})"),
         "guarantees[0].extra: unknown field"},
        {guarantee(R"J({"fixed": "G(a)", "weakenable": true})J"),
         "a fixed requirement takes no other field"},
        {guarantee(R"({"fixed": "F[0,2] a"})"), "guarantees[0].fixed"},
        {guarantee(R"({"pattern": "response", "p": "a", "s": "d"})"),
         "atom 'd' is declared in neither"},
    };
    for (const auto& item : cases) {
        const std::string& text = item.first;
        expect_throws<std::invalid_argument>(
            [&text] { psp::parse_specification(text); },
            "psp: a malformed document is rejected: " + text, item.second);
    }
}

}  // namespace
