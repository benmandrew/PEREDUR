#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "requirement.hpp"

// A repair written to JSON drops its removed guarantees (see to_json in
// include/serialisation.hpp), so a repair read back has fewer guarantees than
// the original and its slots no std::int64_ter pair with the original's by
// index. The climb pairs them by index, so climb-replay puts the removed slots
// back before it climbs.

namespace climb_replay {

// How many of the five fields @p lhs and @p rhs share.
inline std::size_t shared_fields(const Requirement& lhs,
                                 const Requirement& rhs) {
    return static_cast<std::size_t>(lhs.m_condition == rhs.m_condition) +
           static_cast<std::size_t>(lhs.m_response == rhs.m_response) +
           static_cast<std::size_t>(lhs.m_timing == rhs.m_timing) +
           static_cast<std::size_t>(lhs.m_condition_type ==
                                    rhs.m_condition_type) +
           static_cast<std::size_t>(lhs.m_scope == rhs.m_scope);
}

// @p live laid out over the slots of @p original: each requirement of
// @p live keeps its order and takes the slot of @p original that shares the
// most fields with it, over the order-preserving matching with the most
// shared fields in total, and every slot left over holds the original's
// requirement, removed. Ties take the earliest slot. Where @p live is not
// shorter than @p original, it is returned unchanged.
//
// The content of a removed slot is a guess. The run kept the requirement as
// it stood when it was removed, which the JSON does not hold, and a climb that
// restores the slot restores that content.
inline std::vector<Requirement> restore_removed_slots(
    const std::vector<Requirement>& live,
    const std::vector<Requirement>& original) {
    const std::size_t n_live = live.size();
    const std::size_t n_slots = original.size();
    if (n_live >= n_slots) {
        return live;
    }
    // best[i][j]: the most shared fields over matchings of the first i live
    // requirements into the first j slots, or -1 where none exists.
    const auto width = n_slots + 1;
    std::vector<std::int64_t> best((n_live + 1) * width, -1);
    const auto cell = [width](std::size_t row, std::size_t col) {
        return (row * width) + col;
    };
    for (std::size_t j = 0; j <= n_slots; ++j) {
        best[cell(0, j)] = 0;
    }
    for (std::size_t i = 1; i <= n_live; ++i) {
        for (std::size_t j = i; j <= n_slots; ++j) {
            const std::int64_t skip = best[cell(i, j - 1)];
            const std::int64_t take =
                best[cell(i - 1, j - 1)] < 0
                    ? -1
                    : best[cell(i - 1, j - 1)] +
                          static_cast<std::int64_t>(
                              shared_fields(live[i - 1], original[j - 1]));
            best[cell(i, j)] = std::max(skip, take);
        }
    }
    // Walked back from the end, a slot is skipped whenever skipping it keeps
    // the optimum, so among equal matchings each requirement takes the
    // earliest slot it can.
    std::vector<Requirement> slots;
    slots.reserve(n_slots);
    for (const Requirement& req : original) {
        Requirement removed = req;
        removed.m_removed = true;
        slots.push_back(std::move(removed));
    }
    std::size_t n_left = n_live;
    std::size_t slot = n_slots;
    while (n_left > 0) {
        const bool can_skip = slot > n_left && best[cell(n_left, slot - 1)] ==
                                                   best[cell(n_left, slot)];
        if (can_skip) {
            --slot;
            continue;
        }
        slots[slot - 1] = live[n_left - 1];
        --n_left;
        --slot;
    }
    return slots;
}

}  // namespace climb_replay
