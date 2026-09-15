#include "requirement.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

// "true"/"false" are logical constants, not user atoms: they are special-cased
// throughout (requirement_to_ltl, condition_to_string,
// specification_has_false_condition, Formula::simplify) and are boolean
// constants to SPOT/black. Prefixing them would turn a constant into a free
// atom and break that handling, so they are never tagged.
bool is_constant_atom(const std::string& name) {
    return name == "true" || name == "false";
}

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

Formula rewrite_atom_names(
    const Formula& formula,
    const std::function<std::string(const std::string&)>& transform) {
    return formula.rewrite_post_order(
        [&transform](const Formula& node) -> std::optional<Formula> {
            if (const std::optional<std::string> name = node.atom_name()) {
                return Formula::make_atom(transform(*name));
            }
            return std::nullopt;
        });
}

std::vector<std::string> transform_atom_vector(
    const std::vector<std::string>& atoms,
    const std::function<std::string(const std::string&)>& transform) {
    std::vector<std::string> result;
    result.reserve(atoms.size());
    for (const std::string& atom : atoms) {
        result.push_back(transform(atom));
    }
    return result;
}

// Expands F[0..n] R as R | X(R | X(... | X R)) (n nestings of X).
std::string expand_within(const std::string& response, std::size_t ticks) {
    std::string inner = response;
    for (std::size_t i = 0; i < ticks; ++i) {
        std::string next = response;
        next += " | X(";
        next += inner;
        next += ")";
        inner = std::move(next);
    }
    return inner;
}

// Expands G[0..n] R as R & X(R & X(... & X R)) (n nestings of X).
std::string expand_for(const std::string& response, std::size_t ticks) {
    std::string inner = response;
    for (std::size_t i = 0; i < ticks; ++i) {
        std::string next = response;
        next += " & X(";
        next += inner;
        next += ")";
        inner = std::move(next);
    }
    return inner;
}

// Expands G[0..n](!R) & F[n+1..n+1] R as !R & X(!R & X(... & X R))
// (n+1 nestings of !R followed by R). Implements FRET "after n":
// (for n ¬R) ∧ (within (n+1) R) = ¬R at t=0..n, R at t=n+1. For n=0 this is
// !R & X(R): R must not hold at the condition tick, but must hold at the
// next one.
std::string expand_after(const std::string& response, std::size_t ticks) {
    std::string not_response = "!";
    not_response += response;
    std::string inner = response;
    for (std::size_t i = 0; i <= ticks; ++i) {
        std::string next = not_response;
        next += " & X(";
        next += inner;
        next += ")";
        inner = std::move(next);
    }
    return inner;
}

// --- Scopes ---------------------------------------------------------------
//
// A scope restricts the interval over which a requirement is enforced, so a
// bounded obligation that would run past the end of that interval is
// discharged at the boundary instead of having to complete. Every function
// below takes that boundary as a string, empty meaning "no boundary": either
// the requirement is unscoped, or its scope runs to the end of the trace. The
// empty case must reproduce the pre-scope output byte for byte, since every
// archived run, every cache key and the determinism goldens are recorded
// against it.
//
// The whole construction is validated against the vendored FRET formaliser
// over the scope x condition x timing cross product; see
// test_scope_agrees_with_formaliser.

// The point immediately *before* the mode rises. FRET's start-of-mode marker.
std::string start_of_mode(const std::string& mode) {
    return "((!" + mode + ") & X" + mode + ")";
}

// The last point at which the mode still holds. FRET's end-of-mode marker.
std::string end_of_mode(const std::string& mode) {
    return "(" + mode + " & X(!" + mode + "))";
}

// F[0,ticks] response, discharged early at the boundary.
std::string within_body(const std::string& response, std::size_t ticks,
                        const std::string& boundary) {
    std::string base = "(" + expand_within(response, ticks) + ")";
    // The boundary disjunct spans [0, ticks-1], an empty interval at zero
    // ticks: the obligation falls due at the very point the scope is entered,
    // so there is no room for the boundary to pre-empt it. FRET prints this
    // case as the degenerate `F[0,-1] ...`, which SPOT refuses to parse, so
    // reproducing it would emit LTL no tool downstream can read.
    if (boundary.empty() || ticks == 0) {
        return base;
    }
    return "(" + base + " | (" + expand_within(boundary, ticks - 1) + "))";
}

