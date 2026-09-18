#pragma once

// The per-generation bookkeeping both front ends' generation loops share:
// population sizing, the filter-stats fold and the fitness summary a
// generation reports.

#include <algorithm>
#include <cstddef>
#include <vector>

#include "config.hpp"
#include "genetic/filter_report.hpp"
#include "genetic/generation.hpp"
#include "genetic/scored.hpp"

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

// One zeroed row per filter, in filter order, for fold_filter_stats to add to.
template <typename Spec>
std::vector<FilterRunStats> empty_filter_stats(
    const std::vector<FilterFunctionT<Spec>>& filters) {
    std::vector<FilterRunStats> stats;
    stats.reserve(filters.size());
    for (const FilterFunctionT<Spec>& filter : filters) {
        stats.push_back({filter.name(), 0, 0});
    }
    return stats;
}

// Each filter records the last generation's in/out sizes in its own counters;
// this adds them to the running totals, row for row.
template <typename Spec>
void fold_filter_stats(const std::vector<FilterFunctionT<Spec>>& filters,
                       std::vector<FilterRunStats>& stats) {
    for (std::size_t k = 0; k < filters.size(); ++k) {
        stats[k].total_in += filters[k].n_in();
        stats[k].total_out += filters[k].n_out();
    }
}

struct FitnessSummary {
    double best = 0.0;
    double mean = 0.0;
};

// The maximum, not front(): under NSGA-II the population is ordered by front
// rank and crowding distance, so the leading individual need not hold the
// highest weighted scalar. Reporting it as "best" put a number below the mean
// beside it, and one that fell between generations while the search was still
// improving.
template <typename Spec>
FitnessSummary summarise_fitness(const std::vector<Scored<Spec>>& population) {
    double total = 0.0;
    FitnessSummary summary;
    for (const Scored<Spec>& cand : population) {
        total += cand.fitness;
        summary.best = std::max(summary.best, cand.fitness);
    }
    if (!population.empty()) {
        summary.mean = total / static_cast<double>(population.size());
    }
    return summary;
}

template <typename Spec>
std::vector<std::vector<double>> objectives_of(
    const std::vector<Scored<Spec>>& population) {
    std::vector<std::vector<double>> objectives;
    objectives.reserve(population.size());
    for (const Scored<Spec>& cand : population) {
        objectives.push_back(cand.objectives);
    }
    return objectives;
}
