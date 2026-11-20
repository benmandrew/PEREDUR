#pragma once

/// @file ordered_language.hpp
/// @brief The traits through which the order-directed mechanisms read a
///        specification language: the output climb, two-way mutation in
///        exchange mode, maximality removal, novelty and blame narrowing.
///        FRETISH is the one instance.

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "config.hpp"
#include "filter/implication_check.hpp"
#include "fitness/syntactic_similarity.hpp"
#include "genetic/mutation.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/order_step.hpp"
#include "genetic/random_source.hpp"
#include "requirement.hpp"
#include "runner/spot.hpp"

/// The operations a language lends the order-directed mechanisms, as static
/// members of a specialisation for its specification type @p Spec. A
/// mechanism templated over @p Spec calls these and nothing else of the
/// language, so a new language implements the traits and reuses the
/// mechanisms.
///
/// A specialisation provides:
///
/// - the types `Requirement` (one slot), `Field` (an enumeration naming a
///   field of a requirement, as a step records it), `ChainValue` (a value on
///   the chain a bisected step crosses), `Step` (an OrderStep of @p Spec and
///   `Field`), `Implication` (a proof of one requirement implying another)
///   and `MutationPools` (what the per-slot mutator draws from);
/// - slot access: `assumptions`, `guarantees`, `is_live`, `is_weakenable`,
///   `lowering` and `with_slots`;
/// - the order by rule: `requirement_implies`, `spec_implies_by_rule` (alone,
///   and over precomputed `live_lowerings` with a given `Implication`),
///   `classify_move`, `requirement_join`, `requirement_meet` and
///   `keyword_similarity`;
/// - steps: `steps_towards`, `step_chain` and `with_slot_value`;
/// - `check_realizability` of a whole specification;
/// - directed mutation: `can_rewrite`, `mutate_slot` and
///   `free_rewrite_rates`.
template <typename Spec>
struct OrderedLanguage;

/// FRETISH: forwards each trait to the function the mechanisms called before
/// they were templated, so the FRETISH search is unchanged.
template <>
struct OrderedLanguage<Specification> {
    using Requirement = ::Requirement;
    using Field = RequirementField;
    using ChainValue = Timing;
    using Step = ::Step;
    using Implication = RequirementImplication;

    /// The atoms (inputs then outputs) and the timing pool of the
    /// specification being mutated, by reference to the caller's.
    struct MutationPools {
        const std::vector<std::string>& atoms;
        const std::vector<Timing>& timings;
    };

    static const std::vector<Requirement>& assumptions(
        const Specification& spec) {
        return spec.m_assumptions;
    }

    static const std::vector<Requirement>& guarantees(
        const Specification& spec) {
        return spec.m_guarantees;
    }

    static bool is_live(const Requirement& req) { return !req.m_removed; }

    static bool is_weakenable(const Requirement& req) {
        return req.m_weakenable;
    }

    /// The requirement's LTL lowering, which keys the requirement-pair memos.
    static const std::string& lowering(const Requirement& req) {
        return req.m_ltl;
    }

    /// A specification with @p shape's atoms and modes and the given slots.
    /// The constructor merges equal entries, so the result can have fewer.
    static Specification with_slots(const Specification& shape,
                                    std::vector<Requirement> new_assumptions,
                                    std::vector<Requirement> new_guarantees) {
        return Specification(std::move(new_assumptions),
                             std::move(new_guarantees), shape.m_in_atoms,
                             shape.m_out_atoms, shape.m_modes);
    }

    static bool requirement_implies(const Requirement& stronger,
                                    const Requirement& weaker) {
        return ::requirement_implies(stronger, weaker);
    }

    static bool spec_implies_by_rule(const Specification& stronger,
                                     const Specification& weaker) {
        return specification_implies_by_rule(stronger, weaker);
    }

    static bool spec_implies_by_rule(
        const Specification& from, const Specification& dest,
        const std::unordered_set<std::string>& from_guarantees,
        const std::unordered_set<std::string>& dest_assumptions,
        const Implication& implies) {
        return ::spec_implies_by_rule(from, dest, from_guarantees,
                                      dest_assumptions, implies);
    }

    static std::unordered_set<std::string> live_lowerings(
        const std::vector<Requirement>& reqs) {
        return ::live_lowerings(reqs);
    }

    static MoveOrder classify_move(const Specification& parent,
                                   const Specification& child) {
        return ::classify_move(parent, child);
    }

    static std::optional<Requirement> requirement_join(const Requirement& lhs,
                                                       const Requirement& rhs) {
        return ::requirement_join(lhs, rhs);
    }

    static std::optional<Requirement> requirement_meet(const Requirement& lhs,
                                                       const Requirement& rhs) {
        return ::requirement_meet(lhs, rhs);
    }

    static double keyword_similarity(const Requirement& lhs,
                                     const Requirement& rhs) {
        return keyword_order_similarity(lhs, rhs);
    }

    static std::vector<Step> steps_towards(const Specification& current,
                                           const Specification& original) {
        return ::steps_towards(current, original);
    }

    /// The chain a step crosses, its destination last: the timing chain of a
    /// timing step, and nothing for any other step.
    static std::vector<ChainValue> step_chain(const Specification& current,
                                              const Step& step) {
        return timing_step_chain(current, step);
    }

    static std::optional<Specification> with_slot_value(
        const Specification& current, StepSide side, std::size_t slot,
        const ChainValue& value) {
        return with_slot_timing(current, side, slot, value);
    }

    static std::optional<bool> check_realizability(const Specification& spec) {
        return global_real_checker().check_realizability(spec);
    }

    /// Whether a slot rewrite has anything to draw from.
    static bool can_rewrite(const MutationPools& pools) {
        return !pools.atoms.empty();
    }

    /// Slot @p req of @p spec rewritten in @p direction, reporting through
    /// @p free_rewrite whether any field took a free rewrite rather than a
    /// move along its order.
    static Requirement mutate_slot(const Requirement& req,
                                   const Specification& spec,
                                   const MutationPools& pools,
                                   Direction direction,
                                   const RandomSource& random_source,
                                   const Config& cfg, bool* free_rewrite,
                                   StepScale scale) {
        // Trigger atoms come from the inputs alone where there are any (see
        // mutate_specification).
        const std::vector<std::string>& condition_atoms =
            spec.m_in_atoms.empty() ? pools.atoms : spec.m_in_atoms;
        return mutate_requirement_tracked(
            req, pools.atoms, condition_atoms, direction, pools.timings,
            spec.m_modes, random_source, cfg, free_rewrite, scale);
    }

    /// @p cfg with every arm that moves along an order off, leaving the free
    /// rewrites. An arm at 0 that still draws its `next_real` draws it, so a
    /// rewrite keeps the shape of its stream.
    static Config free_rewrite_rates(const Config& cfg) {
        Config free = cfg;
        free.p_timing = 0.0;
        free.p_condition_type = 0.0;
        free.p_scope = 0.0;
        free.p_monotone = 0.0;
        return free;
    }
};
