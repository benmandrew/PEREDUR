#pragma once

/// @file requirement.hpp
/// @brief Core domain types: Timing, ConditionType, Requirement, Specification,
///        and the automaton State used for model counting.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "hash_combine.hpp"
#include "prop_formula.hpp"

namespace timing {

/// Response must hold immediately when condition is true.
struct Immediately {};

/// Response must hold at the next timepoint after condition.
struct NextTimepoint {};

/// Response must hold within `m_ticks` ticks of condition, including the
/// condition tick.
struct WithinTicks {
    std::size_t m_ticks;
};

/// Response must hold for `m_ticks` consecutive ticks, including the condition
/// tick.
struct ForTicks {
    std::size_t m_ticks;
};

/// Response must not hold for `m_ticks` + 1 ticks starting from the condition
/// tick (i.e., at t=0,...,m_ticks), then must hold at t=m_ticks+1.
/// Implements FRET: after m_ticks res = (for m_ticks ¬res) ∧ (within
/// (m_ticks+1) res).
struct AfterTicks {
    std::size_t m_ticks;
};

/// Response must hold at some timepoint at or after the condition.
struct Eventually {};

/// Response must hold at all timepoints from the condition onward.
struct Always {};

/// Response must hold from the condition up to, but not including, the first
/// timepoint at which `m_stop` holds, and throughout if it never does. FRET's
/// `until` timing, `r W s` at Global scope.
struct Until {
    Formula m_stop;
};

/// Response must hold at some timepoint before the first at which `m_stop`
/// holds, which demands nothing if it never does. FRET's `before` timing,
/// `r R !s` at Global scope.
struct Before {
    Formula m_stop;
};

/// Algebraic data type for requirement timing. Until and Before are appended,
/// so every earlier alternative keeps its index, and with it its ordering and
/// hash.
using Timing = std::variant<Immediately, NextTimepoint, WithinTicks, ForTicks,
                            AfterTicks, Eventually, Always, Until, Before>;

inline Timing immediately() { return Immediately{}; }
inline Timing next_timepoint() { return NextTimepoint{}; }
inline Timing within_ticks(std::size_t ticks) { return WithinTicks{ticks}; }
inline Timing for_ticks(std::size_t ticks) { return ForTicks{ticks}; }
inline Timing after_ticks(std::size_t ticks) { return AfterTicks{ticks}; }
inline Timing eventually() { return Eventually{}; }
inline Timing always() { return Always{}; }
inline Timing until(Formula stop) { return Until{std::move(stop)}; }
inline Timing before(Formula stop) { return Before{std::move(stop)}; }

/// True for the alternatives carrying a stop condition.
template <typename T>
inline constexpr bool k_carries_stop =
    std::is_same_v<T, Until> || std::is_same_v<T, Before>;

/// Always false, but dependent on `T`: the final `else` of an `if constexpr`
/// chain over a timing alternative asserts on it, so an alternative the chain
/// does not name fails to compile instead of taking another timing's branch.
template <typename T>
inline constexpr bool k_unhandled_timing = false;

}  // namespace timing

using Timing = timing::Timing;

bool operator<(const Timing& lhs, const Timing& rhs);
bool operator==(const Timing& lhs, const Timing& rhs);

/// The stop condition of an `until` or `before` timing, or null for a timing
/// that carries none.
const Formula* timing_stop(const Timing& timing);

/// Distinguishes how the condition field activates the requirement.
/// - Trigger: requirement fires on a rising edge of the condition (false→true),
///   or if the condition already holds at t=0.
/// - Continual: requirement fires at every timepoint where the condition holds.
enum class ConditionType : std::uint8_t { Trigger, Continual };

/// FRET's scope field: the interval, relative to when a mode holds, over which
/// a requirement is enforced. `Global` (the default, and what an omitted FRET
/// scope means) enforces it over the whole execution.
///
/// The three `Only*` kinds are the "only" scopes: they constrain what happens
/// *outside* the named interval, so the requirement's obligation is negated
/// rather than restricted. See requirement_to_ltl for the lowering and
/// `docs/` for the derivation.
enum class ScopeKind : std::uint8_t {
    Global,
    In,
    NotIn,
    Before,
    After,
    OnlyIn,
    OnlyBefore,
    OnlyAfter,
};

