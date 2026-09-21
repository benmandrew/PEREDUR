#include "mucs.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "requirement.hpp"

namespace fretish {

namespace {

// Every size-@p size subset of @p candidates, lexicographically by position,
// flattened @p size entries at a time.
std::vector<std::size_t> combinations(
    const std::vector<std::size_t>& candidates, std::size_t size) {
    std::vector<std::size_t> flat;
    const std::size_t n_candidates = candidates.size();
    if (size == 0 || size > n_candidates) {
        return flat;
    }
    std::vector<std::size_t> pick(size);
    for (std::size_t idx = 0; idx < size; ++idx) {
        pick[idx] = idx;
    }
    for (;;) {
        for (const std::size_t position : pick) {
            flat.push_back(candidates[position]);
        }
        // The rightmost position that can still move right, as in any
        // lexicographic combination walk.
        std::size_t idx = size;
        while (idx > 0 && pick[idx - 1] == n_candidates - size + (idx - 1)) {
            --idx;
        }
        if (idx == 0) {
            return flat;
        }
        ++pick[idx - 1];
        for (std::size_t later = idx; later < size; ++later) {
            pick[later] = pick[later - 1] + 1;
        }
    }
}

// Verdicts for every subset of one size, by index. One byte per subset:
// 0 realizable, 1 unrealizable, 2 undecided.
constexpr char k_realizable = 0;
constexpr char k_unrealizable = 1;
constexpr char k_undecided = 2;

char encode(const std::optional<bool>& verdict) {
    if (!verdict.has_value()) {
        return k_undecided;
    }
    return *verdict ? k_realizable : k_unrealizable;
}

std::vector<char> subset_verdicts(const Specification& spec,
                                  const std::vector<std::size_t>& flat,
                                  std::size_t size,
                                  const RealizabilityVerdict& verdict,
                                  std::size_t max_in_flight) {
    const std::size_t n_subsets = flat.size() / size;
    std::vector<char> verdicts(n_subsets, k_undecided);
    const auto probe = [&spec, &flat, size, &verdict](std::size_t idx) {
        const auto first =
            flat.begin() + static_cast<std::ptrdiff_t>(idx * size);
        const std::vector<std::size_t> slots(
            first, first + static_cast<std::ptrdiff_t>(size));
        return encode(verdict(guarantee_subset(spec, slots)));
    };
    if (max_in_flight <= 1) {
        for (std::size_t idx = 0; idx < n_subsets; ++idx) {
            verdicts[idx] = probe(idx);
        }
        return verdicts;
    }
    run_bounded_async(
        n_subsets, max_in_flight,
        [&probe](std::size_t idx) {
            return [&probe, idx] { return probe(idx); };
        },
        [&verdicts](std::size_t idx, char result) { verdicts[idx] = result; });
    return verdicts;
}

// QuickXplain (Junker 2004) over slots, as tlsf::quickxplain does over tagged
// formulae. @p background is assumed present and not under test; the result is
// the minimal subset of @p candidates that conflicts together with it. An
// undecided probe reads as a conflict and is counted in @p n_undecided.
class Walk {
   public:
    Walk(const Specification& spec, const RealizabilityVerdict& verdict)
        : m_spec(spec), m_verdict(verdict) {}

    std::vector<std::size_t> run(const std::vector<std::size_t>& background,
                                 bool background_grew,
                                 const std::vector<std::size_t>& candidates) {
        if (background_grew && is_conflict(background)) {
            return {};
        }
        if (candidates.size() == 1) {
            return candidates;
        }
        const auto mid = static_cast<std::ptrdiff_t>(candidates.size() / 2);
        const std::vector<std::size_t> left(candidates.begin(),
                                            candidates.begin() + mid);
        const std::vector<std::size_t> right(candidates.begin() + mid,
                                             candidates.end());
        const std::vector<std::size_t> right_core =
            run(joined(background, left), !left.empty(), right);
        const std::vector<std::size_t> left_core =
            run(joined(background, right_core), !right_core.empty(), left);
        return joined(left_core, right_core);
    }

    [[nodiscard]] std::size_t n_undecided() const { return m_n_undecided; }

