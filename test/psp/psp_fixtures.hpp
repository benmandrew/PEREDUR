#pragma once

// Fixtures the PSP suites share: the measured patterns and scopes, the
// requirement over given fields, the node names of docs/dev/psp-orders.json,
// and that file itself.

#include <array>
#include <cstddef>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "prop_formula.hpp"
#include "psp/requirement.hpp"
#include "test_support.hpp"

namespace psp_fixtures {

inline constexpr std::array<psp::PspScopeKind, 5> k_scopes = {
    psp::PspScopeKind::Globally, psp::PspScopeKind::Before,
    psp::PspScopeKind::After, psp::PspScopeKind::Between,
    psp::PspScopeKind::AfterUntil};

// Every pattern the script measures, in its order: the five base patterns,
// then each bounded one at counts 1 to @p max_count.
inline std::vector<psp::Pattern> measured_patterns(std::size_t max_count = 4) {
    std::vector<psp::Pattern> out = {
        psp::pattern::Absence{}, psp::pattern::Universality{},
        psp::pattern::Existence{}, psp::pattern::Precedence{},
        psp::pattern::Response{}};
    for (std::size_t count = 1; count <= max_count; ++count) {
        out.emplace_back(psp::pattern::BoundedResponse{count});
    }
    for (std::size_t count = 1; count <= max_count; ++count) {
        out.emplace_back(psp::pattern::BoundedInvariance{count});
    }
    for (std::size_t count = 1; count <= max_count; ++count) {
        out.emplace_back(psp::pattern::BoundedExistence{count});
    }
    for (std::size_t count = 1; count <= max_count; ++count) {
        out.emplace_back(psp::pattern::BoundedRecurrence{count});
    }
    return out;
}

// The requirement over the given field formulas, each present where the
// pattern or scope reads it.
inline psp::Requirement over(const psp::Pattern& pattern,
                             psp::PspScopeKind scope,
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

// The pattern's name as docs/dev/psp-orders.json spells it: "bounded-response
// 2".
inline std::string pattern_label(const psp::Pattern& pattern) {
    std::string out = psp::pattern_name(pattern);
    if (const std::optional<std::size_t> count = psp::pattern_count(pattern)) {
        out += " " + std::to_string(*count);
    }
    return out;
}

// The node's name as docs/dev/psp-orders.json keys it: "bounded-response
// 2/between".
inline std::string node_name(const psp::Pattern& pattern,
                             psp::PspScopeKind scope) {
    return pattern_label(pattern) + "/" + psp::scope_name(scope);
}

inline nlohmann::json orders_json() {
    const std::string path =
        std::string(PEREDUR_SOURCE_DIR) + "/docs/dev/psp-orders.json";
    std::ifstream file(path);
    expect(static_cast<bool>(file), "psp: cannot read " + path);
    return nlohmann::json::parse(file);
}

}  // namespace psp_fixtures
