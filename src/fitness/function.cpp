#include "fitness/function.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "filter/well_separation.hpp"
#include "fitness/factory.hpp"
#include "fitness/semantic_similarity.hpp"
#include "fitness/status.hpp"
#include "fitness/syntactic_similarity.hpp"
#include "genetic/generation.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"

namespace {

struct FretishScorers {
    static double syntactic(const Specification& spec,
                            const Specification& original, const Config& cfg) {
        return syntactic_similarity(spec, original, cfg);
    }

    static double semantic(const Specification& spec,
                           const Specification& original, const Config& cfg) {
        return semantic_similarity(spec, original, cfg);
    }

    static std::vector<std::function<double()>> semantic_terms(
        const Specification& spec, const Specification& original,
        const Config& cfg) {
        return semantic_similarity_terms(spec, original,
                                         cfg.default_model_counting_bound,
                                         cfg.similarity_metric);
    }

    static double status(const Specification& spec, const Config& cfg,
                         const std::vector<std::size_t>& slot_order,
                         ComponentCheck component_check) {
        return specification_status(spec, global_sat_checker(),
                                    global_real_checker(), cfg.status_grading,
                                    slot_order, component_check);
    }

    static std::vector<std::string> status_components(
        const Specification& spec) {
        return specification_status_components(spec);
    }

    // One synthesis query per live guarantee, the greedy walk's worst case.
    // Only the order this induces over a region's parts is read, so an upper
    // bound is the right shape of estimate.
    static double status_walk_cost(const Specification& spec) {
        return k_part_cost_synthesis *
               static_cast<double>(live_indices(spec.m_guarantees).size());
    }

    static std::vector<std::size_t> admission_order(
        const Specification& original_spec) {
        const std::vector<std::size_t> slots =
            live_indices(original_spec.m_guarantees);
        RealizabilityChecker& real = global_real_checker();
        const std::vector<std::size_t> positions = conflict_degree_order(
            slots.size(), [&original_spec, &slots,
                           &real](const std::vector<std::size_t>& indices) {
                Specification subset = original_spec;
                subset.m_guarantees.clear();
                subset.m_guarantees.reserve(indices.size());
                for (const std::size_t index : indices) {
                    subset.m_guarantees.push_back(
                        original_spec.m_guarantees[slots[index]]);
                }
                // The same oracle specification_status walks with, undecided
                // resolving as unrealizable in the same direction.
                return real.check_realizability(subset).value_or(false) &&
                       !specification_is_not_well_separated(subset, real);
            });
        // Returned as slots, which survive a guarantee being removed; positions
        // do not. See specification_status.
        std::vector<std::size_t> slot_order;
        slot_order.reserve(positions.size());
        for (const std::size_t position : positions) {
            slot_order.push_back(slots[position]);
        }
        return slot_order;
    }
};

}  // namespace

AggregateWeightedFitnessFunction get_fitness_function(
    const Specification& original_spec, const Config& cfg) {
    return make_fitness_function<Specification, FretishScorers>(original_spec,
                                                                cfg);
}

std::vector<ScoredSpecification> score_and_sort_specifications(
    const Config& cfg, const std::vector<Specification>& specs,
    const AggregateWeightedFitnessFunction& fitness_function) {
    std::vector<ScoredSpecification> scored;
    scored.reserve(specs.size());
    for (const Specification& spec : specs) {
        auto [objectives, fitness] =
            fitness_function.objectives_and_fitness(spec);
        ScoredSpecification entry;
        entry.specification = spec;
        entry.fitness = fitness;
        entry.objectives = std::move(objectives);
        scored.push_back(std::move(entry));
    }
    order_population(cfg, scored);
    return scored;
}
