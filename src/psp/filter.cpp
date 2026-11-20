#include "psp/filter.hpp"

#include <atomic>
#include <optional>
#include <string>

#include "filter/implication.hpp"
#include "filter/implication_check.hpp"

namespace psp {

std::optional<bool> spec_implies(const Specification& from,
                                 const Specification& dest,
                                 SatisfiabilityChecker& checker) {
    if (from == dest) {
        return true;
    }
    const std::optional<bool> sat = implication_query(
        checker, "(" + from.to_ltl() + ") & !(" + dest.to_ltl() + ")");
    if (!sat.has_value()) {
        ImplicationFilterStats::n_timeouts.fetch_add(1,
                                                     std::memory_order_relaxed);
        return std::nullopt;
    }
    return !*sat;
}

bool has_valid_guarantee(const Specification& spec,
                         SatisfiabilityChecker& checker) {
    for (const Requirement& req : spec.m_guarantees) {
        if (req.m_removed) {
            continue;
        }
        const std::optional<bool> falsifiable =
            checker.check_satisfiability("!(" + req.m_ltl + ")");
        if (!falsifiable.value_or(true)) {
            return true;
        }
    }
    return false;
}

}  // namespace psp