/// A scope kind together with the mode it is relative to. `Global` carries an
/// empty mode; every other kind requires one, and that mode must be declared in
/// `Specification::m_modes`.
struct Scope {
    ScopeKind m_kind = ScopeKind::Global;
    /// Name of the mode. Empty exactly when `m_kind` is `Global`.
    std::string m_mode;

    friend bool operator<(const Scope& lhs, const Scope& rhs);
    friend bool operator==(const Scope& lhs, const Scope& rhs);

    /// True when the scope names no mode, i.e. the requirement is enforced over
    /// the whole execution.
    [[nodiscard]] bool is_global() const { return m_kind == ScopeKind::Global; }
};

inline Scope global_scope() { return Scope{}; }

/// True for the three "only" scopes, whose lowering negates the requirement's
/// obligation and applies it outside the named interval.
bool is_only_scope(ScopeKind kind);

/// A FRET requirement specifying a system obligation. Consists of a condition
/// and a response that must be satisfied according to the specified timing
/// constraint. These are used as the basic units for repair and for computing
/// semantic and syntactic similarity metrics in genetic algorithms.
struct Requirement {
    /// The condition (propositional formula) that activates the requirement
    Formula m_condition;
    /// The response obligation (propositional formula)
    Formula m_response;
    /// The timing constraint for satisfaction
    Timing m_timing;
    /// Whether the condition is evaluated as a trigger (rising-edge) or
    /// continually (at every timepoint where it holds)
    ConditionType m_condition_type;
    /// The interval, relative to a mode, over which the requirement is
    /// enforced. Defaults to `Global`, which is what an omitted FRET scope
    /// means and what every requirement carried before scopes existed.
    Scope m_scope;
    /// The LTL formula equivalent to (m_condition, m_response, m_timing,
    /// m_condition_type, m_scope), derived automatically by the constructor
    /// via requirement_to_ltl.
    std::string m_ltl;
    /// When false, the genetic algorithm never mutates this requirement, uses
    /// it as a crossover source, or simplifies it. Defaults to true.
    bool m_weakenable = true;
    /// When true this requirement has been deleted from the specification and
    /// contributes nothing to its meaning: it is not lowered to LTL, not
    /// scored, not filtered on, and not written out. It keeps its slot in the
    /// requirement vector so that everything comparing two specifications
    /// position by position stays aligned — erasing it would shift every later
    /// requirement and silently pair unrelated requirements against the
    /// original. Only `p_remove_guarantee` sets it, and only on a guarantee.
    bool m_removed = false;

    friend bool operator<(const Requirement& lhs, const Requirement& rhs);
    friend bool operator==(const Requirement& lhs, const Requirement& rhs);

    explicit Requirement(
        Formula condition, Formula response, Timing timing,
        ConditionType condition_type = ConditionType::Continual,
        bool weakenable = true, bool removed = false, Scope scope = Scope{});

    /// Returns a one-line FRETish string of the form
    /// "[<scope>] [upon|whenever <condition>] C shall <timing> satisfy
    /// <response>", parseable by the FRET formaliser CLI (see
    /// runner/formaliser.hpp).
    [[nodiscard]] std::string to_string() const;

   private:
    [[nodiscard]] std::string condition_to_string() const;
    [[nodiscard]] std::string scope_to_string() const;
};

struct Specification {
    std::vector<Requirement> m_assumptions;
    std::vector<Requirement> m_guarantees;

    std::vector<std::string> m_in_atoms;
    std::vector<std::string> m_out_atoms;
    /// Names of the modes a requirement's scope may be relative to.
    ///
    /// A mode declared only here is environment-driven: it joins ltlsynt's
    /// input side and no mutation atom pool draws it, so no rewrite can put it
    /// into a condition or a response. A mode also declared in `m_in_atoms` or
    /// `m_out_atoms` is that atom, takes its side, and is drawn like any other
    /// atom. FRET modes the controller sets, such as a ventilator's operating
    /// mode, need the output side.
    ///
    /// An output mode lets the synthesised system choose its own scope: an
    /// `In`-scoped guarantee is implied by `G !mode`, and a `Before`-scoped one
    /// is discharged by holding the mode at t=0. Only the specification's other
    /// requirements stop that, and nothing in the search guards against a
    /// repair that removes them. Nothing constrains the modes against each
    /// other either; mutual exclusion is written as an ordinary non-weakenable
    /// requirement, as `examples/fsm-combined/spec.json` already does for its
    /// enum atoms.
    std::vector<std::string> m_modes;

