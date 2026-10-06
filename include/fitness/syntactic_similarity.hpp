#pragma once

/// @file syntactic_similarity.hpp
/// @brief Syntactic similarity between requirements and specifications by
///        comparing shared sub-formula structure.

#include <cstddef>

#include "config.hpp"
#include "requirement.hpp"

/// Computes syntactic similarity between two requirements as the equally
/// weighted mean of five components: the formula-level similarity of the two
/// triggers, that of the two responses, and one each for the timing, the scope
/// and the condition type.
///
/// The timing, scope and condition-type components are all Jaccard overlaps of
/// downward closures in the field's implication order, so none of them carries
/// a chosen penalty. The condition-type order has two elements — Continual
/// implies Trigger — which fixes its two values at 1 and 1/2.
///
/// The scope component is the exception, being the Jaccard overlap of the
/// timepoint regions the two scopes enforce over, averaged with whether they
/// name the same mode. It is deliberately not the implication order the
/// `p_scope` mutation arm walks: that order depends on the timing as well as
/// the scope, and a similarity between two scopes has to be a property of the
/// scopes alone.
///
/// @param requirement       The first requirement to compare
/// @param other_requirement The second requirement to compare
/// Under KeywordSimilarity::Semantic the last three components are the token
/// measures instead (timing_token_similarity and its siblings), the order
/// measures having moved to the semantic objective.
///
/// @param cfg               Its keyword_similarity picks the keyword measures
/// @return                  A syntactic similarity score in the range [0, 1]
double syntactic_similarity(const Requirement& requirement,
                            const Requirement& other_requirement,
                            const Config& cfg);

/// Computes syntactic similarity between two specifications from five
/// components: the formula-level similarity of the two trigger conjunctions,
/// that of the two response conjunctions, and the per-index averages of the
/// requirements' timing, scope and condition-type similarities. Each
/// specification's triggers are conjoined into one formula and its responses
/// into another. The five components are combined as an equally-weighted mean.
///
/// The last three components pair requirements by index rather than conjoining
/// them, because slot i of a candidate descends from slot i of the original and
/// that is the only pairing which compares a requirement against what it came
/// from. Each divides by the larger of the two requirement counts, so a
/// candidate that gained an assumption or tombstoned a guarantee scores below 1
/// on all three even where every matched pair agrees. Adding the scope and
/// condition-type components therefore weighted that size-mismatch signal more
/// heavily, and it moved FRETISH search results: 12 of 15 seeded runs over the
/// three FRETISH examples returned different repairs, two of them one repair
/// more.
///
/// Both specifications must have at least one requirement; this is asserted,
/// so it goes unchecked under NDEBUG.
///
/// @param specification       The first specification to compare (non-empty)
/// @param other_specification The second specification to compare (non-empty)
/// @param cfg                 Its keyword_similarity picks the keyword
///                            measures, as for the requirement-level score
/// @return                    A syntactic similarity score in the range [0, 1]
double syntactic_similarity(const Specification& specification,
                            const Specification& other_specification,
                            const Config& cfg);

/// The timing term of the order-based keyword measure: the Jaccard overlap of
/// the two timings' downward closures in the timing implication order, under a
/// geometric weighting of the tick families. A stop timing sits outside the
/// order and scores by its kind and stop formula.
/// @return A similarity in [0, 1]
double timing_order_similarity(const Timing& tim, const Timing& tim_other);

/// The scope term of the order-based keyword measure: the Jaccard overlap of
/// the timepoint regions the two scopes enforce over, averaged with whether
/// they name the same mode.
/// @return A similarity in [0, 1]
double scope_order_similarity(const Scope& lhs, const Scope& rhs);

/// The condition-type term of the order-based keyword measure: Jaccard on
/// downsets of the order in which Continual implies Trigger, so 1 for an equal
/// pair and 1/2 otherwise.
/// @return 1.0 or 0.5
double condition_type_order_similarity(ConditionType lhs, ConditionType rhs);

/// The mean of timing_order_similarity, scope_order_similarity and
/// condition_type_order_similarity over a requirement pair. The keyword side of
/// the semantic term under KeywordSimilarity::Semantic.
/// @return A similarity in [0, 1]
double keyword_order_similarity(const Requirement& requirement,
                                const Requirement& other_requirement);

/// The timing term under KeywordSimilarity::Semantic, by syntax alone. Two
/// timings with no argument score 1 if they are the same keyword and 0
/// otherwise. Otherwise the score is the mean of the keyword match and the
/// argument match: two tick counts match when equal, two stops by their
/// formulas' syntactic similarity, and anything else not at all. So `within 5`
/// against `after 5` or `within 6` scores 1/2, and against `always` 0.
/// @return A similarity in [0, 1]
double timing_token_similarity(const Timing& tim, const Timing& tim_other);

/// The scope term under KeywordSimilarity::Semantic: the mean of whether the
/// two scope kinds are equal and whether the two modes are.
/// @return 0, 0.5 or 1
double scope_token_similarity(const Scope& lhs, const Scope& rhs);

/// The condition-type term under KeywordSimilarity::Semantic: 1 if equal, else
/// 0.
/// @return 0 or 1
double condition_type_token_similarity(ConditionType lhs, ConditionType rhs);
