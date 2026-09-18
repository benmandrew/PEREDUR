#pragma once

// Per-filter population counts gathered over a search, and the end-of-run
// report printed from them. Shared by both front ends.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// One filter's input and output population sizes, summed over every generation
// it ran in.
struct FilterRunStats {
    std::string name;
    std::size_t total_in = 0;
    std::size_t total_out = 0;
};

// Merges one evolve run's per-filter totals into a running aggregate (the MUC
// loop sums them across iterations for a single end-of-run report). Filters are
// built in the same order every call, so accumulation is positional.
void accumulate_filter_stats(std::vector<FilterRunStats>& aggregate,
                             const std::vector<FilterRunStats>& run);

// What the report prints when no filter saw a candidate: the FRETISH driver
// has always printed the bare heading, the TLSF one nothing.
enum class EmptyFilterReport : std::uint8_t { Heading, Silent };

// Prints one row per named filter that saw a candidate.
void print_filter_report(const std::vector<FilterRunStats>& stats,
                         EmptyFilterReport when_empty);
