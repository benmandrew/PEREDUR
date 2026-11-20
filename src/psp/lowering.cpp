// The PSP lowering of PLAN.md D4: a body per pattern over an optional boundary
// B, inside a wrapper per scope. This is a port of body() and lower() in
// scripts/psp_field_orders.py, which measured the orders in
// docs/dev/psp-orders.json from that lowering, so the two must stay
// equivalent; the psp_lowering suite checks them at every pattern, scope and
// count from 1 to 4. Under a boundary the point where B holds is outside the
// scope.

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "psp/operand.hpp"
#include "psp/requirement.hpp"

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

// X^i f, with every X operand parenthesised, which black needs.
std::string next(std::size_t times, const std::string& formula) {
    std::string out;
    out.reserve(formula.size() + (3 * times));
    for (std::size_t i = 0; i < times; ++i) {
        out += "X(";
    }
    out += formula;
    out.append(times, ')');
    return out;
}

std::string join(const std::vector<std::string>& parts, const char* separator) {
    std::string out = "(";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += separator;
        }
        out += parts[i];
    }
    out += ")";
    return out;
}

std::string conj(const std::vector<std::string>& parts) {
    return join(parts, " & ");
}

std::string disj(const std::vector<std::string>& parts) {
    return join(parts, " | ");
}

// Concatenates @p parts in one allocation.
std::string cat(std::initializer_list<std::string_view> parts) {
    std::size_t size = 0;
    for (const std::string_view part : parts) {
        size += part.size();
    }
    std::string out;
    out.reserve(size);
    for (const std::string_view part : parts) {
        out += part;
    }
    return out;
}

// F[0,n] f as an X chain.
std::string within(std::size_t count, const std::string& formula) {
    std::vector<std::string> parts;
    parts.reserve(count + 1);
    for (std::size_t j = 0; j <= count; ++j) {
        parts.push_back(next(j, formula));
    }
    return disj(parts);
}

// G[0,n] f as an X chain.
std::string throughout(std::size_t count, const std::string& formula) {
    std::vector<std::string> parts;
    parts.reserve(count + 1);
    for (std::size_t j = 0; j <= count; ++j) {
        parts.push_back(next(j, formula));
    }
    return conj(parts);
}

// `!b U[0,n] (s & !b)`: s within n points, and before b.
std::string within_before(std::size_t count, const std::string& target,
                          const std::string& boundary) {
    const std::string hit = cat({target, " & !", boundary});
    const std::string miss = cat({"!", boundary});
    std::vector<std::string> options;
    options.reserve(count + 1);
    for (std::size_t j = 0; j <= count; ++j) {
        std::vector<std::string> parts = {next(j, hit)};
        for (std::size_t i = 0; i < j; ++i) {
            parts.push_back(next(i, miss));
        }
        options.push_back(conj(parts));
    }
    return disj(options);
}

// s at each of the next n points, up to and excluding the first b.
std::string held_until(std::size_t count, const std::string& target,
                       const std::string& boundary) {
    std::vector<std::string> points;
    points.reserve(count + 1);
    for (std::size_t j = 0; j <= count; ++j) {
        std::vector<std::string> parts = {next(j, target)};
        for (std::size_t i = 0; i <= j; ++i) {
            parts.push_back(next(i, boundary));
        }
        points.push_back(disj(parts));
    }
    return conj(points);
}

// At most n stretches of p, alternating with stretches of !p, before b when
// there is a boundary.
std::string at_most(std::size_t count, const std::string& field_p,
                    const std::optional<std::string>& boundary) {
    if (!boundary) {
        std::string out = cat({"G(!", field_p, ")"});
        for (std::size_t i = 0; i < count; ++i) {
            out = cat({"(!", field_p, " W (", field_p, " W ", out, "))"});
        }
        return out;
    }
    const std::string& bound = *boundary;
    std::string out = cat({"(!", field_p, " W ", bound, ")"});
    for (std::size_t i = 0; i < count; ++i) {
        out = cat({"(!", field_p, " W (", bound, " | (", field_p, " W (", bound,
                   " | ", out, "))))"});
    }
    return out;
}

// The rendered P and S of a pattern requirement, empty where unused.
struct Fields {
    std::string m_p;
    std::string m_s;
};

