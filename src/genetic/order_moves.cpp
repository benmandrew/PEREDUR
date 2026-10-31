#include "genetic/order_moves.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "filter/implication_check.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"

namespace {

// Which side of the pair a bound lies on: above both (a join) or below both (a
// meet).
enum class Bound : std::uint8_t { Upper, Lower };

Bound flipped(Bound bound) {
    return bound == Bound::Upper ? Bound::Lower : Bound::Upper;
}

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

// Whether @p req is implied by a live entry of @p pool: one with the same
// lowering anywhere, or by rule the entry in @p req's own slot.
bool covered(const Requirement& req, std::size_t slot,
             const std::vector<Requirement>& pool,
             const std::unordered_set<std::string>& pool_lowerings) {
    if (pool_lowerings.count(req.m_ltl) != 0) {
        return true;
    }
    return slot < pool.size() && !pool[slot].m_removed &&
           requirement_implies(pool[slot], req);
}

// Every live entry of @p reqs is covered by @p pool.
bool all_covered(const std::vector<Requirement>& reqs,
                 const std::vector<Requirement>& pool) {
    const std::unordered_set<std::string> lowerings = live_lowerings(pool);
    for (std::size_t slot = 0; slot < reqs.size(); ++slot) {
        if (!reqs[slot].m_removed &&
            !covered(reqs[slot], slot, pool, lowerings)) {
            return false;
        }
    }
    return true;
}

std::optional<std::size_t> tick_count(const Timing& timing) {
    return std::visit(
        [](const auto& value) -> std::optional<std::size_t> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, timing::WithinTicks> ||
                          std::is_same_v<T, timing::ForTicks> ||
                          std::is_same_v<T, timing::AfterTicks>) {
                return value.m_ticks;
            } else {
                return std::nullopt;
            }
        },
        timing);
}

std::size_t largest_count(const Timing& lhs, const Timing& rhs) {
    return std::max<std::size_t>(
        {1, tick_count(lhs).value_or(0), tick_count(rhs).value_or(0)});
}

void push_unique(std::vector<Timing>& timings, Timing timing) {
    // std::find would reach std's variant comparison, which needs one on
    // each alternative, rather than the Timing operator== in requirement.hpp.
    const bool present =
        std::any_of(timings.begin(), timings.end(),
                    [&timing](const Timing& other) { return other == timing; });
    if (!present) {
        timings.push_back(std::move(timing));
    }
}

// The inputs first, so that one of them wins a tie over an enumerated timing.
// One tick past the largest count reaches the meet of two `after` timings,
// `after n` and `after m` both implying `within max(n, m) + 1`.
std::vector<Timing> timing_candidates(const Timing& lhs, const Timing& rhs) {
    std::vector<Timing> candidates;
    push_unique(candidates, lhs);
    push_unique(candidates, rhs);
    push_unique(candidates, timing::immediately());
    push_unique(candidates, timing::next_timepoint());
    const std::size_t limit = largest_count(lhs, rhs) + 1;
    for (std::size_t ticks = 1; ticks <= limit; ++ticks) {
        push_unique(candidates, timing::within_ticks(ticks));
        push_unique(candidates, timing::for_ticks(ticks));
    }
    push_unique(candidates, timing::eventually());
    push_unique(candidates, timing::always());
    return candidates;
}

// @p lhs and @p rhs combined under `&` (@p conjoin) or `|`, or the one that
// already is that combination where the two are comparable.
Formula combine(const Formula& lhs, const Formula& rhs, bool conjoin) {
    if (lhs == rhs) {
        return lhs;
    }
    if (formula_implies(lhs, rhs)) {
        return conjoin ? lhs : rhs;
    }
    if (formula_implies(rhs, lhs)) {
        return conjoin ? rhs : lhs;
    }
    return Formula::make_binary(
        conjoin ? Formula::Kind::And : Formula::Kind::Or, lhs, rhs);
}

bool equivalent(const Formula& lhs, const Formula& rhs) {
    return lhs == rhs ||
           (formula_implies(lhs, rhs) && formula_implies(rhs, lhs));
}

