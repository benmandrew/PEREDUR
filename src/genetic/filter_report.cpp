#include "genetic/filter_report.hpp"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <vector>

void print_filter_report(const std::vector<FilterRunStats>& stats) {
    std::cout << "\nFilter report:\n";
    for (const FilterRunStats& stat : stats) {
        if (stat.name.empty() || stat.total_in == 0) {
            continue;
        }
        const double pct_drop =
            100.0 * (1.0 - (static_cast<double>(stat.total_out) /
                            static_cast<double>(stat.total_in)));
        std::cout << std::left << std::setw(20) << stat.name << std::right
                  << std::setw(8) << stat.total_in << " in  " << std::setw(8)
                  << stat.total_out << " out  " << std::fixed
                  << std::setprecision(1) << std::setw(5) << pct_drop
                  << "% avg drop\n";
    }
}
