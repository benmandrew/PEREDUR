#include "genetic/nsga2.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

bool dominates(const std::vector<double>& lhs, const std::vector<double>& rhs) {
    assert(lhs.size() == rhs.size());
    bool strictly_better = false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i] < rhs[i]) {
            return false;
        }
        if (lhs[i] > rhs[i]) {
            strictly_better = true;
        }
    }
    return strictly_better;
}

std::vector<std::size_t> non_domination_ranks(
    const std::vector<std::vector<double>>& objectives) {
    assert(!objectives.empty());
    const std::size_t count = objectives.size();

    // dominated_by[i] = solutions i dominates; domination_count[i] = number of
    // solutions that dominate i (Deb et al. 2002, "fast non-dominated sort").
    std::vector<std::vector<std::size_t>> dominated_by(count);
    std::vector<std::size_t> domination_count(count, 0);
    std::vector<std::size_t> ranks(count, 0);

    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            if (dominates(objectives[i], objectives[j])) {
                dominated_by[i].push_back(j);
                ++domination_count[j];
            } else if (dominates(objectives[j], objectives[i])) {
                dominated_by[j].push_back(i);
                ++domination_count[i];
            }
        }
    }

    std::vector<std::size_t> current_front;
    for (std::size_t i = 0; i < count; ++i) {
        if (domination_count[i] == 0) {
            ranks[i] = 0;
            current_front.push_back(i);
        }
    }

    std::size_t front_index = 0;
    while (!current_front.empty()) {
        std::vector<std::size_t> next_front;
        for (const std::size_t member : current_front) {
            for (const std::size_t dominated : dominated_by[member]) {
                assert(domination_count[dominated] > 0);
                if (--domination_count[dominated] == 0) {
                    ranks[dominated] = front_index + 1;
                    next_front.push_back(dominated);
                }
            }
        }
        ++front_index;
        current_front = std::move(next_front);
    }

    return ranks;
}

std::vector<double> crowding_distances(
    const std::vector<std::vector<double>>& objectives,
    const std::vector<std::size_t>& ranks) {
    assert(objectives.size() == ranks.size());
    const std::size_t count = objectives.size();
    std::vector<double> distances(count, 0.0);
    if (count == 0) {
        return distances;
    }
    const std::size_t n_objectives = objectives[0].size();

    // Group the members of each front so crowding is measured within a front.
    std::size_t max_rank = 0;
    for (const std::size_t rank : ranks) {
        max_rank = std::max(max_rank, rank);
    }
    std::vector<std::vector<std::size_t>> fronts(max_rank + 1);
    for (std::size_t i = 0; i < count; ++i) {
        fronts[ranks[i]].push_back(i);
    }

    constexpr double k_infinity = std::numeric_limits<double>::infinity();
    for (std::vector<std::size_t>& front : fronts) {
        // Maximality demotion can move every member of a front down one rank,
        // which leaves that front empty.
        if (front.empty()) {
            continue;
        }
        for (std::size_t obj = 0; obj < n_objectives; ++obj) {
            std::sort(front.begin(), front.end(),
                      [&objectives, obj](std::size_t lhs, std::size_t rhs) {
                          return objectives[lhs][obj] < objectives[rhs][obj];
                      });
            const double lowest = objectives[front.front()][obj];
            const double highest = objectives[front.back()][obj];
            const double range = highest - lowest;
            // A zero-range objective distinguishes no members, so it anchors
            // nothing. Guarding before the assignment rather than after it is
            // load-bearing: the sort above is not stable, so on a constant
            // objective front.front() and front.back() are an arbitrary
            // permutation of the tied members, and pinning two of them at
            // infinity ranks them above every finitely-scored member of the
            // front for no reason the objectives support. A coarse objective
            // feels this worst -- a continuous one has unique extremes,
            // whereas the three-point status scale can leave hundreds of
            // members tied at 0.5.
            if (range <= 0.0) {
                continue;
            }
            // The extremes of this objective anchor the front and are always
            // retained.
            distances[front.front()] = k_infinity;
            distances[front.back()] = k_infinity;
            for (std::size_t i = 1; i + 1 < front.size(); ++i) {
                const double gap = objectives[front[i + 1]][obj] -
                                   objectives[front[i - 1]][obj];
                distances[front[i]] += gap / range;
            }
        }
    }

    return distances;
}
