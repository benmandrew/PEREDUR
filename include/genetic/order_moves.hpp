#pragma once

/// @file order_moves.hpp
/// @brief The implication order over FRETISH specifications, decided by rule:
///        classifying a move, slot-wise joins and meets, and the
///        strengthenings that step a candidate back towards its original.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "requirement.hpp"

/// How a child specification relates to its parent under implication of the
/// whole assume-guarantee formula `A -> G`.
enum class MoveOrder : std::uint8_t {
    /// Each implies the other.
    Equal,
    /// The child implies the parent, and the converse is not proved.
    Stronger,
    /// The parent implies the child, and the converse is not proved.
    Weaker,
    /// Neither direction is proved.
    Incomparable,
};

/// Whether @p stronger implies @p weaker as specifications, proved by rule
/// with no solver. True only where it is proven, so false means unproven.
///
/// Every live guarantee of @p weaker must be implied by a live guarantee of
/// @p stronger, and every live assumption of @p stronger by a live assumption
/// of @p weaker. A requirement implies another here when the two lower
/// identically, wherever they sit, or when requirement_implies proves it of
/// the two in the same slot. The assumption side runs the other way because
/// `A -> G` is antitone in `A`. Appended assumptions do not pair by index, and
/// the identical-lowering test is what matches them.
bool specification_implies_by_rule(const Specification& stronger,
                                   const Specification& weaker);

/// The order of @p child relative to @p parent, from
/// specification_implies_by_rule in both directions. A weaker guarantee, a
/// stronger assumption, an appended assumption and a tombstoned guarantee each
/// weaken the specification; their inverses strengthen it. Several moved
/// slots combine: all weaker or equal reads Weaker, all stronger or equal
/// reads Stronger, and a mix reads Incomparable.
MoveOrder classify_move(const Specification& parent,
                        const Specification& child);

/// A requirement implying both @p lhs and @p rhs, two live requirements of the
/// same slot, or nothing where none is proved.
///
/// Where one input is proved to imply the other, it is returned unchanged.
/// Otherwise each field is combined along its order: the condition type is
/// Continual if either is; the scope is the stronger of the two by
/// scope_implies, or Global where neither implies the other; the timing is a
/// weakest one implying both by timing_implies, enumerated up to one tick past
/// the inputs' largest count; the response is `p & q`, or `p | q` under an
/// `only` scope, and must be equivalent under `after n ticks`; a continual
/// condition is `c | c'`, and two triggers on inequivalent conditions join as
/// a continual `c | c'`; two stops of one kind combine along their polarity.
/// Where one propositional field implies the other the simpler of the two is
/// kept instead. The result is checked with requirement_implies against both
/// inputs, and nothing is returned unless both hold.
///
/// A locked requirement (`m_weakenable` false) joins only an equal one.
std::optional<Requirement> requirement_join(const Requirement& lhs,
                                            const Requirement& rhs);

/// A requirement implied by both @p lhs and @p rhs, two live requirements of
/// the same slot, or nothing where none is proved. The dual of
/// requirement_join: Trigger if either is, the weaker scope (and nothing where
/// the scopes are incomparable), a strongest timing both imply, `p | q` (or
/// `p & q` under an `only` scope), `c & c'` for a continual condition, and a
/// triggered condition only where the two are equivalent.
std::optional<Requirement> requirement_meet(const Requirement& lhs,
                                            const Requirement& rhs);

/// A slot-wise join or meet of two specifications descending from one
/// original. Each slot holds a proved requirement, possibly a tombstone, or
/// nothing where the slot is not proved. A caller filling the unproved slots
/// from a parent gets a partial bound, which carries no implication.
struct SlotwiseBound {
    /// One entry per assumption slot of the original.
    std::vector<std::optional<Requirement>> m_assumptions;
    /// One entry per guarantee slot.
    std::vector<std::optional<Requirement>> m_guarantees;
    /// The assumptions appended after the original's slots: those both inputs
    /// carry for a join, and the union for a meet.
    std::vector<Requirement> m_appended;

    /// True when every slot is proved.
    [[nodiscard]] bool is_complete() const;
};

