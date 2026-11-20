#pragma once

/// @file mutation.hpp
/// @brief The PSP operators: the per-slot mutator, which moves the pattern,
///        the scope and the count along their orders and rewrites P, S, Q
///        and R by polarity; the whole-specification mutation, the structural
///        parts of which are templated with FRETISH's; and crossover.

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "config.hpp"
#include "genetic/mutation.hpp"
#include "genetic/random_source.hpp"
#include "prop_formula.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"

namespace psp {

/// What the per-slot mutator draws from, computed once per mutation.
struct MutationPoolData {
    /// Inputs then outputs.
    std::vector<std::string> atoms;
    /// The inputs where there are any, else `atoms`: the pool for a
    /// trigger (P where the pattern also reads S) and for Q and R, which the
    /// lowering reads at the current point.
    std::vector<std::string> condition_atoms;
    /// Every distinct formula in the live non-fixed requirements, sorted: the
    /// donations a field newly read after a pattern or scope move takes.
    std::vector<Formula> formulas;
    /// Every distinct count in the live requirements, sorted: the donations
    /// the count arm and the pattern arm may move to.
    std::vector<std::size_t> counts;
};

/// What the PSP arms did over a run, recorded in run.json under `psp`. Each
/// counts a move that changed its field; zero on the other paths.
struct PspMoveStats {
    /// Pattern moves along a covering edge (`psp.p_pattern`).
    inline static std::atomic<std::size_t> n_pattern{0};
    /// Scope moves along a covering edge (`psp.p_scope_kind`).
    inline static std::atomic<std::size_t> n_scope{0};
    /// Count moves (`psp.p_count`).
    inline static std::atomic<std::size_t> n_count{0};
    /// Formula fields newly read after a pattern or scope move, each filled
    /// with a donated formula.
    inline static std::atomic<std::size_t> n_donated{0};
    /// Formula rewrites in the monotone form, along the field's polarity.
    inline static std::atomic<std::size_t> n_monotone{0};
    /// Free formula rewrites.
    inline static std::atomic<std::size_t> n_free{0};
    /// Crossovers that changed the first parent.
    inline static std::atomic<std::size_t> n_crossovers{0};
};

/// The pools of @p spec.
MutationPoolData mutation_pool_data(const Specification& spec);

/// Slot @p req moved in @p direction. Each arm reads its probability before
/// its draw, so an arm at 0 costs no draw:
///
/// - `psp.p_pattern`: a covering edge of the pattern order under the scope's
///   class, over the base patterns and the bounded ones at the slot's count
///   and the donated counts;
/// - `psp.p_scope_kind`: a covering edge of the scope order in the pattern's
///   cell;
/// - `psp.p_count`: the count halved or doubled, or a donated count, in the
///   direction's sense, between 1 and k_max_count;
/// - `psp.p_p`, `psp.p_s`, `psp.p_q`, `psp.p_r`: a rewrite of the field, a
///   monotone one (under `mutation.p_monotone`) in the direction the field's
///   polarity gives, and a free one where it has none.
///
/// A field the requirement reads after a pattern or scope move and did not
/// before takes a donated formula (one next_index); a field it no longer
/// reads is dropped. @p free_rewrite, where given, is set when a formula arm
/// took a free rewrite. A fixed requirement comes back unchanged.
Requirement mutate_requirement(const Requirement& req,
                               const MutationPoolData& pools,
                               Direction direction,
                               const RandomSource& random_source,
                               const Config& cfg, bool* free_rewrite);

/// The PSP whole-specification mutation: the structural actions (add an
/// assumption, tombstone a guarantee), two-way mutation in every
/// GateRealizable mode, blame narrowing and the slot draw, all from the
/// template FRETISH's mutate_specification is an instance of.
Specification mutate_specification(
    const Specification& specification, const RandomSource& random_source,
    const Config& cfg, const ParentContextT<Specification>* parent = nullptr,
    bool* stepped = nullptr);

/// Crossover of @p first with @p second, field by field: one slot pair
/// (assumption or guarantee, by index over the slots both have) and one
/// field of it. The pattern takes the donor's (its count included), the
/// count alone is swapped where both are one bounded pattern, the scope
/// takes the donor's, and a formula field read on both sides is crossed with
/// the FRETISH subformula graft. Fields the result reads and the target did
/// not are taken from the donor. Returns @p first unchanged where the draw
/// lands on a fixed, removed or unweakenable slot.
Specification crossover_specifications(const Specification& first,
                                       const Specification& second,
                                       const RandomSource& random_source);

}  // namespace psp
