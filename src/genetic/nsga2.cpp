#include "genetic/nsga2.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <numeric>
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

std::vector<double> farthest_point_scores(const std::vector<std::size_t>& ranks,
                                          const PairDistance& distance) {
    const std::size_t count = ranks.size();
    std::vector<double> scores(count, 0.0);
    if (count == 0) {
        return scores;
    }
    const std::size_t max_rank = *std::max_element(ranks.begin(), ranks.end());
    std::vector<std::vector<std::size_t>> fronts(max_rank + 1);
    for (std::size_t i = 0; i < count; ++i) {
        fronts[ranks[i]].push_back(i);
    }
    constexpr double k_infinity = std::numeric_limits<double>::infinity();
    std::vector<std::size_t> kept;
    kept.reserve(count);
    for (const std::vector<std::size_t>& front : fronts) {
        if (front.empty()) {
            continue;
        }
        std::vector<double> nearest(front.size(), k_infinity);
        for (std::size_t k = 0; k < front.size(); ++k) {
            for (const std::size_t member : kept) {
                nearest[k] = std::min(nearest[k], distance(member, front[k]));
            }
        }
        // The members not yet picked, in index order, so a tie goes to the
        // earliest.
        std::vector<std::size_t> open(front.size());
        for (std::size_t k = 0; k < front.size(); ++k) {
            open[k] = k;
        }
        while (!open.empty()) {
            std::size_t best = 0;
            for (std::size_t k = 1; k < open.size(); ++k) {
                if (nearest[open[k]] > nearest[open[best]]) {
                    best = k;
                }
            }
            const std::size_t picked = open[best];
            open.erase(open.begin() + static_cast<std::ptrdiff_t>(best));
            scores[front[picked]] = static_cast<double>(open.size() + 1);
            for (const std::size_t slot : open) {
                nearest[slot] = std::min(nearest[slot],
                                         distance(front[picked], front[slot]));
            }
        }
        kept.insert(kept.end(), front.begin(), front.end());
    }
    return scores;
}

std::vector<std::size_t> tiebroken_order(const std::vector<std::size_t>& ranks,
                                         const std::vector<double>& crowding,
                                         const std::vector<double>& tiebreak,
                                         TiebreakReport* report,
                                         std::size_t keep) {
    assert(crowding.size() == ranks.size());
    assert(tiebreak.empty() || tiebreak.size() == ranks.size());
    const std::size_t count = ranks.size();
    std::vector<std::size_t> untied(count);
    std::iota(untied.begin(), untied.end(), std::size_t{0});
    const auto by_crowding = [&ranks, &crowding](std::size_t lhs,
                                                 std::size_t rhs) {
        if (ranks[lhs] != ranks[rhs]) {
            return ranks[lhs] < ranks[rhs];
        }
        return crowding[lhs] > crowding[rhs];
    };
    std::vector<std::size_t> order = untied;
    std::stable_sort(
        order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
            if (ranks[lhs] != ranks[rhs]) {
                return ranks[lhs] < ranks[rhs];
            }
            if (!tiebreak.empty() && tiebreak[lhs] != tiebreak[rhs]) {
                return tiebreak[lhs] > tiebreak[rhs];
            }
            return crowding[lhs] > crowding[rhs];
        });
    if (report == nullptr) {
        return order;
    }
    std::stable_sort(untied.begin(), untied.end(), by_crowding);
    report->reordered = order != untied;
    report->kept_changed = false;
    if (keep > 0 && keep < count && report->reordered) {
        std::vector<bool> kept(count, false);
        for (std::size_t i = 0; i < keep; ++i) {
            kept[untied[i]] = true;
        }
        for (std::size_t i = 0; i < keep; ++i) {
            if (!kept[order[i]]) {
                report->kept_changed = true;
                break;
            }
        }
    }
    return order;
}
