#include "psp/filter.hpp"

#include <algorithm>
#include <atomic>
#include <optional>
#include <string>

#include "filter/implication.hpp"
#include "filter/implication_check.hpp"
#include "psp/implication.hpp"

namespace psp {

std::optional<bool> spec_implies(const Specification& from,
                                 const Specification& dest,
                                 SatisfiabilityChecker& checker) {
    if (from == dest) {
        return true;
    }
    const std::optional<Specification> residual =
        residual_destination(from, dest);
    if (residual.has_value() &&
        std::none_of(residual->m_guarantees.begin(),
                     residual->m_guarantees.end(),
                     [](const Requirement& req) { return !req.m_removed; })) {
        ImplicationFilterStats::n_residual_settled.fetch_add(
            1, std::memory_order_relaxed);
        return true;
    }
    if (residual.has_value()) {
        ImplicationFilterStats::n_residual_queries.fetch_add(
            1, std::memory_order_relaxed);
    }
    const Specification& target = residual.has_value() ? *residual : dest;
    const std::optional<bool> sat = implication_query(
        checker, "(" + from.to_ltl() + ") & !(" + target.to_ltl() + ")");
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
