// Prints scope_implies for every condition type, timing and ordered pair of
// scopes, the weaker at the stronger's mode or another one, for
// scripts/check_lean_scope_order.py to compare with the Lean transcription in
// proofs/Fretish/ScopeOrder.lean. A development tool, not a driver: it is
// built with the tests and installs nowhere.

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
        // The loader reads `within 0` and `for 0` as `immediately`.
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

// A global scope carries the empty mode; every other one names a mode.
Scope scope(ScopeKind kind, const std::string& mode) {
    return Scope{kind, kind == ScopeKind::Global ? std::string() : mode};
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
    const std::vector<std::pair<ConditionType, const char*>> types = {
        {ConditionType::Continual, "continual"},
        {ConditionType::Trigger, "trigger"}};
    const std::vector<std::pair<Timing, std::string>> all = timings();
    for (const auto& [type, type_name] : types) {
        for (const auto& [tim, tim_name] : all) {
            for (const auto& [stronger, stronger_name] : scopes) {
                for (const auto& [weaker, weaker_name] : scopes) {
                    for (const char* mode : {"a", "b"}) {
                        const bool holds =
                            scope_implies(scope(stronger, "a"),
                                          scope(weaker, mode), tim, type);
                        std::cout << type_name << '\t' << tim_name << '\t'
                                  << stronger_name << '\t' << weaker_name
                                  << '\t' << mode << '\t' << (holds ? 1 : 0)
                                  << '\n';
                    }
                }
            }
        }
    }
}

}  // namespace

int main() {
    try {
        print_table();
    } catch (const std::exception& exc) {
        std::cerr << "scope_order_table: " << exc.what() << "\n";
        return 1;
    }
    return 0;
}
