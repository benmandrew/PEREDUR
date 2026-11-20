#include "psp/implication.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

#include "filter/implication_check.hpp"
#include "prop_formula.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"

namespace psp {

namespace {

using pattern::Absence;
using pattern::BoundedExistence;
using pattern::BoundedInvariance;
using pattern::BoundedRecurrence;
using pattern::BoundedResponse;
using pattern::Existence;
using pattern::Precedence;
using pattern::Response;
using pattern::Universality;

template <typename T>
bool holds(const Pattern& pattern) {
    return std::holds_alternative<T>(pattern);
}

// The count relation inside one bounded pattern: bounded invariance
// strengthens as its count grows, and the other three weaken.
bool count_implies(const Pattern& stronger, const Pattern& weaker) {
    // Both carry a count: the caller compares one bounded alternative.
    const std::size_t lhs = pattern_count(stronger).value_or(0);
    const std::size_t rhs = pattern_count(weaker).value_or(0);
    return holds<BoundedInvariance>(stronger) ? lhs >= rhs : lhs <= rhs;
}

// Whether @p stronger implies @p weaker where the two are different
// alternatives. The edges are those of `pattern_order` in
// docs/dev/psp-orders.json, which the psp_implication suite re-derives.
bool cross_pattern_implies(const Pattern& stronger, const Pattern& weaker,
                           ScopeClass in_class) {
    // Every pattern of the form "whenever P, ..." is implied by absence, which
    // never lets P hold, and by universality where the body is met by S at
    // the trigger point.
    const bool weaker_is_triggered =
        holds<BoundedResponse>(weaker) || holds<BoundedInvariance>(weaker) ||
        holds<Precedence>(weaker) || holds<Response>(weaker);
    return std::visit(
        [&](const auto& from) {
            using T = std::decay_t<decltype(from)>;
            if constexpr (std::is_same_v<T, Absence>) {
                return weaker_is_triggered || holds<BoundedExistence>(weaker);
            } else if constexpr (std::is_same_v<T, Universality>) {
                // Before R leaves the scope empty where R holds at once, and
                // an empty scope has no S.
                return weaker_is_triggered ||
                       holds<BoundedRecurrence>(weaker) ||
                       (holds<Existence>(weaker) &&
                        in_class != ScopeClass::Before);
            } else if constexpr (std::is_same_v<T, BoundedInvariance>) {
                return holds<BoundedResponse>(weaker) ||
                       holds<Precedence>(weaker) || holds<Response>(weaker);
            } else if constexpr (std::is_same_v<T, BoundedResponse>) {
                return holds<Response>(weaker);
            } else if constexpr (std::is_same_v<T, BoundedRecurrence>) {
                // A boundary discharges the recurrence, so only a scope that
                // never closes keeps these edges.
                if (in_class != ScopeClass::Unbounded) {
                    return false;
                }
                if (const auto* response =
                        std::get_if<BoundedResponse>(&weaker)) {
                    return response->m_count >= from.m_count;
                }
                return holds<Existence>(weaker) || holds<Response>(weaker);
            } else {
                return false;
            }
        },
        stronger);
}

enum Field : std::uint8_t {
    k_pattern,
    k_scope,
    k_p,
    k_s,
    k_q,
    k_r,
    k_field_count
};

constexpr unsigned k_all_moved = (1U << k_field_count) - 1;
constexpr std::size_t k_formula_fields = 4;

// A field neither side reads takes this value, which no state reads either.
const Formula& unread() {
    static const Formula value("true");
    return value;
}

// A chain of states from `from` to `dest`, each differing from the last in one
// field and implying the next, as the FRETISH RuleSearch in
// src/filter/implication_check.cpp. A state is the set of fields already moved
// to `dest`'s value. Its pattern and scope fix which formulas it reads, and
// each formula field holds a value on both sides: where one side does not read
// a field, it takes the other side's value, which leaves its lowering as it
// was. Every step is then one measured fact under a substitution of formulas
// for the atoms p, s, q and r, and the measured facts hold over free atoms.
class RuleSearch {
   public:
    RuleSearch(const Requirement& from, const Requirement& dest)
        : m_from(from), m_dest(dest) {
        const std::array<const std::optional<Formula>*, k_formula_fields>
            from_fields = {&from.m_p, &from.m_s, &from.m_q, &from.m_r};
        const std::array<const std::optional<Formula>*, k_formula_fields>
            dest_fields = {&dest.m_p, &dest.m_s, &dest.m_q, &dest.m_r};
        for (std::size_t idx = 0; idx < k_formula_fields; ++idx) {
            const std::optional<Formula>& lhs = *from_fields[idx];
            const std::optional<Formula>& rhs = *dest_fields[idx];
            const Formula* either = &unread();
            if (lhs.has_value()) {
                either = &*lhs;
            } else if (rhs.has_value()) {
                either = &*rhs;
            }
            m_from_values[idx] = lhs.has_value() ? &*lhs : either;
            m_dest_values[idx] = rhs.has_value() ? &*rhs : either;
        }
    }

