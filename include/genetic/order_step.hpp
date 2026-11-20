#pragma once

/// @file order_step.hpp
/// @brief The language-neutral vocabulary of the order moves: how a child
///        relates to its parent, and one proved strengthening of a
///        specification.

#include <cstddef>
#include <cstdint>
#include <optional>

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

/// One proved strengthening of a specification of type @p Spec, whose
/// requirements have the fields that the enumeration @p Field names.
template <typename Spec, typename Field>
struct OrderStep {
    StepKind m_kind = StepKind::Field;
    StepSide m_side = StepSide::Guarantee;
    /// The slot edited, indexing the assumptions or the guarantees of the
    /// specification the step was taken from.
    std::size_t m_slot = 0;
    /// The field moved, for a Field step.
    std::optional<Field> m_field;
    /// The specification after the step.
    Spec m_result;
};