// G[0,ticks] response, discharged early at the boundary.
std::string for_body(const std::string& response, std::size_t ticks,
                     const std::string& boundary) {
    std::string base = "(" + expand_for(response, ticks) + ")";
    if (boundary.empty()) {
        return base;
    }
    return "(" + base + " | (" + boundary + " R " + response + "))";
}

// The timing obligation over @p response, discharged early at @p boundary.
std::string timing_body(const Timing& timing, const std::string& response,
                        const std::string& boundary) {
    const bool bounded = !boundary.empty();
    return std::visit(
        [&](const auto& variant) -> std::string {
            using T = std::decay_t<decltype(variant)>;
            if constexpr (std::is_same_v<T, timing::Immediately>) {
                return response;
            } else if constexpr (std::is_same_v<T, timing::NextTimepoint>) {
                return bounded ? "(" + boundary + " | X" + response + ")"
                               : "X" + response;
            } else if constexpr (std::is_same_v<T, timing::WithinTicks>) {
                return within_body(response, variant.m_ticks, boundary);
            } else if constexpr (std::is_same_v<T, timing::ForTicks>) {
                return for_body(response, variant.m_ticks, boundary);
            } else if constexpr (std::is_same_v<T, timing::AfterTicks>) {
                if (!bounded) {
                    return "(" + expand_after(response, variant.m_ticks) + ")";
                }
                // Both halves of FRET's "after n" have to relax separately,
                // which the single X-chain expand_after builds cannot express.
                return "(" +
                       for_body("(!" + response + ")", variant.m_ticks,
                                boundary) +
                       " & " +
                       within_body(response, variant.m_ticks + 1, boundary) +
                       ")";
            } else if constexpr (std::is_same_v<T, timing::Eventually>) {
                return bounded ? "((!" + boundary + ") U " + response + ")"
                               : "F" + response;
            } else if constexpr (std::is_same_v<T, timing::Always>) {
                return bounded ? "(" + boundary + " R " + response + ")"
                               : "G" + response;
            } else {
                static_assert(timing::k_unhandled_timing<T>);
            }
        },
        timing);
}

// The timing whose obligation is the negation of @p timing's: within and for
// swap, eventually and always swap, and the two unbounded ones are their own
// duals. AfterTicks has no dual timing and is handled by its caller.
Timing dual_timing(const Timing& timing) {
    return std::visit(
        [](const auto& variant) -> Timing {
            using T = std::decay_t<decltype(variant)>;
            if constexpr (std::is_same_v<T, timing::WithinTicks>) {
                return timing::for_ticks(variant.m_ticks);
            } else if constexpr (std::is_same_v<T, timing::ForTicks>) {
                return timing::within_ticks(variant.m_ticks);
            } else if constexpr (std::is_same_v<T, timing::Eventually>) {
                return timing::always();
            } else if constexpr (std::is_same_v<T, timing::Always>) {
                return timing::eventually();
            } else {
                static_assert(std::is_same_v<T, timing::Immediately> ||
                              std::is_same_v<T, timing::NextTimepoint> ||
                              std::is_same_v<T, timing::AfterTicks>);
                return variant;
            }
        },
        timing);
}

// The negation of timing_body: the obligation that the response is *not*
// delivered on this timing. The three "only" scopes constrain what happens
// outside their interval, which is exactly this.
std::string negated_timing_body(const Timing& timing,
                                const std::string& response,
                                const std::string& boundary) {
    const std::string negated = "(!" + response + ")";
    if (const auto* after = std::get_if<timing::AfterTicks>(&timing)) {
        // !((for n !r) & (within n+1 r)) == (within n r) | (for n+1 !r).
        return "(" + within_body(response, after->m_ticks, boundary) + " | " +
               for_body(negated, after->m_ticks + 1, boundary) + ")";
    }
    return timing_body(dual_timing(timing), negated, boundary);
}

// The boundary marker whose truth ends the scope's interval, or the empty
// string where the interval runs to the end of the trace and nothing is ever
// discharged early.
std::string scope_boundary(const Scope& scope) {
    switch (scope.m_kind) {
        case ScopeKind::Global:
        case ScopeKind::After:
        case ScopeKind::OnlyBefore:
            return "";
        case ScopeKind::In:
        case ScopeKind::OnlyAfter:
            return end_of_mode(scope.m_mode);
        case ScopeKind::NotIn:
        case ScopeKind::Before:
        case ScopeKind::OnlyIn:
            return start_of_mode(scope.m_mode);
    }
    assert(false);
    __builtin_unreachable();
}

