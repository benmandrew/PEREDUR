#pragma once

/// @file order_moves.hpp
/// @brief The order moves on PSP requirements: how a child relates to its
///        parent, the join and meet of two requirements, the proved steps
///        back towards an original, the count chain a bisected step crosses,
///        and the empty-scope test behind the gutting flag.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "genetic/order_step.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"

namespace psp {

/// The field of a PSP requirement that a step moves. The count moves with
/// the pattern, since it is part of it.
enum class RequirementField : std::uint8_t { Pattern, Scope, P, S, Q, R };

/// One proved strengthening of a PSP specification.
using Step = OrderStep<Specification, RequirementField>;

/// How @p child relates to @p parent, by the rule alone.
MoveOrder classify_move(const Specification& parent,
                        const Specification& child);

/// The least requirement the rule proves implied by both @p lhs and @p rhs,
/// or nothing where none is found. A tombstone, a fixed requirement and one
/// that is not weakenable have no bound but themselves.
std::optional<Requirement> requirement_join(const Requirement& lhs,
                                            const Requirement& rhs);

/// The dual of requirement_join: a requirement the rule proves implies both.
std::optional<Requirement> requirement_meet(const Requirement& lhs,
                                            const Requirement& rhs);

/// The proved one-field steps from @p current towards @p original, as the
/// FRETISH steps_towards: a guarantee moves towards its join with the
/// original's slot and an assumption towards its meet, a tombstoned guarantee
/// is restored, and an appended assumption is dropped. Each step is proved
/// stronger by classify_move.
std::vector<Step> steps_towards(const Specification& current,
                                const Specification& original);

/// The counts strictly between @p from and @p dest, ordered from @p from,
/// with @p dest last; nothing where the two are equal.
std::vector<std::size_t> count_chain(std::size_t from, std::size_t dest);

/// The count chain a pattern step crosses: the step moves one bounded
/// pattern to another count of the same pattern. Nothing for any other step.
std::vector<std::size_t> count_step_chain(const Specification& current,
                                          const Step& step);

/// @p current with the count of slot @p slot set to @p count, or nothing
/// where the slot has no count, the result merges two entries, or the rule
/// does not prove the result stronger.
std::optional<Specification> with_slot_count(const Specification& current,
                                             StepSide side, std::size_t slot,
                                             std::size_t count);

/// The order-aware keyword similarity of two requirements: the mean of the
/// Jaccard similarities of the downsets of their patterns (under the scope
/// class of @p lhs) and of their scopes (in the pattern cell of @p lhs).
double keyword_similarity(const Requirement& lhs, const Requirement& rhs);

/// The token similarity of the two keywords: 1 for the same pattern (count
/// included) and scope, and half for one of the two.
double keyword_token_similarity(const Requirement& lhs, const Requirement& rhs);

/// Whether @p req's scope can never open: R valid under before, Q
/// unsatisfiable under after, and Q implying R under between and
/// after-until, where the scope opens only at `Q & !R`. Every requirement
/// with an empty scope holds vacuously. Fixed requirements are never empty.
bool empties_scope(const Requirement& req);

/// The live guarantee slots whose scope is empty in @p repair and not in
/// @p original: the gutting moves the output flags.
std::vector<std::size_t> gutted_guarantees(const Specification& original,
                                           const Specification& repair);

}  // namespace psp
