#include "filter/implication_check.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

#include "filter/implication.hpp"
#include "fitness/exhaustive_count.hpp"
#include "requirement.hpp"

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

namespace {

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
// stop_direction in src/genetic/mutation.cpp), except that a trigger's
// condition is antitone where the condition types coincide
// (`condition_coincide_sound` in proofs/Fretish/Chain.lean), and the keyword
// steps those of timing_implies, scope_implies and the condition-type order.
// One step moves two fields: the timing and the scope together, from a plain
// scope into an only one whose timing's pattern the plain one rules out
// (cross_step_holds). `reach_sound` in proofs/Fretish/Chain.lean proves the
// search sound.
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
        const unsigned both = (1U << k_timing) | (1U << k_scope);
        return (moved & both) == 0 && cross_step_holds(moved) &&
               reach(moved | both);
    }

    // Whether the timing and the scope may move together, from a plain scope
    // into an only one, where neither one-field middle state implies: the
    // plain timing's pattern rules out the only timing's at the same point.
    // `cross_sound` in proofs/Fretish/Cross.lean.
    [[nodiscard]] bool cross_step_holds(unsigned moved) const {
        if (!timings_clash(m_from.m_timing, m_dest.m_timing)) {
            return false;
        }
        const ScopeKind from = m_from.m_scope.m_kind;
        const ScopeKind dest = m_dest.m_scope.m_kind;
        if (from == ScopeKind::NotIn) {
            return dest == ScopeKind::OnlyIn &&
                   m_from.m_scope.m_mode == m_dest.m_scope.m_mode;
        }
        if (from != ScopeKind::Global) {
            return false;
        }
        if (dest == ScopeKind::OnlyAfter) {
            return true;
        }
        // Only in and only before read the body at later entries too, which a
        // global trigger reaches only through `always`.
        return (dest == ScopeKind::OnlyIn || dest == ScopeKind::OnlyBefore) &&
               (at(moved, k_condition_type).m_condition_type ==
                    ConditionType::Continual ||
                std::holds_alternative<timing::Always>(m_from.m_timing));
    }

    // `after n` keeps the response false for n + 1 ticks and then makes it
    // true, which clashes with a timing that needs it at the first tick
    // (immediately, for k, always), within n ticks (next for n >= 1, within
    // k <= n) or at another tick (after k != n).
    static bool timings_clash(const Timing& lhs, const Timing& rhs) {
        const auto* after = std::get_if<timing::AfterTicks>(&lhs);
        const Timing* other = &rhs;
        if (after == nullptr) {
            after = std::get_if<timing::AfterTicks>(&rhs);
            other = &lhs;
        }
        if (after == nullptr) {
            return false;
        }
        const std::size_t ticks = after->m_ticks;
        return std::visit(
            [ticks](const auto& timing) {
                using T = std::decay_t<decltype(timing)>;
                if constexpr (std::is_same_v<T, timing::Immediately> ||
                              std::is_same_v<T, timing::ForTicks> ||
                              std::is_same_v<T, timing::Always>) {
                    return true;
                } else if constexpr (std::is_same_v<T, timing::NextTimepoint>) {
                    return ticks >= 1;
                } else if constexpr (std::is_same_v<T, timing::WithinTicks>) {
                    return timing.m_ticks <= ticks;
                } else if constexpr (std::is_same_v<T, timing::AfterTicks>) {
                    return timing.m_ticks != ticks;
                } else {
                    return false;
                }
            },
            *other);
    }

    // Whether the state @p moved implies itself with @p field moved too.
    bool step_holds(unsigned moved, Field field) {
        const Timing& timing = at(moved, k_timing).m_timing;
        const ConditionType ctype =
            at(moved, k_condition_type).m_condition_type;
        const ScopeKind scope = at(moved, k_scope).m_scope.m_kind;
        switch (field) {
            case k_condition:
                // An antecedent under continual, and under a trigger where it
                // coincides with continual; elsewhere a trigger's rising edge
                // holds the condition at both polarities.
                return ctype == ConditionType::Continual ||
                               condition_types_coincide(timing, scope)
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

    // The two condition types coincide at Always under the plain scopes and at
    // Eventually under the only ones.
    static bool condition_types_coincide(const Timing& timing,
                                         ScopeKind scope) {
        return is_only_scope(scope)
                   ? std::holds_alternative<timing::Eventually>(timing)
                   : std::holds_alternative<timing::Always>(timing);
    }

    // Continual implies Trigger everywhere, and Trigger implies Continual
    // where they coincide.
    [[nodiscard]] bool condition_type_step(const Timing& timing,
                                           ScopeKind scope) const {
        return m_from.m_condition_type == ConditionType::Continual ||
               condition_types_coincide(timing, scope);
    }

    // Each propositional field has two values, so two answers cover every
    // state: [0] for `from`'s value implying `dest`'s, [1] for the converse.
    using Answers = std::array<std::optional<bool>, 2>;

    static bool onward(Answers& answers, const Formula& stronger,
                       const Formula& weaker) {
        if (!answers[0].has_value()) {
            answers[0] = formula_implies(stronger, weaker);
        }
        return answers[0].value_or(false);
    }

    static bool back(Answers& answers, const Formula& stronger,
                     const Formula& weaker) {
        if (!answers[1].has_value()) {
            answers[1] = formula_implies(stronger, weaker);
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

// rule_cover, counting the covers only the rule proved.
bool covered(const Requirement& req, std::size_t slot,
             const std::vector<Requirement>& pool,
             const std::unordered_set<std::string>& pool_lowerings) {
    switch (rule_cover(req, slot, pool, pool_lowerings, requirement_implies)) {
        case RuleCover::None:
            return false;
        case RuleCover::Lowering:
            return true;
        case RuleCover::Rule:
            ImplicationFilterStats::n_rule_covered.fetch_add(
                1, std::memory_order_relaxed);
            return true;
    }
    assert(false);
    __builtin_unreachable();
}

}  // namespace

// A formula the enumeration declines, by its width or a temporal operator,
// reads as unproven. `true` and `false` may parse as atoms, which only adds
// assignments, so a zero count still proves.
bool formula_implies(const Formula& stronger, const Formula& weaker) {
    if (stronger == weaker) {
        return true;
    }
    const std::optional<Count> models = count_models_exhaustively(
        "(" + stronger.to_string() + ") & !(" + weaker.to_string() + ")");
    return models.has_value() && *models == Count{0};
}

bool requirement_implies(const Requirement& stronger,
                         const Requirement& weaker) {
    if (stronger.m_ltl == weaker.m_ltl) {
        return true;
    }
    return RuleSearch(stronger, weaker).reaches_dest();
}

RuleCover rule_cover(const Requirement& req, std::size_t slot,
                     const std::vector<Requirement>& pool,
                     const std::unordered_set<std::string>& pool_lowerings,
                     const RequirementImplication& implies) {
    // The slot's own entry first: candidates that share an original are
    // mostly equal slot for slot, and a string comparison is cheaper than a
    // hash.
    const bool slot_live = slot < pool.size() && !pool[slot].m_removed;
    if ((slot_live && pool[slot].m_ltl == req.m_ltl) ||
        pool_lowerings.count(req.m_ltl) != 0) {
        return RuleCover::Lowering;
    }
    if (slot_live && implies(pool[slot], req)) {
        return RuleCover::Rule;
    }
    return RuleCover::None;
}

bool spec_implies_by_rule(
    const Specification& from, const Specification& dest,
    const std::unordered_set<std::string>& from_guarantees,
    const std::unordered_set<std::string>& dest_assumptions,
    const RequirementImplication& implies) {
    for (std::size_t slot = 0; slot < from.m_assumptions.size(); ++slot) {
        const Requirement& req = from.m_assumptions[slot];
        if (!req.m_removed &&
            rule_cover(req, slot, dest.m_assumptions, dest_assumptions,
                       implies) == RuleCover::None) {
            return false;
        }
    }
    for (std::size_t slot = 0; slot < dest.m_guarantees.size(); ++slot) {
        const Requirement& req = dest.m_guarantees[slot];
        if (!req.m_removed &&
            rule_cover(req, slot, from.m_guarantees, from_guarantees,
                       implies) == RuleCover::None) {
            return false;
        }
    }
    return true;
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

std::optional<bool> implication_query(SatisfiabilityChecker& checker,
                                      const std::string& counterexample) {
    // A raising query is undecided in the sense a timeout is, and every caller
    // already reads undecided as "not shown to imply". Left to propagate, one
    // unreadable black answer in the final implication sweep would end a run
    // whose search had finished.
    try {
        return checker.check_satisfiability(counterexample,
                                            QueryPolarity::ExpectUnsat);
    } catch (const std::exception&) {
        ImplicationFilterStats::n_errors.fetch_add(1,
                                                   std::memory_order_relaxed);
        return std::nullopt;
    }
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
    const std::optional<bool> sat = implication_query(
        checker, "(" + from.to_ltl() + ") & !(" + target.to_ltl() + ")");
    if (!sat.has_value()) {
        ImplicationFilterStats::n_timeouts.fetch_add(1,
                                                     std::memory_order_relaxed);
        return std::nullopt;
    }
    return !sat.value();
}
