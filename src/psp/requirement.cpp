#include "psp/requirement.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "prop_formula/atoms.hpp"
#include "psp/operand.hpp"
#include "psp/specification.hpp"
#include "requirement.hpp"

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

using AtomTransform = std::function<std::string(const std::string&)>;

std::string prefix_atom_name(const std::string& name) {
    if (is_constant_atom(name)) {
        return name;
    }
    return std::string(k_atom_prefix) + name;
}

std::string unprefix_atom_name(const std::string& name) {
    const std::string prefix(k_atom_prefix);
    if (name.compare(0, prefix.size(), prefix) == 0) {
        return name.substr(prefix.size());
    }
    return name;
}

Formula rewrite_atoms(const Formula& formula, const AtomTransform& transform) {
    return formula.rewrite_post_order(
        [&transform](const Formula& node) -> std::optional<Formula> {
            if (const std::optional<std::string> name = node.atom_name()) {
                return Formula::make_atom(transform(*name));
            }
            return std::nullopt;
        });
}

std::optional<Formula> rewrite_atoms(const std::optional<Formula>& field,
                                     const AtomTransform& transform) {
    if (!field.has_value()) {
        return std::nullopt;
    }
    return rewrite_atoms(*field, transform);
}

Requirement rewrite_requirement(const Requirement& req,
                                const AtomTransform& transform) {
    if (req.m_fixed) {
        return Requirement::fixed(rewrite_atoms(*req.m_fixed, transform),
                                  req.m_removed);
    }
    return {req.m_pattern,
            req.m_scope,
            rewrite_atoms(req.m_p, transform),
            rewrite_atoms(req.m_s, transform),
            rewrite_atoms(req.m_q, transform),
            rewrite_atoms(req.m_r, transform),
            req.m_weakenable,
            req.m_removed};
}

Specification rewrite_specification(const Specification& spec,
                                    const AtomTransform& transform) {
    const auto each = [&transform](const std::vector<Requirement>& reqs) {
        std::vector<Requirement> out;
        out.reserve(reqs.size());
        for (const Requirement& req : reqs) {
            out.push_back(rewrite_requirement(req, transform));
        }
        return out;
    };
    const auto names = [&transform](const std::vector<std::string>& atoms) {
        std::vector<std::string> out;
        out.reserve(atoms.size());
        for (const std::string& atom : atoms) {
            out.push_back(transform(atom));
        }
        return out;
    };
    return Specification(each(spec.m_assumptions), each(spec.m_guarantees),
                         names(spec.m_in_atoms), names(spec.m_out_atoms));
}

// The structured-English phrase of a pattern, with the fields filled in.
std::string pattern_phrase(const Requirement& req) {
    const std::string field_p =
        req.m_p ? detail::operand(*req.m_p) : std::string();
    const std::string field_s =
        req.m_s ? detail::operand(*req.m_s) : std::string();
    return std::visit(
        [&field_p, &field_s](const auto& pat) -> std::string {
            using T = std::decay_t<decltype(pat)>;
            if constexpr (std::is_same_v<T, Absence>) {
                return "it is never the case that " + field_p + " holds";
            } else if constexpr (std::is_same_v<T, Universality>) {
                return "it is always the case that " + field_s + " holds";
            } else if constexpr (std::is_same_v<T, Existence>) {
                return field_s + " eventually holds";
            } else if constexpr (std::is_same_v<T, Precedence>) {
                return "it is always the case that if " + field_p +
                       " holds, then " + field_s + " previously held";
            } else if constexpr (std::is_same_v<T, Response>) {
                return "it is always the case that if " + field_p +
                       " holds, then " + field_s + " eventually holds";
            } else if constexpr (std::is_same_v<T, BoundedResponse>) {
                return "it is always the case that if " + field_p +
                       " holds, then " + field_s + " holds after at most " +
                       std::to_string(pat.m_count) + " time units";
            } else if constexpr (std::is_same_v<T, BoundedInvariance>) {
                return "it is always the case that if " + field_p +
                       " holds, then " + field_s + " holds for at least " +
                       std::to_string(pat.m_count) + " time units";
            } else if constexpr (std::is_same_v<T, BoundedExistence>) {
                return "transitions to states in which " + field_p +
                       " holds occur at most " + std::to_string(pat.m_count) +
                       " times";
            } else if constexpr (std::is_same_v<T, BoundedRecurrence>) {
                return "it is always the case that " + field_s +
                       " holds at least every " + std::to_string(pat.m_count) +
                       " time units";
            } else {
                static_assert(k_unhandled_pattern<T>);
            }
        },
        req.m_pattern);
}

