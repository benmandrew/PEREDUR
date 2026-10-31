#include "genetic/order_crossover.hpp"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "filter/implication_check.hpp"
#include "fitness/status.hpp"
#include "genetic/order_moves.hpp"

namespace {

std::size_t index_of(OrderCrossoverKind kind) {
    return static_cast<std::size_t>(kind);
}

// Fills each slot the rules left unbounded with one parent's value, one coin
// per slot in slot order, and reports whether any slot needed it.
bool fill_unproved(std::vector<std::optional<Requirement>>& slots,
                   const std::vector<Requirement>& lhs,
                   const std::vector<Requirement>& rhs,
                   const RandomSource& random_source) {
    bool partial = false;
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (slots[slot].has_value()) {
            continue;
        }
        partial = true;
        const bool take_rhs = random_source.next_bool();
        const bool rhs_only = slot >= lhs.size();
        const std::vector<Requirement>& donor =
            ((take_rhs && slot < rhs.size()) || rhs_only) ? rhs : lhs;
        slots[slot] = donor[slot];
    }
    return partial;
}

OrderCrossoverOutcome bound_crossover(SlotwiseBound bound,
                                      const Specification& lhs,
                                      const Specification& rhs,
                                      const RandomSource& random_source) {
    const bool partial_assumptions =
        fill_unproved(bound.m_assumptions, lhs.m_assumptions, rhs.m_assumptions,
                      random_source);
    const bool partial_guarantees = fill_unproved(
        bound.m_guarantees, lhs.m_guarantees, rhs.m_guarantees, random_source);
    return {assemble(bound, lhs), !partial_assumptions && !partial_guarantees};
}

bool strictly_implies(const Requirement& lhs, const Requirement& rhs) {
    const bool forward = requirement_implies(lhs, rhs);
    return forward && !requirement_implies(rhs, lhs);
}

// A tombstone reads as `true`, so a live guarantee is stronger than one.
bool stronger_guarantee(const Requirement& base, const Requirement& donor) {
    if (base == donor || donor.m_removed) {
        return false;
    }
    return base.m_removed || strictly_implies(donor, base);
}

// `A -> G` is antitone in A, so a weaker assumption strengthens the whole.
bool stronger_assumption(const Requirement& base, const Requirement& donor) {
    if (base == donor || base.m_removed) {
        return false;
    }
    return donor.m_removed || strictly_implies(base, donor);
}

struct Site {
    bool m_assumption = false;
    std::size_t m_slot = 0;
};

std::mutex g_pending_mutex;
std::vector<std::pair<Specification, OrderCrossoverKind>> g_pending;

}  // namespace

std::optional<OrderCrossoverKind> order_crossover_kind(ParentVerdict first,
                                                       ParentVerdict second) {
    if (first == ParentVerdict::Unknown || second == ParentVerdict::Unknown) {
        return std::nullopt;
    }
    if (first != second) {
        return OrderCrossoverKind::Relink;
    }
    return first == ParentVerdict::Realizable ? OrderCrossoverKind::Join
                                              : OrderCrossoverKind::Meet;
}

std::optional<std::size_t> status_objective_index(const Config& cfg) {
    if (cfg.fitness_weight_status <= 0.0) {
        return std::nullopt;
    }
    std::size_t index = 0;
    if (cfg.fitness_weight_syntactic > 0.0) {
        ++index;
    }
    if (cfg.fitness_weight_semantic > 0.0) {
        ++index;
    }
    return index;
}

ParentVerdict parent_verdict(const Scored<Specification>& parent,
                             std::optional<std::size_t> status_index,
                             const RealizabilityChecker& checker) {
    if (status_index.has_value() && *status_index < parent.objectives.size() &&
        parent.objectives[*status_index] >= k_status_realizable) {
        return ParentVerdict::Realizable;
    }
    if (checker.proved_unrealizable(parent.specification)) {
        return ParentVerdict::Unrealizable;
    }
    return ParentVerdict::Unknown;
}

OrderCrossoverOutcome join_crossover(const Specification& lhs,
                                     const Specification& rhs,
                                     std::size_t n_original_assumptions,
                                     const RandomSource& random_source) {
    return bound_crossover(specification_join(lhs, rhs, n_original_assumptions),
                           lhs, rhs, random_source);
}

OrderCrossoverOutcome meet_crossover(const Specification& lhs,
                                     const Specification& rhs,
                                     std::size_t n_original_assumptions,
                                     const RandomSource& random_source) {
    return bound_crossover(specification_meet(lhs, rhs, n_original_assumptions),
                           lhs, rhs, random_source);
}

