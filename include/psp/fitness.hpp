#pragma once

/// @file fitness.hpp
/// @brief The PSP objectives: syntactic similarity, semantic similarity and
///        status, built into a fitness function by the factory the other
///        front ends share.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "config.hpp"
#include "fitness/function.hpp"
#include "fitness/status.hpp"
#include "psp/requirement.hpp"
#include "psp/specification.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"

namespace psp {

/// The syntactic similarity of two requirements: the mean of the keyword
/// token similarity (pattern with its count, and scope) and of the formula
/// similarity of each of P, S, Q and R that either side reads, a field one
/// side alone reads scoring 0. Two fixed requirements compare their formulas.
double syntactic_similarity(const Requirement& lhs, const Requirement& rhs);

/// The per-slot mean of syntactic_similarity, paired by index as on FRETISH:
/// a surplus slot scores 0 and the mean is over the larger side, and a slot
/// removed on one side alone scores 0.
double syntactic_similarity(const Specification& spec,
                            const Specification& original);

/// One term per slot that differs from @p original's: the mean of the
/// model-counting similarity of the two lowerings and keyword_similarity,
/// as the FRETISH pair score. A slot removed on one side alone scores 0.
std::vector<std::function<double()>> semantic_similarity_terms(
    const Specification& spec, const Specification& original,
    std::size_t step_count);

/// The status components: the lowering of each live requirement, each a
/// satisfiability query ahead of the realizability walk.
std::vector<std::string> specification_status_components(
    const Specification& spec);

/// The status of @p spec as on FRETISH: the MRS walk over the live
/// guarantees, each subset checked by ltlsynt with the well-separation test,
/// or the AuRUS ladder under StatusGrading::Aurus.
double specification_status(const Specification& spec,
                            SatisfiabilityChecker& sat,
                            RealizabilityChecker& real, StatusGrading grading,
                            const std::vector<std::size_t>& slot_order,
                            ComponentCheck component_check,
                            std::size_t speculation_depth,
                            std::vector<std::size_t>* refused_slots);

/// Whether @p spec carries a valid live guarantee or jointly unsatisfiable
/// live assumptions, either of which makes it realizable for nothing.
bool specification_is_vacuous(const Specification& spec,
                              SatisfiabilityChecker& sat);

/// The PSP fitness function over @p original.
AggregateWeightedFitnessFunctionT<Specification> get_fitness_function(
    const Specification& original, const Config& cfg);

}  // namespace psp
