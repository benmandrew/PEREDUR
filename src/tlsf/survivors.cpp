#include "survivors.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "config.hpp"
#include "filter/implication.hpp"
#include "filter/streaming_maximal.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/output_gate.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/scored.hpp"
#include "runner/black.hpp"
#include "tlsf/specification.hpp"

namespace tlsf::internal {

namespace {

// Serial on purpose wherever this is called: the specifications reaching it
// were scored during the search, so the fitness cache is warm and each rescore
// is a cache hit; fanning it out would nest solver dispatch inside solver
// dispatch for nothing.
Scored<Specification> score_one(
    const Specification& spec,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness) {
    auto [objectives, scalar] = fitness.objectives_and_fitness(spec);
    Scored<Specification> scored;
    scored.specification = spec;
    scored.fitness = scalar;
    scored.objectives = std::move(objectives);
    return scored;
}

std::vector<Specification> specifications_of(
    const std::vector<Scored<Specification>>& scored) {
    std::vector<Specification> specs;
    specs.reserve(scored.size());
    for (const Scored<Specification>& candidate : scored) {
        specs.push_back(candidate.specification);
    }
    return specs;
}

// Restricts the survivors to the entries a filter kept, in the original order.
// The filters return bare specifications, so the scores have to be matched back
// on by value.
std::vector<Scored<Specification>> keep_matching(
    const std::vector<Scored<Specification>>& survivors,
    const std::vector<Specification>& kept) {
    std::vector<Scored<Specification>> result;
    result.reserve(kept.size());
    // Each kept spec claims one survivor, not every survivor equal to it. The
    // implication filter collapses structurally equal specs to a single copy,
    // so an any_of match would hand that copy to all of them and put the
    // duplicates straight back. TLSF dedups per generation rather than at the
    // end, and accumulate_repairs unions across generations, so duplicates do
    // reach here.
    std::vector<bool> claimed(kept.size(), false);
    for (const Scored<Specification>& scored : survivors) {
        for (std::size_t i = 0; i < kept.size(); ++i) {
            if (!claimed[i] && kept[i] == scored.specification) {
                claimed[i] = true;
                result.push_back(scored);
                break;
            }
        }
    }
    return result;
}

}  // namespace

std::vector<Scored<Specification>> realizable_survivors(
    const std::vector<Scored<Specification>>& population, const Config& cfg,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness) {
    // Compacted in population order, so the output matches a serial sweep
    // exactly whatever order the concurrent queries answered in.
    const std::vector<char> keep = gate_verdicts(population, cfg);
    std::vector<Scored<Specification>> survivors;
    for (std::size_t idx = 0; idx < population.size(); ++idx) {
        if (keep[idx] == 0) {
            continue;
        }
        const Scored<Specification>& scored = population[idx];
        const bool seen =
            std::any_of(survivors.begin(), survivors.end(),
                        [&scored](const Scored<Specification>& kept) {
                            return kept.specification == scored.specification;
                        });
        if (!seen) {
            survivors.push_back(score_one(scored.specification, fitness));
        }
    }
    order_population(cfg, survivors);
    return survivors;
}

std::vector<Scored<Specification>> merge_accumulated_survivors(
    std::vector<Scored<Specification>> survivors,
    const std::vector<Specification>& accumulated, const Config& cfg,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness) {
    // With the key off this is every TLSF run's path, and the work below --
    // a copy of the survivors, a hash set over them and a second ordering --
    // would all be spent arriving back at the argument.
    if (accumulated.empty()) {
        return survivors;
    }
    std::vector<Specification> specs = specifications_of(survivors);
    const std::size_t n_before = specs.size();
    AccumulatorStats::n_contributed += merge_accumulated(specs, accumulated);
    for (std::size_t idx = n_before; idx < specs.size(); ++idx) {
        survivors.push_back(score_one(specs[idx], fitness));
    }
    order_population(cfg, survivors);
    return survivors;
}

std::vector<Scored<Specification>> keep_maximal(
    const std::vector<Scored<Specification>>& survivors,
    const Specification& original, const Config& cfg,
    SatisfiabilityChecker& checker) {
    const std::vector<Specification> specs = specifications_of(survivors);
    const std::vector<Specification> maximal =
        make_implication_filter<Specification>(
            checker, syntactic_similarity_key(original, cfg))(specs);
    return keep_matching(survivors, maximal);
}

std::vector<Scored<Specification>> finish_maximal_stream(
    const std::vector<Scored<Specification>>& survivors,
    StreamingMaximalFilter<Specification>& stream) {
    for (const Scored<Specification>& scored : survivors) {
        stream.push(scored.specification, {});
    }
    return keep_matching(survivors, stream.finish());
}

}  // namespace tlsf::internal