std::optional<Scope> bound_scope(const Requirement& lhs, const Requirement& rhs,
                                 Bound bound) {
    if (lhs.m_scope == rhs.m_scope) {
        return lhs.m_scope;
    }
    // The scope order depends on the timing and condition type, which the
    // result has not yet fixed, so an edge under either input's is taken.
    const auto implies = [&](const Scope& stronger, const Scope& weaker) {
        return scope_implies(stronger, weaker, lhs.m_timing,
                             lhs.m_condition_type) ||
               scope_implies(stronger, weaker, rhs.m_timing,
                             rhs.m_condition_type);
    };
    const bool lhs_stronger = implies(lhs.m_scope, rhs.m_scope);
    const bool rhs_stronger = implies(rhs.m_scope, lhs.m_scope);
    if (bound == Bound::Upper) {
        if (lhs_stronger) {
            return lhs.m_scope;
        }
        return rhs_stronger ? rhs.m_scope : Scope{};
    }
    if (lhs_stronger) {
        return rhs.m_scope;
    }
    if (rhs_stronger) {
        return lhs.m_scope;
    }
    return std::nullopt;
}

// Two stops of one kind combine along the stop's polarity: positive in
// `until`, negative in `before`, swapped under an only scope.
std::optional<Timing> bound_stop(const Timing& lhs, const Timing& rhs,
                                 ScopeKind scope, Bound bound) {
    const Formula* lhs_stop = timing_stop(lhs);
    const Formula* rhs_stop = timing_stop(rhs);
    if (lhs_stop == nullptr || rhs_stop == nullptr ||
        lhs.index() != rhs.index()) {
        return std::nullopt;
    }
    const bool is_before = std::holds_alternative<timing::Before>(lhs);
    const bool positive = is_before == is_only_scope(scope);
    // A stronger requirement has a smaller positive stop.
    const bool conjoin = positive == (bound == Bound::Upper);
    Formula stop = combine(*lhs_stop, *rhs_stop, conjoin);
    return is_before ? timing::before(std::move(stop))
                     : timing::until(std::move(stop));
}

// A weakest timing implying both (Upper) or a strongest both imply (Lower),
// among the enumerated candidates.
std::optional<Timing> bound_timing(const Timing& lhs, const Timing& rhs,
                                   ScopeKind scope, Bound bound) {
    if (lhs == rhs) {
        return lhs;
    }
    if (std::optional<Timing> stop = bound_stop(lhs, rhs, scope, bound)) {
        return stop;
    }
    // `above` holds when @p upper implies @p lower in the order.
    const auto above = [scope](const Timing& upper, const Timing& lower) {
        return timing_implies(upper, lower, scope);
    };
    std::vector<Timing> admissible;
    for (const Timing& candidate : timing_candidates(lhs, rhs)) {
        const bool fits = bound == Bound::Upper
                              ? above(candidate, lhs) && above(candidate, rhs)
                              : above(lhs, candidate) && above(rhs, candidate);
        if (fits) {
            admissible.push_back(candidate);
        }
    }
    // Strictly closer to the inputs: weaker for a join, stronger for a meet.
    const auto closer = [&](const Timing& other, const Timing& candidate) {
        return bound == Bound::Upper
                   ? above(candidate, other) && !above(other, candidate)
                   : above(other, candidate) && !above(candidate, other);
    };
    for (const Timing& candidate : admissible) {
        const bool extreme = std::none_of(
            admissible.begin(), admissible.end(),
            [&](const Timing& other) { return closer(other, candidate); });
        if (extreme) {
            return candidate;
        }
    }
    return std::nullopt;
}

