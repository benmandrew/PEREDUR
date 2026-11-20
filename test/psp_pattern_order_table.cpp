// Prints psp::pattern_implies for every scope and every ordered pair of
// patterns with counts up to 4, one `scope<TAB>stronger<TAB>weaker<TAB>0|1`
// line each, for the Lean cross-check of the PSP pattern order (phase P6 of
// the PSP plan). Patterns are named as docs/dev/psp-orders.json names them. A
// development tool, not a driver: it is built with the tests and installs
// nowhere.

#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "psp/implication.hpp"
#include "psp/requirement.hpp"

namespace {

constexpr std::size_t k_max_count = 4;

std::vector<psp::Pattern> patterns() {
    std::vector<psp::Pattern> out = {
        psp::pattern::Absence{}, psp::pattern::Universality{},
        psp::pattern::Existence{}, psp::pattern::Precedence{},
        psp::pattern::Response{}};
    for (std::size_t count = 1; count <= k_max_count; ++count) {
        out.emplace_back(psp::pattern::BoundedResponse{count});
        out.emplace_back(psp::pattern::BoundedInvariance{count});
        out.emplace_back(psp::pattern::BoundedExistence{count});
        out.emplace_back(psp::pattern::BoundedRecurrence{count});
    }
    return out;
}

std::string label(const psp::Pattern& pattern) {
    std::string out = psp::pattern_name(pattern);
    if (const std::optional<std::size_t> count = psp::pattern_count(pattern)) {
        out += " " + std::to_string(*count);
    }
    return out;
}

void print_table() {
    const std::array<psp::PspScopeKind, 5> scopes = {
        psp::PspScopeKind::Globally, psp::PspScopeKind::Before,
        psp::PspScopeKind::After, psp::PspScopeKind::Between,
        psp::PspScopeKind::AfterUntil};
    const std::vector<psp::Pattern> all = patterns();
    for (const psp::PspScopeKind scope : scopes) {
        for (const psp::Pattern& stronger : all) {
            for (const psp::Pattern& weaker : all) {
                const bool holds = psp::pattern_implies(
                    stronger, weaker, psp::scope_class(scope));
                std::cout << psp::scope_name(scope) << '\t' << label(stronger)
                          << '\t' << label(weaker) << '\t' << (holds ? 1 : 0)
                          << '\n';
            }
        }
    }
}

}  // namespace

int main() {
    try {
        print_table();
    } catch (const std::exception& exc) {
        std::cerr << "psp_pattern_order_table: " << exc.what() << "\n";
        return 1;
    }
    return 0;
}
