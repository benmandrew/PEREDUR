#include "genetic/filter_report.hpp"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <vector>

void accumulate_filter_stats(std::vector<FilterRunStats>& aggregate,
                             const std::vector<FilterRunStats>& run) {
    if (aggregate.empty()) {
        aggregate = run;
        return;
    }
    for (std::size_t i = 0; i < run.size() && i < aggregate.size(); ++i) {
        aggregate[i].total_in += run[i].total_in;
        aggregate[i].total_out += run[i].total_out;
    }
}

void print_filter_report(const std::vector<FilterRunStats>& stats,
                         EmptyFilterReport when_empty) {
    const auto reported = [](const FilterRunStats& stat) {
        return !stat.name.empty() && stat.total_in > 0;
    };
    if (when_empty == EmptyFilterReport::Silent &&
        std::none_of(stats.begin(), stats.end(), reported)) {
        return;
    }
    std::cout << "\nFilter report:\n";
    for (const FilterRunStats& stat : stats) {
        if (!reported(stat)) {
            continue;
        }
        const double pct_drop =
            100.0 * (1.0 - static_cast<double>(stat.total_out) /
                               static_cast<double>(stat.total_in));
        std::cout << std::left << std::setw(20) << stat.name << std::right
                  << std::setw(8) << stat.total_in << " in  " << std::setw(8)
                  << stat.total_out << " out  " << std::fixed
                  << std::setprecision(1) << std::setw(5) << pct_drop
                  << "% avg drop\n";
    }
}
