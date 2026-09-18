#include "tlsf/fitness.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "filter/well_separation.hpp"
#include "fitness/factory.hpp"
#include "fitness/mean_or_perfect.hpp"
#include "fitness/semantic_similarity.hpp"
#include "fitness/status.hpp"
#include "guarantee_parts.hpp"
#include "prop_formula.hpp"
#include "prop_formula/atoms.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
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

}  // namespace

double tlsf_semantic_similarity(const tlsf::Specification& spec,
                                const tlsf::Specification& original,
                                const Config& cfg) {
    const std::vector<std::function<double()>> terms =
        tlsf_semantic_similarity_terms(spec, original, cfg);
    std::vector<double> values;
    values.reserve(terms.size());
    for (const std::function<double()>& term : terms) {
        values.push_back(term());
    }
    return mean_or_perfect(values);
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
                       !specification_is_not_well_separated(subset, real);
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
        return realizable && !specification_is_not_well_separated(spec, real);
    });
}

namespace {

struct TlsfScorers {
    static double syntactic(const tlsf::Specification& spec,
                            const tlsf::Specification& original,
                            const Config& cfg) {
        return tlsf_syntactic_similarity(spec, original, cfg);
    }

    static double semantic(const tlsf::Specification& spec,
                           const tlsf::Specification& original,
                           const Config& cfg) {
        return tlsf_semantic_similarity(spec, original, cfg);
    }

    static std::vector<std::function<double()>> semantic_terms(
        const tlsf::Specification& spec, const tlsf::Specification& original,
        const Config& cfg) {
        return tlsf_semantic_similarity_terms(spec, original, cfg);
    }

    static double status(const tlsf::Specification& spec, const Config& cfg,
                         const std::vector<std::size_t>& order,
                         ComponentCheck component_check) {
        return tlsf_status(spec, cfg, order, component_check);
    }

    static std::vector<std::string> status_components(
        const tlsf::Specification& spec) {
        return tlsf_status_components(spec);
    }

    // One synthesis query per live guarantee conjunct, the greedy walk's worst
    // case.
    static double status_walk_cost(const tlsf::Specification& spec) {
        return k_part_cost_synthesis *
               static_cast<double>(tlsf::count_live_guarantees(spec));
    }

    // Built on the specification being evolved, which under repair_mode = "muc"
    // is each core's sub-specification in turn.
    static std::vector<std::size_t> admission_order(
        const tlsf::Specification& original) {
        const std::vector<tlsf::CoreFormula> parts =
            tlsf::split_guarantee_parts(original);
        RealizabilityChecker& real = global_real_checker();
        return conflict_degree_order(
            parts.size(), [&original, &parts,
                           &real](const std::vector<std::size_t>& indices) {
                const tlsf::Specification subset =
                    tlsf::build_part_subset(original, parts, indices);
                // The same oracle tlsf_status walks with, undecided resolving
                // as unrealizable in the same direction.
                return real.check_realizability_ltl(
                               subset.to_ltl(), subset.m_inputs,
                               subset.m_outputs,
                               tlsf::specification_sides(subset))
                           .value_or(false) &&
                       !specification_is_not_well_separated(subset, real);
            });
    }
};

}  // namespace

AggregateWeightedFitnessFunctionT<tlsf::Specification>
tlsf_get_fitness_function(const tlsf::Specification& original,
                          const Config& cfg) {
    return make_fitness_function<tlsf::Specification, TlsfScorers>(original,
                                                                   cfg);
}