    explicit Specification(std::vector<Requirement> assumptions = {},
                           std::vector<Requirement> guarantees = {},
                           std::vector<std::string> in_atoms = {},
                           std::vector<std::string> out_atoms = {},
                           std::vector<std::string> modes = {});

    friend bool operator<(const Specification& lhs, const Specification& rhs);
    friend bool operator==(const Specification& lhs, const Specification& rhs);

    /// Returns one FRETish line per live requirement (assumptions then
    /// guarantees), each as produced by Requirement::to_string, joined by
    /// newlines. Removed requirements are omitted: the result is the
    /// specification's meaning, not its storage.
    [[nodiscard]] std::string to_string() const;

    /// Returns the whole specification as one LTL formula in SPOT syntax:
    /// `(a1 & ... & an) -> (g1 & ... & gm)` over the live requirements' own
    /// `m_ltl`, or the guarantee conjunction alone where no assumption is
    /// live. Removed requirements are skipped, and an empty conjunction
    /// lowers to `true` rather than to nothing, so `() -> ()` is unreachable.
    ///
    /// Every whole-specification query goes through this -- realizability and
    /// the implication check both -- so the two ask about the same formula.
    /// `tlsf::Specification::to_ltl` is the twin on the other path.
    [[nodiscard]] std::string to_ltl() const;
};

/// The signals ltlsynt plays as the environment: the declared inputs followed
/// by the modes declared in neither atom list. Such a mode is
/// environment-driven (see `Specification::m_modes`), so every query that
/// partitions a specification's alphabet goes through this rather than reading
/// `m_in_atoms` directly.
/// Returns `m_in_atoms` unchanged when no modes are declared, so a
/// specification without them keeps the cache and subsumption keys it had.
std::vector<std::string> environment_signals(
    const Specification& specification);

/// Number of requirements in @p reqs that have not been removed.
std::size_t count_live(const std::vector<Requirement>& reqs);

/// Positions in @p reqs of the requirements that have not been removed, in
/// order. Callers walking a subset of a requirement list index through this so
/// that a walk position maps back to the slot it came from.
std::vector<std::size_t> live_indices(const std::vector<Requirement>& reqs);

/// Vestigial placeholder. Model counting used to build requirement automata
/// here, and this described a state of one; counting now goes through SPOT and
/// Ganak instead, and `TransferSystem::m_states` holds only default-constructed
/// values, so none of these fields carries information. It survives because
/// `m_states` is typed on it and sized from it. Do not read the fields.
struct State {
    bool m_condition_holds = false;
    bool m_response_holds = false;
    bool m_countdown_state = false;
    std::size_t m_countdown_ticks = 0;
};

/// Returns true if any assumption or guarantee has a condition that is the
/// literal atom "false" (e.g. after simplifying "!(true)"). Such a
/// requirement is vacuously satisfied by every trace and imposes no
/// constraint, so specifications containing one should be excluded from the
/// population rather than treated as ordinary candidates.
/// The condition sits only in the antecedent of the lowered implication, under
/// every timing and both ConditionTypes, so a false one is vacuous
/// unconditionally. There is deliberately no syntactic dual over responses: the
/// AfterTicks lowering negates the response, so a `true` response there is the
/// *strongest* guarantee rather than a no-op. That case is left to
/// specification_has_valid_guarantee, which decides it per timing.
bool specification_has_false_condition(const Specification& specification);

/// Converts a Timing enum value to a human-readable string representation.
std::string to_string(const Timing& timing);

/// Renders a scope as its FRETish clause ("in m", "except in m", "only before
/// m", ...), or the empty string for `Global`. The `except in` spelling is the
/// one the FRET grammar accepts for `NotIn`; "not in" is a parse error there.
std::string to_string(const Scope& scope);

/// Converts a Requirement to an LTL formula string in SPOT syntax. For
/// Continual condition type, bounded timing variants are expanded into X (next)
/// operator chains: WithinTicks(n) yields G(C -> (R | X(R | ... | X R))) and
/// ForTicks(n) yields G(C -> (R & X(R & ... & X R))). For Trigger condition
/// type, the formula encodes a rising-edge detector:
/// G((!C & X(C)) -> X(body)) & (C -> body).
/// The result is suitable for passing directly to ltl2tgba or ltlsynt.
std::string requirement_to_ltl(const Requirement& requirement);

/// Internal lowercase tag prepended to every atomic-proposition name at load
/// time. A lowercase-first identifier is always lexed as a single atomic
/// proposition by both SPOT and black, so no atom name can be mistaken for a
/// temporal operator (G F X U R W M). The tag is internal-only and stripped
/// from every user-facing output.
inline constexpr std::string_view k_atom_prefix = "iap_";

