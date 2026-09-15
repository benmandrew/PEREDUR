#include "fitness/semantic_similarity.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include "fitness/model_counter.hpp"
#include "fitness/transfer_matrix.hpp"
#include "formula_key.hpp"

namespace {

double ratio_or_throw(Count numerator, Count denominator) {
    assert(denominator != 0);
    return static_cast<double>(numerator / denominator);
}

// One directional containment term: how much of @p whole's satisfying traces
// are shared. Direct is the plain ratio, clamped to [0, 1] because rounding can
// push shared a few ulps past whole now that Count is a float. Logarithmic
// takes the ratio of the counts' logarithms, which -- since trace counts grow
// like lambda^k -- tends to the ratio of the two languages' growth rates and
// stays roughly constant as the bound grows, where the plain ratio decays
// toward 0. A count of 1 has no exponential regime to compare (log 1 == 0), so
// the log branch falls back to membership at that boundary.
double directional_containment(Count shared, Count whole,
                               SimilarityMetric metric) {
    if (metric == SimilarityMetric::Logarithmic) {
        if (whole <= 1) {
            return shared >= whole ? 1.0 : 0.0;
        }
        if (shared <= 1) {
            return 0.0;
        }
        return std::clamp(std::log(static_cast<double>(shared)) /
                              std::log(static_cast<double>(whole)),
                          0.0, 1.0);
    }
    return std::clamp(ratio_or_throw(shared, whole), 0.0, 1.0);
}

// The number of timepoints a timing constrains, i.e. the first step count at
// which its deadline has been reached. Eventually and Always have no deadline,
// so they impose no requirement on the bound and return 0.
std::size_t timing_horizon(const Timing& timing) {
    return std::visit(
        [](const auto& value) -> std::size_t {
            using T = std::decay_t<decltype(value)>;
            // Both close their window at m_ticks, so both are due at the tick
            // after it.
            constexpr bool is_window = std::is_same_v<T, timing::WithinTicks> ||
                                       std::is_same_v<T, timing::ForTicks>;
            if constexpr (std::is_same_v<T, timing::Immediately>) {
                return 1;
            } else if constexpr (std::is_same_v<T, timing::NextTimepoint>) {
                return 2;
            } else if constexpr (is_window) {
                return value.m_ticks + 1;
            } else if constexpr (std::is_same_v<T, timing::AfterTicks>) {
                // The response falls due at m_ticks + 1, one past the window
                // in which it must not hold.
                return value.m_ticks + 2;
            } else {
                static_assert(std::is_same_v<T, timing::Eventually> ||
                              std::is_same_v<T, timing::Always>);
                return 0;
            }
        },
        timing);
}

// A bounded timing compiles to a safety automaton (ltl2tgba emits
// "acc-name: all") that rejects by running out of transitions at its deadline;
// Eventually compiles to a complete Buchi automaton where rejection rides
// entirely on the accepting mask. Counting k-step traces asks both the same
// question only once k reaches the bounded operand's deadline -- below it the
// safety automaton still counts traces that already missed the deadline, which
// breaks conjunction_count <= min(individual counts) and lets the similarity
// ratio exceed 1. Raising k to the horizon costs almost nothing (the bound
// barely moves wall time, which is dominated by black/ltlsynt), so prefer it.
//
// The ceiling wins ties: a k past max_representable_step_count would overflow
// Count. Clamping there can land back under the horizon for atom-rich
// requirements, which reopens the ratio defect -- a wrong-but-bounded score is
// preferred to a silently saturated count.
std::size_t effective_step_count(const Requirement& requirement,
                                 const Requirement& other_requirement,
                                 std::size_t step_count, std::size_t n_atoms) {
    const std::size_t horizon =
        std::max(timing_horizon(requirement.m_timing),
                 timing_horizon(other_requirement.m_timing));
    return std::min(std::max(step_count, horizon),
                    max_representable_step_count(n_atoms));
}

SemanticSimilarityCounts count_semantic_similarity_terms(
    const Requirement& requirement, const Requirement& other_requirement,
    std::size_t step_count) {
    // All three systems must use the same atom universe so that the
    // conjunction count is always <= each individual count, keeping
    // the similarity ratio in [0, 1]. A shared universe is necessary but not
    // sufficient: see effective_step_count for the bound's role.
    const std::size_t n_atoms =
        count_joint_atoms(requirement, other_requirement);
    const std::size_t steps = effective_step_count(
        requirement, other_requirement, step_count, n_atoms);
    const std::string ltl = requirement_to_ltl(requirement);
    const std::string other_ltl = requirement_to_ltl(other_requirement);
    const std::string conjunction_ltl = "(" + ltl + ") & (" + other_ltl + ")";
    return {
        cached_count_traces(ltl, n_atoms, steps),
        cached_count_traces(other_ltl, n_atoms, steps),
        cached_count_traces(conjunction_ltl, n_atoms, steps),
    };
}

}  // namespace

Count cached_count_traces(const std::string& ltl, std::size_t n_total_atoms,
                          std::size_t step_count) {
    static std::unordered_map<std::string, Count> cache;
    static std::mutex cache_mutex;
    // The renamed canonical form: a trace count over a fixed atom universe is
    // invariant under a bijection on the atoms, and the universe is pinned by
    // n_total_atoms, which stays in the key for that reason.
    const std::string key = formula_key::renamed(ltl) + "|" +
                            std::to_string(n_total_atoms) + "|" +
                            std::to_string(step_count);
    {
        std::scoped_lock lock(cache_mutex);
        const auto found = cache.find(key);
        if (found != cache.end()) {
            CountTracesStats::n_hits++;
            return found->second;
        }
        CountTracesStats::n_misses++;
    }
    const TransferSystem system =
        build_transfer_system_from_ltl(ltl, n_total_atoms);
    const Count count = count_traces(system, step_count);
    std::scoped_lock lock(cache_mutex);
    cache.emplace(key, count);
    return count;
}