/// A specification implying both @p lhs and @p rhs, slot by slot. The first
/// @p n_original_assumptions assumptions of each input pair by index, as do
/// the guarantees; a slot that only one input has is unproved.
///
/// A guarantee slot takes requirement_join of the pair, and is live if either
/// is. An original assumption slot takes the weaker of the pair through
/// requirement_meet, and is a tombstone if either is, since a tombstoned
/// assumption constrains nothing. Appended assumptions match by lowering.
SlotwiseBound specification_join(const Specification& lhs,
                                 const Specification& rhs,
                                 std::size_t n_original_assumptions);

/// A specification implied by both @p lhs and @p rhs, slot by slot. The dual
/// of specification_join: a guarantee slot takes requirement_meet and is a
/// tombstone if either is, an original assumption slot takes requirement_join
/// and is live if either is, and the appended assumptions are the union, in
/// @p lhs's order then @p rhs's.
SlotwiseBound specification_meet(const Specification& lhs,
                                 const Specification& rhs,
                                 std::size_t n_original_assumptions);

/// The specification a complete @p bound describes, with the atoms and modes
/// of @p shape, or nothing where a slot is unproved or the Specification
/// constructor would merge two equal entries and so shift every later slot.
std::optional<Specification> assemble(const SlotwiseBound& bound,
                                      const Specification& shape);

/// The side of a specification a step edits.
enum class StepSide : std::uint8_t { Assumption, Guarantee };

/// What a step changes.
enum class StepKind : std::uint8_t {
    /// One field of one slot moves to its target's value.
    Field,
    /// A tombstoned guarantee comes back with the fields it was removed with.
    Restore,
    /// An appended assumption is dropped.
    DropAssumption,
};

/// One field of a requirement.
enum class RequirementField : std::uint8_t {
    Condition,
    Response,
    Timing,
    ConditionType,
    Scope,
};

/// One proved strengthening of a specification.
struct Step {
    StepKind m_kind = StepKind::Field;
    StepSide m_side = StepSide::Guarantee;
    /// The slot edited, indexing `m_assumptions` or `m_guarantees` of the
    /// specification the step was taken from.
    std::size_t m_slot = 0;
    /// The field moved, for a Field step.
    std::optional<RequirementField> m_field;
    /// The specification after the step.
    Specification m_result;
};

/// The strengthenings of @p current that move it towards @p original, each
/// proved Stronger by classify_move. @p current descends from @p original, so
/// its slots pair with the original's by index.
///
/// A live guarantee slot that differs from the original's moves towards
/// `requirement_join(current, original)`, and an original assumption slot
/// towards `requirement_meet`, since weakening an assumption strengthens the
/// specification. Each field in which the slot differs from its target gives
/// one Field step where it is proved. Some field always is: the target was
/// proved by a chain of one-field steps ending at the slot, and the last link
/// of that chain is one of these. A tombstoned guarantee gives a Restore step
/// and each appended assumption a DropAssumption step. Locked slots never
/// move.
///
/// The steps come in slot order, assumptions first, then the drops. A step
/// that would make two entries equal, which the Specification constructor
/// would merge, is left out.
std::vector<Step> steps_towards(const Specification& current,
                                const Specification& original);

/// The timings on the chain of the order under @p scope from @p from towards
/// @p dest: those strictly between the two, in order from @p from, followed by
/// @p dest. A climb bisects this list for the furthest realisable entry.
///
/// The intermediate timings are the `within n` and `for n` timings with a
/// count from 1 to the larger of the two endpoints' counts, which every scope
/// orders totally. Returns nothing where @p from and @p dest are equal or not
/// comparable under timing_implies.
std::vector<Timing> timing_chain(const Timing& from, const Timing& dest,
                                 ScopeKind scope);

/// @p current with the timing of slot @p slot on side @p side replaced by
/// @p timing, or nothing where the Specification constructor would merge the
/// edited entry into another or the result is not proved Stronger by
/// classify_move.
std::optional<Specification> with_slot_timing(const Specification& current,
                                              StepSide side, std::size_t slot,
                                              const Timing& timing);

/// The timing_chain a Field step on the Timing field crosses from @p current,
/// its destination last, or nothing for any other step or where the two
/// timings are equal or not comparable.
std::vector<Timing> timing_step_chain(const Specification& current,
                                      const Step& step);
