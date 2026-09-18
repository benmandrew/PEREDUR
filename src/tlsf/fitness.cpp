#include "tlsf/fitness.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fitness/semantic_similarity.hpp"
#include "fitness/status.hpp"
#include "guarantee_parts.hpp"
#include "prop_formula.hpp"
#include "prop_formula/atoms.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/mucs.hpp"

namespace {

using tlsf::Section;
using tlsf::SectionEntry;

constexpr std::size_t k_n_sections = 6;

// Semantic similarity of a single (changed) formula pair. Counts the bounded
// traces of both formulae and their conjunction over one shared atom universe
// (the union of both formulae's atoms, so the conjunction count never exceeds
// either individual count), then hands the three counts to the shared
// semantic_similarity_from_counts -- the same routine the FRETISH requirement
// pairs use -- so the configured metric (direct or logarithmic) and its [0, 1]
// clamp apply here too.
double formula_pair_semantic_similarity(const Formula& first,
                                        const Formula& second,
                                        std::size_t bound,
                                        SimilarityMetric metric) {
    std::unordered_set<std::string> atoms;
    collect_atoms(first, atoms);
    collect_atoms(second, atoms);
    const std::size_t n_atoms = atoms.size();
    if (n_atoms == 0) {
        return first == second ? 1.0 : 0.0;
    }
    const std::string ltl_first = first.to_string();
    const std::string ltl_second = second.to_string();
    const std::string conjunction =
        "(" + ltl_first + ") & (" + ltl_second + ")";
    // Clamp before counting: past max_representable_step_count the products
    // inside count_traces saturate to infinity, and the assert that catches it
    // is compiled out under NDEBUG, so an unclamped bound yields a silently
    // wrong score in release rather than aborting. TLSF has no timing horizon
    // to raise the bound to (the temporal structure is in the formula itself),
    // so the ceiling is the only adjustment.
    const std::size_t step_count =
        std::min(bound, max_representable_step_count(n_atoms));
    // cached_count_traces, not count_traces: the original spec's formulae are
    // re-counted against every offspring in the population, so one shared
    // memo covers the whole run -- and it is the same cache the FRETISH path
    // uses.
    const SemanticSimilarityCounts counts{
        cached_count_traces(ltl_first, n_atoms, step_count),
        cached_count_traces(ltl_second, n_atoms, step_count),
        cached_count_traces(conjunction, n_atoms, step_count)};
    return semantic_similarity_from_counts(counts, metric);
}

}  // namespace

double tlsf_syntactic_similarity(const tlsf::Specification& spec,
                                 const tlsf::Specification& original,
                                 [[maybe_unused]] const Config& cfg) {
    double total = 0.0;
    std::size_t n_pairs = 0;
    const auto spec_sections = tlsf::sections_of(spec);
    const auto original_sections = tlsf::sections_of(original);
    for (std::size_t section = 0; section < k_n_sections; ++section) {
        const Section& lhs = *spec_sections[section];
        const Section& rhs = *original_sections[section];
        const std::size_t paired = std::min(lhs.size(), rhs.size());
        for (std::size_t i = 0; i < paired; ++i) {
            // A conjunct deleted on one side alone has nothing to compare
            // against, and deletion is the largest change a slot admits, so it
            // scores zero. Deleted on both, the slot matches.
            if (lhs[i].m_removed || rhs[i].m_removed) {
                total += lhs[i].m_removed && rhs[i].m_removed ? 1.0 : 0.0;
                continue;
            }
            total += lhs[i].m_formula.syntactic_similarity(rhs[i].m_formula);
        }
        // Missing pairs (the size difference) contribute similarity 0.
        n_pairs += std::max(lhs.size(), rhs.size());
    }
    if (n_pairs == 0) {
        return 1.0;
    }
    return total / static_cast<double>(n_pairs);
}

