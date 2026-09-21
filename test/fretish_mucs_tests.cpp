#include <algorithm>
#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fixtures.hpp"
#include "prop_formula.hpp"
#include "repair/mucs.hpp"
#include "requirement.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

constexpr std::string_view k_test_suite = "fretish_mucs";

// `G response` for each named output, over input `req` and outputs a..e, with
// one mode. The fake verdicts below read a probe by the responses it holds.
Specification spec_over(const std::vector<std::string>& responses) {
    std::vector<Requirement> guarantees;
    guarantees.reserve(responses.size());
    for (const std::string& response : responses) {
        guarantees.push_back(make_req("true", response));
    }
    return Specification({make_req("req", "req")}, std::move(guarantees),
                         {"req"}, {"a", "b", "c", "d", "e"}, {"mode"});
}

bool holds(const Specification& spec, const std::string& response) {
    return std::any_of(spec.m_guarantees.begin(), spec.m_guarantees.end(),
                       [&response](const Requirement& req) {
                           return !req.m_removed &&
                                  req.m_response.to_string() == response;
                       });
}

// Unrealizable exactly when the probe holds every response in one of
// @p conflicts.
fretish::RealizabilityVerdict conflicts_on(
    const std::vector<std::vector<std::string>>& conflicts) {
    return [conflicts](const Specification& probe) -> std::optional<bool> {
        for (const std::vector<std::string>& conflict : conflicts) {
            const bool all = std::all_of(conflict.begin(), conflict.end(),
                                         [&probe](const std::string& atom) {
                                             return holds(probe, atom);
                                         });
            if (all) {
                return false;
            }
        }
        return true;
    };
}

TEST(test_fretish_core_singleton) {
    const Specification spec = spec_over({"a", "b", "c"});
    const fretish::GuaranteeCore core =
        fretish::extract_core(spec, conflicts_on({{"b"}}), 3, 1, true);
    expect(core.slots == std::vector<std::size_t>{1}, "core is slot {1}");
    expect(core.spec.m_guarantees.size() == 1 && holds(core.spec, "b"),
           "the core spec holds exactly the culprit");
    expect(!core.provisional && core.n_undecided == 0,
           "a screened core is confirmed");
}

TEST(test_fretish_core_pair_by_screen_and_walk) {
    const Specification spec = spec_over({"a", "b", "c", "d"});
    const fretish::RealizabilityVerdict verdict = conflicts_on({{"a", "c"}});
    const fretish::GuaranteeCore screened =
        fretish::extract_core(spec, verdict, 3, 1, true);
    expect(screened.slots == std::vector<std::size_t>({0, 2}),
           "the screen finds the pair {0, 2}");
    // Depth 1 screens no pair, so the walk has to find it.
    const fretish::GuaranteeCore walked =
        fretish::extract_core(spec, verdict, 1, 1, true);
    expect(walked.slots == std::vector<std::size_t>({0, 2}),
           "QuickXplain finds the same pair");
    const fretish::GuaranteeCore forbidden =
        fretish::extract_core(spec, verdict, 1, 1, false);
    expect(forbidden.slots.empty(),
           "with the walk forbidden, a pair beyond the depth is not found");
}

// Two pair conflicts; the lower-ranked one comes back whichever probe answers
// first.
TEST(test_fretish_core_lowest_rank_under_concurrency) {
    const Specification spec = spec_over({"a", "b", "c", "d"});
    const fretish::RealizabilityVerdict verdict =
        conflicts_on({{"b", "d"}, {"a", "c"}});
    for (int round = 0; round < 5; ++round) {
        const fretish::GuaranteeCore core =
            fretish::extract_core(spec, verdict, 2, 4, false);
        expect(core.slots == std::vector<std::size_t>({0, 2}),
               "the lexicographically first conflict is returned");
    }
}

TEST(test_fretish_core_skips_tombstones) {
    Specification spec = spec_over({"a", "b", "c"});
    spec.m_guarantees[0].m_removed = true;
    std::atomic<bool> saw_tombstone{false};
    const fretish::RealizabilityVerdict base = conflicts_on({{"a"}, {"c"}});
    const fretish::RealizabilityVerdict verdict =
        [&base, &saw_tombstone](const Specification& probe) {
            for (const Requirement& req : probe.m_guarantees) {
                if (req.m_removed) {
                    saw_tombstone = true;
                }
            }
            return base(probe);
        };
    const fretish::GuaranteeCore core =
        fretish::extract_core(spec, verdict, 3, 1, true);
    expect(core.slots == std::vector<std::size_t>{2},
           "the removed guarantee is no candidate, so the core is slot {2}");
    expect(!saw_tombstone, "no probe carries a tombstone");
}

TEST(test_fretish_core_carries_environment) {
    const Specification spec = spec_over({"a", "b"});
    const fretish::GuaranteeCore core =
        fretish::extract_core(spec, conflicts_on({{"b"}}), 3, 1, true);
    expect(core.spec.m_assumptions == spec.m_assumptions,
           "every assumption goes into the core");
    expect(core.spec.m_modes == spec.m_modes, "the modes go into the core");
    expect(core.spec.m_in_atoms == spec.m_in_atoms &&
               core.spec.m_out_atoms == spec.m_out_atoms,
           "the alphabet goes into the core");
}

TEST(test_fretish_screen_counts_undecided) {
    const Specification spec = spec_over({"a", "b"});
    const fretish::RealizabilityVerdict verdict =
        [](const Specification& probe) -> std::optional<bool> {
        if (holds(probe, "a")) {
            return std::nullopt;
        }
        return true;
    };
    const fretish::SubsetScreen screen =
        fretish::screen_small_subsets(spec, verdict, 2, 1);
    expect(!screen.conflict, "an undecided probe is no conflict");
    expect(screen.n_undecided == 2, "{a} and {a, b} went undecided");
    expect(!screen.all_decided_realizable,
           "an undecided probe fails the clean-screen test");
    const fretish::SubsetScreen clean =
        fretish::screen_small_subsets(spec, conflicts_on({}), 2, 1);
    expect(clean.all_decided_realizable && clean.n_undecided == 0,
           "a spec with no small conflict screens clean");
}

TEST(test_fretish_reintegrate_restores_slots) {
    const Specification context = spec_over({"a", "b", "c", "d"});
    const std::vector<std::size_t> slots = {1, 3};
    Specification repaired = fretish::guarantee_subset(context, slots);
    repaired.m_guarantees[0] = make_req("req", "e");
    repaired.m_guarantees[1].m_removed = true;
    repaired.m_assumptions.push_back(make_req("true", "req"));

    const Specification whole = fretish::reintegrate(repaired, context, slots);
    expect(whole.m_guarantees.size() == 4, "no slot is added or lost");
    expect(whole.m_guarantees[0] == context.m_guarantees[0] &&
               whole.m_guarantees[2] == context.m_guarantees[2],
           "the non-core slots are the context's");
    expect(whole.m_guarantees[1] == make_req("req", "e"),
           "a repaired guarantee returns to its own slot");
    expect(whole.m_guarantees[3].m_removed,
           "a removed core guarantee stays a tombstone in its slot");
    expect(whole.m_assumptions == repaired.m_assumptions,
           "the environment side is the repaired core's");
    expect(whole.m_modes == context.m_modes, "the modes are kept");
}

}  // namespace
