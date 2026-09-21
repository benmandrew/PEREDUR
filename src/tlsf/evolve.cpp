#include "evolve.hpp"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#include "config.hpp"
#include "dashboard.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/generation.hpp"
#include "genetic/generation_loop.hpp"
#include "genetic/output_gate.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "runner/black.hpp"
#include "tlsf/operators.hpp"
#include "tlsf/specification.hpp"

namespace tlsf::internal {

std::vector<Scored<Specification>> evolve_population(
    const Specification& spec, const Config& cfg,
    const RandomSource& random_source,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    std::vector<FilterRunStats>& filter_stats_out,
    const DashboardProgress& progress,
    RepairAccumulator<Specification>& accumulator_out, SearchBudget& budget) {
    const std::vector<FilterFunctionT<Specification>> per_gen_filters =
        get_filter_functions(spec, global_sat_checker());

    const std::vector<Specification> seed_population(cfg.population_size, spec);
    std::vector<Scored<Specification>> population =
        score_population(cfg, seed_population, fitness);

    // Sized on cfg.population_size, the seed population's size. Which
    // candidates count as "best" is not truncation on the weighted scalar:
    // order_population applies cfg.selection_scheme, which defaults to NSGA-II.
    const GenerationSizes sizes = generation_sizes(cfg, cfg.population_size);
    filter_stats_out = empty_filter_stats(per_gen_filters);

    for (std::size_t gen = 0; gen < cfg.generations; ++gen) {
        // Before the generation as well as between offspring, matching
        // checkTermination() at the head of AuRUS's evolve(count) loop. The
        // budget spans the whole run, so under MUC repair a core that opens
        // with it already spent evolves nothing rather than restarting it.
        if (budget.active() && budget.exhausted()) {
            break;
        }
        const auto gen_start = std::chrono::steady_clock::now();
        // MUC repair restarts its generation count on every core it evolves, so
        // the dashboard is given a number that keeps climbing across
        // iterations; muc_iter carries the structure that flattens away.
        const std::size_t dashboard_gen = progress.gen_offset + gen + 1;
        std::size_t stage_index = 0;
        auto on_stage = [&progress, &stage_index,
                         dashboard_gen](const StageObservation& obs) {
            if (progress.writer != nullptr) {
                progress.writer->stage(dashboard_gen, stage_index++, obs,
                                       progress.muc_iter);
            }
        };
        population = evolve_generation_generic(
            cfg, population, sizes.selection, sizes.elitism, fitness,
            per_gen_filters, tlsf_operators(), random_source, nullptr, on_stage,
            &budget);
        budget.count_generation();
        fold_filter_stats(per_gen_filters, filter_stats_out);
        const FitnessSummary summary = summarise_fitness(population);
        std::cout << "gen " << (gen + 1) << "/" << cfg.generations
                  << "  best fitness " << summary.best << "\n";
        accumulate_gate_passing(population, cfg, dashboard_gen,
                                accumulator_out);
        if (progress.writer != nullptr) {
            progress.writer->generation(
                dashboard_gen,
                std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                              gen_start)
                    .count(),
                summary.best, summary.mean,
                // No count: this path checks realizability once, after
                // evolution, so any number here would be one the run never
                // measured.
                mean_objectives(progress.objective_names,
                                objectives_of(population)),
                std::nullopt, population.size(), progress.muc_iter);
        }
    }
    return population;
}

}  // namespace tlsf::internal
