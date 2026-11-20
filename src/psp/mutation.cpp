#include "psp/mutation.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "config.hpp"
#include "genetic/crossover.hpp"
#include "genetic/monotone.hpp"
#include "genetic/mutation.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "psp/implication.hpp"
#include "psp/ordered_language.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"
#include "runner/spot.hpp"

namespace psp {

namespace {

constexpr std::array<PspScopeKind, 5> k_scopes = {
    PspScopeKind::Globally, PspScopeKind::Before, PspScopeKind::After,
    PspScopeKind::Between, PspScopeKind::AfterUntil};

constexpr std::array<FormulaField, 4> k_formula_fields = {
    FormulaField::P, FormulaField::S, FormulaField::Q, FormulaField::R};

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

// The four formula fields of a requirement, P, S, Q and R, by value.
using Fields = std::array<std::optional<Formula>, 4>;

Fields fields_of(const Requirement& req) {
    return {req.m_p, req.m_s, req.m_q, req.m_r};
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

// The patterns a pattern move ranges over: the base patterns, and the bounded
// ones at @p pattern's count and at each donated count.
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

// The covers of @p from in @p universe under @p implies, in @p direction:
// the entries strictly weaker (Weaken) or stronger (Strengthen) than @p from
// with nothing of the universe strictly between.
template <typename T, typename Implies>
std::vector<T> covers(const T& from, const std::vector<T>& universe,
                      Direction direction, const Implies& implies) {
    const auto strictly = [&implies](const T& stronger, const T& weaker) {
        return implies(stronger, weaker) && !implies(weaker, stronger);
    };
    const auto beyond = [&](const T& lhs, const T& rhs) {
        return direction == Direction::Weaken ? strictly(lhs, rhs)
                                              : strictly(rhs, lhs);
    };
    std::vector<T> moved;
    for (const T& candidate : universe) {
        if (beyond(from, candidate)) {
            moved.push_back(candidate);
        }
    }
    std::vector<T> result;
    for (const T& candidate : moved) {
        const bool covered =
            std::any_of(moved.begin(), moved.end(), [&](const T& between) {
                return beyond(between, candidate) && beyond(from, between);
            });
        if (!covered) {
            result.push_back(candidate);
        }
    }
    return result;
}

Pattern move_pattern(const Pattern& pattern, ScopeClass in_class,
                     Direction direction,
                     const std::vector<std::size_t>& counts,
                     const RandomSource& random_source) {
    const std::vector<Pattern> moved =
        covers(pattern, pattern_universe(pattern, counts), direction,
               [in_class](const Pattern& lhs, const Pattern& rhs) {
                   return pattern_implies(lhs, rhs, in_class);
               });
    if (moved.empty()) {
        return pattern;
    }
    return moved[random_source.next_index(moved.size())];
}

PspScopeKind move_scope(PspScopeKind scope, PatternCell cell,
                        Direction direction,
                        const RandomSource& random_source) {
    const std::vector<PspScopeKind> universe(k_scopes.begin(), k_scopes.end());
    const std::vector<PspScopeKind> moved = covers(
        scope, universe, direction, [cell](PspScopeKind lhs, PspScopeKind rhs) {
            return scope_implies(lhs, rhs, cell);
        });
    if (moved.empty()) {
        return scope;
    }
    return moved[random_source.next_index(moved.size())];
}

// The count arm: halved or doubled, or a donated count, in the direction's
// sense. Bounded invariance strengthens as its count grows and the other
// three weaken.
Pattern move_count(const Pattern& pattern, Direction direction,
                   const std::vector<std::size_t>& counts,
                   const RandomSource& random_source) {
    const std::size_t count = pattern_count(pattern).value_or(1);
    const bool grows_weaker =
        !std::holds_alternative<pattern::BoundedInvariance>(pattern);
    const bool grows = (direction == Direction::Weaken) == grows_weaker;
    std::vector<std::size_t> candidates;
    const std::size_t scaled = grows ? std::min(count * 2, k_max_count)
                                     : std::max<std::size_t>(1, count / 2);
    if (scaled != count) {
        candidates.push_back(scaled);
    }
    for (const std::size_t donated : counts) {
        if (donated >= 1 && donated <= k_max_count &&
            (grows ? donated > count : donated < count)) {
            candidates.push_back(donated);
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());
    if (candidates.empty()) {
        return pattern;
    }
    return with_count(pattern,
                      candidates[random_source.next_index(candidates.size())]);
}

// Fills each field @p pattern and @p scope read and @p values lacks with a
// donated formula, and drops each it no longer reads.
void fit_fields(Fields& values, const Pattern& pattern, PspScopeKind scope,
                const std::vector<Formula>& formulas,
                const RandomSource& random_source) {
    for (std::size_t idx = 0; idx < k_formula_fields.size(); ++idx) {
        if (!reads(k_formula_fields[idx], pattern, scope)) {
            values[idx].reset();
        } else if (!values[idx].has_value()) {
            PspMoveStats::n_donated++;
            values[idx] =
                formulas.empty()
                    ? Formula("true")
                    : formulas[random_source.next_index(formulas.size())];
        }
    }
}

// The monotone direction a field of polarity @p polarity must move for the
// requirement to move @p direction, or nothing where it has none.
std::optional<MonotoneDirection> field_direction(
    std::optional<Polarity> polarity, Direction direction) {
    if (!polarity.has_value() || *polarity == Polarity::None) {
        return std::nullopt;
    }
    const bool weaken =
        (direction == Direction::Weaken) == (*polarity == Polarity::Monotone);
    return weaken ? MonotoneDirection::Weaken : MonotoneDirection::Strengthen;
}

// As rewrite_field in src/genetic/mutation.cpp: the monotone form where the
// field has a direction and p_monotone fires, else a free rewrite.
Formula rewrite_formula(const Formula& field,
                        const std::vector<std::string>& atoms,
                        std::optional<MonotoneDirection> direction,
                        const Config& cfg, const RandomSource& random_source,
                        bool* free_rewrite) {
    if (cfg.p_monotone > 0.0 && direction.has_value() && !atoms.empty() &&
        random_source.next_real() < cfg.p_monotone) {
        PspMoveStats::n_monotone++;
        return monotone_rewrite(field, *direction, atoms, random_source);
    }
    if (free_rewrite != nullptr) {
        *free_rewrite = true;
    }
    PspMoveStats::n_free++;
    return mutate_formula(field, atoms, random_source);
}

double field_rate(const Config& cfg, FormulaField field) {
    switch (field) {
        case FormulaField::P:
            return cfg.psp_p_p;
        case FormulaField::S:
            return cfg.psp_p_s;
        case FormulaField::Q:
            return cfg.psp_p_q;
        case FormulaField::R:
            return cfg.psp_p_r;
    }
    assert(false);
    __builtin_unreachable();
}

// The pool a field's rewrite draws atoms from. A trigger and the scope
// delimiters are read at the current point, so they draw from the inputs,
// as a FRETISH trigger does; a body draws from every atom.
const std::vector<std::string>& field_atoms(FormulaField field,
                                            const Pattern& pattern,
                                            const MutationPoolData& pools) {
    switch (field) {
        case FormulaField::P:
            return pattern_uses_s(pattern) ? pools.condition_atoms
                                           : pools.atoms;
        case FormulaField::S:
            return pools.atoms;
        case FormulaField::Q:
        case FormulaField::R:
            return pools.condition_atoms;
    }
    assert(false);
    __builtin_unreachable();
}

void collect_formulas(const std::vector<Requirement>& reqs,
                      std::vector<Formula>& formulas,
                      std::vector<std::size_t>& counts) {
    for (const Requirement& req : reqs) {
        if (req.m_removed || req.is_fixed()) {
            continue;
        }
        for (const std::optional<Formula>& field : fields_of(req)) {
            if (field.has_value()) {
                formulas.push_back(*field);
            }
        }
        if (const std::optional<std::size_t> count =
                pattern_count(req.m_pattern)) {
            counts.push_back(*count);
        }
    }
}

// Whether @p reqs holds an entry equal to the one at @p idx.
bool creates_duplicate(const std::vector<Requirement>& reqs, std::size_t idx) {
    for (std::size_t i = 0; i < reqs.size(); ++i) {
        if (i != idx && reqs[i] == reqs[idx]) {
            return true;
        }
    }
    return false;
}

// The fields a crossover of @p target with @p donor may take.
enum class CrossField : std::uint8_t { Pattern, Count, Scope, P, S, Q, R };

std::vector<CrossField> crossable_fields(const Requirement& target,
                                         const Requirement& donor) {
    std::vector<CrossField> fields;
    if (!(target.m_pattern == donor.m_pattern)) {
        fields.push_back(CrossField::Pattern);
    }
    if (target.m_pattern.index() == donor.m_pattern.index() &&
        pattern_count(target.m_pattern).has_value() &&
        !(target.m_pattern == donor.m_pattern)) {
        fields.push_back(CrossField::Count);
    }
    if (target.m_scope != donor.m_scope) {
        fields.push_back(CrossField::Scope);
    }
    const Fields lhs = fields_of(target);
    const Fields rhs = fields_of(donor);
    constexpr std::array<CrossField, 4> k_cross_formulas = {
        CrossField::P, CrossField::S, CrossField::Q, CrossField::R};
    for (std::size_t idx = 0; idx < lhs.size(); ++idx) {
        if (lhs[idx].has_value() && rhs[idx].has_value()) {
            fields.push_back(k_cross_formulas[idx]);
        }
    }
    return fields;
}

Requirement cross_field(const Requirement& target, const Requirement& donor,
                        CrossField field, const RandomSource& random_source) {
    Pattern pattern = target.m_pattern;
    PspScopeKind scope = target.m_scope;
    Fields values = fields_of(target);
    const Fields donated = fields_of(donor);
    switch (field) {
        case CrossField::Pattern:
            pattern = donor.m_pattern;
            break;
        case CrossField::Count:
            pattern =
                with_count(pattern, pattern_count(donor.m_pattern).value_or(1));
            break;
        case CrossField::Scope:
            scope = donor.m_scope;
            break;
        case CrossField::P:
        case CrossField::S:
        case CrossField::Q:
        case CrossField::R: {
            const auto idx = static_cast<std::size_t>(field) -
                             static_cast<std::size_t>(CrossField::P);
            const std::optional<Formula>& mine = values[idx];
            const std::optional<Formula>& theirs = donated[idx];
            if (mine.has_value() && theirs.has_value()) {
                Formula child =
                    crossover_formula(*mine, *theirs, random_source);
                child.remove_double_negation();
                values[idx] = std::move(child);
            }
            break;
        }
    }
    for (std::size_t idx = 0; idx < k_formula_fields.size(); ++idx) {
        if (!reads(k_formula_fields[idx], pattern, scope)) {
            values[idx].reset();
        } else if (!values[idx].has_value()) {
            values[idx] = donated[idx];
        }
    }
    return {pattern,   scope,     values[0],           values[1],
            values[2], values[3], target.m_weakenable, target.m_removed};
}

}  // namespace

MutationPoolData mutation_pool_data(const Specification& spec) {
    MutationPoolData pools;
    pools.atoms = spec.m_in_atoms;
    pools.atoms.insert(pools.atoms.end(), spec.m_out_atoms.begin(),
                       spec.m_out_atoms.end());
    pools.condition_atoms =
        spec.m_in_atoms.empty() ? pools.atoms : spec.m_in_atoms;
    collect_formulas(spec.m_assumptions, pools.formulas, pools.counts);
    collect_formulas(spec.m_guarantees, pools.formulas, pools.counts);
    std::sort(pools.formulas.begin(), pools.formulas.end());
    pools.formulas.erase(
        std::unique(pools.formulas.begin(), pools.formulas.end()),
        pools.formulas.end());
    std::sort(pools.counts.begin(), pools.counts.end());
    pools.counts.erase(std::unique(pools.counts.begin(), pools.counts.end()),
                       pools.counts.end());
    return pools;
}

Requirement mutate_requirement(const Requirement& req,
                               const MutationPoolData& pools,
                               Direction direction,
                               const RandomSource& random_source,
                               const Config& cfg, bool* free_rewrite) {
    if (req.is_fixed()) {
        return req;
    }
    Pattern pattern = req.m_pattern;
    PspScopeKind scope = req.m_scope;
    Fields values = fields_of(req);
    if (cfg.psp_p_pattern > 0.0 &&
        random_source.next_real() < cfg.psp_p_pattern) {
        pattern = move_pattern(pattern, scope_class(scope), direction,
                               pools.counts, random_source);
        if (!(pattern == req.m_pattern)) {
            PspMoveStats::n_pattern++;
        }
    }
    // Read off the moved pattern: the scope order is a fact about the cell
    // the requirement is now in.
    if (cfg.psp_p_scope_kind > 0.0 &&
        random_source.next_real() < cfg.psp_p_scope_kind) {
        scope =
            move_scope(scope, pattern_cell(pattern), direction, random_source);
        if (scope != req.m_scope) {
            PspMoveStats::n_scope++;
        }
    }
    fit_fields(values, pattern, scope, pools.formulas, random_source);
    if (cfg.psp_p_count > 0.0 && pattern_count(pattern).has_value() &&
        random_source.next_real() < cfg.psp_p_count) {
        const Pattern before = pattern;
        pattern = move_count(pattern, direction, pools.counts, random_source);
        if (!(pattern == before)) {
            PspMoveStats::n_count++;
        }
    }
    for (std::size_t idx = 0; idx < k_formula_fields.size(); ++idx) {
        const FormulaField field = k_formula_fields[idx];
        const double rate = field_rate(cfg, field);
        const std::optional<Formula>& value = values[idx];
        if (rate <= 0.0 || !value.has_value() ||
            random_source.next_real() >= rate) {
            continue;
        }
        const std::vector<std::string>& atoms =
            field_atoms(field, pattern, pools);
        if (atoms.empty()) {
            continue;
        }
        values[idx] = rewrite_formula(
            *value, atoms,
            field_direction(field_polarity(field, pattern, scope), direction),
            cfg, random_source, free_rewrite);
    }
    return {pattern,   scope,     values[0],        values[1],
            values[2], values[3], req.m_weakenable, req.m_removed};
}

Specification crossover_specifications(const Specification& first,
                                       const Specification& second,
                                       const RandomSource& random_source) {
    const std::size_t n_assumptions =
        std::min(first.m_assumptions.size(), second.m_assumptions.size());
    const std::size_t n_guarantees =
        std::min(first.m_guarantees.size(), second.m_guarantees.size());
    if (n_assumptions + n_guarantees == 0) {
        return first;
    }
    const std::size_t idx =
        random_source.next_index(n_assumptions + n_guarantees);
    const bool is_assumption = idx < n_assumptions;
    const std::size_t local = is_assumption ? idx : idx - n_assumptions;
    std::vector<Requirement> assumptions = first.m_assumptions;
    std::vector<Requirement> guarantees = first.m_guarantees;
    std::vector<Requirement>& side = is_assumption ? assumptions : guarantees;
    const Requirement& target = side[local];
    const Requirement& donor = is_assumption ? second.m_assumptions[local]
                                             : second.m_guarantees[local];
    const auto movable = [](const Requirement& req) {
        return !req.m_removed && req.m_weakenable && !req.is_fixed();
    };
    if (!movable(target) || !movable(donor)) {
        return first;
    }
    const std::vector<CrossField> fields = crossable_fields(target, donor);
    if (fields.empty()) {
        return first;
    }
    const CrossField field = fields[random_source.next_index(fields.size())];
    side[local] = cross_field(target, donor, field, random_source);
    if (creates_duplicate(side, local)) {
        return first;
    }
    PspMoveStats::n_crossovers++;
    return Specification(std::move(assumptions), std::move(guarantees),
                         first.m_in_atoms, first.m_out_atoms);
}

SpecificationSides specification_sides(const Specification& spec) {
    SpecificationSides sides;
    for (const Requirement& req : spec.m_assumptions) {
        if (!req.m_removed) {
            sides.m_assumptions.push_back(req.m_ltl);
        }
    }
    for (const Requirement& req : spec.m_guarantees) {
        if (!req.m_removed) {
            sides.m_guarantees.push_back(req.m_ltl);
        }
    }
    sides.m_scope = "psp";
    return sides;
}

std::optional<bool> check_realizability(const Specification& spec) {
    return global_real_checker().check_realizability_ltl(
        spec.to_ltl(), spec.m_in_atoms, spec.m_out_atoms,
        specification_sides(spec));
}

}  // namespace psp
