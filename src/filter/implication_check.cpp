#include "filter/implication_check.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "filter/implication.hpp"
#include "fitness/exhaustive_count.hpp"
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

// Whether `stronger -> weaker` is propositionally valid, by enumerating the
// assignments of `stronger & !weaker`. A formula the enumeration declines, by
// its width or a temporal operator, reads as unproven. `true` and `false` may
// parse as atoms, which only adds assignments, so a zero count still proves.
bool valid_implication(const Formula& stronger, const Formula& weaker) {
    if (stronger == weaker) {
        return true;
    }
    const std::optional<Count> models = count_models_exhaustively(
        "(" + stronger.to_string() + ") & !(" + weaker.to_string() + ")");
    return models.has_value() && *models == Count{0};
}

enum Field : std::uint8_t {
    k_condition,
    k_response,
    k_timing,
    k_condition_type,
    k_scope,
    k_field_count
};

constexpr unsigned k_all_moved = (1U << k_field_count) - 1;

// A chain of requirements from `from` to `dest`, each differing from the last
// in one field and implying the next. A state is the set of fields already
// moved to `dest`'s value, and every state is itself a requirement whose
// lowering reads its atoms only through those fields, so each step is a fact
// about one field in the context the others give it. The polarities are those
// the monotone arm reads (response_direction, condition_direction and
// stop_direction in src/genetic/mutation.cpp), and the keyword steps those of
// timing_implies, scope_implies and the condition-type order.
class RuleSearch {
   public:
    RuleSearch(const Requirement& from, const Requirement& dest)
        : m_from(from), m_dest(dest) {}

    bool reaches_dest() {
        unsigned start = 0;
        start |= bit(k_condition, m_from.m_condition == m_dest.m_condition);
        start |= bit(k_response, m_from.m_response == m_dest.m_response);
        start |= bit(k_timing, m_from.m_timing == m_dest.m_timing);
        start |= bit(k_condition_type,
                     m_from.m_condition_type == m_dest.m_condition_type);
        start |= bit(k_scope, m_from.m_scope == m_dest.m_scope);
        return reach(start);
    }

   private:
    static unsigned bit(Field field, bool set) {
        return set ? 1U << field : 0U;
    }

    [[nodiscard]] const Requirement& at(unsigned moved, Field field) const {
        return (moved & (1U << field)) != 0 ? m_dest : m_from;
    }

    bool reach(unsigned moved) {
        if (moved == k_all_moved) {
            return true;
        }
        if (m_visited[moved]) {
            return false;
        }
        m_visited[moved] = true;
        for (unsigned field = 0; field < k_field_count; ++field) {
            const unsigned next = moved | (1U << field);
            if (next != moved && step_holds(moved, static_cast<Field>(field)) &&
                reach(next)) {
                return true;
            }
        }
        return false;
    }

    // Whether the state @p moved implies itself with @p field moved too.
    bool step_holds(unsigned moved, Field field) {
        const Timing& timing = at(moved, k_timing).m_timing;
        const ConditionType ctype =
            at(moved, k_condition_type).m_condition_type;
        const ScopeKind scope = at(moved, k_scope).m_scope.m_kind;
        switch (field) {
            case k_condition:
                // An antecedent under continual; a trigger's rising edge holds
                // the condition at both polarities.
                return ctype == ConditionType::Continual
                           ? back(m_condition, m_dest.m_condition,
                                  m_from.m_condition)
                           : equivalent(m_condition, m_from.m_condition,
                                        m_dest.m_condition);
            case k_response:
                if (std::holds_alternative<timing::AfterTicks>(timing)) {
                    return equivalent(m_response, m_from.m_response,
                                      m_dest.m_response);
                }
                return is_only_scope(scope)
                           ? back(m_response, m_dest.m_response,
                                  m_from.m_response)
                           : onward(m_response, m_from.m_response,
                                    m_dest.m_response);
            case k_timing:
                return timing_step(scope);
            case k_condition_type:
                return condition_type_step(timing, scope);
            case k_scope:
                return scope_implies(m_from.m_scope, m_dest.m_scope, timing,
                                     ctype);
            case k_field_count:
                break;
        }
        assert(false);
        __builtin_unreachable();
    }

