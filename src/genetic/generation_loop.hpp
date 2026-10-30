#pragma once

// The generation loop both front ends run: breed, filter, score and select
// once per generation, then report the generation on the status line and offer
// its gate-passing candidates to the accumulator.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/filter_report.hpp"
#include "genetic/generation.hpp"
#include "genetic/output_gate.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "status_line.hpp"

// Each generation breeds `selection` offspring and carries the best `elitism`
// parents over verbatim. Unlike selection, elitism has no floor of 1, so a
// small population or rate can legitimately yield no elites; the config
// guarantees elitism_rate < selection_rate, keeping it below `selection`.
struct GenerationSizes {
    std::size_t selection;
    std::size_t elitism;
};

inline GenerationSizes generation_sizes(const Config& cfg,
                                        std::size_t population_size) {
    const auto size = static_cast<double>(population_size);
    return {std::max(std::size_t{1},
                     static_cast<std::size_t>(size * cfg.selection_rate)),
            static_cast<std::size_t>(size * cfg.elitism_rate)};
}

namespace generation_loop_detail {

inline std::string format_fixed(double value, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

inline double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         start)
        .count();
}

}  // namespace generation_loop_detail

// Runs up to cfg.generations generations from @p population, stopping early
// once @p budget is spent. Every generation's gate-passing candidates go to
// @p accumulator.
template <typename Spec>
SearchResult<Spec> run_generations(
    const Config& cfg, std::vector<Scored<Spec>> population,
    const AggregateWeightedFitnessFunctionT<Spec>& fitness,
    const std::vector<FilterFunctionT<Spec>>& filters,
    const RandomSource& random_source, RepairAccumulator<Spec>& accumulator,
    SearchBudget& budget) {
    using generation_loop_detail::format_fixed;
    using generation_loop_detail::seconds_since;

    // Each filter records the last generation's in/out sizes in its own
    // counters, which are added here to one running row per filter.
    std::vector<FilterRunStats> filter_stats;
    filter_stats.reserve(filters.size());
    for (const FilterFunctionT<Spec>& filter : filters) {
        filter_stats.push_back({filter.name(), 0, 0});
    }

    StatusLine status;
    const std::size_t col_gen = status.add("gen");
    // Transient: within-generation progress is the reason the line updates at
    // all, and reads 100% on every committed line by construction.
    const std::size_t col_pct = status.add("%", true);
    const std::size_t col_time = status.add("time");
    const std::size_t col_best = status.add("best");
    const std::size_t col_real = status.add("real");

    // Sized on cfg.population_size, the seed population's size. Which
    // candidates count as "best" is not truncation on the weighted scalar:
    // order_population applies cfg.selection_scheme.
    const GenerationSizes sizes = generation_sizes(cfg, cfg.population_size);
    const std::string total_str = std::to_string(cfg.generations);
    const FitnessFunctionT<Spec> gate_status = status_objective_of(fitness);
    for (std::size_t gen = 1; gen <= cfg.generations; ++gen) {
        // Checked before the generation as well as between offspring, matching
        // checkTermination() at the head of AuRUS's evolve(count) loop. Without
        // it a spent budget still pays for a generation of filtering, scoring
        // and selection over an offspring set breeding left empty.
        if (budget.active() && budget.exhausted()) {
            break;
        }
        const auto start = std::chrono::steady_clock::now();
        status.set(col_gen, std::to_string(gen) + "/" + total_str);
        const auto on_progress = [&](std::size_t done, std::size_t total) {
            status.set(col_pct, std::to_string(done * 100 / total) + "%");
            status.set(col_time, format_fixed(seconds_since(start), 2) + "s");
            status.render();
        };
        population = evolve_generation(
            cfg, population, sizes.selection, sizes.elitism, fitness, filters,
            random_source, on_progress, nullptr, &budget);
        budget.count_generation();
        for (std::size_t k = 0; k < filters.size(); ++k) {
            filter_stats[k].total_in += filters[k].n_in();
            filter_stats[k].total_out += filters[k].n_out();
        }

        // The maximum, not front(): under NSGA-II the population is ordered by
        // front rank and crowding distance, so the leading individual need not
        // hold the highest weighted scalar. Reporting it as "best" put a
        // number below the mean beside it, and one that fell between
        // generations while the search was still improving.
        double best = 0.0;
        for (const Scored<Spec>& cand : population) {
            best = std::max(best, cand.fitness);
        }
        const std::size_t n_real = accumulate_gate_passing(
            population, cfg, gen, accumulator, gate_status);

        const double elapsed = seconds_since(start);
        status.set(col_time, format_fixed(elapsed, 2) + "s");
        if (!population.empty()) {
            status.set(col_best, format_fixed(best, 3));
        }
        status.set(col_real, std::to_string(n_real));
        status.finish();
    }
    return {std::move(population), std::move(filter_stats)};
}