// Places the conditioned obligation @p inner at the start of the scope's
// interval, and at every later re-entry where the interval repeats.
std::string scope_wrap(const Scope& scope, const std::string& inner) {
    const std::string& mode = scope.m_mode;
    const std::string som = start_of_mode(mode);
    const std::string eom = end_of_mode(mode);
    switch (scope.m_kind) {
        case ScopeKind::Global:
        // "only after" constrains the prefix running up to the mode's last
        // point, which starts at t=0, so the obligation needs no placing. Its
        // boundary does all the work.
        case ScopeKind::OnlyAfter:
            return inner;
        case ScopeKind::In:
            return "(G((!" + som + ") | X(" + inner + ")) & (" + mode + " -> " +
                   inner + "))";
        // "only in" constrains everything outside the mode, which is the same
        // interval "except in" enforces over, so the two share a wrapper and
        // differ only in that this one negates the obligation.
        case ScopeKind::NotIn:
        case ScopeKind::OnlyIn:
            return "(G((!" + eom + ") | X(" + inner + ")) & ((!" + mode +
                   ") -> " + inner + "))";
        case ScopeKind::Before:
            return "(" + inner + " | " + mode + ")";
        case ScopeKind::After:
            return "(((!" + eom + ") U (" + eom + " & X(" + inner +
                   "))) | G(!" + eom + "))";
        case ScopeKind::OnlyBefore:
            return "(((!" + mode + ") -> (((!" + som + ") U (" + som + " & X(" +
                   inner + "))) | G(!" + som + "))) & (" + mode + " -> " +
                   inner + "))";
    }
    assert(false);
    __builtin_unreachable();
}

}  // namespace

bool operator<(const Timing& lhs, const Timing& rhs) {
    if (lhs.index() != rhs.index()) {
        return lhs.index() < rhs.index();
    }
    const auto get_ticks = [](const Timing& tim) -> std::size_t {
        return std::visit(
            [](const auto& val) -> std::size_t {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, timing::WithinTicks> ||
                              std::is_same_v<T, timing::ForTicks> ||
                              std::is_same_v<T, timing::AfterTicks>) {
                    return val.m_ticks;
                } else {
                    static_assert(std::is_same_v<T, timing::Immediately> ||
                                  std::is_same_v<T, timing::NextTimepoint> ||
                                  std::is_same_v<T, timing::Eventually> ||
                                  std::is_same_v<T, timing::Always>);
                    return 0;
                }
            },
            tim);
    };
    return get_ticks(lhs) < get_ticks(rhs);
}

bool operator==(const Timing& lhs, const Timing& rhs) {
    return !(lhs < rhs) && !(rhs < lhs);
}

bool operator<(const Scope& lhs, const Scope& rhs) {
    if (lhs.m_kind != rhs.m_kind) {
        return lhs.m_kind < rhs.m_kind;
    }
    return lhs.m_mode < rhs.m_mode;
}

bool operator==(const Scope& lhs, const Scope& rhs) {
    return lhs.m_kind == rhs.m_kind && lhs.m_mode == rhs.m_mode;
}

bool is_only_scope(ScopeKind kind) {
    return kind == ScopeKind::OnlyIn || kind == ScopeKind::OnlyBefore ||
           kind == ScopeKind::OnlyAfter;
}

Requirement::Requirement(Formula condition, Formula response,
                         const Timing& timing, ConditionType condition_type,
                         bool weakenable, bool removed, Scope scope)
    : m_condition(std::move(condition)),
      m_response(std::move(response)),
      m_timing(timing),
      m_condition_type(condition_type),
      // Ahead of m_ltl in the member order, and it has to stay there:
      // requirement_to_ltl reads the scope off *this.
      m_scope(std::move(scope)),
      m_ltl(requirement_to_ltl(*this)),
      m_weakenable(weakenable),
      m_removed(removed) {}

namespace {

// Removed requirements are skipped: they are not part of what the
// specification says, so they must not reach a solver.
std::string ltl_conjunction(const std::vector<Requirement>& reqs) {
    std::string out;
    bool first = true;
    for (const Requirement& req : reqs) {
        if (req.m_removed) {
            continue;
        }
        if (!first) {
            out += " & ";
        }
        out += "(" + req.m_ltl + ")";
        first = false;
    }
    // An all-removed list conjoins to nothing. Emit the unit of conjunction
    // rather than an empty string, which would produce `() -> ()`.
    if (out.empty()) {
        out = "true";
    }
    return out;
}

}  // namespace

