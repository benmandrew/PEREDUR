#pragma once

/// @file crossover.hpp
/// @brief Crossover operators that combine two parent requirements or
///        specifications to produce an offspring.

#include "genetic/random_source.hpp"
#include "requirement.hpp"

/// A subformula graft of @p second_parent into @p first_parent: a replaced
/// subformula or a fresh binary join, with equal probability, at a site drawn
/// uniformly over the nodes of @p first_parent. The PSP crossover reuses it
/// for each formula field.
Formula crossover_formula(const Formula& first_parent,
                          const Formula& second_parent,
                          const RandomSource& random_source);

/// Produces an offspring requirement by crossing over every field of the
/// parents but the two flags. Each of the two formula fields is grafted as in
/// AuRUS: with equal probability a subformula of the first parent's field is
/// replaced by one drawn from the second parent's, or the two are joined under
/// a fresh binary operator. Neither branch copies a field verbatim.
///
/// The graft site is drawn uniformly over the field's nodes, so every node is
/// as likely to be rewritten as any other.
///
/// The timing, condition type and scope are each taken from one parent on a
/// coin, a timing that carries a tick count possibly taking the other
/// parent's. The scope keeps its mode, so the two parents must declare the
/// same modes, which crossover_specifications checks before calling this.
///
/// @param first_parent  First parent requirement
/// @param second_parent Second parent requirement
/// @param random_source Random source for branch and selector choices
/// @return              Offspring requirement
Requirement crossover_requirements(const Requirement& first_parent,
                                   const Requirement& second_parent,
                                   const RandomSource& random_source);

/// Produces an offspring specification by merging one requirement per side.
/// For the assumptions and again for the guarantees, one slot of @p
/// first_parent and one of @p second_parent are drawn uniformly and
/// independently — the donor need not, and usually does not, occupy the target
/// slot — and their crossover replaces the target slot. Every other slot is
/// inherited from @p first_parent, whose shape the offspring therefore keeps.
/// Deleted and non-weakenable requirements take no part on either side.
///
/// @param first_parent  First parent specification
/// @param second_parent Second parent specification
/// @param random_source Random source for slot and branch choices
/// @return              Offspring specification, or @p first_parent unchanged
///                      if the parents' in/out atoms differ or either side has
///                      no eligible slot
Specification crossover_specifications(const Specification& first_parent,
                                       const Specification& second_parent,
                                       const RandomSource& random_source);