namespace {

std::vector<std::function<double()>> tlsf_semantic_similarity_terms(
    const tlsf::Specification& spec, const tlsf::Specification& original,
    const Config& cfg) {
    const std::size_t bound = cfg.default_model_counting_bound;
    const SimilarityMetric metric = cfg.similarity_metric;
    std::vector<std::function<double()>> terms;
    const auto spec_sections = tlsf::sections_of(spec);
    const auto original_sections = tlsf::sections_of(original);
    for (std::size_t section = 0; section < k_n_sections; ++section) {
        const Section& lhs = *spec_sections[section];
        const Section& rhs = *original_sections[section];
        const std::size_t paired = std::min(lhs.size(), rhs.size());
        for (std::size_t i = 0; i < paired; ++i) {
            if (lhs[i] == rhs[i]) {
                continue;
            }
            // Deleted on one side only: a real change, and the largest the slot
            // admits, so it scores zero. There is no formula left to count, and
            // counting the survivor against nothing would spend a model count
            // on an answer already known. It is still a term rather than a
            // skipped pair, because it counts toward the mean.
            if (lhs[i].m_removed || rhs[i].m_removed) {
                terms.emplace_back([] { return 0.0; });
                continue;
            }
            terms.emplace_back([&first = lhs[i].m_formula,
                                &second = rhs[i].m_formula, bound, metric] {
                return formula_pair_semantic_similarity(first, second, bound,
                                                        metric);
            });
        }
    }
    return terms;
}

/// The fold both part-wise similarity objectives share: the mean of the terms,
/// an empty set of terms meaning nothing differed and scoring a perfect match.
double mean_or_perfect(const std::vector<double>& values) {
    if (values.empty()) {
        return 1.0;
    }
    double total = 0.0;
    for (const double value : values) {
        total += value;
    }
    return total / static_cast<double>(values.size());
}

}  // namespace

double tlsf_semantic_similarity(const tlsf::Specification& spec,
                                const tlsf::Specification& original,
                                const Config& cfg) {
    const std::vector<std::function<double()>> terms =
        tlsf_semantic_similarity_terms(spec, original, cfg);
    if (terms.empty()) {
        return 1.0;
    }
    double total = 0.0;
    for (const std::function<double()>& term : terms) {
        total += term();
    }
    return total / static_cast<double>(terms.size());
}

std::vector<std::string> tlsf_status_components(
    const tlsf::Specification& spec) {
    // A TLSF specification's components are the individual formulae of its six
    // sections; the FRETISH path decomposes differently but scores on the same
    // scale, which is why both route through status_score.
    std::vector<std::string> components;
    for (const Section* section : tlsf::sections_of(spec)) {
        for (const SectionEntry& entry : *section) {
            // A deleted conjunct is not a component of the specification;
            // scoring its satisfiability would charge a candidate for content
            // it no longer has.
            if (entry.m_removed) {
                continue;
            }
            components.push_back(entry.m_formula.to_string());
        }
    }
    return components;
}

