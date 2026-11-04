// Prints timing_implies for every scope and every ordered pair of timings with
// tick counts up to 4, for scripts/check_lean_timing_order.py to compare with
// the Lean transcription in proofs/Fretish/Cpp.lean. A development tool, not a
// driver: it is built with the tests and installs nowhere.

#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "prop_formula.hpp"
#include "requirement.hpp"

namespace {

constexpr std::size_t k_max_ticks = 4;

std::vector<std::pair<Timing, std::string>> timings() {
    std::vector<std::pair<Timing, std::string>> out = {
        {timing::immediately(), "immediately"},
        {timing::next_timepoint(), "next"}};
    for (std::size_t ticks = 0; ticks <= k_max_ticks; ++ticks) {
        const std::string count = std::to_string(ticks);
        // timing_implies asserts that neither side is `within 0` or `for 0`.
        if (ticks > 0) {
            out.emplace_back(timing::within_ticks(ticks), "within " + count);
            out.emplace_back(timing::for_ticks(ticks), "for " + count);
        }
        out.emplace_back(timing::after_ticks(ticks), "after " + count);
    }
    out.emplace_back(timing::eventually(), "eventually");
    out.emplace_back(timing::always(), "always");
    out.emplace_back(timing::until(Formula("s")), "until");
    out.emplace_back(timing::before(Formula("s")), "before");
    return out;
}

void print_table() {
    const std::vector<std::pair<ScopeKind, const char*>> scopes = {
        {ScopeKind::Global, "Global"},
        {ScopeKind::In, "In"},
        {ScopeKind::NotIn, "NotIn"},
        {ScopeKind::Before, "Before"},
        {ScopeKind::After, "After"},
        {ScopeKind::OnlyIn, "OnlyIn"},
        {ScopeKind::OnlyBefore, "OnlyBefore"},
        {ScopeKind::OnlyAfter, "OnlyAfter"}};
    const std::vector<std::pair<Timing, std::string>> all = timings();
    for (const auto& [scope, scope_name] : scopes) {
        for (const auto& [stronger, stronger_name] : all) {
            for (const auto& [weaker, weaker_name] : all) {
                std::cout << scope_name << '\t' << stronger_name << '\t'
                          << weaker_name << '\t'
                          << (timing_implies(stronger, weaker, scope) ? 1 : 0)
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
        std::cerr << "timing_order_table: " << exc.what() << "\n";
        return 1;
    }
    return 0;
}
