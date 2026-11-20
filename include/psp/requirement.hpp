#pragma once

/// @file psp/requirement.hpp
/// @brief The PSP front end's requirement: a Dwyer pattern under a scope, over
///        the formulas P, S, Q and R, or a fixed raw-LTL entry.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "hash_combine.hpp"
#include "prop_formula.hpp"

namespace psp {

namespace pattern {

/// "it is never the case that P holds": `G !P`.
struct Absence {};

/// "it is always the case that S holds": `G S`.
struct Universality {};

/// "S eventually holds": `F S`.
struct Existence {};

/// "if P holds, then S previously held": `!P W S`.
struct Precedence {};

/// "if P holds, then S eventually holds": `G(P -> F S)`.
struct Response {};

/// "if P holds, then S holds after at most c time units":
/// `G(P -> F[0,c] S)`.
struct BoundedResponse {
    std::size_t m_count;
};

/// "if P holds, then S holds for at least c time units":
/// `G(P -> G[0,c] S)`.
struct BoundedInvariance {
    std::size_t m_count;
};

/// "transitions to states where P holds occur at most k times": at most k
/// stretches of P, alternating with stretches of `!P`.
struct BoundedExistence {
    std::size_t m_count;
};

/// "S holds at least every c time units": `G F[0,c] S`.
struct BoundedRecurrence {
    std::size_t m_count;
};

// Each alternative compares by its count, and a base pattern equals itself, so
// std::variant's own comparisons order a Pattern by alternative and then by
// count. They live here rather than on Pattern because argument-dependent
// lookup on a std::variant searches the alternatives' namespace, not psp.
inline bool operator==(const Absence& /*lhs*/, const Absence& /*rhs*/) {
    return true;
}
inline bool operator<(const Absence& /*lhs*/, const Absence& /*rhs*/) {
    return false;
}

inline bool operator==(const Universality& /*lhs*/,
                       const Universality& /*rhs*/) {
    return true;
}
inline bool operator<(const Universality& /*lhs*/,
                      const Universality& /*rhs*/) {
    return false;
}

inline bool operator==(const Existence& /*lhs*/, const Existence& /*rhs*/) {
    return true;
}
inline bool operator<(const Existence& /*lhs*/, const Existence& /*rhs*/) {
    return false;
}

inline bool operator==(const Precedence& /*lhs*/, const Precedence& /*rhs*/) {
    return true;
}
inline bool operator<(const Precedence& /*lhs*/, const Precedence& /*rhs*/) {
    return false;
}

inline bool operator==(const Response& /*lhs*/, const Response& /*rhs*/) {
    return true;
}
inline bool operator<(const Response& /*lhs*/, const Response& /*rhs*/) {
    return false;
}

inline bool operator==(const BoundedResponse& lhs, const BoundedResponse& rhs) {
    return lhs.m_count == rhs.m_count;
}
inline bool operator<(const BoundedResponse& lhs, const BoundedResponse& rhs) {
    return lhs.m_count < rhs.m_count;
}

inline bool operator==(const BoundedInvariance& lhs,
                       const BoundedInvariance& rhs) {
    return lhs.m_count == rhs.m_count;
}
inline bool operator<(const BoundedInvariance& lhs,
                      const BoundedInvariance& rhs) {
    return lhs.m_count < rhs.m_count;
}

inline bool operator==(const BoundedExistence& lhs,
                       const BoundedExistence& rhs) {
    return lhs.m_count == rhs.m_count;
}
inline bool operator<(const BoundedExistence& lhs,
                      const BoundedExistence& rhs) {
    return lhs.m_count < rhs.m_count;
}

inline bool operator==(const BoundedRecurrence& lhs,
                       const BoundedRecurrence& rhs) {
    return lhs.m_count == rhs.m_count;
}
inline bool operator<(const BoundedRecurrence& lhs,
                      const BoundedRecurrence& rhs) {
    return lhs.m_count < rhs.m_count;
}

}  // namespace pattern

/// The PSP patterns PLAN.md D3 admits, as an algebraic data type like
/// `timing::Timing`. The four bounded patterns carry their count.
using Pattern =
    std::variant<pattern::Absence, pattern::Universality, pattern::Existence,
                 pattern::Precedence, pattern::Response,
                 pattern::BoundedResponse, pattern::BoundedInvariance,
                 pattern::BoundedExistence, pattern::BoundedRecurrence>;

/// True for the alternatives carrying a count.
template <typename T>
inline constexpr bool k_bounded_pattern =
    std::is_same_v<T, pattern::BoundedResponse> ||
    std::is_same_v<T, pattern::BoundedInvariance> ||
    std::is_same_v<T, pattern::BoundedExistence> ||
    std::is_same_v<T, pattern::BoundedRecurrence>;

/// Always false, but dependent on `T`, for the final `else` of an
/// `if constexpr` chain over the pattern alternatives.
template <typename T>
inline constexpr bool k_unhandled_pattern = false;

/// The count of a bounded pattern, or nothing for a base pattern.
std::optional<std::size_t> pattern_count(const Pattern& pattern);

/// The pattern's name as `docs/dev/psp-orders.json` and the `.psp` format
/// spell it: "absence", "bounded-response" and so on.
const char* pattern_name(const Pattern& pattern);

/// Whether the pattern reads P: absence, precedence, response, bounded
/// response, bounded invariance and bounded existence.
bool pattern_uses_p(const Pattern& pattern);

/// Whether the pattern reads S: every pattern but absence and bounded
/// existence.
bool pattern_uses_s(const Pattern& pattern);

/// The PSP scopes. Each is closed on the left and open on the right, and
/// `After` opens at the first Q (PLAN.md D4).
enum class PspScopeKind : std::uint8_t {
    Globally,
    Before,
    After,
    Between,
    AfterUntil,
};

/// The scope's name as `docs/dev/psp-orders.json` and the `.psp` format spell
/// it: "globally", "before", "after", "between" and "afteruntil".
const char* scope_name(PspScopeKind scope);

/// Whether the scope reads Q: after, between and after-until.
bool scope_uses_q(PspScopeKind scope);

/// Whether the scope reads R: before, between and after-until.
bool scope_uses_r(PspScopeKind scope);

/// One PSP requirement: a pattern under a scope over the formulas P and S (the
/// pattern's) and Q and R (the scope's), or a fixed raw-LTL formula.
///
/// Each of P, S, Q and R is present exactly where its pattern or scope reads
/// it, and each is propositional. Q and R are formula fields with polarities of
/// their own, not mode atoms (PLAN.md D5).
///
/// A fixed requirement (PLAN.md D6) carries an LTL formula in `m_fixed` and
/// nothing else: no field is present, the pattern and scope keep their
/// defaults, and it is never weakenable. It carries the non-pattern lines of an
/// imported specification, so that they survive import.
struct Requirement {
    Pattern m_pattern = pattern::Universality{};
    PspScopeKind m_scope = PspScopeKind::Globally;
    /// The trigger or forbidden proposition.
    std::optional<Formula> m_p;
    /// The target proposition.
    std::optional<Formula> m_s;
    /// The scope's opener.
    std::optional<Formula> m_q;
    /// The scope's closer.
    std::optional<Formula> m_r;
    /// The raw LTL of a fixed requirement, which is then the whole of it.
    std::optional<Formula> m_fixed;
    /// The LTL lowering, derived by the constructor through
    /// requirement_to_ltl.
    std::string m_ltl;
    /// When false the search never changes this requirement. Always false on
    /// a fixed one.
    bool m_weakenable = true;
    /// A tombstone, as on the FRETISH `Requirement`: the slot is kept so that
    /// slots pair by index, and it contributes nothing to the specification.
    bool m_removed = false;