// The field-wise combination of two incomparable requirements, unchecked.
std::optional<Requirement> combine_fields(const Requirement& lhs,
                                          const Requirement& rhs, Bound bound) {
    const bool upper = bound == Bound::Upper;
    const ConditionType either =
        upper ? ConditionType::Continual : ConditionType::Trigger;
    ConditionType ctype =
        lhs.m_condition_type == either || rhs.m_condition_type == either
            ? either
            : lhs.m_condition_type;
    // A continual `c | c'` fires wherever either trigger does, so two
    // triggers on different conditions still have a join.
    if (upper && ctype == ConditionType::Trigger &&
        !equivalent(lhs.m_condition, rhs.m_condition)) {
        ctype = ConditionType::Continual;
    }
    const std::optional<Scope> scope = bound_scope(lhs, rhs, bound);
    if (!scope.has_value()) {
        return std::nullopt;
    }
    std::optional<Timing> timing =
        bound_timing(lhs.m_timing, rhs.m_timing, scope->m_kind, bound);
    if (!timing.has_value()) {
        return std::nullopt;
    }
    // The polarities are those of response_direction and condition_direction
    // in src/genetic/mutation.cpp.
    Formula response;
    if (std::holds_alternative<timing::AfterTicks>(*timing)) {
        if (!equivalent(lhs.m_response, rhs.m_response)) {
            return std::nullopt;
        }
        response = lhs.m_response;
    } else {
        const bool positive = !is_only_scope(scope->m_kind);
        response = combine(lhs.m_response, rhs.m_response, positive == upper);
    }
    Formula condition;
    if (ctype == ConditionType::Continual) {
        condition = combine(lhs.m_condition, rhs.m_condition, !upper);
    } else {
        if (!equivalent(lhs.m_condition, rhs.m_condition)) {
            return std::nullopt;
        }
        condition = lhs.m_condition;
    }
    return Requirement(std::move(condition), std::move(response),
                       std::move(*timing), ctype, /*weakenable=*/true,
                       /*removed=*/false, *scope);
}

std::optional<Requirement> requirement_bound(const Requirement& lhs,
                                             const Requirement& rhs,
                                             Bound bound) {
    if (lhs == rhs) {
        return lhs;
    }
    if (lhs.m_removed || rhs.m_removed || !lhs.m_weakenable ||
        !rhs.m_weakenable) {
        return std::nullopt;
    }
    const bool lhs_implies = requirement_implies(lhs, rhs);
    const bool rhs_implies = requirement_implies(rhs, lhs);
    if (lhs_implies || rhs_implies) {
        const bool take_lhs = lhs_implies == (bound == Bound::Upper) ||
                              (lhs_implies && rhs_implies);
        return take_lhs ? lhs : rhs;
    }
    std::optional<Requirement> result = combine_fields(lhs, rhs, bound);
    if (!result.has_value()) {
        return std::nullopt;
    }
    const bool proved = bound == Bound::Upper
                            ? requirement_implies(*result, lhs) &&
                                  requirement_implies(*result, rhs)
                            : requirement_implies(lhs, *result) &&
                                  requirement_implies(rhs, *result);
    if (!proved) {
        return std::nullopt;
    }
    return result;
}

// The bound of one slot, a tombstone reading as `true`, the top of the order.
std::optional<Requirement> slot_bound(const Requirement& lhs,
                                      const Requirement& rhs, Bound bound) {
    if (!lhs.m_removed && !rhs.m_removed) {
        return requirement_bound(lhs, rhs, bound);
    }
    if (lhs.m_removed && rhs.m_removed) {
        return lhs;
    }
    const Requirement& live = lhs.m_removed ? rhs : lhs;
    const Requirement& removed = lhs.m_removed ? lhs : rhs;
    return bound == Bound::Upper ? live : removed;
}

std::vector<std::optional<Requirement>> paired_slots(
    const std::vector<Requirement>& lhs, const std::vector<Requirement>& rhs,
    std::size_t count, Bound bound) {
    std::vector<std::optional<Requirement>> slots(count);
    for (std::size_t slot = 0; slot < count; ++slot) {
        if (slot < lhs.size() && slot < rhs.size()) {
            slots[slot] = slot_bound(lhs[slot], rhs[slot], bound);
        }
    }
    return slots;
}

std::vector<Requirement> live_tail(const std::vector<Requirement>& reqs,
                                   std::size_t start) {
    std::vector<Requirement> tail;
    for (std::size_t slot = start; slot < reqs.size(); ++slot) {
        if (!reqs[slot].m_removed) {
            tail.push_back(reqs[slot]);
        }
    }
    return tail;
}