// The body of a pattern other than bounded existence with no boundary.
std::string unbounded_body(const Pattern& pattern, const Fields& fields) {
    const std::string& field_p = fields.m_p;
    const std::string& field_s = fields.m_s;
    return std::visit(
        [&field_p, &field_s](const auto& pat) -> std::string {
            using T = std::decay_t<decltype(pat)>;
            if constexpr (std::is_same_v<T, Absence>) {
                return cat({"G(!", field_p, ")"});
            } else if constexpr (std::is_same_v<T, Universality>) {
                return cat({"G(", field_s, ")"});
            } else if constexpr (std::is_same_v<T, Existence>) {
                return cat({"F(", field_s, ")"});
            } else if constexpr (std::is_same_v<T, Precedence>) {
                return cat({"(!", field_p, " W ", field_s, ")"});
            } else if constexpr (std::is_same_v<T, Response>) {
                return cat({"G(", field_p, " -> F(", field_s, "))"});
            } else if constexpr (std::is_same_v<T, BoundedResponse>) {
                return cat(
                    {"G(", field_p, " -> ", within(pat.m_count, field_s), ")"});
            } else if constexpr (std::is_same_v<T, BoundedInvariance>) {
                return cat({"G(", field_p, " -> ",
                            throughout(pat.m_count, field_s), ")"});
            } else if constexpr (std::is_same_v<T, BoundedRecurrence>) {
                return cat({"G", within(pat.m_count, field_s)});
            } else if constexpr (std::is_same_v<T, BoundedExistence>) {
                return at_most(pat.m_count, field_p, std::nullopt);
            } else {
                static_assert(k_unhandled_pattern<T>);
            }
        },
        pattern);
}

// The body of a pattern under boundary @p bound.
std::string bounded_body(const Pattern& pattern, const Fields& fields,
                         const std::string& bound) {
    const std::string& field_p = fields.m_p;
    const std::string& field_s = fields.m_s;
    return std::visit(
        [&field_p, &field_s, &bound](const auto& pat) -> std::string {
            using T = std::decay_t<decltype(pat)>;
            if constexpr (std::is_same_v<T, Absence>) {
                return cat({"(!", field_p, " W ", bound, ")"});
            } else if constexpr (std::is_same_v<T, Universality>) {
                return cat({"(", field_s, " W ", bound, ")"});
            } else if constexpr (std::is_same_v<T, Existence>) {
                return cat({"(!", bound, " U (", field_s, " & !", bound, "))"});
            } else if constexpr (std::is_same_v<T, Precedence>) {
                return cat(
                    {"(!", field_p, " W (", field_s, " | ", bound, "))"});
            } else if constexpr (std::is_same_v<T, Response>) {
                return cat({"((", field_p, " -> (!", bound, " U (", field_s,
                            " & !", bound, "))) W ", bound, ")"});
            } else if constexpr (std::is_same_v<T, BoundedResponse>) {
                return cat({"((", field_p, " -> ",
                            within_before(pat.m_count, field_s, bound), ") W ",
                            bound, ")"});
            } else if constexpr (std::is_same_v<T, BoundedInvariance>) {
                return cat({"((", field_p, " -> ",
                            held_until(pat.m_count, field_s, bound), ") W ",
                            bound, ")"});
            } else if constexpr (std::is_same_v<T, BoundedRecurrence>) {
                return cat({"(",
                            within(pat.m_count, cat({field_s, " | ", bound})),
                            " W ", bound, ")"});
            } else if constexpr (std::is_same_v<T, BoundedExistence>) {
                return at_most(pat.m_count, field_p, bound);
            } else {
                static_assert(k_unhandled_pattern<T>);
            }
        },
        pattern);
}

// The pattern's body at the scope's first point, with boundary @p boundary or
// none.
std::string body(const Requirement& req,
                 const std::optional<std::string>& boundary) {
    const Fields fields{req.m_p ? detail::operand(*req.m_p) : std::string(),
                        req.m_s ? detail::operand(*req.m_s) : std::string()};
    if (boundary) {
        return bounded_body(req.m_pattern, fields, *boundary);
    }
    return unbounded_body(req.m_pattern, fields);
}

}  // namespace

std::string requirement_to_ltl(const Requirement& requirement) {
    if (requirement.m_fixed) {
        return requirement.m_fixed->to_string();
    }
    const std::string field_q =
        requirement.m_q ? detail::operand(*requirement.m_q) : std::string();
    const std::string field_r =
        requirement.m_r ? detail::operand(*requirement.m_r) : std::string();
    switch (requirement.m_scope) {
        case PspScopeKind::Globally:
            return body(requirement, std::nullopt);
        case PspScopeKind::Before:
            return cat(
                {"(F(", field_r, ") -> ", body(requirement, field_r), ")"});
        case PspScopeKind::After:
            return cat({"(!", field_q, " W (", field_q, " & ",
                        body(requirement, std::nullopt), "))"});
        case PspScopeKind::Between:
            return cat({"G((", field_q, " & !", field_r, " & F(", field_r,
                        ")) -> ", body(requirement, field_r), ")"});
        case PspScopeKind::AfterUntil:
            return cat({"G((", field_q, " & !", field_r, ") -> ",
                        body(requirement, field_r), ")"});
    }
    assert(false && "unhandled PspScopeKind");
    return {};
}

}  // namespace psp