    bool reaches_dest() {
        unsigned start = 0;
        start |= bit(k_pattern, m_from.m_pattern == m_dest.m_pattern);
        start |= bit(k_scope, m_from.m_scope == m_dest.m_scope);
        for (std::size_t idx = 0; idx < k_formula_fields; ++idx) {
            start |= bit(formula_bit(idx),
                         *m_from_values[idx] == *m_dest_values[idx]);
        }
        return reach(start);
    }

   private:
    static unsigned bit(Field field, bool set) {
        return set ? 1U << field : 0U;
    }

    static Field formula_bit(std::size_t idx) {
        return static_cast<Field>(k_p + idx);
    }

    static bool is_moved(unsigned moved, Field field) {
        return (moved & (1U << field)) != 0;
    }

    [[nodiscard]] const Pattern& pattern_at(unsigned moved) const {
        return is_moved(moved, k_pattern) ? m_dest.m_pattern : m_from.m_pattern;
    }

    [[nodiscard]] PspScopeKind scope_at(unsigned moved) const {
        return is_moved(moved, k_scope) ? m_dest.m_scope : m_from.m_scope;
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
        const Pattern& pattern = pattern_at(moved);
        const PspScopeKind scope = scope_at(moved);
        switch (field) {
            case k_pattern:
                return pattern_implies(m_from.m_pattern, m_dest.m_pattern,
                                       scope_class(scope));
            case k_scope:
                return scope_implies(m_from.m_scope, m_dest.m_scope,
                                     pattern_cell(pattern));
            case k_p:
            case k_s:
            case k_q:
            case k_r:
                return formula_step(static_cast<std::size_t>(field - k_p),
                                    pattern, scope);
            case k_field_count:
                break;
        }
        assert(false);
        __builtin_unreachable();
    }

    bool formula_step(std::size_t idx, const Pattern& pattern,
                      PspScopeKind scope) {
        const std::optional<Polarity> polarity =
            field_polarity(static_cast<FormulaField>(idx), pattern, scope);
        if (!polarity.has_value()) {
            // The state does not read the field, so moving it changes nothing.
            return true;
        }
        switch (*polarity) {
            case Polarity::Monotone:
                return onward(idx);
            case Polarity::Antitone:
                return back(idx);
            case Polarity::None:
                return onward(idx) && back(idx);
        }
        assert(false);
        __builtin_unreachable();
    }

    // Each formula field has two values, so two answers cover every state:
    // [0] for `from`'s value implying `dest`'s, [1] for the converse.
    bool onward(std::size_t idx) {
        std::optional<bool>& answer = m_answers[idx][0];
        if (!answer.has_value()) {
            answer = formula_implies(*m_from_values[idx], *m_dest_values[idx]);
        }
        return *answer;
    }

    bool back(std::size_t idx) {
        std::optional<bool>& answer = m_answers[idx][1];
        if (!answer.has_value()) {
            answer = formula_implies(*m_dest_values[idx], *m_from_values[idx]);
        }
        return *answer;
    }