// A join keeps the appended assumptions both inputs carry, which each input's
// assumptions imply; a meet keeps the union, which implies each input's.
std::vector<Requirement> appended_bound(const Specification& lhs,
                                        const Specification& rhs,
                                        std::size_t n_original, Bound bound) {
    const std::vector<Requirement> lhs_tail =
        live_tail(lhs.m_assumptions, n_original);
    const std::vector<Requirement> rhs_tail =
        live_tail(rhs.m_assumptions, n_original);
    const std::unordered_set<std::string> rhs_lowerings =
        live_lowerings(rhs_tail);
    std::vector<Requirement> appended;
    std::unordered_set<std::string> kept;
    for (const Requirement& req : lhs_tail) {
        if ((bound == Bound::Lower || rhs_lowerings.count(req.m_ltl) != 0) &&
            kept.insert(req.m_ltl).second) {
            appended.push_back(req);
        }
    }
    if (bound == Bound::Lower) {
        for (const Requirement& req : rhs_tail) {
            if (kept.insert(req.m_ltl).second) {
                appended.push_back(req);
            }
        }
    }
    return appended;
}

SlotwiseBound specification_bound(const Specification& lhs,
                                  const Specification& rhs,
                                  std::size_t n_original_assumptions,
                                  Bound bound) {
    SlotwiseBound result;
    // `A -> G` is antitone in A, so the assumptions take the opposite bound.
    result.m_assumptions = paired_slots(lhs.m_assumptions, rhs.m_assumptions,
                                        n_original_assumptions, flipped(bound));
    result.m_guarantees = paired_slots(
        lhs.m_guarantees, rhs.m_guarantees,
        std::max(lhs.m_guarantees.size(), rhs.m_guarantees.size()), bound);
    result.m_appended = appended_bound(lhs, rhs, n_original_assumptions, bound);
    return result;
}

Requirement with_field(const Requirement& base, const Requirement& source,
                       RequirementField field) {
    Formula condition = base.m_condition;
    Formula response = base.m_response;
    Timing timing = base.m_timing;
    ConditionType ctype = base.m_condition_type;
    Scope scope = base.m_scope;
    switch (field) {
        case RequirementField::Condition:
            condition = source.m_condition;
            break;
        case RequirementField::Response:
            response = source.m_response;
            break;
        case RequirementField::Timing:
            timing = source.m_timing;
            break;
        case RequirementField::ConditionType:
            ctype = source.m_condition_type;
            break;
        case RequirementField::Scope:
            scope = source.m_scope;
            break;
    }
    return Requirement(std::move(condition), std::move(response),
                       std::move(timing), ctype, base.m_weakenable,
                       base.m_removed, std::move(scope));
}

bool field_differs(const Requirement& lhs, const Requirement& rhs,
                   RequirementField field) {
    switch (field) {
        case RequirementField::Condition:
            return !(lhs.m_condition == rhs.m_condition);
        case RequirementField::Response:
            return !(lhs.m_response == rhs.m_response);
        case RequirementField::Timing:
            return !(lhs.m_timing == rhs.m_timing);
        case RequirementField::ConditionType:
            return lhs.m_condition_type != rhs.m_condition_type;
        case RequirementField::Scope:
            return !(lhs.m_scope == rhs.m_scope);
    }
    return false;
}

constexpr std::array<RequirementField, 5> k_fields = {
    RequirementField::Condition, RequirementField::Response,
    RequirementField::Timing, RequirementField::ConditionType,
    RequirementField::Scope};

class StepCollector {
   public:
    StepCollector(const Specification& current, const Specification& original)
        : m_current(current), m_original(original) {}

    std::vector<Step> collect() {
        const std::size_t n_original = m_original.m_assumptions.size();
        const std::size_t n_assumptions =
            std::min(n_original, m_current.m_assumptions.size());
        for (std::size_t slot = 0; slot < n_assumptions; ++slot) {
            move_slot(StepSide::Assumption, slot,
                      m_original.m_assumptions[slot]);
        }
        const std::size_t n_guarantees = std::min(
            m_current.m_guarantees.size(), m_original.m_guarantees.size());
        for (std::size_t slot = 0; slot < n_guarantees; ++slot) {
            const Requirement& current = m_current.m_guarantees[slot];
            const Requirement& original = m_original.m_guarantees[slot];
            if (original.m_removed) {
                continue;
            }
            if (current.m_removed) {
                Requirement restored = current;
                restored.m_removed = false;
                try_step(StepKind::Restore, StepSide::Guarantee, slot,
                         std::nullopt, restored);
                continue;
            }
            move_slot(StepSide::Guarantee, slot, original);
        }
        for (std::size_t slot = n_original;
             slot < m_current.m_assumptions.size(); ++slot) {
            drop_assumption(slot);
        }
        return std::move(m_steps);
    }

