#include "filter/implication_check.hpp"

#include <algorithm>
#include <atomic>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "filter/implication.hpp"
#include "requirement.hpp"

namespace {

std::unordered_set<std::string> live_lowerings(
    const std::vector<Requirement>& reqs) {
    std::unordered_set<std::string> lowerings;
    for (const Requirement& req : reqs) {
        if (!req.m_removed) {
            lowerings.insert(req.m_ltl);
        }
    }
    return lowerings;
}

}  // namespace

std::optional<Specification> residual_destination(const Specification& from,
                                                  const Specification& dest) {
    const std::unordered_set<std::string> dest_assumptions =
        live_lowerings(dest.m_assumptions);
    const bool assumptions_carried = std::all_of(
        from.m_assumptions.begin(), from.m_assumptions.end(),
        [&dest_assumptions](const Requirement& req) {
            return req.m_removed || dest_assumptions.count(req.m_ltl) != 0;
        });
    if (!assumptions_carried) {
        return std::nullopt;
    }
    const std::unordered_set<std::string> from_guarantees =
        live_lowerings(from.m_guarantees);
    Specification residual = dest;
    bool dropped = false;
    for (Requirement& req : residual.m_guarantees) {
        if (!req.m_removed && from_guarantees.count(req.m_ltl) != 0) {
            req.m_removed = true;
            dropped = true;
        }
    }
    if (!dropped) {
        return std::nullopt;
    }
    return residual;
}

std::optional<bool> spec_implies(const Specification& from,
                                 const Specification& dest,
                                 SatisfiabilityChecker& checker) {
    if (from == dest) {
        return true;
    }
    const std::optional<Specification> residual =
        residual_destination(from, dest);
    if (residual.has_value() && count_live(residual->m_guarantees) == 0) {
        ImplicationFilterStats::n_residual_settled.fetch_add(
            1, std::memory_order_relaxed);
        return true;
    }
    if (residual.has_value()) {
        ImplicationFilterStats::n_residual_queries.fetch_add(
            1, std::memory_order_relaxed);
    }
    const Specification& target = residual.has_value() ? *residual : dest;
    const std::optional<bool> sat = checker.check_satisfiability(
        "(" + from.to_ltl() + ") & !(" + target.to_ltl() + ")",
        QueryPolarity::ExpectUnsat);
    if (!sat.has_value()) {
        ImplicationFilterStats::n_timeouts.fetch_add(1,
                                                     std::memory_order_relaxed);
        return std::nullopt;
    }
    return !sat.value();
}
