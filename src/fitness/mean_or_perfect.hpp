#pragma once

#include <vector>

// The fold every part-wise similarity objective shares: the mean of its terms,
// with an empty set of terms meaning nothing differed and scoring a perfect
// match. Both similarity scores define themselves that way, and summing in term
// order keeps the whole-candidate score bit-identical to the decomposed one.
inline double mean_or_perfect(const std::vector<double>& values) {
    if (values.empty()) {
        return 1.0;
    }
    double total = 0.0;
    for (const double value : values) {
        total += value;
    }
    return total / static_cast<double>(values.size());
}