   private:
    [[nodiscard]] const std::vector<Requirement>& side(StepSide which) const {
        return which == StepSide::Assumption ? m_current.m_assumptions
                                             : m_current.m_guarantees;
    }

    void move_slot(StepSide which, std::size_t slot,
                   const Requirement& original) {
        const Requirement& current = side(which)[slot];
        if (current.m_removed || !current.m_weakenable || current == original) {
            return;
        }
        // A stronger guarantee or a weaker assumption strengthens the whole.
        const std::optional<Requirement> target =
            which == StepSide::Guarantee ? requirement_join(current, original)
                                         : requirement_meet(current, original);
        if (!target.has_value() || target->m_ltl == current.m_ltl) {
            return;
        }
        for (const RequirementField field : k_fields) {
            if (!field_differs(current, *target, field)) {
                continue;
            }
            const Requirement stepped = with_field(current, *target, field);
            if (stepped.m_ltl != current.m_ltl) {
                try_step(StepKind::Field, which, slot, field, stepped);
            }
        }
    }

    void try_step(StepKind kind, StepSide which, std::size_t slot,
                  std::optional<RequirementField> field,
                  const Requirement& replacement) {
        std::vector<Requirement> assumptions = m_current.m_assumptions;
        std::vector<Requirement> guarantees = m_current.m_guarantees;
        (which == StepSide::Assumption ? assumptions : guarantees)[slot] =
            replacement;
        push(kind, which, slot, field, std::move(assumptions),
             std::move(guarantees));
    }

    void drop_assumption(std::size_t slot) {
        if (m_current.m_assumptions[slot].m_removed) {
            return;
        }
        std::vector<Requirement> assumptions = m_current.m_assumptions;
        assumptions.erase(assumptions.begin() +
                          static_cast<std::ptrdiff_t>(slot));
        push(StepKind::DropAssumption, StepSide::Assumption, slot, std::nullopt,
             std::move(assumptions), m_current.m_guarantees);
    }

    void push(StepKind kind, StepSide which, std::size_t slot,
              std::optional<RequirementField> field,
              std::vector<Requirement> assumptions,
              std::vector<Requirement> guarantees) {
        const std::size_t n_assumptions = assumptions.size();
        const std::size_t n_guarantees = guarantees.size();
        Specification result(std::move(assumptions), std::move(guarantees),
                             m_current.m_in_atoms, m_current.m_out_atoms,
                             m_current.m_modes);
        if (result.m_assumptions.size() != n_assumptions ||
            result.m_guarantees.size() != n_guarantees ||
            classify_move(m_current, result) != MoveOrder::Stronger) {
            return;
        }
        m_steps.push_back(Step{kind, which, slot, field, std::move(result)});
    }

    const Specification& m_current;
    const Specification& m_original;
    std::vector<Step> m_steps;
};

}  // namespace

bool specification_implies_by_rule(const Specification& stronger,
                                   const Specification& weaker) {
    return all_covered(stronger.m_assumptions, weaker.m_assumptions) &&
           all_covered(weaker.m_guarantees, stronger.m_guarantees);
}

MoveOrder classify_move(const Specification& parent,
                        const Specification& child) {
    if (parent == child) {
        return MoveOrder::Equal;
    }
    const bool child_implies = specification_implies_by_rule(child, parent);
    const bool parent_implies = specification_implies_by_rule(parent, child);
    if (child_implies && parent_implies) {
        return MoveOrder::Equal;
    }
    if (child_implies) {
        return MoveOrder::Stronger;
    }
    return parent_implies ? MoveOrder::Weaker : MoveOrder::Incomparable;
}

std::optional<Requirement> requirement_join(const Requirement& lhs,
                                            const Requirement& rhs) {
    return requirement_bound(lhs, rhs, Bound::Upper);
}

std::optional<Requirement> requirement_meet(const Requirement& lhs,
                                            const Requirement& rhs) {
    return requirement_bound(lhs, rhs, Bound::Lower);
}

