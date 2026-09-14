#pragma once

#include <cstddef>
#include <string>
#include <vector>

// PEREDUR's end-of-run reports, printed to stdout after the search finishes.

// One filter's input and output population sizes, summed over every generation
// it ran in.
struct FilterRunStats {
    std::string name;
    std::size_t total_in{0};
    std::size_t total_out{0};
};

void print_filter_report(const std::vector<FilterRunStats>& stats);

void print_scoring_report();

// The engine-internal counters: per-tool calls and cache totals, the ltl2tgba
// tautology substitutions, the constant-folded count, and the fitness cache hit
// rate. Printed only under --diagnostics; every figure in it is also written to
// run.json, which is where a campaign should read them from. A counter added
// here has to be added to write_run_manifest too, or the flag's default loses
// it.
void print_diagnostics_report();

void print_cpu_report(double wall_s);
