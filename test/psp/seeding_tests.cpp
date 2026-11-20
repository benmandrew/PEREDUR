// The faults of the PSP seeded corpus (psp::seeded_edits): none weaker than
// the slot it replaces, each one the rule calls stronger confirmed by SPOT,
// every fault class present, and the slots that must not move left alone.

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "ltl_oracle.hpp"
#include "prop_formula.hpp"
#include "psp/requirement.hpp"
#include "psp/seeding.hpp"
#include "psp/specification.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "psp_seeding";

using psp::PspScopeKind;
using psp::Requirement;
using psp::SeededEdit;
using psp::SeedRelation;
using psp::Specification;

std::optional<Formula> formula(const char* text) {
    return text == nullptr ? std::nullopt
                           : std::optional<Formula>(Formula(text));
}

// The fix of examples/psp-cruise: one guarantee under each event-delimited
// scope but after, and a bounded pattern.
Specification cruise() {
    return Specification(
        {Requirement(psp::pattern::Universality{}, PspScopeKind::Globally,
                     std::nullopt, formula("!(set & brake)"))},
        {Requirement(psp::pattern::Universality{}, PspScopeKind::AfterUntil,
                     std::nullopt, formula("cruise"), formula("set"),
                     formula("brake | fault")),
         Requirement(psp::pattern::Response{}, PspScopeKind::Globally,
                     formula("fault"), formula("!cruise")),
         Requirement(psp::pattern::Absence{}, PspScopeKind::Before,
                     formula("cruise"), std::nullopt, std::nullopt,
                     formula("set")),
         Requirement(psp::pattern::BoundedResponse{2}, PspScopeKind::Between,
                     formula("slow"), formula("throttle"), formula("set"),
                     formula("brake"))},
        {"set", "brake", "fault", "slow"}, {"cruise", "throttle"});
}

// SPOT's verdict on each `lhs -> rhs`, in batches small enough for one
// ltlfilt run each.
std::vector<bool> spot_implications(const std::vector<Implication>& queries) {
    constexpr std::size_t k_batch = 100;
    std::vector<bool> out;
    for (std::size_t start = 0; start < queries.size(); start += k_batch) {
        const std::vector<Implication> batch(
            queries.begin() + static_cast<std::ptrdiff_t>(start),
            queries.begin() + static_cast<std::ptrdiff_t>(
                                  std::min(queries.size(), start + k_batch)));
        const std::vector<bool> holds = ltl_implications(batch);
        out.insert(out.end(), holds.begin(), holds.end());
    }
    return out;
}

// Every fault the rule proves implies its slot implies it to SPOT. The rule
// never proves the slot implies the fault, but SPOT sometimes does: `cruise &
// !brake` for `cruise` in a scope that closes on brake is equivalent to it.
// seed.py's compare filter drops those; most faults are not among them.
TEST(psp_seeded_edits_are_stronger_or_unproven) {
    const Specification base = cruise();
    const std::vector<SeededEdit> edits = psp::seeded_edits(base);
    expect(!edits.empty(), "the cruise fix has seeded faults");
    std::vector<Implication> queries;
    for (const SeededEdit& edit : edits) {
        const std::string& before = base.m_guarantees[edit.m_slot].m_ltl;
        queries.push_back({before, edit.m_requirement.m_ltl});
        queries.push_back({edit.m_requirement.m_ltl, before});
    }
    const std::vector<bool> holds = spot_implications(queries);
    std::size_t n_stronger = 0;
    std::size_t n_sideways_incomparable = 0;
    std::size_t n_weaker_or_equal = 0;
    for (std::size_t k = 0; k < edits.size(); ++k) {
        const SeededEdit& edit = edits[k];
        const bool weaker_or_equal = holds[2 * k];
        const bool stronger = holds[(2 * k) + 1];
        const std::string label = edit.m_edit + " on slot " +
                                  std::to_string(edit.m_slot) + ": " +
                                  edit.m_requirement.to_string();
        if (weaker_or_equal) {
            ++n_weaker_or_equal;
        }
        if (edit.m_relation == SeedRelation::Stronger) {
            ++n_stronger;
            expect(stronger,
                   "the rule proved a fault implies its slot and "
                   "SPOT does not: " +
                       label);
        } else if (!stronger) {
            ++n_sideways_incomparable;
        }
    }
    expect(n_stronger > 0, "some fault is a step up an order");
    expect(n_sideways_incomparable > 0, "some fault is incomparable");
    expect(n_weaker_or_equal * 10 < edits.size(),
           "fewer than one fault in ten is weaker or equivalent to SPOT, " +
               std::to_string(n_weaker_or_equal) + " of " +
               std::to_string(edits.size()));
}