std::string Specification::to_ltl() const {
    if (count_live(m_assumptions) == 0) {
        return ltl_conjunction(m_guarantees);
    }
    return "(" + ltl_conjunction(m_assumptions) + ") -> (" +
           ltl_conjunction(m_guarantees) + ")";
}

std::vector<std::string> environment_signals(
    const Specification& specification) {
    if (specification.m_modes.empty()) {
        return specification.m_in_atoms;
    }
    std::vector<std::string> signals = specification.m_in_atoms;
    signals.insert(signals.end(), specification.m_modes.begin(),
                   specification.m_modes.end());
    return signals;
}

std::size_t count_live(const std::vector<Requirement>& reqs) {
    return static_cast<std::size_t>(
        std::count_if(reqs.begin(), reqs.end(),
                      [](const Requirement& req) { return !req.m_removed; }));
}

std::vector<std::size_t> live_indices(const std::vector<Requirement>& reqs) {
    std::vector<std::size_t> indices;
    indices.reserve(reqs.size());
    for (std::size_t i = 0; i < reqs.size(); ++i) {
        if (!reqs[i].m_removed) {
            indices.push_back(i);
        }
    }
    return indices;
}

bool operator<(const Requirement& lhs, const Requirement& rhs) {
    if (lhs.m_timing < rhs.m_timing || rhs.m_timing < lhs.m_timing) {
        return lhs.m_timing < rhs.m_timing;
    }
    if (lhs.m_condition < rhs.m_condition) {
        return true;
    }
    if (rhs.m_condition < lhs.m_condition) {
        return false;
    }
    if (lhs.m_response < rhs.m_response) {
        return true;
    }
    if (rhs.m_response < lhs.m_response) {
        return false;
    }
    if (lhs.m_condition_type != rhs.m_condition_type) {
        return lhs.m_condition_type < rhs.m_condition_type;
    }
    if (lhs.m_scope < rhs.m_scope) {
        return true;
    }
    if (rhs.m_scope < lhs.m_scope) {
        return false;
    }
    if (lhs.m_ltl != rhs.m_ltl) {
        return lhs.m_ltl < rhs.m_ltl;
    }
    if (lhs.m_weakenable != rhs.m_weakenable) {
        return !lhs.m_weakenable && rhs.m_weakenable;
    }
    return !lhs.m_removed && rhs.m_removed;
}

bool operator==(const Requirement& lhs, const Requirement& rhs) {
    return !(lhs.m_timing < rhs.m_timing) && !(rhs.m_timing < lhs.m_timing) &&
           lhs.m_condition == rhs.m_condition &&
           lhs.m_response == rhs.m_response &&
           lhs.m_condition_type == rhs.m_condition_type &&
           lhs.m_scope == rhs.m_scope && lhs.m_ltl == rhs.m_ltl &&
           lhs.m_weakenable == rhs.m_weakenable &&
           lhs.m_removed == rhs.m_removed;
}

Specification::Specification(std::vector<Requirement> assumptions,
                             std::vector<Requirement> guarantees,
                             std::vector<std::string> in_atoms,
                             std::vector<std::string> out_atoms,
                             std::vector<std::string> modes)
    : m_in_atoms(std::move(in_atoms)),
      m_out_atoms(std::move(out_atoms)),
      m_modes(std::move(modes)) {
    auto deduplicate =
        [](std::vector<Requirement> reqs) -> std::vector<Requirement> {
        std::set<Requirement> seen;
        std::vector<Requirement> unique;
        for (auto& req : reqs) {
            auto [seen_iter, inserted] = seen.insert(req);
            if (inserted) {
                unique.push_back(std::move(req));
            }
        }
        return unique;
    };
    m_assumptions = deduplicate(std::move(assumptions));
    m_guarantees = deduplicate(std::move(guarantees));
}

// A mode is an atom of the lowered formula like any other, so it is tagged
// alongside the condition and response atoms: an untagged mode named `Grant`
// would lex as G applied to `rant`, which is the whole reason the tag exists.
Scope rewrite_scope_mode(
    const Scope& scope,
    const std::function<std::string(const std::string&)>& transform) {
    if (scope.is_global()) {
        return scope;
    }
    return Scope{scope.m_kind, transform(scope.m_mode)};
}

Requirement add_atom_prefix(const Requirement& req) {
    return Requirement(rewrite_atom_names(req.m_condition, prefix_atom_name),
                       rewrite_atom_names(req.m_response, prefix_atom_name),
                       req.m_timing, req.m_condition_type, req.m_weakenable,
                       req.m_removed,
                       rewrite_scope_mode(req.m_scope, prefix_atom_name));
}