double tlsf_status(const tlsf::Specification& spec, const Config& cfg,
                   const std::vector<std::size_t>& admission_order,
                   ComponentCheck component_check) {
    SatisfiabilityChecker& sat = global_sat_checker();
    RealizabilityChecker& real = global_real_checker();
    if (cfg.status_grading == StatusGrading::Aurus) {
        // Sides rather than section formulae, and no component tier.
        // assumption_ltl() and guarantee_ltl() are AuRUS's own environment and
        // system formulae -- INITIALLY & G REQUIRE & ASSUME against PRESET &
        // G ASSERT & GUARANTEE -- so the ladder asks what it asks there.
        return status_score_aurus(
            spec.assumption_ltl(), spec.guarantee_ltl(), sat, [&spec, &real] {
                // Realizability alone, with no well-separation query behind
                // it, unlike either branch below. See status_score_aurus.
                return real
                    .check_realizability_ltl(spec.to_ltl(), spec.m_inputs,
                                             spec.m_outputs,
                                             tlsf::specification_sides(spec))
                    .value_or(false);
            });
    }
    // An empty component list passes the tier vacuously, which is exactly what
    // ComponentCheck::Skipped asks for; both scales below already handle it.
    const std::vector<std::string> components =
        component_check == ComponentCheck::Included
            ? tlsf_status_components(spec)
            : std::vector<std::string>{};
    if (cfg.status_grading == StatusGrading::Mrs) {
        const std::vector<tlsf::CoreFormula> parts =
            tlsf::split_guarantee_parts(spec);
        return status_score_mrs(
            components, parts.size(), sat,
            [&spec, &parts, &real](const std::vector<std::size_t>& indices) {
                const tlsf::Specification subset =
                    tlsf::build_part_subset(spec, parts, indices);
                // Undecided resolves as unrealizable, so the part is rejected:
                // a timed-out query must not buy a candidate a point.
                return real.check_realizability_ltl(
                               subset.to_ltl(), subset.m_inputs,
                               subset.m_outputs,
                               tlsf::specification_sides(subset))
                           .value_or(false) &&
                       !tlsf_is_not_well_separated(subset, real);
            },
            admission_order);
    }

    return status_score(components, sat, [&spec, &real] {
        const bool realizable =
            real.check_realizability_ltl(spec.to_ltl(), spec.m_inputs,
                                         spec.m_outputs,
                                         tlsf::specification_sides(spec))
                .value_or(false);
        // Behind the realizability query, as on the FRETISH path: an
        // unrealizable candidate cannot be realizable for the wrong reason.
        return realizable && !tlsf_is_not_well_separated(spec, real);
    });
}

namespace {

// The admission order for the whole run, computed here rather than at a call
// site because this is the one place that sees the specification being evolved
// and is entered once, before anything is scored -- including once per core
// under repair_mode = "muc", where the sub-specification is what gets walked.
// Under MrsAdmissionOrder::Spec it costs nothing and returns empty, which the
// walk reads as index order.
/// The state a decomposed objective needs, held once for the run.
struct SimilarityContext {
    tlsf::Specification original;
    Config cfg;
};

struct StatusContext {
    Config cfg;
    std::vector<std::size_t> order;
};

std::vector<std::size_t> tlsf_mrs_admission_order(
    const tlsf::Specification& original, const Config& cfg) {
    if (cfg.status_grading != StatusGrading::Mrs ||
        cfg.mrs_admission_order != MrsAdmissionOrder::Degree) {
        return {};
    }
    const std::vector<tlsf::CoreFormula> parts =
        tlsf::split_guarantee_parts(original);
    RealizabilityChecker& real = global_real_checker();
    return conflict_degree_order(
        parts.size(),
        [&original, &parts, &real](const std::vector<std::size_t>& indices) {
            const tlsf::Specification subset =
                tlsf::build_part_subset(original, parts, indices);
            // The same oracle tlsf_status walks with, undecided resolving as
            // unrealizable in the same direction.
            return real.check_realizability_ltl(
                           subset.to_ltl(), subset.m_inputs, subset.m_outputs,
                           tlsf::specification_sides(subset))
                       .value_or(false) &&
                   !tlsf_is_not_well_separated(subset, real);
        });
}

}  // namespace

