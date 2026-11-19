#include "genetic/diversity.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "requirement.hpp"

namespace {

// Slots of one side in which @p lhs and @p rhs differ, pairing by index.
std::size_t differing_slots(const std::vector<Requirement>& lhs,
                            const std::vector<Requirement>& rhs) {
    const std::size_t longest = std::max(lhs.size(), rhs.size());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < longest; ++i) {
        if (i >= lhs.size() || i >= rhs.size() || !(lhs[i] == rhs[i])) {
            ++differing;
        }
    }
    return differing;
}

}  // namespace

double specification_distance(const Specification& lhs,
                              const Specification& rhs) {
    const std::size_t slots =
        std::max(lhs.m_assumptions.size(), rhs.m_assumptions.size()) +
        std::max(lhs.m_guarantees.size(), rhs.m_guarantees.size());
    if (slots == 0) {
        return 0.0;
    }
    const std::size_t differing =
        differing_slots(lhs.m_assumptions, rhs.m_assumptions) +
        differing_slots(lhs.m_guarantees, rhs.m_guarantees);
    return static_cast<double>(differing) / static_cast<double>(slots);
}
