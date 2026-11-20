#include "genetic/diversity.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "filter/implication_check.hpp"
#include "fitness/status.hpp"
#include "genetic/mutation.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/ordered_language.hpp"
#include "profile.hpp"
#include "psp/ordered_language.hpp"
#include "psp/specification.hpp"
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

template <typename Spec>
BasicDiversityState<Spec>::BasicDiversityState(
    const std::vector<Spec>* accumulated)
    : m_accumulated(accumulated) {}

template <typename Spec>
bool BasicDiversityState<Spec>::memoised_implies(const Req& stronger,
                                                 const Req& weaker) {
    std::pair<std::string, std::string> key{Language::lowering(stronger),
                                            Language::lowering(weaker)};
    const auto found = m_pair_memo.find(key);
    if (found != m_pair_memo.end()) {
        return found->second;
    }
    const bool proved = Language::requirement_implies(stronger, weaker);
    m_pair_memo.emplace(std::move(key), proved);
    return proved;
}

template <typename Spec>
std::size_t BasicDiversityState<Spec>::implied_by(const Spec& candidate) {
    if (m_accumulated == nullptr) {
        return 0;
    }
    const std::vector<Spec>& accumulated = *m_accumulated;
    while (m_guarantee_lowerings.size() < accumulated.size()) {
        m_guarantee_lowerings.push_back(Language::live_lowerings(
            Language::guarantees(accumulated[m_guarantee_lowerings.size()])));
    }
    Novelty& novelty = m_novelty[candidate];
    if (novelty.checked == accumulated.size()) {
        return novelty.count;
    }
    const std::unordered_set<std::string> assumptions =
        Language::live_lowerings(Language::assumptions(candidate));
    const typename Language::Implication implies = [this](const Req& stronger,
                                                          const Req& weaker) {
        return memoised_implies(stronger, weaker);
    };
    for (; novelty.checked < accumulated.size(); ++novelty.checked) {
        NoveltyStats::n_checks++;
        if (Language::spec_implies_by_rule(
                accumulated[novelty.checked], candidate,
                m_guarantee_lowerings[novelty.checked], assumptions, implies)) {
            NoveltyStats::n_implied++;
            ++novelty.count;
        }
    }
    return novelty.count;
}

template <typename Spec>
void BasicDiversityState<Spec>::retain(const std::vector<Scored<Spec>>& keep) {
    std::unordered_set<Spec> live;
    for (const Scored<Spec>& scored : keep) {
        live.insert(scored.specification);
    }
    for (auto it = m_novelty.begin(); it != m_novelty.end();) {
        it = live.count(it->first) != 0 ? std::next(it) : m_novelty.erase(it);
    }
    for (auto it = m_lineage.begin(); it != m_lineage.end();) {
        it = live.count(it->first) != 0 ? std::next(it) : m_lineage.erase(it);
    }
}

