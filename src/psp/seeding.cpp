#include "psp/seeding.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "prop_formula.hpp"
#include "prop_formula/atoms.hpp"
#include "psp/implication.hpp"
#include "psp/order_moves.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"

namespace psp {

namespace {

constexpr std::array<PspScopeKind, 5> k_scopes = {
    PspScopeKind::Globally, PspScopeKind::Before, PspScopeKind::After,
    PspScopeKind::Between, PspScopeKind::AfterUntil};

constexpr std::array<FormulaField, 4> k_fields = {
    FormulaField::P, FormulaField::S, FormulaField::Q, FormulaField::R};

constexpr std::array<const char*, 4> k_field_names = {"p", "s", "q", "r"};

using Fields = std::array<std::optional<Formula>, 4>;

Fields fields_of(const Requirement& req) {
    return {req.m_p, req.m_s, req.m_q, req.m_r};
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

Requirement build(const Pattern& pattern, PspScopeKind scope,
                  const Fields& values) {
    return {pattern,   scope,     values[0], values[1],
            values[2], values[3], true,      false};
}

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

// What the edits draw on: the specification's counts, the formulas each role
// reads somewhere, and the atoms by kind.
struct Pools {
    std::vector<std::size_t> counts;
    std::array<std::vector<Formula>, 4> by_field;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
};

void add_unique(std::vector<Formula>& pool, const Formula& formula) {
    if (std::find(pool.begin(), pool.end(), formula) == pool.end()) {
        pool.push_back(formula);
    }
}

Pools pools_of(const Specification& spec) {
    Pools pools;
    pools.inputs = spec.m_in_atoms;
    pools.outputs = spec.m_out_atoms;
    for (const std::vector<Requirement>* list :
         {&spec.m_assumptions, &spec.m_guarantees}) {
        for (const Requirement& req : *list) {
            if (req.m_removed || req.is_fixed()) {
                continue;
            }
            const Fields values = fields_of(req);
            for (std::size_t idx = 0; idx < values.size(); ++idx) {
                if (values[idx].has_value()) {
                    add_unique(pools.by_field[idx], *values[idx]);
                }
            }
            if (const std::optional<std::size_t> count =
                    pattern_count(req.m_pattern)) {
                pools.counts.push_back(*count);
            }
        }
    }
    // Q and R are both scope delimiters: either may take the other's
    // formulas, and an input atom.
    std::vector<Formula> delimiters = pools.by_field[2];
    for (const Formula& formula : pools.by_field[3]) {
        add_unique(delimiters, formula);
    }
    for (const std::string& atom : pools.inputs) {
        add_unique(delimiters, Formula::make_atom(atom));
    }
    pools.by_field[2] = delimiters;
    pools.by_field[3] = delimiters;
    for (const std::size_t idx : {std::size_t{0}, std::size_t{1}}) {
        if (pools.by_field[idx].empty()) {
            for (const std::string& atom : pools.inputs) {
                add_unique(pools.by_field[idx], Formula::make_atom(atom));
            }
            for (const std::string& atom : pools.outputs) {
                add_unique(pools.by_field[idx], Formula::make_atom(atom));
            }
        }
    }
    std::sort(pools.counts.begin(), pools.counts.end());
    pools.counts.erase(std::unique(pools.counts.begin(), pools.counts.end()),
                       pools.counts.end());
    return pools;
}

void atom_occurrences(const Formula& formula, std::vector<std::string>& out);

// The atoms of the fields in @p values at @p indices.
std::set<std::string> atoms_at(const Fields& values,
                               std::initializer_list<std::size_t> indices) {
    std::set<std::string> out;
    for (const std::size_t idx : indices) {
        if (values[idx].has_value()) {
            std::vector<std::string> names;
            atom_occurrences(*values[idx], names);
            out.insert(names.begin(), names.end());
        }
    }
    return out;
}

// Every assignment of the fields @p pattern and @p scope read: a field
// @p values holds keeps its value, and one it lacks takes each formula of its
// pool in turn. A donated Q or R shares no atom with P or S, since a trigger
// that also closes the scope leaves the requirement vacuous.
std::vector<Fields> fitted(const Fields& values, const Pattern& pattern,
                           PspScopeKind scope, const Pools& pools) {
    const std::set<std::string> body = atoms_at(values, {0, 1});
    std::vector<Fields> out = {Fields{}};
    for (std::size_t idx = 0; idx < k_fields.size(); ++idx) {
        if (!reads(k_fields[idx], pattern, scope)) {
            continue;
        }
        std::vector<Formula> options;
        if (values[idx].has_value()) {
            options.push_back(*values[idx]);
        } else {
            for (const Formula& option : pools.by_field[idx]) {
                std::vector<std::string> names;
                atom_occurrences(option, names);
                const bool shared =
                    idx >= 2 && std::any_of(names.begin(), names.end(),
                                            [&body](const std::string& name) {
                                                return body.count(name) != 0;
                                            });
                if (!shared) {
                    options.push_back(option);
                }
            }
        }
        std::vector<Fields> next;
        for (const Fields& partial : out) {
            for (const Formula& option : options) {
                Fields grown = partial;
                grown[idx] = option;
                next.push_back(grown);
            }
        }
        out = std::move(next);
    }
    return out;
}

// The patterns a pattern edit ranges over: the base patterns, and the bounded
// ones at the slot's count and each count of the specification (1 where
// there is none).
std::vector<Pattern> pattern_universe(const Pattern& pattern,
                                      const std::vector<std::size_t>& counts) {
    std::vector<std::size_t> at_counts = counts;
    if (const std::optional<std::size_t> own = pattern_count(pattern)) {
        at_counts.push_back(*own);
    }
    if (at_counts.empty()) {
        at_counts.push_back(1);
    }
    std::sort(at_counts.begin(), at_counts.end());
    at_counts.erase(std::unique(at_counts.begin(), at_counts.end()),
                    at_counts.end());
    std::vector<Pattern> universe = {
        pattern::Absence{}, pattern::Universality{}, pattern::Existence{},
        pattern::Precedence{}, pattern::Response{}};
    for (const std::size_t count : at_counts) {
        universe.emplace_back(pattern::BoundedResponse{count});
        universe.emplace_back(pattern::BoundedInvariance{count});
        universe.emplace_back(pattern::BoundedExistence{count});
        universe.emplace_back(pattern::BoundedRecurrence{count});
    }
    return universe;
}

// The entries of @p universe strictly stronger than @p from under
// @p implies with nothing of the universe strictly between: its upper covers.
template <typename T, typename Implies>
std::vector<T> upper_covers(const T& from, const std::vector<T>& universe,
                            const Implies& implies) {
    const auto strictly = [&implies](const T& stronger, const T& weaker) {
        return implies(stronger, weaker) && !implies(weaker, stronger);
    };
    std::vector<T> above;
    for (const T& candidate : universe) {
        if (strictly(candidate, from)) {
            above.push_back(candidate);
        }
    }
    std::vector<T> result;
    for (const T& candidate : above) {
        const bool covered =
            std::any_of(above.begin(), above.end(), [&](const T& between) {
                return strictly(candidate, between) && strictly(between, from);
            });
        if (!covered) {
            result.push_back(candidate);
        }
    }
    return result;
}

// The entries of @p universe that @p implies does not compare with @p from
// in either direction.
template <typename T, typename Implies>
std::vector<T> incomparable(const T& from, const std::vector<T>& universe,
                            const Implies& implies) {
    std::vector<T> result;
    for (const T& candidate : universe) {
        if (!implies(candidate, from) && !implies(from, candidate)) {
            result.push_back(candidate);
        }
    }
    return result;
}

// The atom occurrences of @p formula, in pre-order, constants excepted.
void atom_occurrences(const Formula& formula, std::vector<std::string>& out) {
    if (const std::optional<std::string> name = formula.atom_name()) {
        if (!is_constant_atom(*name)) {
            out.push_back(*name);
        }
        return;
    }
    if (const std::optional<Formula> child = formula.unary_child()) {
        atom_occurrences(*child, out);
        return;
    }
    if (const auto children = formula.binary_children()) {
        atom_occurrences(children->first, out);
        atom_occurrences(children->second, out);
    }
}

// @p formula with its @p target-th atom occurrence (pre-order, constants
// excepted) replaced by @p replace's result. A negated occurrence passes its
// negation node to @p replace, so a flip can remove it.
Formula rewrite_occurrence(
    const Formula& formula, std::size_t target, std::size_t& seen,
    const std::function<Formula(const Formula&)>& replace) {
    if (const std::optional<std::string> name = formula.atom_name()) {
        if (!is_constant_atom(*name) && seen++ == target) {
            return replace(formula);
        }
        return formula;
    }
    if (const std::optional<Formula> child = formula.unary_child()) {
        if (const std::optional<std::string> name = child->atom_name()) {
            if (formula.kind() == Formula::Kind::Not &&
                !is_constant_atom(*name) && seen == target) {
                ++seen;
                return replace(formula);
            }
        }
        return Formula::make_unary(
            formula.kind(), rewrite_occurrence(*child, target, seen, replace));
    }
    if (const auto children = formula.binary_children()) {
        const Formula left =
            rewrite_occurrence(children->first, target, seen, replace);
        const Formula right =
            rewrite_occurrence(children->second, target, seen, replace);
        return Formula::make_binary(formula.kind(), left, right);
    }
    return formula;
}

Formula at_occurrence(const Formula& formula, std::size_t target,
                      const std::function<Formula(const Formula&)>& replace) {
    std::size_t seen = 0;
    return rewrite_occurrence(formula, target, seen, replace);
}

// The atoms a field's literals draw from: a trigger and the delimiters are
// read at the current point, so the inputs (where there are any); a body
// every atom.
std::vector<std::string> literal_atoms(FormulaField field,
                                       const Pattern& pattern,
                                       const Pools& pools) {
    const bool condition =
        field == FormulaField::Q || field == FormulaField::R ||
        (field == FormulaField::P && pattern_uses_s(pattern));
    std::vector<std::string> atoms = pools.inputs;
    if (!condition || atoms.empty()) {
        atoms.insert(atoms.end(), pools.outputs.begin(), pools.outputs.end());
    }
    return atoms;
}

struct Candidate {
    std::string edit;
    std::string field;
    Requirement requirement;
};

void pattern_edits(const Requirement& req, const Pools& pools,
                   std::vector<Candidate>& out) {
    const ScopeClass in_class = scope_class(req.m_scope);
    const auto implies = [in_class](const Pattern& lhs, const Pattern& rhs) {
        return pattern_implies(lhs, rhs, in_class);
    };
    const std::vector<Pattern> universe =
        pattern_universe(req.m_pattern, pools.counts);
    for (const Pattern& moved :
         upper_covers(req.m_pattern, universe, implies)) {
        for (const Fields& values :
             fitted(fields_of(req), moved, req.m_scope, pools)) {
            out.push_back(
                {"pattern-step", "pattern", build(moved, req.m_scope, values)});
        }
    }
    for (const Pattern& moved :
         incomparable(req.m_pattern, universe, implies)) {
        if (moved.index() == req.m_pattern.index() ||
            pattern_uses_p(moved) != pattern_uses_p(req.m_pattern) ||
            pattern_uses_s(moved) != pattern_uses_s(req.m_pattern)) {
            continue;
        }
        out.push_back({"pattern-swap", "pattern",
                       build(moved, req.m_scope, fields_of(req))});
    }
}

void scope_edits(const Requirement& req, const Pools& pools,
                 std::vector<Candidate>& out) {
    const PatternCell cell = pattern_cell(req.m_pattern);
    const auto implies = [cell](PspScopeKind lhs, PspScopeKind rhs) {
        return scope_implies(lhs, rhs, cell);
    };
    const std::vector<PspScopeKind> universe(k_scopes.begin(), k_scopes.end());
    for (const PspScopeKind moved :
         upper_covers(req.m_scope, universe, implies)) {
        for (const Fields& values :
             fitted(fields_of(req), req.m_pattern, moved, pools)) {
            out.push_back(
                {"scope-step", "scope", build(req.m_pattern, moved, values)});
        }
    }
    for (const PspScopeKind moved :
         incomparable(req.m_scope, universe, implies)) {
        if (moved == req.m_scope) {
            continue;
        }
        Fields values = fields_of(req);
        const bool want_q = scope_uses_q(moved);
        const bool want_r = scope_uses_r(moved);
        // A one-delimiter scope read with the other delimiter: before R read
        // as after R, and the reverse.
        if (want_q && !want_r && !values[2].has_value()) {
            values[2] = values[3];
        }
        if (want_r && !want_q && !values[3].has_value()) {
            values[3] = values[2];
        }
        for (const Fields& fitted_values :
             fitted(values, req.m_pattern, moved, pools)) {
            if (want_q && want_r && fitted_values[2] == fitted_values[3]) {
                continue;  // Q = R empties the scope.
            }
            out.push_back({"scope-swap", "scope",
                           build(req.m_pattern, moved, fitted_values)});
        }
    }
    if (scope_uses_q(req.m_scope) && scope_uses_r(req.m_scope)) {
        Fields values = fields_of(req);
        std::swap(values[2], values[3]);
        out.push_back({"swap-delimiters", "q,r",
                       build(req.m_pattern, req.m_scope, values)});
    }
}

void count_edits(const Requirement& req, std::vector<Candidate>& out) {
    const std::optional<std::size_t> count = pattern_count(req.m_pattern);
    if (!count.has_value()) {
        return;
    }
    const bool grows_weaker =
        !std::holds_alternative<pattern::BoundedInvariance>(req.m_pattern);
    std::set<std::size_t> moved;
    if (grows_weaker) {
        moved = {*count - 1, *count / 2};
    } else {
        moved = {*count + 1, *count * 2};
    }
    for (const std::size_t next : moved) {
        if (next >= 1 && next <= k_max_count && next != *count) {
            out.push_back({"count-step", "count",
                           build(with_count(req.m_pattern, next), req.m_scope,
                                 fields_of(req))});
        }
    }
}

void formula_edits(const Requirement& req, const Pools& pools,
                   std::vector<Candidate>& out) {
    const Fields values = fields_of(req);
    for (std::size_t idx = 0; idx < k_fields.size(); ++idx) {
        if (!values[idx].has_value()) {
            continue;
        }
        const Formula& field = *values[idx];
        std::vector<std::string> occurrences;
        atom_occurrences(field, occurrences);
        const auto with_field = [&](const Formula& replaced) {
            Fields edited = values;
            edited[idx] = replaced;
            return build(req.m_pattern, req.m_scope, edited);
        };
        const std::optional<Polarity> polarity =
            field_polarity(k_fields[idx], req.m_pattern, req.m_scope);
        // A literal over a delimiter's atom is often absorbed by the scope
        // (`cruise & !brake` where brake closes it), so the literals avoid
        // them.
        const std::set<std::string> delimiters = atoms_at(values, {2, 3});
        if (polarity.has_value() && *polarity != Polarity::None) {
            const Formula::Kind join = *polarity == Polarity::Monotone
                                           ? Formula::Kind::And
                                           : Formula::Kind::Or;
            for (const std::string& atom :
                 literal_atoms(k_fields[idx], req.m_pattern, pools)) {
                if (std::find(occurrences.begin(), occurrences.end(), atom) !=
                        occurrences.end() ||
                    delimiters.count(atom) != 0) {
                    continue;
                }
                const Formula positive = Formula::make_atom(atom);
                for (const Formula& literal :
                     {positive,
                      Formula::make_unary(Formula::Kind::Not, positive)}) {
                    out.push_back({"narrow-field", k_field_names[idx],
                                   with_field(Formula::make_binary(join, field,
                                                                   literal))});
                }
            }
        }
        for (std::size_t occ = 0; occ < occurrences.size(); ++occ) {
            out.push_back(
                {"flip-literal", k_field_names[idx],
                 with_field(at_occurrence(field, occ, [](const Formula& node) {
                     if (const std::optional<Formula> child =
                             node.unary_child()) {
                         return *child;
                     }
                     return Formula::make_unary(Formula::Kind::Not, node);
                 }))});
        }
        for (std::size_t occ = 0; occ < occurrences.size(); ++occ) {
            const std::string& name = occurrences[occ];
            const bool is_input =
                std::find(pools.inputs.begin(), pools.inputs.end(), name) !=
                pools.inputs.end();
            const std::vector<std::string>& kind =
                is_input ? pools.inputs : pools.outputs;
            for (const std::string& other : kind) {
                if (other == name) {
                    continue;
                }
                out.push_back(
                    {"swap-signal", k_field_names[idx],
                     with_field(at_occurrence(
                         field, occ, [&other](const Formula& node) {
                             const Formula atom = Formula::make_atom(other);
                             return node.kind() == Formula::Kind::Not
                                        ? Formula::make_unary(
                                              Formula::Kind::Not, atom)
                                        : atom;
                         }))});
            }
        }
    }
}

}  // namespace

const char* seed_relation_name(SeedRelation relation) {
    switch (relation) {
        case SeedRelation::Stronger:
            return "stronger";
        case SeedRelation::Sideways:
            return "sideways";
    }
    assert(false);
    __builtin_unreachable();
}

std::vector<SeededEdit> seeded_edits(const Specification& base) {
    const Pools pools = pools_of(base);
    std::vector<SeededEdit> result;
    for (std::size_t slot = 0; slot < base.m_guarantees.size(); ++slot) {
        const Requirement& req = base.m_guarantees[slot];
        if (req.m_removed || req.is_fixed() || !req.m_weakenable) {
            continue;
        }
        std::vector<Candidate> candidates;
        pattern_edits(req, pools, candidates);
        scope_edits(req, pools, candidates);
        count_edits(req, candidates);
        formula_edits(req, pools, candidates);
        std::vector<Requirement> seen;
        for (Candidate& candidate : candidates) {
            const Requirement& edited = candidate.requirement;
            if (edited == req || requirement_implies(req, edited) ||
                (empties_scope(edited) && !empties_scope(req)) ||
                std::find(seen.begin(), seen.end(), edited) != seen.end()) {
                continue;
            }
            const bool duplicates =
                std::any_of(base.m_guarantees.begin(), base.m_guarantees.end(),
                            [&edited](const Requirement& other) {
                                return !other.m_removed && other == edited;
                            });
            if (duplicates) {
                continue;
            }
            seen.push_back(edited);
            const SeedRelation relation = requirement_implies(edited, req)
                                              ? SeedRelation::Stronger
                                              : SeedRelation::Sideways;
            result.push_back({slot, std::move(candidate.edit),
                              std::move(candidate.field), edited, relation});
        }
    }
    return result;
}

Specification apply_seeded_edit(const Specification& base,
                                const SeededEdit& edit) {
    assert(edit.m_slot < base.m_guarantees.size());
    Specification out = base;
    out.m_guarantees[edit.m_slot] = edit.m_requirement;
    return out;
}

}  // namespace psp