bool SlotwiseBound::is_complete() const {
    const auto proved = [](const std::optional<Requirement>& slot) {
        return slot.has_value();
    };
    return std::all_of(m_assumptions.begin(), m_assumptions.end(), proved) &&
           std::all_of(m_guarantees.begin(), m_guarantees.end(), proved);
}

SlotwiseBound specification_join(const Specification& lhs,
                                 const Specification& rhs,
                                 std::size_t n_original_assumptions) {
    return specification_bound(lhs, rhs, n_original_assumptions, Bound::Upper);
}

SlotwiseBound specification_meet(const Specification& lhs,
                                 const Specification& rhs,
                                 std::size_t n_original_assumptions) {
    return specification_bound(lhs, rhs, n_original_assumptions, Bound::Lower);
}

std::optional<Specification> assemble(const SlotwiseBound& bound,
                                      const Specification& shape) {
    std::vector<Requirement> assumptions;
    assumptions.reserve(bound.m_assumptions.size() + bound.m_appended.size());
    for (const std::optional<Requirement>& slot : bound.m_assumptions) {
        if (!slot.has_value()) {
            return std::nullopt;
        }
        assumptions.push_back(*slot);
    }
    assumptions.insert(assumptions.end(), bound.m_appended.begin(),
                       bound.m_appended.end());
    std::vector<Requirement> guarantees;
    guarantees.reserve(bound.m_guarantees.size());
    for (const std::optional<Requirement>& slot : bound.m_guarantees) {
        if (!slot.has_value()) {
            return std::nullopt;
        }
        guarantees.push_back(*slot);
    }
    const std::size_t n_assumptions = assumptions.size();
    const std::size_t n_guarantees = guarantees.size();
    Specification result(std::move(assumptions), std::move(guarantees),
                         shape.m_in_atoms, shape.m_out_atoms, shape.m_modes);
    if (result.m_assumptions.size() != n_assumptions ||
        result.m_guarantees.size() != n_guarantees) {
        return std::nullopt;
    }
    return result;
}

std::vector<Step> steps_towards(const Specification& current,
                                const Specification& original) {
    return StepCollector(current, original).collect();
}

std::vector<Timing> timing_chain(const Timing& from, const Timing& dest,
                                 ScopeKind scope) {
    if (from == dest) {
        return {};
    }
    const bool strengthening = timing_implies(dest, from, scope);
    if (!strengthening && !timing_implies(from, dest, scope)) {
        return {};
    }
    // `above` holds when @p upper implies @p lower in the order.
    const auto above = [scope](const Timing& upper, const Timing& lower) {
        return timing_implies(upper, lower, scope);
    };
    const Timing& upper = strengthening ? dest : from;
    const Timing& lower = strengthening ? from : dest;
    std::vector<Timing> between;
    const std::size_t limit = largest_count(from, dest);
    for (std::size_t ticks = 1; ticks <= limit; ++ticks) {
        for (const Timing& candidate :
             {timing::within_ticks(ticks), timing::for_ticks(ticks)}) {
            if (!(candidate == from) && !(candidate == dest) &&
                above(upper, candidate) && above(candidate, lower)) {
                between.push_back(candidate);
            }
        }
    }
#ifndef NDEBUG
    for (const Timing& lhs : between) {
        for (const Timing& rhs : between) {
            assert(above(lhs, rhs) || above(rhs, lhs));
        }
    }
#endif
    // On a chain, the number of entries a timing implies is its rank from the
    // bottom. Counted before sorting: a comparator reading the vector it sorts
    // sees it half-permuted, which misordered chains of more than a few ticks.
    std::vector<std::pair<std::ptrdiff_t, std::size_t>> ranked;
    ranked.reserve(between.size());
    for (std::size_t i = 0; i < between.size(); ++i) {
        const std::ptrdiff_t rank = std::count_if(
            between.begin(), between.end(),
            [&](const Timing& other) { return above(between[i], other); });
        ranked.emplace_back(strengthening ? rank : -rank, i);
    }
    std::sort(ranked.begin(), ranked.end());
    std::vector<Timing> chain;
    chain.reserve(between.size() + 1);
    for (const auto& [rank, index] : ranked) {
        chain.push_back(between[index]);
    }
    chain.push_back(dest);
    return chain;
}