std::string scope_phrase(const Requirement& req) {
    const std::string field_q =
        req.m_q ? detail::operand(*req.m_q) : std::string();
    const std::string field_r =
        req.m_r ? detail::operand(*req.m_r) : std::string();
    switch (req.m_scope) {
        case PspScopeKind::Globally:
            return "Globally";
        case PspScopeKind::Before:
            return "Before " + field_r;
        case PspScopeKind::After:
            return "After " + field_q;
        case PspScopeKind::Between:
            return "Between " + field_q + " and " + field_r;
        case PspScopeKind::AfterUntil:
            return "After " + field_q + " until " + field_r;
    }
    assert(false && "unhandled PspScopeKind");
    return {};
}

// The fields in one fixed order, for the comparisons.
auto fields(const Requirement& req) {
    return std::tie(req.m_pattern, req.m_scope, req.m_p, req.m_s, req.m_q,
                    req.m_r, req.m_fixed, req.m_ltl, req.m_weakenable,
                    req.m_removed);
}

}  // namespace

std::optional<std::size_t> pattern_count(const Pattern& pattern) {
    return std::visit(
        [](const auto& pat) -> std::optional<std::size_t> {
            using T = std::decay_t<decltype(pat)>;
            if constexpr (k_bounded_pattern<T>) {
                return pat.m_count;
            } else {
                return std::nullopt;
            }
        },
        pattern);
}

const char* pattern_name(const Pattern& pattern) {
    return std::visit(
        [](const auto& pat) -> const char* {
            using T = std::decay_t<decltype(pat)>;
            if constexpr (std::is_same_v<T, Absence>) {
                return "absence";
            } else if constexpr (std::is_same_v<T, Universality>) {
                return "universality";
            } else if constexpr (std::is_same_v<T, Existence>) {
                return "existence";
            } else if constexpr (std::is_same_v<T, Precedence>) {
                return "precedence";
            } else if constexpr (std::is_same_v<T, Response>) {
                return "response";
            } else if constexpr (std::is_same_v<T, BoundedResponse>) {
                return "bounded-response";
            } else if constexpr (std::is_same_v<T, BoundedInvariance>) {
                return "bounded-invariance";
            } else if constexpr (std::is_same_v<T, BoundedExistence>) {
                return "bounded-existence";
            } else if constexpr (std::is_same_v<T, BoundedRecurrence>) {
                return "bounded-recurrence";
            } else {
                static_assert(k_unhandled_pattern<T>);
            }
        },
        pattern);
}

bool pattern_uses_p(const Pattern& pattern) {
    return !std::holds_alternative<pattern::Universality>(pattern) &&
           !std::holds_alternative<pattern::Existence>(pattern) &&
           !std::holds_alternative<pattern::BoundedRecurrence>(pattern);
}

bool pattern_uses_s(const Pattern& pattern) {
    return !std::holds_alternative<pattern::Absence>(pattern) &&
           !std::holds_alternative<pattern::BoundedExistence>(pattern);
}

const char* scope_name(PspScopeKind scope) {
    switch (scope) {
        case PspScopeKind::Globally:
            return "globally";
        case PspScopeKind::Before:
            return "before";
        case PspScopeKind::After:
            return "after";
        case PspScopeKind::Between:
            return "between";
        case PspScopeKind::AfterUntil:
            return "afteruntil";
    }
    assert(false && "unhandled PspScopeKind");
    return "";
}

bool scope_uses_q(PspScopeKind scope) {
    return scope == PspScopeKind::After || scope == PspScopeKind::Between ||
           scope == PspScopeKind::AfterUntil;
}

bool scope_uses_r(PspScopeKind scope) {
    return scope == PspScopeKind::Before || scope == PspScopeKind::Between ||
           scope == PspScopeKind::AfterUntil;
}

Requirement::Requirement(Pattern pattern, PspScopeKind scope,
                         std::optional<Formula> field_p,
                         std::optional<Formula> field_s,
                         std::optional<Formula> field_q,
                         std::optional<Formula> field_r, bool weakenable,
                         bool removed)
    : m_pattern(pattern),
      m_scope(scope),
      m_p(std::move(field_p)),
      m_s(std::move(field_s)),
      m_q(std::move(field_q)),
      m_r(std::move(field_r)),
      m_weakenable(weakenable),
      m_removed(removed) {
    assert(m_p.has_value() == pattern_uses_p(m_pattern));
    assert(m_s.has_value() == pattern_uses_s(m_pattern));
    assert(m_q.has_value() == scope_uses_q(m_scope));
    assert(m_r.has_value() == scope_uses_r(m_scope));
    m_ltl = requirement_to_ltl(*this);
}