template <typename Spec>
std::vector<double> novelty_objective(
    BasicDiversityState<Spec>& state,
    const std::vector<Scored<Spec>>& population,
    std::optional<std::size_t> status_index) {
    PEREDUR_PROFILE_SCOPE("select/novelty");
    std::vector<double> objective(population.size(), 0.0);
    if (!status_index.has_value()) {
        return objective;
    }
    for (std::size_t i = 0; i < population.size(); ++i) {
        const Scored<Spec>& scored = population[i];
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
template <typename Req>
void count_edits(const std::vector<Req>& member,
                 const std::vector<Req>& original,
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

template <typename Spec>
const SlotCoverage& BasicDiversityState<Spec>::slot_coverage(
    const Spec& original) {
    if (m_accumulated == nullptr) {
        return m_coverage;
    }
    for (; m_covered < m_accumulated->size(); ++m_covered) {
        const Spec& member = (*m_accumulated)[m_covered];
        count_edits(Language::assumptions(member),
                    Language::assumptions(original), m_coverage.assumptions);
        count_edits(Language::guarantees(member),
                    Language::guarantees(original), m_coverage.guarantees);
    }
    return m_coverage;
}

namespace {

// How many draws a slot gets before its individual is left as the original.
constexpr std::size_t k_spread_attempts = 8;

// The global slots (assumptions first, then guarantees) spread_init weakens.
std::vector<std::size_t> spread_targets(
    const Specification& original, const std::vector<std::size_t>& refused) {
    const std::size_t n_assumptions = original.m_assumptions.size();
    const auto usable = [](const Requirement& req) {
        return req.m_weakenable && !req.m_removed;
    };
    std::vector<std::size_t> targets;
    for (const std::size_t local : refused) {
        if (local < original.m_guarantees.size() &&
            usable(original.m_guarantees[local])) {
            targets.push_back(n_assumptions + local);
        }
    }
    if (targets.empty()) {
        for (std::size_t local = 0; local < original.m_guarantees.size();
             ++local) {
            if (usable(original.m_guarantees[local])) {
                targets.push_back(n_assumptions + local);
            }
        }
    }
    for (std::size_t local = 0; local < n_assumptions; ++local) {
        if (usable(original.m_assumptions[local])) {
            targets.push_back(local);
        }
    }
    return targets;
}

// One proved weakening of @p original in global slot @p idx, or nothing.
std::optional<Specification> weaken_slot(const Specification& original,
                                         std::size_t idx, const Config& cfg,
                                         const RandomSource& random_source) {
    const std::size_t n_assumptions = original.m_assumptions.size();
    const bool is_assumption = idx < n_assumptions;
    const std::size_t local = is_assumption ? idx : idx - n_assumptions;
    std::vector<std::string> atoms = original.m_in_atoms;
    atoms.insert(atoms.end(), original.m_out_atoms.begin(),
                 original.m_out_atoms.end());
    if (atoms.empty()) {
        return std::nullopt;
    }
    const std::vector<std::string>& condition_atoms =
        original.m_in_atoms.empty() ? atoms : original.m_in_atoms;
    const std::vector<Timing> timing_pool = collect_timing_pool(original);
    Config directed = cfg;
    directed.p_monotone = 1.0;
    // Weakening the specification strengthens an assumption.
    const Direction direction =
        is_assumption ? Direction::Strengthen : Direction::Weaken;
    for (std::size_t attempt = 0; attempt < k_spread_attempts; ++attempt) {
        std::vector<Requirement> assumptions = original.m_assumptions;
        std::vector<Requirement> guarantees = original.m_guarantees;
        std::vector<Requirement>& side =
            is_assumption ? assumptions : guarantees;
        side[local] = mutate_requirement(
            side[local], atoms, condition_atoms, direction, timing_pool,
            original.m_modes, random_source, directed);
        Specification candidate(std::move(assumptions), std::move(guarantees),
                                original.m_in_atoms, original.m_out_atoms,
                                original.m_modes);
        // The constructor merges equal entries, which would shift the slots.
        if (candidate.m_assumptions.size() != n_assumptions ||
            candidate.m_guarantees.size() != original.m_guarantees.size()) {
            continue;
        }
        if (classify_move(original, candidate) == MoveOrder::Weaker) {
            return candidate;
        }
    }
    return std::nullopt;
}

}  // namespace

std::vector<Specification> spread_initial_population(
    const Specification& original, const std::vector<std::size_t>& refused,
    std::size_t population_size, const Config& cfg,
    const RandomSource& random_source) {
    std::vector<Specification> population;
    population.reserve(population_size);
    if (population_size == 0) {
        return population;
    }
    population.push_back(original);
    const std::vector<std::size_t> targets = spread_targets(original, refused);
    SpreadInitStats::n_targets = targets.size();
    for (std::size_t k = 1; k < population_size; ++k) {
        std::optional<Specification> weakened =
            targets.empty()
                ? std::nullopt
                : weaken_slot(original, targets[(k - 1) % targets.size()], cfg,
                              random_source);
        if (weakened.has_value()) {
            SpreadInitStats::n_seeded++;
            population.push_back(std::move(*weakened));
        } else {
            SpreadInitStats::n_failed++;
            population.push_back(original);
        }
    }
    return population;
}

namespace {

// The slots of one side in which @p edited differs from @p parent.
template <typename Req>
void edited_slots(const std::vector<Req>& parent,
                  const std::vector<Req>& edited, bool guarantee,
                  std::vector<SlotKey>& slots) {
    const std::size_t longest = std::max(parent.size(), edited.size());
    for (std::size_t i = 0; i < longest; ++i) {
        if (i >= parent.size() || i >= edited.size() ||
            !(parent[i] == edited[i])) {
            slots.push_back({guarantee, i});
        }
    }
}

}  // namespace

template <typename Spec>
std::vector<std::size_t> BasicDiversityState<Spec>::flipped_slots(
    const Spec& parent) const {
    std::vector<std::size_t> slots;
    const auto found = m_lineage.find(parent);
    if (found == m_lineage.end()) {
        return slots;
    }
    const std::size_t n_assumptions = Language::assumptions(parent).size();
    for (const auto& [slot, flips] : found->second) {
        if (flips.tabu_until > m_generation) {
            slots.push_back(slot.guarantee ? n_assumptions + slot.local
                                           : slot.local);
        }
    }
    std::sort(slots.begin(), slots.end());
    return slots;
}

template <typename Spec>
void BasicDiversityState<Spec>::note_offspring(const Spec& parent,
                                               bool parent_realizable,
                                               const Spec& edited,
                                               const Spec& bred) {
    HeldOffspring held;
    edited_slots(Language::assumptions(parent), Language::assumptions(edited),
                 false, held.edited);
    edited_slots(Language::guarantees(parent), Language::guarantees(edited),
                 true, held.edited);
    if (held.edited.empty()) {
        return;
    }
    const auto found = m_lineage.find(parent);
    if (found != m_lineage.end()) {
        held.lineage = found->second;
    }
    held.bred = bred;
    held.parent_realizable = parent_realizable;
    m_held.push_back(std::move(held));
}

template <typename Spec>
void BasicDiversityState<Spec>::settle_offspring(
    const std::vector<Scored<Spec>>& scored, std::size_t status_index,
    std::size_t span) {
    std::unordered_map<Spec, bool> realizable;
    for (const Scored<Spec>& entry : scored) {
        if (status_index < entry.objectives.size()) {
            realizable.emplace(
                entry.specification,
                entry.objectives[status_index] >= k_status_realizable);
        }
    }
    for (HeldOffspring& held : m_held) {
        const auto found = realizable.find(held.bred);
        if (found == realizable.end() || m_lineage.count(held.bred) != 0) {
            continue;
        }
        if (found->second != held.parent_realizable) {
            for (const SlotKey& slot : held.edited) {
                FlipLineageStats::n_flips++;
                SlotFlips& flips = held.lineage[slot];
                (found->second ? flips.weakened : flips.strengthened) = true;
                if (flips.weakened && flips.strengthened) {
                    FlipLineageStats::n_tabu++;
                    flips.weakened = false;
                    flips.strengthened = false;
                    // Tabu from the next generation's breeding on, for span
                    // generations.
                    flips.tabu_until = m_generation + span + 1;
                }
            }
        }
        m_lineage.emplace(held.bred, std::move(held.lineage));
    }
    m_held.clear();
}

template class BasicDiversityState<Specification>;
template std::vector<double> novelty_objective(
    DiversityState&, const std::vector<Scored<Specification>>&,
    std::optional<std::size_t>);
template class BasicDiversityState<psp::Specification>;
template std::vector<double> novelty_objective(
    BasicDiversityState<psp::Specification>&,
    const std::vector<Scored<psp::Specification>>&, std::optional<std::size_t>);