/// Returns a copy of @p req with k_atom_prefix prepended to every atom leaf of
/// its condition and response formulae (m_ltl is re-derived by the
/// constructor).
Requirement add_atom_prefix(const Requirement& req);

/// Inverse of add_atom_prefix: removes a leading k_atom_prefix from every atom
/// leaf if present. Defensive at name granularity so directly-constructed
/// requirements (e.g. in tests) are unharmed.
Requirement strip_atom_prefix(const Requirement& req);

/// Applies add_atom_prefix to every requirement and prepends k_atom_prefix to
/// every entry of m_in_atoms/m_out_atoms.
Specification add_atom_prefix(const Specification& spec);

/// Inverse of the Specification add_atom_prefix: strips every requirement and
/// removes a leading k_atom_prefix from each atom-vector entry if present.
Specification strip_atom_prefix(const Specification& spec);

/// \cond
namespace std {  // NOLINT(build/namespaces)

template <>
struct hash<Timing> {
    // std::visit is specified as throwing bad_variant_access on a
    // valueless variant. Every Timing alternative is nothrow-copyable, so
    // Timing can never become valueless and the throw is unreachable.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    std::size_t operator()(const Timing& timing) const noexcept {
        return std::visit(
            [idx = timing.index()](const auto& val) -> std::size_t {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, timing::WithinTicks> ||
                              std::is_same_v<T, timing::ForTicks> ||
                              std::is_same_v<T, timing::AfterTicks>) {
                    return hash_combine(idx,
                                        std::hash<std::size_t>{}(val.m_ticks));
                } else if constexpr (timing::k_carries_stop<T>) {
                    return hash_combine(idx, std::hash<Formula>{}(val.m_stop));
                } else {
                    static_assert(std::is_same_v<T, timing::Immediately> ||
                                  std::is_same_v<T, timing::NextTimepoint> ||
                                  std::is_same_v<T, timing::Eventually> ||
                                  std::is_same_v<T, timing::Always>);
                    return idx;
                }
            },
            timing);
    }
};

template <>
struct hash<Requirement> {
    std::size_t operator()(const Requirement& req) const noexcept {
        std::size_t seed = std::hash<Formula>{}(req.m_condition);
        seed = hash_combine(seed, std::hash<Formula>{}(req.m_response));
        seed = hash_combine(seed, std::hash<Timing>{}(req.m_timing));
        seed = hash_combine(seed, std::hash<bool>{}(req.m_condition_type ==
                                                    ConditionType::Trigger));
        seed = hash_combine(seed,
                            std::hash<std::uint8_t>{}(
                                static_cast<std::uint8_t>(req.m_scope.m_kind)));
        seed = hash_combine(seed, std::hash<std::string>{}(req.m_scope.m_mode));
        // m_ltl is deliberately absent: the constructor derives it from
        // the five fields above via requirement_to_ltl, so scanning it adds a
        // string hash the length of a rendered LTL formula and no
        // discrimination. operator== still compares it, so two requirements
        // that somehow disagree on it collide rather than compare equal.
        seed = hash_combine(seed, std::hash<bool>{}(req.m_weakenable));
        // In the hash and in operator== both, because the fitness cache keys on
        // the specification: a removed guarantee must not collide with the live
        // one it replaced and inherit its score.
        seed = hash_combine(seed, std::hash<bool>{}(req.m_removed));
        return seed;
    }
};

template <>
struct hash<Specification> {
    std::size_t operator()(const Specification& spec) const noexcept {
        std::size_t seed = 0;
        for (const Requirement& req : spec.m_assumptions) {
            seed = hash_combine(seed, std::hash<Requirement>{}(req));
        }
        for (const Requirement& req : spec.m_guarantees) {
            seed = hash_combine(seed, std::hash<Requirement>{}(req));
        }
        for (const std::string& atom : spec.m_in_atoms) {
            seed = hash_combine(seed, std::hash<std::string>{}(atom));
        }
        for (const std::string& atom : spec.m_out_atoms) {
            seed = hash_combine(seed, std::hash<std::string>{}(atom));
        }
        for (const std::string& mode : spec.m_modes) {
            seed = hash_combine(seed, std::hash<std::string>{}(mode));
        }
        return seed;
    }
};

}  // namespace std
/// \endcond
