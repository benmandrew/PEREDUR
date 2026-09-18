#include "filter/bloat.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace {

// Removed requirements are excluded throughout: the cap is on the size of the
// specification, and a removed requirement is not in it. Counting one would
// keep charging a candidate for a formula it has already deleted.
std::size_t max_formula_size(const Specification& spec) {
    std::size_t max = 0;
    const auto scan = [&max](const std::vector<Requirement>& reqs) {
        for (const Requirement& req : reqs) {
            if (req.m_removed) {
                continue;
            }
            max = std::max(max, req.m_condition.n_subformulae());
            max = std::max(max, req.m_response.n_subformulae());
            if (const Formula* stop = timing_stop(req.m_timing)) {
                max = std::max(max, stop->n_subformulae());
            }
        }
    };
    scan(spec.m_assumptions);
    scan(spec.m_guarantees);
    return max;
}

bool any_formula_exceeds(const Specification& spec, std::size_t cap) {
    auto req_exceeds = [cap](const Requirement& req) {
        const Formula* stop = timing_stop(req.m_timing);
        return !req.m_removed &&
               (req.m_condition.n_subformulae() > cap ||
                req.m_response.n_subformulae() > cap ||
                (stop != nullptr && stop->n_subformulae() > cap));
    };
    return std::any_of(spec.m_assumptions.begin(), spec.m_assumptions.end(),
                       req_exceeds) ||
           std::any_of(spec.m_guarantees.begin(), spec.m_guarantees.end(),
                       req_exceeds);
}

}  // namespace

FilterFunction make_bloat_cap_filter(const Specification& original,
                                     double max_ratio) {
    const std::size_t original_max = max_formula_size(original);
    return {"bloat-cap",
            [original_max, max_ratio](std::vector<Specification> pop) {
                if (original_max == 0) {
                    return pop;
                }
                const auto cap = static_cast<std::size_t>(
                    max_ratio * static_cast<double>(original_max));
                std::vector<Specification> survivors;
                survivors.reserve(pop.size());
                for (Specification& spec : pop) {
                    if (!any_formula_exceeds(spec, cap)) {
                        survivors.push_back(std::move(spec));
                    }
                }
                return survivors;
            },
            FilterKind::Preference};
}