Requirement strip_atom_prefix(const Requirement& req) {
    return Requirement(rewrite_atom_names(req.m_condition, unprefix_atom_name),
                       rewrite_atom_names(req.m_response, unprefix_atom_name),
                       req.m_timing, req.m_condition_type, req.m_weakenable,
                       req.m_removed,
                       rewrite_scope_mode(req.m_scope, unprefix_atom_name));
}

Specification add_atom_prefix(const Specification& spec) {
    auto map_prefix = [](const std::vector<Requirement>& reqs) {
        std::vector<Requirement> result;
        result.reserve(reqs.size());
        for (const Requirement& req : reqs) {
            result.push_back(add_atom_prefix(req));
        }
        return result;
    };
    return Specification(
        map_prefix(spec.m_assumptions), map_prefix(spec.m_guarantees),
        transform_atom_vector(spec.m_in_atoms, prefix_atom_name),
        transform_atom_vector(spec.m_out_atoms, prefix_atom_name),
        transform_atom_vector(spec.m_modes, prefix_atom_name));
}

Specification strip_atom_prefix(const Specification& spec) {
    auto map_strip = [](const std::vector<Requirement>& reqs) {
        std::vector<Requirement> result;
        result.reserve(reqs.size());
        for (const Requirement& req : reqs) {
            result.push_back(strip_atom_prefix(req));
        }
        return result;
    };
    return Specification(
        map_strip(spec.m_assumptions), map_strip(spec.m_guarantees),
        transform_atom_vector(spec.m_in_atoms, unprefix_atom_name),
        transform_atom_vector(spec.m_out_atoms, unprefix_atom_name),
        transform_atom_vector(spec.m_modes, unprefix_atom_name));
}

bool operator<(const Specification& lhs, const Specification& rhs) {
    if (lhs.m_assumptions != rhs.m_assumptions) {
        return lhs.m_assumptions < rhs.m_assumptions;
    }
    return lhs.m_guarantees < rhs.m_guarantees;
}

bool operator==(const Specification& lhs, const Specification& rhs) {
    return lhs.m_assumptions == rhs.m_assumptions &&
           lhs.m_guarantees == rhs.m_guarantees &&
           lhs.m_in_atoms == rhs.m_in_atoms &&
           lhs.m_out_atoms == rhs.m_out_atoms && lhs.m_modes == rhs.m_modes;
}

std::string to_string(const Timing& timing) {
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, timing::Immediately>) {
                return "immediately";
            } else if constexpr (std::is_same_v<T, timing::NextTimepoint>) {
                return "at the next timepoint";
            } else if constexpr (std::is_same_v<T, timing::WithinTicks>) {
                return "within " + std::to_string(value.m_ticks) + " ticks";
            } else if constexpr (std::is_same_v<T, timing::ForTicks>) {
                return "for " + std::to_string(value.m_ticks) + " ticks";
            } else if constexpr (std::is_same_v<T, timing::AfterTicks>) {
                return "after " + std::to_string(value.m_ticks) + " ticks";
            } else if constexpr (std::is_same_v<T, timing::Eventually>) {
                return "eventually";
            } else if constexpr (std::is_same_v<T, timing::Always>) {
                return "always";
            } else {
                static_assert(timing::k_unhandled_timing<T>);
            }
        },
        timing);
}

std::string to_string(ConditionType condition_type) {
    switch (condition_type) {
        case ConditionType::Trigger:
            return "upon";
        case ConditionType::Continual:
            return "whenever";
    }
    assert(false);
    __builtin_unreachable();
}

std::string to_string(const Scope& scope) {
    switch (scope.m_kind) {
        case ScopeKind::Global:
            return "";
        case ScopeKind::In:
            return "in " + scope.m_mode;
        // FRET's grammar spells this one "except in" (or "unless in", or "when
        // not in"). A bare "not in" is a parse error there, so the internal
        // NotIn name and the FRETish text deliberately differ.
        case ScopeKind::NotIn:
            return "except in " + scope.m_mode;
        case ScopeKind::Before:
            return "before " + scope.m_mode;
        case ScopeKind::After:
            return "after " + scope.m_mode;
        case ScopeKind::OnlyIn:
            return "only in " + scope.m_mode;
        case ScopeKind::OnlyBefore:
            return "only before " + scope.m_mode;
        case ScopeKind::OnlyAfter:
            return "only after " + scope.m_mode;
    }
    assert(false);
    __builtin_unreachable();
}

std::string Requirement::scope_to_string() const {
    return ::to_string(m_scope);
}