// Each fault class of SeededEdit occurs on the cruise fix, so none of them is
// silently empty.
TEST(psp_seeded_edits_cover_every_fault_class) {
    std::set<std::string> classes;
    for (const SeededEdit& edit : psp::seeded_edits(cruise())) {
        classes.insert(edit.m_edit);
    }
    for (const char* name : {"pattern-step", "pattern-swap", "scope-step",
                             "scope-swap", "swap-delimiters", "count-step",
                             "narrow-field", "flip-literal", "swap-signal"}) {
        expect(classes.count(name) == 1,
               std::string("no seeded fault of class ") + name);
    }
}

// A bounded response's count falls, the swap reverses the delimiters, and
// before R read as after R takes R as its opener.
TEST(psp_seeded_edits_move_the_expected_fields) {
    const Specification base = cruise();
    const std::vector<SeededEdit> edits = psp::seeded_edits(base);
    std::set<std::size_t> counts;
    bool swapped = false;
    bool before_as_after = false;
    for (const SeededEdit& edit : edits) {
        const Requirement& req = edit.m_requirement;
        if (edit.m_edit == "count-step") {
            expect(edit.m_slot == 3, "only slot 3 has a count");
            counts.insert(psp::pattern_count(req.m_pattern).value_or(0));
        }
        if (edit.m_edit == "swap-delimiters" && edit.m_slot == 3) {
            swapped = req.m_q == formula("brake") && req.m_r == formula("set");
        }
        if (edit.m_edit == "scope-swap" && edit.m_slot == 2 &&
            req.m_scope == PspScopeKind::After) {
            before_as_after = req.m_q == formula("set") && !req.m_r;
        }
    }
    expect(counts == std::set<std::size_t>{1},
           "bounded response 2 steps to count 1 only");
    expect(swapped, "between set and brake swaps to between brake and set");
    expect(before_as_after, "before set is read as after set");
}

// Assumptions, fixed, unweakenable and removed guarantees are never edited,
// and apply_seeded_edit replaces exactly the edited slot.
TEST(psp_seeded_edits_leave_frozen_slots_alone) {
    Specification base = cruise();
    base.m_guarantees[1].m_weakenable = false;
    base.m_guarantees[2].m_removed = true;
    base.m_guarantees.push_back(Requirement::fixed(Formula("G F cruise")));
    std::map<std::size_t, std::size_t> per_slot;
    for (const SeededEdit& edit : psp::seeded_edits(base)) {
        ++per_slot[edit.m_slot];
        const Specification mutant = psp::apply_seeded_edit(base, edit);
        expect(mutant.m_assumptions == base.m_assumptions,
               "an edit leaves the assumptions");
        for (std::size_t slot = 0; slot < base.m_guarantees.size(); ++slot) {
            const bool same =
                mutant.m_guarantees[slot] == base.m_guarantees[slot];
            expect(same == (slot != edit.m_slot),
                   "apply_seeded_edit replaces the edited slot alone");
        }
    }
    expect(per_slot.count(0) == 1 && per_slot.count(3) == 1,
           "the weakenable slots are edited");
    expect(per_slot.count(1) == 0 && per_slot.count(2) == 0 &&
               per_slot.count(4) == 0,
           "unweakenable, removed and fixed slots are not");
}

// The listing is a function of the specification alone.
TEST(psp_seeded_edits_are_deterministic) {
    const std::vector<SeededEdit> first = psp::seeded_edits(cruise());
    const std::vector<SeededEdit> second = psp::seeded_edits(cruise());
    expect(first.size() == second.size(), "same number of faults");
    for (std::size_t k = 0; k < std::min(first.size(), second.size()); ++k) {
        expect(first[k].m_slot == second[k].m_slot &&
                   first[k].m_edit == second[k].m_edit &&
                   first[k].m_requirement == second[k].m_requirement,
               "same fault at index " + std::to_string(k));
    }
}

}  // namespace