   private:
    // Sorted, so that one set lowers to one formula however the walk reached
    // it, and shares the screen's memo entries.
    static std::vector<std::size_t> joined(
        std::vector<std::size_t> lhs, const std::vector<std::size_t>& rhs) {
        lhs.insert(lhs.end(), rhs.begin(), rhs.end());
        std::sort(lhs.begin(), lhs.end());
        return lhs;
    }

    bool is_conflict(const std::vector<std::size_t>& slots) {
        const std::optional<bool> realizable =
            m_verdict(guarantee_subset(m_spec, slots));
        if (!realizable.has_value()) {
            ++m_n_undecided;
            return true;
        }
        return !*realizable;
    }

    const Specification& m_spec;
    const RealizabilityVerdict& m_verdict;
    std::size_t m_n_undecided = 0;
};

}  // namespace

Specification guarantee_subset(const Specification& spec,
                               const std::vector<std::size_t>& slots) {
    Specification subset = spec;
    subset.m_guarantees.clear();
    subset.m_guarantees.reserve(slots.size());
    for (const std::size_t slot : slots) {
        assert(slot < spec.m_guarantees.size());
        assert(!spec.m_guarantees[slot].m_removed);
        subset.m_guarantees.push_back(spec.m_guarantees[slot]);
    }
    return subset;
}

SubsetScreen screen_small_subsets(const Specification& spec,
                                  const RealizabilityVerdict& verdict,
                                  std::size_t max_size,
                                  std::size_t max_in_flight) {
    const std::vector<std::size_t> candidates = live_indices(spec.m_guarantees);
    SubsetScreen screen;
    for (std::size_t size = 1; size <= max_size && size <= candidates.size();
         ++size) {
        const std::vector<std::size_t> flat = combinations(candidates, size);
        const std::vector<char> verdicts =
            subset_verdicts(spec, flat, size, verdict, max_in_flight);
        for (std::size_t idx = 0; idx < verdicts.size(); ++idx) {
            if (verdicts[idx] == k_undecided) {
                ++screen.n_undecided;
            }
            if (verdicts[idx] != k_realizable) {
                screen.all_decided_realizable = false;
            }
            if (verdicts[idx] == k_unrealizable && !screen.conflict) {
                const auto first =
                    flat.begin() + static_cast<std::ptrdiff_t>(idx * size);
                screen.conflict = std::vector<std::size_t>(
                    first, first + static_cast<std::ptrdiff_t>(size));
            }
        }
        if (screen.conflict) {
            break;
        }
    }
    return screen;
}

GuaranteeCore extract_core(const Specification& spec,
                           const RealizabilityVerdict& verdict,
                           std::size_t screen_depth, std::size_t max_in_flight,
                           bool allow_walk) {
    GuaranteeCore core;
    const SubsetScreen screen =
        screen_small_subsets(spec, verdict, screen_depth, max_in_flight);
    core.n_undecided = screen.n_undecided;
    if (screen.conflict) {
        core.slots = *screen.conflict;
    } else if (allow_walk) {
        const std::vector<std::size_t> candidates =
            live_indices(spec.m_guarantees);
        if (!candidates.empty()) {
            Walk walk(spec, verdict);
            std::vector<std::size_t> slots = walk.run(
                /*background=*/{}, /*background_grew=*/false, candidates);
            std::sort(slots.begin(), slots.end());
            core.slots = std::move(slots);
            core.n_undecided += walk.n_undecided();
            core.provisional = walk.n_undecided() > 0;
        }
    }
    core.spec = guarantee_subset(spec, core.slots);
    return core;
}

Specification reintegrate(const Specification& repaired_core,
                          const Specification& context,
                          const std::vector<std::size_t>& slots) {
    assert(repaired_core.m_guarantees.size() == slots.size());
    Specification result = context;
    result.m_assumptions = repaired_core.m_assumptions;
    for (std::size_t idx = 0; idx < slots.size(); ++idx) {
        assert(slots[idx] < result.m_guarantees.size());
        result.m_guarantees[slots[idx]] = repaired_core.m_guarantees[idx];
    }
    return result;
}

}  // namespace fretish
