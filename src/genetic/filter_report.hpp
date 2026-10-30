#pragma once

// Per-filter population counts gathered over a search, and the end-of-run
// report printed from them. Shared by both front ends.

#include <vector>

#include "genetic/pipeline.hpp"

// Prints the heading and then one row per named filter that saw a candidate.
void print_filter_report(const std::vector<FilterRunStats>& stats);