    bool timing_step(ScopeKind scope) {
        const Formula* from_stop = timing_stop(m_from.m_timing);
        const Formula* dest_stop = timing_stop(m_dest.m_timing);
        const bool stop_only =
            from_stop != nullptr && dest_stop != nullptr &&
            m_from.m_timing.index() == m_dest.m_timing.index();
        if (!stop_only) {
            return timing_implies(m_from.m_timing, m_dest.m_timing, scope);
        }
        // Positive in `until`, negative in `before`, swapped under an only
        // scope.
        const bool is_before =
            std::holds_alternative<timing::Before>(m_from.m_timing);
        return is_before != is_only_scope(scope)
                   ? back(m_stop, *dest_stop, *from_stop)
                   : onward(m_stop, *from_stop, *dest_stop);
    }

    // Continual implies Trigger everywhere; the two coincide at Always under
    // the plain scopes and at Eventually under the only ones.
    [[nodiscard]] bool condition_type_step(const Timing& timing,
                                           ScopeKind scope) const {
        if (m_from.m_condition_type == ConditionType::Continual) {
            return true;
        }
        return is_only_scope(scope)
                   ? std::holds_alternative<timing::Eventually>(timing)
                   : std::holds_alternative<timing::Always>(timing);
    }

    // Each propositional field has two values, so two answers cover every
    // state: [0] for `from`'s value implying `dest`'s, [1] for the converse.
    using Answers = std::array<std::optional<bool>, 2>;

    static bool onward(Answers& answers, const Formula& stronger,
                       const Formula& weaker) {
        if (!answers[0].has_value()) {
            answers[0] = valid_implication(stronger, weaker);
        }
        return answers[0].value_or(false);
    }

    static bool back(Answers& answers, const Formula& stronger,
                     const Formula& weaker) {
        if (!answers[1].has_value()) {
            answers[1] = valid_implication(stronger, weaker);
        }
        return answers[1].value_or(false);
    }

    static bool equivalent(Answers& answers, const Formula& lhs,
                           const Formula& rhs) {
        return onward(answers, lhs, rhs) && back(answers, rhs, lhs);
    }

    const Requirement& m_from;
    const Requirement& m_dest;
    std::array<bool, k_all_moved + 1> m_visited{};
    Answers m_condition;
    Answers m_response;
    Answers m_stop;
};

// Whether @p req is implied by a live entry of @p pool: one with the same
// lowering, or by rule the entry in @p req's own slot. Slots pair by index,
// tombstones included, so the entry there is the one the two specifications
// inherited from one original.
bool covered(const Requirement& req, std::size_t slot,
             const std::vector<Requirement>& pool,
             const std::unordered_set<std::string>& pool_lowerings) {
    if (pool_lowerings.count(req.m_ltl) != 0) {
        return true;
    }
    if (slot >= pool.size() || pool[slot].m_removed ||
        !requirement_implies(pool[slot], req)) {
        return false;
    }
    ImplicationFilterStats::n_rule_covered.fetch_add(1,
                                                     std::memory_order_relaxed);
    return true;
}

}  // namespace

bool requirement_implies(const Requirement& stronger,
                         const Requirement& weaker) {
    if (stronger.m_ltl == weaker.m_ltl) {
        return true;
    }
    return RuleSearch(stronger, weaker).reaches_dest();
}

std::optional<Specification> residual_destination(const Specification& from,
                                                  const Specification& dest) {
    const std::unordered_set<std::string> dest_assumptions =
        live_lowerings(dest.m_assumptions);
    for (std::size_t slot = 0; slot < from.m_assumptions.size(); ++slot) {
        const Requirement& req = from.m_assumptions[slot];
        if (!req.m_removed &&
            !covered(req, slot, dest.m_assumptions, dest_assumptions)) {
            return std::nullopt;
        }
    }
    const std::unordered_set<std::string> from_guarantees =
        live_lowerings(from.m_guarantees);
    Specification residual = dest;
    bool dropped = false;
    for (std::size_t slot = 0; slot < residual.m_guarantees.size(); ++slot) {
        Requirement& req = residual.m_guarantees[slot];
        if (!req.m_removed &&
            covered(req, slot, from.m_guarantees, from_guarantees)) {
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
