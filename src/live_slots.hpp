#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

// Walks over a list of slots, each with an `m_removed` tombstone, shared by
// the FRETISH requirement lists and the TLSF sections. A tombstoned slot is
// skipped, never erased, because slots pair by index.

template <typename Slots>
std::size_t count_live_slots(const Slots& slots) {
    return static_cast<std::size_t>(
        std::count_if(slots.begin(), slots.end(),
                      [](const auto& slot) { return !slot.m_removed; }));
}

template <typename Slots>
std::vector<std::size_t> live_slot_indices(const Slots& slots) {
    std::vector<std::size_t> indices;
    indices.reserve(slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].m_removed) {
            indices.push_back(i);
        }
    }
    return indices;
}
