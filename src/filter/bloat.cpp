#include "filter/bloat.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "requirement.hpp"
#include "tlsf/specification.hpp"

namespace {

// Removed requirements and deleted conjuncts are excluded: the cap is on the
// size of the specification, and a removed formula is not in it. Counting one
// would keep charging a candidate for a formula it has already dropped.
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

std::size_t max_formula_size(const tlsf::Specification& spec) {
    std::size_t max = 0;
    for (const tlsf::Section* section : tlsf::sections_of(spec)) {
        for (const tlsf::SectionEntry& entry : *section) {
            if (entry.m_removed) {
                continue;
            }
            max = std::max(max, entry.m_formula.n_subformulae());
        }
    }
    return max;
}

}  // namespace

template <typename Spec>
FilterFunctionT<Spec> make_bloat_cap_filter(const Spec& original,
                                            double max_ratio) {
    const std::size_t original_max = max_formula_size(original);
    return {"bloat-cap",
            [original_max, max_ratio](std::vector<Spec> pop) {
                if (original_max == 0) {
                    return pop;
                }
                const auto cap = static_cast<std::size_t>(
                    max_ratio * static_cast<double>(original_max));
                std::vector<Spec> survivors;
                survivors.reserve(pop.size());
                for (Spec& spec : pop) {
                    // Some formula exceeds the cap exactly when the largest
                    // one does.
                    if (max_formula_size(spec) <= cap) {
                        survivors.push_back(std::move(spec));
                    }
                }
                return survivors;
            },
            FilterKind::Preference};
}

template FilterFunctionT<Specification> make_bloat_cap_filter(
    const Specification&, double);
template FilterFunctionT<tlsf::Specification> make_bloat_cap_filter(
    const tlsf::Specification&, double);