Requirement Requirement::fixed(Formula ltl, bool removed) {
    Requirement req;
    req.m_fixed = std::move(ltl);
    req.m_weakenable = false;
    req.m_removed = removed;
    req.m_ltl = requirement_to_ltl(req);
    return req;
}

std::string Requirement::to_string() const {
    if (m_fixed) {
        return "Fixed: " + m_fixed->to_string() + ".";
    }
    return scope_phrase(*this) + ", " + pattern_phrase(*this) + ".";
}

bool operator<(const Requirement& lhs, const Requirement& rhs) {
    return fields(lhs) < fields(rhs);
}

bool operator==(const Requirement& lhs, const Requirement& rhs) {
    return fields(lhs) == fields(rhs);
}

Requirement add_atom_prefix(const Requirement& req) {
    return rewrite_requirement(req, prefix_atom_name);
}

Requirement strip_atom_prefix(const Requirement& req) {
    return rewrite_requirement(req, unprefix_atom_name);
}

// NOLINTNEXTLINE(bugprone-exception-escape): pattern_count does not throw.
std::size_t PatternHash::operator()(const Pattern& pattern) const noexcept {
    const std::optional<std::size_t> count = pattern_count(pattern);
    return hash_combine(pattern.index(), count.value_or(0));
}

Specification::Specification(std::vector<Requirement> assumptions,
                             std::vector<Requirement> guarantees,
                             std::vector<std::string> in_atoms,
                             std::vector<std::string> out_atoms)
    : m_in_atoms(std::move(in_atoms)), m_out_atoms(std::move(out_atoms)) {
    const auto deduplicate = [](std::vector<Requirement> reqs) {
        std::set<Requirement> seen;
        std::vector<Requirement> unique;
        for (Requirement& req : reqs) {
            if (seen.insert(req).second) {
                unique.push_back(std::move(req));
            }
        }
        return unique;
    };
    m_assumptions = deduplicate(std::move(assumptions));
    m_guarantees = deduplicate(std::move(guarantees));
}

bool operator<(const Specification& lhs, const Specification& rhs) {
    return std::tie(lhs.m_assumptions, lhs.m_guarantees, lhs.m_in_atoms,
                    lhs.m_out_atoms) <
           std::tie(rhs.m_assumptions, rhs.m_guarantees, rhs.m_in_atoms,
                    rhs.m_out_atoms);
}

bool operator==(const Specification& lhs, const Specification& rhs) {
    return lhs.m_assumptions == rhs.m_assumptions &&
           lhs.m_guarantees == rhs.m_guarantees &&
           lhs.m_in_atoms == rhs.m_in_atoms &&
           lhs.m_out_atoms == rhs.m_out_atoms;
}

std::string Specification::to_string() const {
    std::string out;
    for (const std::vector<Requirement>* reqs :
         {&m_assumptions, &m_guarantees}) {
        for (const Requirement& req : *reqs) {
            if (req.m_removed) {
                continue;
            }
            if (!out.empty()) {
                out += "\n";
            }
            out += req.to_string();
        }
    }
    return out;
}

namespace {

// Removed requirements are skipped, and an empty conjunction is `true`, as in
// the FRETISH ltl_conjunction.
std::string ltl_conjunction(const std::vector<Requirement>& reqs) {
    std::string out;
    for (const Requirement& req : reqs) {
        if (req.m_removed) {
            continue;
        }
        if (!out.empty()) {
            out += " & ";
        }
        out += "(" + req.m_ltl + ")";
    }
    return out.empty() ? "true" : out;
}

bool any_live(const std::vector<Requirement>& reqs) {
    return std::any_of(reqs.begin(), reqs.end(),
                       [](const Requirement& req) { return !req.m_removed; });
}

}  // namespace

std::string Specification::to_ltl() const {
    if (!any_live(m_assumptions)) {
        return ltl_conjunction(m_guarantees);
    }
    return "(" + ltl_conjunction(m_assumptions) + ") -> (" +
           ltl_conjunction(m_guarantees) + ")";
}

Specification add_atom_prefix(const Specification& spec) {
    return rewrite_specification(spec, prefix_atom_name);
}

Specification strip_atom_prefix(const Specification& spec) {
    return rewrite_specification(spec, unprefix_atom_name);
}

}  // namespace psp
