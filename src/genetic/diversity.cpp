#include "genetic/diversity.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "filter/implication_check.hpp"
#include "fitness/status.hpp"
#include "profile.hpp"
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

DiversityState::DiversityState(const std::vector<Specification>* accumulated)
    : m_accumulated(accumulated) {}

bool DiversityState::memoised_implies(const Requirement& stronger,
                                      const Requirement& weaker) {
    std::pair<std::string, std::string> key{stronger.m_ltl, weaker.m_ltl};
    const auto found = m_pair_memo.find(key);
    if (found != m_pair_memo.end()) {
        return found->second;
    }
    const bool proved = requirement_implies(stronger, weaker);
    m_pair_memo.emplace(std::move(key), proved);
    return proved;
}

std::size_t DiversityState::implied_by(const Specification& candidate) {
    if (m_accumulated == nullptr) {
        return 0;
    }
    const std::vector<Specification>& accumulated = *m_accumulated;
    while (m_guarantee_lowerings.size() < accumulated.size()) {
        m_guarantee_lowerings.push_back(live_lowerings(
            accumulated[m_guarantee_lowerings.size()].m_guarantees));
    }
    Novelty& novelty = m_novelty[candidate];
    if (novelty.checked == accumulated.size()) {
        return novelty.count;
    }
    const std::unordered_set<std::string> assumptions =
        live_lowerings(candidate.m_assumptions);
    const RequirementImplication implies = [this](const Requirement& stronger,
                                                  const Requirement& weaker) {
        return memoised_implies(stronger, weaker);
    };
    for (; novelty.checked < accumulated.size(); ++novelty.checked) {
        NoveltyStats::n_checks++;
        if (spec_implies_by_rule(accumulated[novelty.checked], candidate,
                                 m_guarantee_lowerings[novelty.checked],
                                 assumptions, implies)) {
            NoveltyStats::n_implied++;
            ++novelty.count;
        }
    }
    return novelty.count;
}

void DiversityState::retain(const std::vector<Scored<Specification>>& keep) {
    std::unordered_set<Specification> live;
    for (const Scored<Specification>& scored : keep) {
        live.insert(scored.specification);
    }
    for (auto it = m_novelty.begin(); it != m_novelty.end();) {
        it = live.count(it->first) != 0 ? std::next(it) : m_novelty.erase(it);
    }
}

std::vector<double> novelty_objective(
    DiversityState& state, const std::vector<Scored<Specification>>& population,
    std::optional<std::size_t> status_index) {
    PEREDUR_PROFILE_SCOPE("select/novelty");
    std::vector<double> objective(population.size(), 0.0);
    if (!status_index.has_value()) {
        return objective;
    }
    for (std::size_t i = 0; i < population.size(); ++i) {
        const Scored<Specification>& scored = population[i];
        NoveltyStats::n_evaluations++;
        if (*status_index >= scored.objectives.size() ||
            scored.objectives[*status_index] < k_status_realizable) {
            continue;
        }
        const std::size_t count = state.implied_by(scored.specification);
        if (count > 0) {
            NoveltyStats::n_covered++;
        }
        objective[i] = -static_cast<double>(count);
    }
    return objective;
}

namespace {

// Adds one to each slot of @p counts in which @p member differs from
// @p original, growing @p counts to cover every slot @p member has.
void count_edits(const std::vector<Requirement>& member,
                 const std::vector<Requirement>& original,
                 std::vector<std::size_t>& counts) {
    if (counts.size() < member.size()) {
        counts.resize(member.size(), 0);
    }
    for (std::size_t i = 0; i < member.size(); ++i) {
        if (i >= original.size() || !(member[i] == original[i])) {
            ++counts[i];
        }
    }
}

}  // namespace

std::size_t coverage_of(const SlotCoverage& coverage, std::size_t idx,
                        std::size_t n_assumptions) {
    const bool is_assumption = idx < n_assumptions;
    const std::vector<std::size_t>& counts =
        is_assumption ? coverage.assumptions : coverage.guarantees;
    const std::size_t local = is_assumption ? idx : idx - n_assumptions;
    return local < counts.size() ? counts[local] : 0;
}

const SlotCoverage& DiversityState::slot_coverage(
    const Specification& original) {
    if (m_accumulated == nullptr) {
        return m_coverage;
    }
    for (; m_covered < m_accumulated->size(); ++m_covered) {
        const Specification& member = (*m_accumulated)[m_covered];
        count_edits(member.m_assumptions, original.m_assumptions,
                    m_coverage.assumptions);
        count_edits(member.m_guarantees, original.m_guarantees,
                    m_coverage.guarantees);
    }
    return m_coverage;
}