AggregateWeightedFitnessFunctionT<tlsf::Specification>
tlsf_get_fitness_function(const tlsf::Specification& original,
                          const Config& cfg) {
    std::vector<WeightedFitnessFunctionT<tlsf::Specification>> functions;
    // Held once for the run rather than copied into every part: the original is
    // a whole Specification and Config is large, and a scoring region builds
    // parts for every candidate in the population.
    const auto ctx = std::make_shared<const SimilarityContext>(
        SimilarityContext{original, cfg});
    if (cfg.fitness_weight_syntactic > 0.0) {
        auto synsim = [ctx](const tlsf::Specification& spec) {
            return tlsf_syntactic_similarity(spec, ctx->original, ctx->cfg);
        };
        // One part, and the cheapest kind there is: this objective never leaves
        // the process, so splitting its arithmetic across workers would cost
        // more in dispatch than it could save. It is still declared rather than
        // left undecomposed, so the launch order knows it is free.
        auto split = [synsim](const tlsf::Specification& spec) {
            ObjectiveWork work;
            work.parts.push_back({[synsim, &spec] { return synsim(spec); },
                                  k_part_cost_in_process});
            work.combine = [](const std::vector<double>& values) {
                return values.front();
            };
            return work;
        };
        functions.push_back(
            {synsim, cfg.fitness_weight_syntactic, "syntactic", split});
    }
    if (cfg.fitness_weight_semantic > 0.0) {
        auto semsim = [ctx](const tlsf::Specification& spec) {
            return tlsf_semantic_similarity(spec, ctx->original, ctx->cfg);
        };
        // One part per changed section slot, each three bounded model counts
        // over a formula pair independent of every other slot's.
        auto split = [ctx](const tlsf::Specification& spec) {
            ObjectiveWork work;
            for (std::function<double()>& term : tlsf_semantic_similarity_terms(
                     spec, ctx->original, ctx->cfg)) {
                work.parts.push_back(
                    {std::move(term), k_part_cost_model_count});
            }
            work.combine = mean_or_perfect;
            return work;
        };
        functions.push_back(
            {semsim, cfg.fitness_weight_semantic, "semantic", split});
    }
    if (cfg.fitness_weight_status > 0.0) {
        const auto status_ctx = std::make_shared<const StatusContext>(
            StatusContext{cfg, tlsf_mrs_admission_order(original, cfg)});
        auto status = [status_ctx](const tlsf::Specification& spec) {
            return tlsf_status(spec, status_ctx->cfg, status_ctx->order);
        };
        // One part per component satisfiability query, plus the realizability
        // walk. The walk is handed ComponentCheck::Skipped and the fold applies
        // the component tier from those parts, so no query is asked twice.
        //
        // What that gives up is the walk's short circuit: a candidate with an
        // unsatisfiable component now pays its synthesis queries rather than
        // being graded before they start. The guard cannot be kept without
        // either duplicating every component query or serialising the walk
        // behind them, and the queries it would have saved are the ones
        // RealizabilityChecker memoises most heavily.
        auto split = [status_ctx](const tlsf::Specification& spec) {
            ObjectiveWork work;
            std::vector<std::string> components = tlsf_status_components(spec);
            const std::size_t n_components = components.size();
            for (std::string& component : components) {
                work.parts.push_back(
                    {[formula = std::move(component)] {
                         return global_sat_checker()
                                        .check_satisfiability(formula)
                                        .value_or(false)
                                    ? 1.0
                                    : k_status_component_unsatisfiable;
                     },
                     k_part_cost_satisfiability});
            }
            // One synthesis query per live guarantee conjunct, the greedy
            // walk's worst case. Only the order this induces is read, so an
            // upper bound is the right shape of estimate.
            work.parts.push_back(
                {[status_ctx, &spec] {
                     return tlsf_status(spec, status_ctx->cfg,
                                        status_ctx->order,
                                        ComponentCheck::Skipped);
                 },
                 k_part_cost_synthesis *
                     static_cast<double>(tlsf::count_live_guarantees(spec))});
            work.combine = [n_components](const std::vector<double>& values) {
                for (std::size_t i = 0; i < n_components; ++i) {
                    if (values[i] == k_status_component_unsatisfiable) {
                        return k_status_component_unsatisfiable;
                    }
                }
                return values.back();
            };
            return work;
        };
        functions.push_back(
            {status, cfg.fitness_weight_status, "status", split});
    }
    return AggregateWeightedFitnessFunctionT<tlsf::Specification>(
        std::move(functions));
}