    const Requirement& m_from;
    const Requirement& m_dest;
    std::array<const Formula*, k_formula_fields> m_from_values{};
    std::array<const Formula*, k_formula_fields> m_dest_values{};
    std::array<bool, k_all_moved + 1> m_visited{};
    std::array<std::array<std::optional<bool>, 2>, k_formula_fields>
        m_answers{};
};

enum class Cover : std::uint8_t { None, Lowering, Rule };

// As the FRETISH rule_cover: the entry in @p req's own slot first, then the
// pool's lowerings, then the rule on the slot's entry.
Cover rule_cover(const Requirement& req, std::size_t slot,
                 const std::vector<Requirement>& pool,
                 const std::unordered_set<std::string>& pool_lowerings,
                 const RequirementImplication& implies) {
    const bool slot_live = slot < pool.size() && !pool[slot].m_removed;
    if ((slot_live && pool[slot].m_ltl == req.m_ltl) ||
        pool_lowerings.count(req.m_ltl) != 0) {
        return Cover::Lowering;
    }
    if (slot_live && implies(pool[slot], req)) {
        return Cover::Rule;
    }
    return Cover::None;
}

}  // namespace

ScopeClass scope_class(PspScopeKind scope) {
    switch (scope) {
        case PspScopeKind::Globally:
        case PspScopeKind::After:
            return ScopeClass::Unbounded;
        case PspScopeKind::Between:
        case PspScopeKind::AfterUntil:
            return ScopeClass::Bounded;
        case PspScopeKind::Before:
            return ScopeClass::Before;
    }
    assert(false);
    __builtin_unreachable();
}

PatternCell pattern_cell(const Pattern& pattern) {
    return std::visit(
        [](const auto& alternative) {
            using T = std::decay_t<decltype(alternative)>;
            constexpr bool k_response = std::is_same_v<T, Response> ||
                                        std::is_same_v<T, BoundedResponse>;
            if constexpr (std::is_same_v<T, Existence>) {
                return PatternCell::Existence;
            } else if constexpr (std::is_same_v<T, Precedence>) {
                return PatternCell::Precedence;
            } else if constexpr (k_response) {
                return PatternCell::Response;
            } else {
                return PatternCell::Invariant;
            }
        },
        pattern);
}

bool pattern_implies(const Pattern& stronger, const Pattern& weaker,
                     ScopeClass in_class) {
    if (stronger.index() == weaker.index()) {
        return !pattern_count(stronger).has_value() ||
               count_implies(stronger, weaker);
    }
    return cross_pattern_implies(stronger, weaker, in_class);
}

bool scope_implies(PspScopeKind stronger, PspScopeKind weaker,
                   PatternCell cell) {
    using K = PspScopeKind;
    if (stronger == weaker) {
        return true;
    }
    // After-until opens a scope wherever between does, and closes it no
    // later, in every cell.
    if (stronger == K::AfterUntil && weaker == K::Between) {
        return true;
    }
    switch (cell) {
        case PatternCell::Invariant:
            // A safety body holds on every suffix of a scope it holds on, so
            // a scope inside another is implied by it: globally contains
            // every scope, and after the first Q every after-until and
            // between scope.
            return stronger == K::Globally ||
                   (stronger == K::After &&
                    (weaker == K::AfterUntil || weaker == K::Between));
        case PatternCell::Existence:
            return false;
        case PatternCell::Precedence:
            return stronger == K::Globally && weaker == K::Before;
        case PatternCell::Response:
            return stronger == K::Globally && weaker == K::After;
    }
    assert(false);
    __builtin_unreachable();
}

std::optional<Polarity> field_polarity(FormulaField field,
                                       const Pattern& pattern,
                                       PspScopeKind scope) {
    switch (field) {
        case FormulaField::P:
            if (!pattern_uses_p(pattern)) {
                return std::nullopt;
            }
            // The count of P stretches is not monotone in P.
            return holds<BoundedExistence>(pattern) ? Polarity::None
                                                    : Polarity::Antitone;
        case FormulaField::S:
            if (!pattern_uses_s(pattern)) {
                return std::nullopt;
            }
            return Polarity::Monotone;
        case FormulaField::Q:
            if (!scope_uses_q(scope)) {
                return std::nullopt;
            }
            // After opens at the first Q, so a body read from there on is
            // antitone in Q only where it holds on every suffix.
            return scope == PspScopeKind::After && (holds<Existence>(pattern) ||
                                                    holds<Precedence>(pattern))
                       ? Polarity::None
                       : Polarity::Antitone;
        case FormulaField::R:
            if (!scope_uses_r(scope)) {
                return std::nullopt;
            }
            if (scope == PspScopeKind::AfterUntil) {
                return pattern_cell(pattern) == PatternCell::Existence ||
                               pattern_cell(pattern) == PatternCell::Response
                           ? Polarity::None
                           : Polarity::Monotone;
            }
            return scope == PspScopeKind::Before && holds<Existence>(pattern)
                       ? Polarity::Antitone
                       : Polarity::None;
    }
    assert(false);
    __builtin_unreachable();
}

bool requirement_implies(const Requirement& stronger,
                         const Requirement& weaker) {
    if (stronger.m_ltl == weaker.m_ltl) {
        return true;
    }
    if (stronger.is_fixed() || weaker.is_fixed()) {
        return false;
    }
    return RuleSearch(stronger, weaker).reaches_dest();
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

bool spec_implies_by_rule(
    const Specification& from, const Specification& dest,
    const std::unordered_set<std::string>& from_guarantees,
    const std::unordered_set<std::string>& dest_assumptions,
    const RequirementImplication& implies) {
    for (std::size_t slot = 0; slot < from.m_assumptions.size(); ++slot) {
        const Requirement& req = from.m_assumptions[slot];
        if (!req.m_removed &&
            rule_cover(req, slot, dest.m_assumptions, dest_assumptions,
                       implies) == Cover::None) {
            return false;
        }
    }
    for (std::size_t slot = 0; slot < dest.m_guarantees.size(); ++slot) {
        const Requirement& req = dest.m_guarantees[slot];
        if (!req.m_removed &&
            rule_cover(req, slot, from.m_guarantees, from_guarantees,
                       implies) == Cover::None) {
            return false;
        }
    }
    return true;
}

bool spec_implies_by_rule(const Specification& from,
                          const Specification& dest) {
    return spec_implies_by_rule(from, dest, live_lowerings(from.m_guarantees),
                                live_lowerings(dest.m_assumptions),
                                requirement_implies);
}

std::optional<Specification> residual_destination(const Specification& from,
                                                  const Specification& dest) {
    const std::unordered_set<std::string> dest_assumptions =
        live_lowerings(dest.m_assumptions);
    for (std::size_t slot = 0; slot < from.m_assumptions.size(); ++slot) {
        const Requirement& req = from.m_assumptions[slot];
        if (!req.m_removed &&
            rule_cover(req, slot, dest.m_assumptions, dest_assumptions,
                       requirement_implies) == Cover::None) {
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
            rule_cover(req, slot, from.m_guarantees, from_guarantees,
                       requirement_implies) != Cover::None) {
            req.m_removed = true;
            dropped = true;
        }
    }
    if (!dropped) {
        return std::nullopt;
    }
    return residual;
}

}  // namespace psp
