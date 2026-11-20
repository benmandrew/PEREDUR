#include "psp/order_moves.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "filter/implication_check.hpp"
#include "genetic/order_step.hpp"
#include "prop_formula.hpp"
#include "psp/implication.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"

namespace psp {

namespace {

// Which side of the pair a bound lies on: above both (a join) or below both
// (a meet).
enum class Bound : std::uint8_t { Upper, Lower };

constexpr std::array<PspScopeKind, 5> k_scopes = {
    PspScopeKind::Globally, PspScopeKind::Before, PspScopeKind::After,
    PspScopeKind::Between, PspScopeKind::AfterUntil};

constexpr std::array<FormulaField, 4> k_formula_fields = {
    FormulaField::P, FormulaField::S, FormulaField::Q, FormulaField::R};

constexpr std::array<RequirementField, 6> k_fields = {
    RequirementField::Pattern, RequirementField::Scope, RequirementField::P,
    RequirementField::S,       RequirementField::Q,     RequirementField::R};

std::optional<Formula> Requirement::* member_of(FormulaField field) {
    switch (field) {
        case FormulaField::P:
            return &Requirement::m_p;
        case FormulaField::S:
            return &Requirement::m_s;
        case FormulaField::Q:
            return &Requirement::m_q;
        case FormulaField::R:
            return &Requirement::m_r;
    }
    assert(false);
    __builtin_unreachable();
}

bool reads(FormulaField field, const Pattern& pattern, PspScopeKind scope) {
    switch (field) {
        case FormulaField::P:
            return pattern_uses_p(pattern);
        case FormulaField::S:
            return pattern_uses_s(pattern);
        case FormulaField::Q:
            return scope_uses_q(scope);
        case FormulaField::R:
            return scope_uses_r(scope);
    }
    assert(false);
    __builtin_unreachable();
}

bool equivalent(const Formula& lhs, const Formula& rhs) {
    return lhs == rhs ||
           (formula_implies(lhs, rhs) && formula_implies(rhs, lhs));
}

// @p lhs and @p rhs under `&` (@p conjoin) or `|`, or the one that already is
// that combination where the two are comparable.
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

// A pattern of the same alternative as @p pattern with count @p count.
Pattern with_count(const Pattern& pattern, std::size_t count) {
    return std::visit(
        [count](const auto& alternative) -> Pattern {
            using T = std::decay_t<decltype(alternative)>;
            if constexpr (k_bounded_pattern<T>) {
                return T{count};
            } else {
                return alternative;
            }
        },
        pattern);
}

// The bound of two patterns under @p in_class: the weaker (Upper) or
// stronger (Lower) where they are comparable, and the count bound where they
// are one bounded pattern.
std::optional<Pattern> bound_pattern(const Pattern& lhs, const Pattern& rhs,
                                     ScopeClass in_class, Bound bound) {
    if (lhs == rhs) {
        return lhs;
    }
    const bool lhs_implies = pattern_implies(lhs, rhs, in_class);
    const bool rhs_implies = pattern_implies(rhs, lhs, in_class);
    if (lhs_implies) {
        return bound == Bound::Upper ? rhs : lhs;
    }
    if (rhs_implies) {
        return bound == Bound::Upper ? lhs : rhs;
    }
    return std::nullopt;
}

std::optional<PspScopeKind> bound_scope(PspScopeKind lhs, PspScopeKind rhs,
                                        PatternCell cell, Bound bound) {
    if (lhs == rhs) {
        return lhs;
    }
    if (scope_implies(lhs, rhs, cell)) {
        return bound == Bound::Upper ? rhs : lhs;
    }
    if (scope_implies(rhs, lhs, cell)) {
        return bound == Bound::Upper ? lhs : rhs;
    }
    return std::nullopt;
}

// The field-wise bound of two incomparable requirements, unchecked. A field
// the bound reads takes the combination of both sides' values where both read
// it, along its polarity, and the one side's value where one does.
std::optional<Requirement> combine_fields(const Requirement& lhs,
                                          const Requirement& rhs, Bound bound) {
    if (pattern_cell(lhs.m_pattern) != pattern_cell(rhs.m_pattern) &&
        lhs.m_scope != rhs.m_scope) {
        return std::nullopt;
    }
    const std::optional<PspScopeKind> scope = bound_scope(
        lhs.m_scope, rhs.m_scope, pattern_cell(lhs.m_pattern), bound);
    if (!scope.has_value()) {
        return std::nullopt;
    }
    const std::optional<Pattern> pattern =
        bound_pattern(lhs.m_pattern, rhs.m_pattern, scope_class(*scope), bound);
    if (!pattern.has_value()) {
        return std::nullopt;
    }
    std::array<std::optional<Formula>, 4> values;
    for (std::size_t idx = 0; idx < k_formula_fields.size(); ++idx) {
        const FormulaField field = k_formula_fields[idx];
        const std::optional<Polarity> polarity =
            field_polarity(field, *pattern, *scope);
        if (!polarity.has_value()) {
            continue;
        }
        const std::optional<Formula>& left = lhs.*member_of(field);
        const std::optional<Formula>& right = rhs.*member_of(field);
        if (!left.has_value() || !right.has_value()) {
            values[idx] = left.has_value() ? left : right;
            if (!values[idx].has_value()) {
                return std::nullopt;
            }
            continue;
        }
        if (*polarity == Polarity::None) {
            if (!equivalent(*left, *right)) {
                return std::nullopt;
            }
            values[idx] = left;
            continue;
        }
        // A monotone field weakens the requirement as it weakens, so the join
        // takes the disjunction and the meet the conjunction; an antitone
        // field the other way round.
        const bool conjoin =
            (*polarity == Polarity::Monotone) == (bound == Bound::Lower);
        values[idx] = combine(*left, *right, conjoin);
    }
    return Requirement(*pattern, *scope, values[0], values[1], values[2],
                       values[3]);
}

std::optional<Requirement> requirement_bound(const Requirement& lhs,
                                             const Requirement& rhs,
                                             Bound bound) {
    if (lhs == rhs) {
        return lhs;
    }
    if (lhs.m_removed || rhs.m_removed || !lhs.m_weakenable ||
        !rhs.m_weakenable || lhs.is_fixed() || rhs.is_fixed()) {
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

// @p base with @p field taken from @p source. A pattern or scope move keeps
// each formula @p base reads and the new keyword still reads, takes from
// @p source a formula only the new keyword reads, and drops one it no longer
// reads. A formula move on a field @p base does not read is nothing.
std::optional<Requirement> with_field(const Requirement& base,
                                      const Requirement& source,
                                      RequirementField field) {
    Pattern pattern = base.m_pattern;
    PspScopeKind scope = base.m_scope;
    std::array<std::optional<Formula>, 4> values = {base.m_p, base.m_s,
                                                    base.m_q, base.m_r};
    switch (field) {
        case RequirementField::Pattern:
            pattern = source.m_pattern;
            break;
        case RequirementField::Scope:
            scope = source.m_scope;
            break;
        case RequirementField::P:
        case RequirementField::S:
        case RequirementField::Q:
        case RequirementField::R: {
            const auto idx = static_cast<std::size_t>(field) -
                             static_cast<std::size_t>(RequirementField::P);
            if (!values[idx].has_value() ||
                !(source.*member_of(k_formula_fields[idx])).has_value()) {
                return std::nullopt;
            }
            values[idx] = source.*member_of(k_formula_fields[idx]);
            break;
        }
    }
    for (std::size_t idx = 0; idx < k_formula_fields.size(); ++idx) {
        const FormulaField formula = k_formula_fields[idx];
        if (!reads(formula, pattern, scope)) {
            values[idx].reset();
        } else if (!values[idx].has_value()) {
            values[idx] = source.*member_of(formula);
            if (!values[idx].has_value()) {
                return std::nullopt;
            }
        }
    }
    return Requirement(pattern, scope, values[0], values[1], values[2],
                       values[3], base.m_weakenable, base.m_removed);
}

bool field_differs(const Requirement& lhs, const Requirement& rhs,
                   RequirementField field) {
    switch (field) {
        case RequirementField::Pattern:
            return !(lhs.m_pattern == rhs.m_pattern);
        case RequirementField::Scope:
            return lhs.m_scope != rhs.m_scope;
        case RequirementField::P:
            return !(lhs.m_p == rhs.m_p);
        case RequirementField::S:
            return !(lhs.m_s == rhs.m_s);
        case RequirementField::Q:
            return !(lhs.m_q == rhs.m_q);
        case RequirementField::R:
            return !(lhs.m_r == rhs.m_r);
    }
    return false;
}

// A result with as many entries on each side as @p n_assumptions and
// @p n_guarantees, proved stronger than @p current, or nothing.
std::optional<Specification> proved_stronger(
    const Specification& current, std::vector<Requirement> assumptions,
    std::vector<Requirement> guarantees) {
    const std::size_t n_assumptions = assumptions.size();
    const std::size_t n_guarantees = guarantees.size();
    Specification result(std::move(assumptions), std::move(guarantees),
                         current.m_in_atoms, current.m_out_atoms);
    if (result.m_assumptions.size() != n_assumptions ||
        result.m_guarantees.size() != n_guarantees ||
        classify_move(current, result) != MoveOrder::Stronger) {
        return std::nullopt;
    }
    return result;
}

// As the FRETISH StepCollector in src/genetic/order_moves.cpp.
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
        if (current.m_removed || !current.m_weakenable || current.is_fixed() ||
            current == original) {
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
            const std::optional<Requirement> stepped =
                with_field(current, *target, field);
            if (stepped.has_value() && stepped->m_ltl != current.m_ltl) {
                try_step(StepKind::Field, which, slot, field, *stepped);
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
        std::optional<Specification> result = proved_stronger(
            m_current, std::move(assumptions), std::move(guarantees));
        if (result.has_value()) {
            m_steps.push_back(
                Step{kind, which, slot, field, std::move(*result)});
        }
    }

    const Specification& m_current;
    const Specification& m_original;
    std::vector<Step> m_steps;
};

// Every pattern up to count @p max_count: the base patterns and each bounded
// one at counts 1 to @p max_count.
std::vector<Pattern> pattern_universe(std::size_t max_count) {
    std::vector<Pattern> universe = {
        pattern::Absence{}, pattern::Universality{}, pattern::Existence{},
        pattern::Precedence{}, pattern::Response{}};
    for (std::size_t count = 1; count <= max_count; ++count) {
        universe.emplace_back(pattern::BoundedResponse{count});
        universe.emplace_back(pattern::BoundedInvariance{count});
        universe.emplace_back(pattern::BoundedExistence{count});
        universe.emplace_back(pattern::BoundedRecurrence{count});
    }
    return universe;
}

// Jaccard of the two membership vectors.
double jaccard(const std::vector<bool>& lhs, const std::vector<bool>& rhs) {
    std::size_t both = 0;
    std::size_t either = 0;
    for (std::size_t idx = 0; idx < lhs.size(); ++idx) {
        both += lhs[idx] && rhs[idx] ? 1 : 0;
        either += lhs[idx] || rhs[idx] ? 1 : 0;
    }
    return either == 0
               ? 1.0
               : static_cast<double>(both) / static_cast<double>(either);
}

double pattern_downset_similarity(const Pattern& lhs, const Pattern& rhs,
                                  ScopeClass in_class) {
    const auto max_count = std::max<std::size_t>(
        {1, pattern_count(lhs).value_or(0), pattern_count(rhs).value_or(0)});
    const std::vector<Pattern> universe = pattern_universe(max_count);
    std::vector<bool> lhs_down(universe.size());
    std::vector<bool> rhs_down(universe.size());
    for (std::size_t idx = 0; idx < universe.size(); ++idx) {
        lhs_down[idx] = pattern_implies(lhs, universe[idx], in_class);
        rhs_down[idx] = pattern_implies(rhs, universe[idx], in_class);
    }
    return jaccard(lhs_down, rhs_down);
}

double scope_downset_similarity(PspScopeKind lhs, PspScopeKind rhs,
                                PatternCell cell) {
    std::vector<bool> lhs_down(k_scopes.size());
    std::vector<bool> rhs_down(k_scopes.size());
    for (std::size_t idx = 0; idx < k_scopes.size(); ++idx) {
        lhs_down[idx] = scope_implies(lhs, k_scopes[idx], cell);
        rhs_down[idx] = scope_implies(rhs, k_scopes[idx], cell);
    }
    return jaccard(lhs_down, rhs_down);
}

}  // namespace

MoveOrder classify_move(const Specification& parent,
                        const Specification& child) {
    if (parent == child) {
        return MoveOrder::Equal;
    }
    const bool child_implies = spec_implies_by_rule(child, parent);
    const bool parent_implies = spec_implies_by_rule(parent, child);
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

std::vector<Step> steps_towards(const Specification& current,
                                const Specification& original) {
    return StepCollector(current, original).collect();
}

std::vector<std::size_t> count_chain(std::size_t from, std::size_t dest) {
    std::vector<std::size_t> chain;
    for (std::size_t count = from; count != dest;) {
        count = count < dest ? count + 1 : count - 1;
        chain.push_back(count);
    }
    return chain;
}

std::vector<std::size_t> count_step_chain(const Specification& current,
                                          const Step& step) {
    if (step.m_kind != StepKind::Field ||
        step.m_field != RequirementField::Pattern) {
        return {};
    }
    const auto side_of =
        [&step](const Specification& spec) -> const std::vector<Requirement>& {
        return step.m_side == StepSide::Assumption ? spec.m_assumptions
                                                   : spec.m_guarantees;
    };
    const Requirement& from = side_of(current)[step.m_slot];
    const Requirement& dest = side_of(step.m_result)[step.m_slot];
    if (from.m_pattern.index() != dest.m_pattern.index()) {
        return {};
    }
    const std::optional<std::size_t> from_count = pattern_count(from.m_pattern);
    const std::optional<std::size_t> dest_count = pattern_count(dest.m_pattern);
    if (!from_count.has_value() || !dest_count.has_value()) {
        return {};
    }
    return count_chain(*from_count, *dest_count);
}

std::optional<Specification> with_slot_count(const Specification& current,
                                             StepSide side, std::size_t slot,
                                             std::size_t count) {
    std::vector<Requirement> assumptions = current.m_assumptions;
    std::vector<Requirement> guarantees = current.m_guarantees;
    std::vector<Requirement>& edited =
        side == StepSide::Assumption ? assumptions : guarantees;
    assert(slot < edited.size());
    const Requirement& base = edited[slot];
    if (base.is_fixed() || !pattern_count(base.m_pattern).has_value()) {
        return std::nullopt;
    }
    edited[slot] = Requirement(with_count(base.m_pattern, count), base.m_scope,
                               base.m_p, base.m_s, base.m_q, base.m_r,
                               base.m_weakenable, base.m_removed);
    return proved_stronger(current, std::move(assumptions),
                           std::move(guarantees));
}

double keyword_similarity(const Requirement& lhs, const Requirement& rhs) {
    if (lhs.is_fixed() || rhs.is_fixed()) {
        return lhs.m_ltl == rhs.m_ltl ? 1.0 : 0.0;
    }
    return (pattern_downset_similarity(lhs.m_pattern, rhs.m_pattern,
                                       scope_class(lhs.m_scope)) +
            scope_downset_similarity(lhs.m_scope, rhs.m_scope,
                                     pattern_cell(lhs.m_pattern))) /
           2.0;
}

double keyword_token_similarity(const Requirement& lhs,
                                const Requirement& rhs) {
    if (lhs.is_fixed() || rhs.is_fixed()) {
        return lhs.m_ltl == rhs.m_ltl ? 1.0 : 0.0;
    }
    const double same_pattern = lhs.m_pattern == rhs.m_pattern ? 1.0 : 0.0;
    const double same_scope = lhs.m_scope == rhs.m_scope ? 1.0 : 0.0;
    return (same_pattern + same_scope) / 2.0;
}

namespace {

// @p formula with each constant replaced by a tautology or a contradiction
// over a fresh atom. formula_implies counts the constants as atoms, as the
// model counter it stands in for does, so without this it proves neither
// `false -> r` nor `q -> true`, and a rewrite of Q to `false` is the gutting
// move that monotone weakening makes most often.
Formula without_constants(const Formula& formula) {
    const Formula fresh = Formula::make_atom("psp_gutting_constant");
    const Formula negated = Formula::make_unary(Formula::Kind::Not, fresh);
    return formula.rewrite_post_order(
        [&fresh, &negated](const Formula& sub) -> std::optional<Formula> {
            const std::optional<std::string> name = sub.atom_name();
            if (name == std::optional<std::string>("true")) {
                return Formula::make_binary(Formula::Kind::Or, fresh, negated);
            }
            if (name == std::optional<std::string>("false")) {
                return Formula::make_binary(Formula::Kind::And, fresh, negated);
            }
            return std::nullopt;
        });
}

bool implies_exactly(const Formula& stronger, const Formula& weaker) {
    return formula_implies(without_constants(stronger),
                           without_constants(weaker));
}

}  // namespace

bool empties_scope(const Requirement& req) {
    if (req.is_fixed()) {
        return false;
    }
    switch (req.m_scope) {
        case PspScopeKind::Globally:
            return false;
        case PspScopeKind::Before:
            return req.m_r.has_value() &&
                   implies_exactly(Formula("true"), *req.m_r);
        case PspScopeKind::After:
            return req.m_q.has_value() &&
                   implies_exactly(*req.m_q, Formula("false"));
        case PspScopeKind::Between:
        case PspScopeKind::AfterUntil:
            return req.m_q.has_value() && req.m_r.has_value() &&
                   implies_exactly(*req.m_q, *req.m_r);
    }
    assert(false);
    __builtin_unreachable();
}

std::vector<std::size_t> gutted_guarantees(const Specification& original,
                                           const Specification& repair) {
    std::vector<std::size_t> gutted;
    for (std::size_t slot = 0; slot < repair.m_guarantees.size(); ++slot) {
        const Requirement& req = repair.m_guarantees[slot];
        if (req.m_removed || !empties_scope(req)) {
            continue;
        }
        const bool was_empty = slot < original.m_guarantees.size() &&
                               !original.m_guarantees[slot].m_removed &&
                               empties_scope(original.m_guarantees[slot]);
        if (!was_empty) {
            gutted.push_back(slot);
        }
    }
    return gutted;
}

}  // namespace psp
