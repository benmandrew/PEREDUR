#include "genetic/simplify.hpp"

#include <cassert>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "prop_formula.hpp"
#include "requirement.hpp"

namespace {

// A stop timing with its stop simplified. `until false` is Always spelt another
// way, so it folds into Always rather than splitting the cache keys of one
// obligation. `until true` and `before true` gut or contradict their
// requirement and are left to the filters, which see the lowered formula.
Timing simplify_stop_timing(const Timing& timing) {
    const Formula* stop = timing_stop(timing);
    assert(stop != nullptr);
    Formula simplified = *stop;
    simplified.simplify();
    if (std::holds_alternative<timing::Before>(timing)) {
        return timing::before(std::move(simplified));
    }
    if (simplified.atom_name() == std::optional<std::string>("false")) {
        return timing::always();
    }
    return timing::until(std::move(simplified));
}

}  // namespace

Specification simplify_offspring(Specification offspring) {
    Specification pre_simplify = offspring;
    // Removed requirements are left alone alongside locked ones. Their content
    // is never read again, so simplifying it buys nothing, and rewriting it
    // could collapse two tombstones onto the same shape and hand the dedup
    // below a size change that discards the whole offspring.
    const auto simplify_all = [](std::vector<Requirement>& reqs) {
        for (auto& req : reqs) {
            if (!req.m_weakenable || req.m_removed) {
                continue;
            }
            req.m_condition.simplify();
            req.m_response.simplify();
            if (timing_stop(req.m_timing) != nullptr) {
                req.m_timing = simplify_stop_timing(req.m_timing);
            }
            req.m_ltl = requirement_to_ltl(req);
        }
    };
    simplify_all(offspring.m_assumptions);
    simplify_all(offspring.m_guarantees);
    Specification rededuped(offspring.m_assumptions, offspring.m_guarantees,
                            offspring.m_in_atoms, offspring.m_out_atoms,
                            offspring.m_modes);
    if (rededuped.m_assumptions.size() != pre_simplify.m_assumptions.size() ||
        rededuped.m_guarantees.size() != pre_simplify.m_guarantees.size()) {
        return pre_simplify;
    }
    return rededuped;
}
