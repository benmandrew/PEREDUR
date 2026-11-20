#pragma once

/// @file ordered_language.hpp
/// @brief OrderedLanguage<psp::Specification>: the traits through which the
///        order-directed mechanisms read PSP.

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "config.hpp"
#include "genetic/mutation.hpp"
#include "genetic/order_step.hpp"
#include "genetic/ordered_language.hpp"
#include "genetic/random_source.hpp"
#include "psp/implication.hpp"
#include "psp/mutation.hpp"
#include "psp/order_moves.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"
#include "runner/spot.hpp"

namespace psp {

/// The sides a realizability query on @p spec passes, tombstones dropped.
SpecificationSides specification_sides(const Specification& spec);

/// Realizability of @p spec by ltlsynt over its lowering, memoised.
std::optional<bool> check_realizability(const Specification& spec);

}  // namespace psp

/// PSP: each trait forwards to the psp:: function of the same role. The
/// field a step records is psp::RequirementField, and the chain a bisected
/// step crosses is the count of one bounded pattern.
template <>
struct OrderedLanguage<psp::Specification> {
    using Requirement = psp::Requirement;
    using Field = psp::RequirementField;
    using ChainValue = std::size_t;
    using Step = psp::Step;
    using Implication = psp::RequirementImplication;
    using PoolData = psp::MutationPoolData;

    struct MutationPools {
        const PoolData& data;
    };

    static const std::vector<Requirement>& assumptions(
        const psp::Specification& spec) {
        return spec.m_assumptions;
    }

    static const std::vector<Requirement>& guarantees(
        const psp::Specification& spec) {
        return spec.m_guarantees;
    }

    static bool is_live(const Requirement& req) { return !req.m_removed; }

    /// A fixed raw-LTL requirement has no fields to move, so it is never
    /// weakenable whatever its flag says.
    static bool is_weakenable(const Requirement& req) {
        return req.m_weakenable && !req.is_fixed();
    }

    static const std::string& lowering(const Requirement& req) {
        return req.m_ltl;
    }

    static psp::Specification with_slots(
        const psp::Specification& shape,
        std::vector<Requirement> new_assumptions,
        std::vector<Requirement> new_guarantees) {
        return psp::Specification(std::move(new_assumptions),
                                  std::move(new_guarantees), shape.m_in_atoms,
                                  shape.m_out_atoms);
    }

    static bool requirement_implies(const Requirement& stronger,
                                    const Requirement& weaker) {
        return psp::requirement_implies(stronger, weaker);
    }

    static bool spec_implies_by_rule(const psp::Specification& stronger,
                                     const psp::Specification& weaker) {
        return psp::spec_implies_by_rule(stronger, weaker);
    }

    static bool spec_implies_by_rule(
        const psp::Specification& from, const psp::Specification& dest,
        const std::unordered_set<std::string>& from_guarantees,
        const std::unordered_set<std::string>& dest_assumptions,
        const Implication& implies) {
        return psp::spec_implies_by_rule(from, dest, from_guarantees,
                                         dest_assumptions, implies);
    }

    static std::unordered_set<std::string> live_lowerings(
        const std::vector<Requirement>& reqs) {
        return psp::live_lowerings(reqs);
    }

    static MoveOrder classify_move(const psp::Specification& parent,
                                   const psp::Specification& child) {
        return psp::classify_move(parent, child);
    }

    static std::optional<Requirement> requirement_join(const Requirement& lhs,
                                                       const Requirement& rhs) {
        return psp::requirement_join(lhs, rhs);
    }

    static std::optional<Requirement> requirement_meet(const Requirement& lhs,
                                                       const Requirement& rhs) {
        return psp::requirement_meet(lhs, rhs);
    }

    static double keyword_similarity(const Requirement& lhs,
                                     const Requirement& rhs) {
        return psp::keyword_similarity(lhs, rhs);
    }

    static std::vector<Step> steps_towards(const psp::Specification& current,
                                           const psp::Specification& original) {
        return psp::steps_towards(current, original);
    }

    /// The count chain of a pattern step within one bounded pattern, and
    /// nothing for any other step.
    static std::vector<ChainValue> step_chain(const psp::Specification& current,
                                              const Step& step) {
        return psp::count_step_chain(current, step);
    }

    static std::optional<psp::Specification> with_slot_value(
        const psp::Specification& current, StepSide side, std::size_t slot,
        const ChainValue& value) {
        return psp::with_slot_count(current, side, slot, value);
    }

    static std::optional<bool> check_realizability(
        const psp::Specification& spec) {
        return psp::check_realizability(spec);
    }

    static PoolData pool_data(const psp::Specification& spec) {
        return psp::mutation_pool_data(spec);
    }

    static MutationPools pools_of(const PoolData& data) { return {data}; }

    static bool can_rewrite(const MutationPools& pools) {
        return !pools.data.atoms.empty();
    }

    /// The step scale reaches the structural removal only: no PSP arm has a
    /// large move of its own.
    static Requirement mutate_slot(const Requirement& req,
                                   const psp::Specification& /*spec*/,
                                   const MutationPools& pools,
                                   Direction direction,
                                   const RandomSource& random_source,
                                   const Config& cfg, bool* free_rewrite,
                                   StepScale /*scale*/) {
        return psp::mutate_requirement(req, pools.data, direction,
                                       random_source, cfg, free_rewrite);
    }

    /// @p cfg with the pattern, scope and count arms and the monotone form
    /// off, leaving the free formula rewrites.
    static Config free_rewrite_rates(const Config& cfg) {
        Config free = cfg;
        free.psp_p_pattern = 0.0;
        free.psp_p_scope_kind = 0.0;
        free.psp_p_count = 0.0;
        free.p_monotone = 0.0;
        return free;
    }
};
