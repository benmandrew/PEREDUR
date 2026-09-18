#pragma once

#include <cstddef>
#include <vector>

#include "tlsf/specification.hpp"

namespace tlsf::internal {

// A live conjunct an operator may rewrite: its section, its slot in it, and
// which of the side's three sections it came from. Deleted conjuncts are left
// out, so an operator neither spends itself on content nothing reads nor
// resurrects what a parent threw away.
struct Slot {
    Section* m_section;
    std::size_t m_index;
    std::size_t m_section_index;
};

// @p sections is one side's three sections, initial-condition section first.
template <typename Sections>
std::vector<Slot> live_slots(const Sections& sections) {
    std::vector<Slot> slots;
    for (std::size_t index = 0; index < sections.size(); ++index) {
        Section* section = sections[index];
        for (std::size_t i = 0; i < section->size(); ++i) {
            if (!(*section)[i].m_removed) {
                slots.push_back({section, i, index});
            }
        }
    }
    return slots;
}

}  // namespace tlsf::internal
