#include "survivors.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "config.hpp"
#include "filter/correctness.hpp"
#include "filter/streaming_maximal.hpp"
#include "fingerprint/prefilter.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/scored.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "thread_pool.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/fitness.hpp"
#include "tlsf/specification.hpp"

namespace tlsf::internal {

namespace {

// The correctness checks, built once. Their predicates capture the global
// checkers, which outlive every caller.
const std::vector<CorrectnessCheckT<Specification>>& gate_checks() {
    static const std::vector<CorrectnessCheckT<Specification>> checks =
        tlsf_correctness_checks(global_sat_checker(), global_real_checker());
    return checks;
}

// A candidate counts as a repair only when it is realizable and passes every
// correctness check. Elites and the seed population reach this collection
// without having passed the per-generation chain, and any of those checks can
// be turned off outright, so the whole table is re-applied here -- mirroring
// the FRETISH path's is_realizable_repair. Unconditional on both paths: the
// per-generation flags tune search pressure, never output correctness.
//
// Status leads, as it does on the FRETISH path: it is a scored objective, so
// its verdict is memoised for everything the last generation scored, while a
// check whose per-generation stage was off has nothing warming its queries.
bool is_tlsf_repair(const Specification& spec, const Config& cfg) {
    return tlsf_status(spec, cfg) == 1.0 &&
           !first_failing_check(spec, gate_checks()).has_value();
}

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

std::vector<char> gate_verdicts(
    const std::vector<Scored<Specification>>& population, const Config& cfg) {
    // Each status check is an `ltlsynt` query and the whole population is
    // checked, so a serial sweep here costs a subprocess per distinct
    // candidate.
    const std::size_t max_in_flight = dispatch_window();
    std::vector<char> keep(population.size(), 0);
    if (max_in_flight <= 1) {
        for (std::size_t idx = 0; idx < population.size(); ++idx) {
            keep[idx] =
                is_tlsf_repair(population[idx].specification, cfg) ? 1 : 0;
        }
    } else {
        run_bounded_async(
            population.size(), max_in_flight,
            [&population, &cfg](std::size_t idx) {
                return [&spec = population[idx].specification, &cfg] {
                    return is_tlsf_repair(spec, cfg);
                };
            },
            [&keep](std::size_t idx, bool realizable) {
                keep[idx] = realizable ? 1 : 0;
            });
    }
    return keep;
}

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
    const std::vector<Specification> maximal = tlsf_make_implication_filter(
        checker, tlsf_syntactic_similarity_key(original, cfg))(specs);
    return keep_matching(survivors, maximal);
}

std::unique_ptr<StreamingMaximalFilter<Specification>> make_maximal_stream(
    const Specification& original, const Config& cfg,
    const std::string& output_dir) {
    // Both repair modes stream: run_muc reintegrates every gate-passing core
    // repair into a whole specification and gates it as one, so what reaches
    // the filter is the same kind of object the monolithic path pushes.
    if (!implication_streams(cfg)) {
        return nullptr;
    }
    MaximalStreamRules<Specification> rules;
    rules.implies = [](const Specification& lhs, const Specification& rhs,
                       SatisfiabilityChecker& checker) {
        return tlsf_spec_implies(lhs, rhs, checker).value_or(false);
    };
    rules.similarity = tlsf_syntactic_similarity_key(original, cfg);
    rules.fingerprints = [](const std::vector<Specification>& specs) {
        return fingerprint::prefilter::fingerprints_of(specs);
    };
    return std::make_unique<StreamingMaximalFilter<Specification>>(
        cfg, std::move(rules),
        (std::filesystem::path(output_dir) /
         AccumulatedRepairWriter<Specification>::k_subdirectory / "maximal.tsv")
            .string());
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
