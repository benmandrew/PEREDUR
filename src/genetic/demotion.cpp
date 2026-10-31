#include "genetic/demotion.hpp"

#include <cassert>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "filter/implication_check.hpp"
#include "fitness/status.hpp"
#include "hash_combine.hpp"
#include "profile.hpp"
#include "requirement.hpp"

namespace {

// Views into the population's own lowerings, which outlive the memo.
using LoweringPair = std::pair<std::string_view, std::string_view>;

struct LoweringPairHash {
    std::size_t operator()(const LoweringPair& pair) const noexcept {
        const std::hash<std::string_view> hash;
        return hash_combine(hash(pair.first), hash(pair.second));
    }
};

// The realizable members of one population, with the lowering pools
// spec_implies_by_rule reads, and a memo of every requirement pair it proved.
class RuleOrder {
   public:
    RuleOrder(const std::vector<Scored<Specification>>& population,
              std::size_t status_index)
        : m_population(population),
          m_implies(
              [this](const Requirement& stronger, const Requirement& weaker) {
                  return memoised_implies(stronger, weaker);
              }) {
        for (std::size_t i = 0; i < population.size(); ++i) {
            assert(status_index < population[i].objectives.size());
            if (population[i].objectives[status_index] == k_status_realizable) {
                const Specification& spec = population[i].specification;
                m_members.push_back(i);
                m_guarantees.push_back(live_lowerings(spec.m_guarantees));
                m_assumptions.push_back(live_lowerings(spec.m_assumptions));
            }
        }
    }

    [[nodiscard]] std::size_t size() const { return m_members.size(); }

    // The population index of realizable member @p member.
    [[nodiscard]] std::size_t index(std::size_t member) const {
        return m_members[member];
    }

    // Whether member @p from is proved to imply member @p dest.
    bool implies(std::size_t from, std::size_t dest) {
        return spec_implies_by_rule(m_population[m_members[from]].specification,
                                    m_population[m_members[dest]].specification,
                                    m_guarantees[from], m_assumptions[dest],
                                    m_implies);
    }

    // Whether member @p stronger is proved strictly stronger than member
    // @p weaker.
    bool strictly_implies(std::size_t stronger, std::size_t weaker) {
        return implies(stronger, weaker) && !implies(weaker, stronger);
    }

   private:
    bool memoised_implies(const Requirement& stronger,
                          const Requirement& weaker) {
        const LoweringPair key{stronger.m_ltl, weaker.m_ltl};
        const auto found = m_memo.find(key);
        if (found != m_memo.end()) {
            return found->second;
        }
        const bool proved = requirement_implies(stronger, weaker);
        m_memo.emplace(key, proved);
        return proved;
    }

    const std::vector<Scored<Specification>>& m_population;
    std::vector<std::size_t> m_members;
    std::vector<std::unordered_set<std::string>> m_guarantees;
    std::vector<std::unordered_set<std::string>> m_assumptions;
    std::unordered_map<LoweringPair, bool, LoweringPairHash> m_memo;
    RequirementImplication m_implies;
};

// Settles one unordered pair of members, demoting whichever is strictly
// weaker. A set flag stays set, so a pair with one side already demoted needs
// only the direction that could demote the other.
void settle_pair(RuleOrder& order, std::size_t first, std::size_t second,
                 std::vector<bool>& demoted) {
    const bool first_done = demoted[order.index(first)];
    const bool second_done = demoted[order.index(second)];
    if (first_done && second_done) {
        return;
    }
    if (first_done) {
        demoted[order.index(second)] = order.strictly_implies(first, second);
        return;
    }
    if (second_done) {
        demoted[order.index(first)] = order.strictly_implies(second, first);
        return;
    }
    const bool forward = order.implies(first, second);
    const bool backward = order.implies(second, first);
    if (forward != backward) {
        demoted[order.index(forward ? second : first)] = true;
    }
}

}  // namespace

std::vector<bool> proved_weaker_realizable(
    const std::vector<Scored<Specification>>& population,
    std::size_t status_index) {
    PEREDUR_PROFILE_SCOPE("select/demotion");
    std::vector<bool> demoted(population.size(), false);
    RuleOrder order(population, status_index);
    for (std::size_t first = 0; first < order.size(); ++first) {
        for (std::size_t second = first + 1; second < order.size(); ++second) {
            settle_pair(order, first, second, demoted);
        }
    }
    return demoted;
}
