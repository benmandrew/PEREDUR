#include "evolve.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#include "config.hpp"
#include "dashboard.hpp"
#include "filter/bloat.hpp"
#include "filter/correctness.hpp"
#include "filter/implication.hpp"
#include "filter_report.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/generation.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "survivors.hpp"
#include "thread_pool.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/operators.hpp"
#include "tlsf/specification.hpp"

namespace tlsf::internal {

// The vacuity filter this builds carries all three tests the FRETISH one does:
// the syntactic screen for a trivial section literal, the per-formula guarantee
// validity check, and the assumption-satisfiability conjunction. The guarantee
// half earns its place because nothing else rejects a gutted guarantee.
std::vector<FilterFunctionT<Specification>> build_per_gen_filters(
    const Specification& spec) {
    const std::size_t max_in_flight = dispatch_window();
    std::vector<FilterFunctionT<Specification>> filters;
    FilterFunctionT<Specification> dedup = make_dedup_filter<Specification>();
    filters.push_back(std::move(dedup));
    FilterFunctionT<Specification> bloat = make_bloat_cap_filter(spec);
    filters.push_back(std::move(bloat));
    // From the shared table, as on the FRETISH path: a property enforced here
    // is enforced by the final gate and the input screen too, because all three
    // read the same rows.
    for (const CorrectnessCheckT<Specification>& check :
         correctness_checks<Specification>(global_sat_checker(),
                                           global_real_checker())) {
        if (check.per_generation) {
            filters.push_back(make_predicate_filter<Specification>(
                check.name, check.admissible, max_in_flight));
        }
    }
    return filters;
}

namespace {

// Not free: nothing else asks the gate per generation, so this sweep is work
// the run would not otherwise do. The status query behind it is memoised from
// scoring, leaving the correctness rows as the real cost. Hence the early
// return rather than a caller-side branch. The FRETISH twin
// (`accumulate_gate_passing`, src/repair/evolution.cpp) was unconditional
// until 2026-09-09 and is now gated the same way.
void accumulate_gate_passing(
    const std::vector<Scored<Specification>>& population, const Config& cfg,
    std::size_t generation, RepairAccumulator<Specification>& accumulator) {
    if (!accumulator.enabled()) {
        return;
    }
    const std::vector<char> keep = gate_verdicts(population, cfg);
    for (std::size_t idx = 0; idx < population.size(); ++idx) {
        if (keep[idx] != 0) {
            accumulator.insert(population[idx].specification, generation);
        }
    }
}

}  // namespace

std::vector<Scored<Specification>> evolve_population(
    const Specification& spec, const Config& cfg,
    const RandomSource& random_source,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    std::vector<FilterRunStats>& filter_stats_out,
    const DashboardProgress& progress,
    RepairAccumulator<Specification>& accumulator_out, SearchBudget& budget) {
    const std::vector<FilterFunctionT<Specification>> per_gen_filters =
        build_per_gen_filters(spec);

    const std::vector<Specification> seed_population(cfg.population_size, spec);
    std::vector<Scored<Specification>> population =
        score_population(cfg, seed_population, fitness);

    // Population sizing, matching the FRETISH path: each generation breeds
    // selection_size offspring and carries the best elitism_size parents over
    // verbatim. Both are derived once from the seed population size (cfg
    // guarantees elitism_rate < selection_rate). Which candidates count as
    // "best" is not truncation on the weighted scalar: order_population applies
    // cfg.selection_scheme, which defaults to NSGA-II.
    const std::size_t selection_size = std::max(
        std::size_t{1},
        static_cast<std::size_t>(static_cast<double>(cfg.population_size) *
                                 cfg.selection_rate));
    const auto elitism_size = static_cast<std::size_t>(
        static_cast<double>(cfg.population_size) * cfg.elitism_rate);

    filter_stats_out.clear();
    filter_stats_out.reserve(per_gen_filters.size());
    for (const FilterFunctionT<Specification>& filter : per_gen_filters) {
        filter_stats_out.push_back({filter.name(), 0, 0});
    }

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
            cfg, population, selection_size, elitism_size, fitness,
            per_gen_filters, tlsf_operators(), random_source, nullptr, on_stage,
            &budget);
        budget.count_generation();
        // Each filter records this generation's in/out sizes in its own mutable
        // counters; fold them into the running totals for the end-of-run
        // report.
        for (std::size_t k = 0; k < per_gen_filters.size(); ++k) {
            filter_stats_out[k].total_in += per_gen_filters[k].n_in();
            filter_stats_out[k].total_out += per_gen_filters[k].n_out();
        }
        // The maximum, not front(): NSGA-II orders by front rank and
        // crowding distance, so the leading individual need not hold the
        // highest weighted scalar. Reporting front() made the printed best
        // fall between generations while the search was still improving.
        // Scalars only, so a run without a dashboard pays a single pass and
        // no copies; the objectives vector below is the part worth gating.
        double total = 0.0;
        double maximum = 0.0;
        for (const Scored<Specification>& cand : population) {
            total += cand.fitness;
            maximum = std::max(maximum, cand.fitness);
        }
        std::cout << "gen " << (gen + 1) << "/" << cfg.generations
                  << "  best fitness " << maximum << "\n";
        accumulate_gate_passing(population, cfg, dashboard_gen,
                                accumulator_out);
        if (progress.writer != nullptr) {
            std::vector<std::vector<double>> objectives;
            objectives.reserve(population.size());
            for (const Scored<Specification>& cand : population) {
                objectives.push_back(cand.objectives);
            }
            progress.writer->generation(
                dashboard_gen,
                std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                              gen_start)
                    .count(),
                maximum,
                population.empty()
                    ? 0.0
                    : total / static_cast<double>(population.size()),
                // No count: this path checks realizability once, after
                // evolution, so any number here would be one the run never
                // measured.
                mean_objectives(progress.objective_names, objectives),
                std::nullopt, population.size(), progress.muc_iter);
        }
    }
    return population;
}

}  // namespace tlsf::internal