OrderCrossoverOutcome relink_crossover(const Specification& realizable,
                                       const Specification& unrealizable,
                                       std::size_t n_original_assumptions,
                                       const RandomSource& random_source) {
    std::vector<Site> sites;
    const std::size_t n_assumptions =
        std::min({n_original_assumptions, realizable.m_assumptions.size(),
                  unrealizable.m_assumptions.size()});
    for (std::size_t slot = 0; slot < n_assumptions; ++slot) {
        if (stronger_assumption(realizable.m_assumptions[slot],
                                unrealizable.m_assumptions[slot])) {
            sites.push_back({true, slot});
        }
    }
    const std::size_t n_guarantees = std::min(realizable.m_guarantees.size(),
                                              unrealizable.m_guarantees.size());
    for (std::size_t slot = 0; slot < n_guarantees; ++slot) {
        if (stronger_guarantee(realizable.m_guarantees[slot],
                               unrealizable.m_guarantees[slot])) {
            sites.push_back({false, slot});
        }
    }
    if (sites.empty()) {
        return {};
    }
    std::vector<bool> adopt(sites.size(), false);
    bool any = false;
    for (std::size_t site = 0; site < sites.size(); ++site) {
        const bool coin = random_source.next_bool();
        adopt[site] = coin;
        any = any || coin;
    }
    if (!any) {
        const std::size_t forced = random_source.next_index(sites.size());
        adopt[forced] = true;
    }
    Specification child = realizable;
    for (std::size_t site = 0; site < sites.size(); ++site) {
        if (!adopt[site]) {
            continue;
        }
        const Site& chosen = sites[site];
        if (chosen.m_assumption) {
            child.m_assumptions[chosen.m_slot] =
                unrealizable.m_assumptions[chosen.m_slot];
        } else {
            child.m_guarantees[chosen.m_slot] =
                unrealizable.m_guarantees[chosen.m_slot];
        }
    }
    return {std::move(child), true};
}

OrderCrossoverOutcome order_crossover(OrderCrossoverKind kind,
                                      const Specification& first,
                                      const Specification& second,
                                      bool first_realizable,
                                      std::size_t n_original_assumptions,
                                      const RandomSource& random_source) {
    OrderCrossoverOutcome outcome;
    switch (kind) {
        case OrderCrossoverKind::Join:
            outcome = join_crossover(first, second, n_original_assumptions,
                                     random_source);
            break;
        case OrderCrossoverKind::Meet:
            outcome = meet_crossover(first, second, n_original_assumptions,
                                     random_source);
            break;
        case OrderCrossoverKind::Relink:
            outcome =
                first_realizable
                    ? relink_crossover(first, second, n_original_assumptions,
                                       random_source)
                    : relink_crossover(second, first, n_original_assumptions,
                                       random_source);
            break;
    }
    // A bound equal to a parent is a parent already scored, and a relinked
    // child equal to the unrealizable parent is known unrealizable.
    if (outcome.m_child.has_value() &&
        (*outcome.m_child == first || *outcome.m_child == second)) {
        outcome.m_child.reset();
    }
    if (!outcome.m_child.has_value()) {
        outcome.m_proved = false;
    }
    OrderCrossoverStats::n_attempted[index_of(kind)]++;
    if (outcome.m_proved) {
        OrderCrossoverStats::n_proved[index_of(kind)]++;
    }
    return outcome;
}

void OrderCrossoverStats::note_offspring(std::optional<OrderCrossoverKind> kind,
                                         const Specification& offspring) {
    if (!kind.has_value()) {
        return;
    }
    const std::scoped_lock lock(g_pending_mutex);
    g_pending.emplace_back(offspring, *kind);
}

void OrderCrossoverStats::settle(
    const std::vector<Scored<Specification>>& scored,
    std::optional<std::size_t> status_index) {
    const std::scoped_lock lock(g_pending_mutex);
    if (g_pending.empty()) {
        return;
    }
    if (status_index.has_value()) {
        std::unordered_map<Specification, bool> realizable;
        realizable.reserve(scored.size());
        for (const Scored<Specification>& entry : scored) {
            realizable.emplace(
                entry.specification,
                *status_index < entry.objectives.size() &&
                    entry.objectives[*status_index] >= k_status_realizable);
        }
        for (const auto& [offspring, kind] : g_pending) {
            const auto found = realizable.find(offspring);
            if (found != realizable.end() && found->second) {
                n_realizable[index_of(kind)]++;
            }
        }
    }
    g_pending.clear();
}
