#pragma once

/// @file fitness.hpp
/// @brief TLSF analogues of the FRETISH fitness components (syntactic,
///        semantic, status) and the factory assembling them into a
///        weighted aggregate over tlsf::Specification.

#include <cstddef>
#include <string>
#include <vector>

#include "config.hpp"
#include "fitness/function.hpp"
#include "fitness/status.hpp"
#include "tlsf/specification.hpp"

/// Positional per-section syntactic similarity of @p spec against @p original.
/// For each of the six sections, formula i of @p spec is paired with formula i
/// of @p original over `min(size)`; a size difference contributes similarity 0
/// for each missing pair. The result is the average of Formula::syntactic
/// similarity over all such pairs across all sections, in [0, 1]. When both
/// specifications hold no formulae the result is 1.0.
double tlsf_syntactic_similarity(const tlsf::Specification& spec,
                                 const tlsf::Specification& original,
                                 const Config& cfg);

/// Positional per-section semantic similarity of @p spec against @p original.
/// Identical formula pairs are excluded; each differing pair contributes the
/// harmonic mean of its two bounded-model-counting containment ratios at bound
/// `cfg.default_model_counting_bound`. The result averages over the differing
/// pairs, in [0, 1]. When no pair differs the result is 1.0.
double tlsf_semantic_similarity(const tlsf::Specification& spec,
                                const tlsf::Specification& original,
                                const Config& cfg);

/// Realizability status of @p spec on the shared scale of `fitness/status.hpp`:
///   0.0 — some section formula is individually unsatisfiable
///   0.5 — every section formula is satisfiable but the lowering is
///         unrealizable
///   1.0 — the lowering is realizable
///
/// The components are the individual formulae of all six sections. Scoring is
/// delegated to `status_score`, which the FRETISH path also uses, so the two
/// front ends cannot drift onto different scales again.
///
/// Under StatusGrading::Aurus the scale is instead AuRUS's six-level ladder
/// over the two sides as whole formulae (`assumption_ltl()` against
/// `guarantee_ltl()`, which are its environment and system formulae), through
/// `status_score_aurus`. That branch has no component tier and asks no
/// well-separation query; see the header of `fitness/status.hpp`.
///
/// @p admission_order indexes the parts `tlsf::split_guarantee_parts` returns
/// and is read only under StatusGrading::Mrs; empty means index order. It is
/// projected onto the candidate's own part count, since mutation rewrites a
/// formula into a different number of conjuncts.
/// Before that walk, the conjunction `assumption_ltl() & guarantee_ltl()` is
/// tested for satisfiability once the components pass, and scores 0.0 when it
/// fails.
///
/// @p component_check says whether the score tests its own components; the
/// split scoring path runs each of those queries as a dispatch item of its own
/// and passes ComponentCheck::Skipped so the same query is not asked twice.
double tlsf_status(const tlsf::Specification& spec, const Config& cfg,
                   const std::vector<std::size_t>& admission_order = {},
                   ComponentCheck component_check = ComponentCheck::Included);

/// The components @ref tlsf_status tests individually: the formula of every
/// live entry of all six sections, in section order. Exposed so a scoring pool
/// can run each one's `black` query concurrently rather than leaving them to
/// the sequential fold inside the score.
std::vector<std::string> tlsf_status_components(
    const tlsf::Specification& spec);

/// Builds the weighted aggregate of the three TLSF fitness components, gated on
/// the same `cfg.fitness_weight_*` fields the FRETISH factory uses. Components
/// with a non-positive weight are omitted.
AggregateWeightedFitnessFunctionT<tlsf::Specification>
tlsf_get_fitness_function(const tlsf::Specification& original,
                          const Config& cfg);
