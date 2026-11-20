#pragma once

/// @file psp/seeding.hpp
/// @brief Single-slot faults seeded into a realisable PSP specification, for
///        the PSP seeded corpus: each edit strengthens one guarantee by a step
///        of the measured orders, or moves it sideways.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "psp/requirement.hpp"
#include "psp/specification.hpp"

namespace psp {

/// How a seeded guarantee relates to the one it replaces, by
/// requirement_implies alone.
enum class SeedRelation : std::uint8_t {
    /// The rule proves the edit implies the slot. It may still be
    /// equivalent to it, as `cruise & !brake` is to `cruise` in a scope that
    /// closes on brake; `compare` decides.
    Stronger,
    /// The rule proves neither direction. `compare` decides the
    /// specification's relation to its base, as the FRETISH sideways corpus
    /// does: an edit the rule leaves unproven can still be stronger.
    Sideways,
};

/// The name `index.tsv` gives @p relation: "stronger" or "sideways".
const char* seed_relation_name(SeedRelation relation);

/// One seeded fault: guarantee @p m_slot of the base replaced by
/// @p m_requirement.
struct SeededEdit {
    /// The guarantee's index in the base.
    std::size_t m_slot = 0;
    /// The fault class:
    ///
    /// - `pattern-step`: the pattern moved to a cover above it in the order
    ///   of the scope's class (a bounded one at the slot's count or at a
    ///   count of the specification);
    /// - `pattern-swap`: the pattern moved to one that reads the same fields
    ///   and that the order does not compare with it (precedence for
    ///   response, say);
    /// - `scope-step`: the scope moved to a cover above it in the pattern's
    ///   cell;
    /// - `scope-swap`: the scope moved to one the cell does not compare with
    ///   it, its opener taking the old closer or the reverse where one side
    ///   reads only one (before R read as after R);
    /// - `swap-delimiters`: Q and R exchanged under between or after-until;
    /// - `count-step`: a bounded pattern's count moved one or a factor of two
    ///   in the stronger direction;
    /// - `narrow-field`: a literal conjoined to P, S, Q or R where the field
    ///   is monotone, or disjoined where it is antitone;
    /// - `flip-literal`: one atom occurrence in a field negated, or its
    ///   negation removed;
    /// - `swap-signal`: one atom occurrence replaced by another atom of the
    ///   same kind, input for input and output for output.
    std::string m_edit;
    /// The field the edit moved: "pattern", "count", "scope", "p", "s", "q"
    /// or "r" ("q,r" for swap-delimiters).
    std::string m_field;
    /// The edited guarantee.
    Requirement m_requirement;
    /// Its relation to the base's guarantee by rule.
    SeedRelation m_relation = SeedRelation::Sideways;
};

/// Every single-slot fault of @p base that the rule does not prove weaker
/// than or equal to the slot it replaces, in a fixed order: slot by slot,
/// then by fault class in the order SeededEdit lists them. Fixed, removed
/// and unweakenable guarantees and the assumptions are never edited, and an
/// edit that makes the guarantee equal to another live one of @p base is
/// left out, since the specification would then drop it.
///
/// A field that a pattern or scope move makes the requirement read takes,
/// in turn, each formula of the specification that some live requirement
/// reads in that role or, for Q and R, each input atom; so one move can give
/// several edits.
std::vector<SeededEdit> seeded_edits(const Specification& base);

/// @p base with guarantee @p edit.m_slot replaced by @p edit.m_requirement.
Specification apply_seeded_edit(const Specification& base,
                                const SeededEdit& edit);

}  // namespace psp