std::size_t max_representable_step_count(std::size_t n_atoms) {
    // sizeof(Count) would overstate the limit, since only the 15-bit exponent
    // governs range.
    constexpr auto max_exponent =
        static_cast<std::size_t>(std::numeric_limits<Count>::max_exponent);
    return n_atoms == 0 ? max_exponent - 1 : (max_exponent - 1) / n_atoms;
}

double semantic_similarity_from_counts(const SemanticSimilarityCounts& counts,
                                       SimilarityMetric metric) {
    if (counts.m_requirement_count == 0 &&
        counts.m_other_requirement_count == 0) {
        return 1.0;
    }
    if (counts.m_requirement_count == 0 ||
        counts.m_other_requirement_count == 0) {
        return 0.0;
    }
    const double first = directional_containment(
        counts.m_conjunction_count, counts.m_requirement_count, metric);
    const double second = directional_containment(
        counts.m_conjunction_count, counts.m_other_requirement_count, metric);
    // Harmonic mean of the two directional containment terms: unlike the
    // arithmetic mean, this can't be pulled up to 0.5 by one term alone
    // hitting 1 (e.g. a requirement weakened to a tautology, which trivially
    // contains the original and makes second == 1 regardless of how small
    // first is). Mutually exclusive requirements give first == second == 0,
    // which the formula below would otherwise divide as 0/0.
    if (first == 0.0 && second == 0.0) {
        return 0.0;
    }
    return (2.0 * first * second) / (first + second);
}

double semantic_similarity(const Requirement& requirement,
                           const Requirement& other_requirement,
                           std::size_t step_count, SimilarityMetric metric) {
    const SemanticSimilarityCounts counts = count_semantic_similarity_terms(
        requirement, other_requirement, step_count);
    return semantic_similarity_from_counts(counts, metric);
}

double semantic_similarity(const Requirement& requirement,
                           const Requirement& other_requirement,
                           const Config& cfg) {
    return semantic_similarity(requirement, other_requirement,
                               cfg.default_model_counting_bound,
                               cfg.similarity_metric);
}

std::vector<std::function<double()>> semantic_similarity_terms(
    const Specification& specification,
    const Specification& other_specification, std::size_t step_count,
    SimilarityMetric metric) {
    assert(!specification.m_assumptions.empty() ||
           !specification.m_guarantees.empty());
    assert(!other_specification.m_assumptions.empty() ||
           !other_specification.m_guarantees.empty());
    std::vector<std::function<double()>> terms;
    // Requirement pairs that are identical contribute a trivial 1.0 and
    // dilute the average toward 1 as the specification grows, drowning out
    // the pairs that actually differ. Excluding them keeps the score
    // sensitive to the requirements a mutation actually touched.
    //
    // The two specifications need not have the same number of requirements: the
    // p_add_assumption mutation grows a candidate's assumption list relative to
    // the original it is scored against. Pair requirements by index over the
    // count they share; surplus requirements on either side have no counterpart
    // to compare against and are skipped. Advancing a second iterator in
    // lockstep to reqs1.end() (as before) would run it past reqs2.end()
    // whenever reqs1 is longer, which is undefined behaviour once NDEBUG
    // disables the asserts that used to guard it.
    const auto collect = [&terms, step_count, metric](
                             const std::vector<Requirement>& reqs1,
                             const std::vector<Requirement>& reqs2) {
        const std::size_t common = std::min(reqs1.size(), reqs2.size());
        for (std::size_t i = 0; i < common; ++i) {
            if (reqs1[i] == reqs2[i]) {
                continue;
            }
            // Removed on one side only: a real change, and the largest one the
            // slot admits, so it scores zero. There is no formula left to
            // count, and counting the survivor against nothing would charge a
            // model count for an answer already known. It is still a term
            // rather than a skipped pair, because it counts toward the mean.
            if (reqs1[i].m_removed || reqs2[i].m_removed) {
                terms.emplace_back([] { return 0.0; });
                continue;
            }
            terms.emplace_back([&first = reqs1[i], &second = reqs2[i],
                                step_count, metric] {
                return semantic_similarity(first, second, step_count, metric);
            });
        }
    };
    collect(specification.m_assumptions, other_specification.m_assumptions);
    collect(specification.m_guarantees, other_specification.m_guarantees);
    return terms;
}

double semantic_similarity(const Specification& specification,
                           const Specification& other_specification,
                           std::size_t step_count, SimilarityMetric metric) {
    const std::vector<std::function<double()>> terms =
        semantic_similarity_terms(specification, other_specification,
                                  step_count, metric);
    if (terms.empty()) {
        return 1.0;
    }
    double total = 0.0;
    for (const std::function<double()>& term : terms) {
        total += term();
    }
    return total / static_cast<double>(terms.size());
}

double semantic_similarity(const Specification& specification,
                           const Specification& other_specification,
                           const Config& cfg) {
    return semantic_similarity(specification, other_specification,
                               cfg.default_model_counting_bound,
                               cfg.similarity_metric);
}
