// Prints psp::scope_implies for every pattern with counts up to 4 and every
// ordered pair of scopes, one `pattern<TAB>stronger<TAB>weaker<TAB>0|1` line
// each, for the Lean cross-check of the PSP scope order (phase P6 of the PSP
// plan). Patterns and scopes are named as docs/dev/psp-orders.json names them.
// A development tool, not a driver: it is built with the tests and installs
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
    for (const psp::Pattern& pattern : patterns()) {
        for (const psp::PspScopeKind stronger : scopes) {
            for (const psp::PspScopeKind weaker : scopes) {
                const bool holds = psp::scope_implies(
                    stronger, weaker, psp::pattern_cell(pattern));
                std::cout << label(pattern) << '\t' << psp::scope_name(stronger)
                          << '\t' << psp::scope_name(weaker) << '\t'
                          << (holds ? 1 : 0) << '\n';
            }
        }
    }
}

}  // namespace

int main() {
    try {
        print_table();
    } catch (const std::exception& exc) {
        std::cerr << "psp_scope_order_table: " << exc.what() << "\n";
        return 1;
    }
    return 0;
}