    /// A pattern requirement. Asserts that each of P, S, Q and R is present
    /// exactly where the pattern or the scope reads it.
    Requirement(Pattern pattern, PspScopeKind scope,
                std::optional<Formula> field_p, std::optional<Formula> field_s,
                std::optional<Formula> field_q = {},
                std::optional<Formula> field_r = {}, bool weakenable = true,
                bool removed = false);

    /// A fixed raw-LTL requirement, never weakenable.
    static Requirement fixed(Formula ltl, bool removed = false);

    [[nodiscard]] bool is_fixed() const { return m_fixed.has_value(); }

    /// The requirement in Konrad and Cheng's structured English, as
    /// "Between Q and R, it is always the case that if P holds, then S
    /// eventually holds." A fixed requirement prints as "Fixed: <ltl>."
    [[nodiscard]] std::string to_string() const;

    friend bool operator<(const Requirement& lhs, const Requirement& rhs);
    friend bool operator==(const Requirement& lhs, const Requirement& rhs);

   private:
    Requirement() = default;
};

/// Lowers @p requirement to LTL in SPOT syntax, as the body of its pattern
/// under the wrapper of its scope (PLAN.md D4, and the table in
/// `docs/dev/psp.md`). Bounded operators are expanded into `X` chains. A fixed
/// requirement lowers to its formula.
///
/// `scripts/psp_field_orders.py` holds the reference lowering, and the
/// `psp_lowering` suite checks this one equivalent to it at every pattern,
/// scope and count from 1 to 4.
std::string requirement_to_ltl(const Requirement& requirement);

/// A copy of @p req with `k_atom_prefix` in front of every atom of P, S, Q, R
/// and the fixed formula, the constants excepted. `m_ltl` is re-derived.
Requirement add_atom_prefix(const Requirement& req);

/// The inverse of add_atom_prefix, defensive at name granularity as the
/// FRETISH one is.
Requirement strip_atom_prefix(const Requirement& req);

/// Hashes a Pattern, which is a standard-library variant.
struct PatternHash {
    std::size_t operator()(const Pattern& pattern) const noexcept;
};

}  // namespace psp

/// \cond
namespace std {  // NOLINT(build/namespaces)

template <>
struct hash<psp::Requirement> {
    std::size_t operator()(const psp::Requirement& req) const noexcept {
        const auto optional_hash = [](const std::optional<Formula>& field) {
            return field.has_value()
                       ? hash_combine(1, std::hash<Formula>{}(*field))
                       : static_cast<std::size_t>(0);
        };
        const psp::PatternHash pattern_hash;
        std::size_t seed = pattern_hash(req.m_pattern);
        seed = hash_combine(seed, static_cast<std::size_t>(req.m_scope));
        seed = hash_combine(seed, optional_hash(req.m_p));
        seed = hash_combine(seed, optional_hash(req.m_s));
        seed = hash_combine(seed, optional_hash(req.m_q));
        seed = hash_combine(seed, optional_hash(req.m_r));
        seed = hash_combine(seed, optional_hash(req.m_fixed));
        // m_ltl is derived from the fields above, as on the FRETISH side.
        seed = hash_combine(seed, std::hash<bool>{}(req.m_weakenable));
        seed = hash_combine(seed, std::hash<bool>{}(req.m_removed));
        return seed;
    }
};

}  // namespace std
/// \endcond