std::string Requirement::condition_to_string() const {
    // requirement_to_ltl only drops the G(...) wrapper for a true condition
    // when m_condition_type is Trigger (a trigger on an always-true
    // condition reduces to a bare initial obligation); for Continual it
    // always emits G(condition -> body), so the FRETish must keep an
    // explicit condition clause here too — omitting it makes the formaliser
    // CLI treat the requirement as unscoped and drop the G, silently
    // changing "always" into "only at the initial timepoint".
    if (m_condition == Formula::true_formula &&
        m_condition_type == ConditionType::Trigger) {
        return "";
    }
    return ::to_string(m_condition_type) + " " + m_condition.to_string();
}

std::string Requirement::to_string() const {
    const std::string scope = scope_to_string();
    return (scope.empty() ? std::string() : scope + " ") +
           condition_to_string() + " C shall " + ::to_string(m_timing) +
           " satisfy " + m_response.to_string();
}

std::string Specification::to_string() const {
    std::string result;
    const auto append = [&result](const std::vector<Requirement>& reqs) {
        for (const Requirement& req : reqs) {
            if (req.m_removed) {
                continue;
            }
            if (!result.empty()) {
                result += "\n";
            }
            result += req.to_string();
        }
    };
    append(m_assumptions);
    append(m_guarantees);
    return result;
}

bool specification_has_false_condition(const Specification& specification) {
    // A removed requirement is exempt: its residual condition is not part of
    // the specification, so it must not make one read as vacuous.
    auto condition_is_false = [](const Requirement& req) {
        return !req.m_removed && req.m_condition.atom_name() == "false";
    };
    return std::any_of(specification.m_assumptions.begin(),
                       specification.m_assumptions.end(), condition_is_false) ||
           std::any_of(specification.m_guarantees.begin(),
                       specification.m_guarantees.end(), condition_is_false);
}

std::string requirement_to_ltl(const Requirement& requirement) {
    // FRETISH conditions and responses are propositional; the temporal
    // structure comes entirely from the timing wrapper built below. Temporal
    // operators in a condition/response would be double-wrapped and produce
    // malformed LTL, so guard the invariant here.
    assert(requirement.m_condition.is_propositional());
    assert(requirement.m_response.is_propositional());
    const std::string condition_str =
        "(" + requirement.m_condition.to_string() + ")";
    const std::string response_str =
        "(" + requirement.m_response.to_string() + ")";

    const Scope& scope = requirement.m_scope;
    const std::string boundary = scope_boundary(scope);
    // An "only" scope says the response is delivered *only* inside its
    // interval, so what it constrains is the interval's complement, and the
    // obligation carried there is the negation of the timing's.
    const std::string body =
        is_only_scope(scope.m_kind)
            ? negated_timing_body(requirement.m_timing, response_str, boundary)
            : timing_body(requirement.m_timing, response_str, boundary);

    const bool bounded = !boundary.empty();
    if (requirement.m_condition_type == ConditionType::Continual) {
        const std::string obligation =
            "(" + condition_str + " -> " + body + ")";
        return scope_wrap(scope, bounded
                                     ? "(" + boundary + " R " + obligation + ")"
                                     : "G" + obligation);
    }
    // Trigger: fires on rising edge (!C & X(C)) -> X(body), plus bare (C ->
    // body) at t=0.  When C is the constant true the rising-edge clause is
    // vacuously true (!(true) is always false), so the formula collapses to
    // just the initial obligation.  Emitting the full G form causes black's
    // BMC to time out on deeply-nested X bodies even though it is trivially
    // satisfied.  The collapse survives a scope by the same algebra: the
    // rising-edge half becomes `boundary R true`, which is true.
    if (requirement.m_condition.to_string() == "true") {
        return scope_wrap(scope, condition_str + " -> " + body);
    }
    if (!bounded) {
        return scope_wrap(
            scope, "G((!" + condition_str + " & X" + condition_str + ") -> X(" +
                       body + ")) & (" + condition_str + " -> " + body + ")");
    }
    // Inside a bounded scope the rising edge itself has to be confined: an edge
    // at the boundary belongs to the next interval, not this one, so the
    // boundary is excluded from both the edge and the point the body lands on.
    return scope_wrap(scope, "((" + boundary + " R (((!" + condition_str +
                                 ") & (X" + condition_str + " & (!" + boundary +
                                 "))) -> (X(" + body + ") & (!" + boundary +
                                 ")))) & (" + condition_str + " -> " + body +
                                 "))");
}
