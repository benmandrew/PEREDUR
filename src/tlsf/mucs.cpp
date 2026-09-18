#include "tlsf/mucs.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <optional>
#include <vector>

#include "bounded_async.hpp"
#include "runner/spot.hpp"
#include "thread_pool.hpp"
#include "tlsf/specification.hpp"

namespace tlsf {

namespace {

// Section ids for the guarantee (system) side, in the fixed order candidates
// are enumerated. Matches the all_sections() convention in tlsf/fitness.cpp.
constexpr std::size_t k_preset = 1;
constexpr std::size_t k_assert = 4;
constexpr std::size_t k_guarantee = 5;

// The guarantee-side formulae of `spec`, in a stable order (PRESET, then
// ASSERT, then GUARANTEE), each tagged with its section for reconstruction.
// Deleted conjuncts are left out: a core is a subset of what the specification
// says, and they say nothing. A candidate spec is built fresh from the subset,
// so the tombstones do not travel with it.
std::vector<CoreFormula> guarantee_side_candidates(const Specification& spec) {
    std::vector<CoreFormula> candidates;
    candidates.reserve(spec.m_preset.size() + spec.m_assert.size() +
                       spec.m_guarantee.size());
    const auto collect = [&candidates](std::size_t section_id,
                                       const Section& section) {
        for (const Formula& formula : live_formulae(section)) {
            candidates.push_back({section_id, formula});
        }
    };
    collect(k_preset, spec.m_preset);
    collect(k_assert, spec.m_assert);
    collect(k_guarantee, spec.m_guarantee);
    return candidates;
}

// Appends a tagged formula to the guarantee-side section named by its id.
void append_by_section(Specification& spec, const CoreFormula& entry) {
    switch (entry.section_id) {
        case k_preset:
            spec.m_preset.emplace_back(entry.formula);
            break;
        case k_assert:
            spec.m_assert.emplace_back(entry.formula);
            break;
        default:
            spec.m_guarantee.emplace_back(entry.formula);
            break;
    }
}

// Builds a Specification with `base`'s full environment side (INITIALLY,
// REQUIRE, ASSUME) and metadata, and only `subset` on the guarantee side. With
// `subset` = all of base's guarantee-side formulae this reproduces `base`.
Specification build_candidate_spec(const Specification& base,
                                   const std::vector<CoreFormula>& subset) {
    Specification sub;
    sub.m_title = base.m_title;
    sub.m_description = base.m_description;
    sub.m_semantics = base.m_semantics;
    sub.m_inputs = base.m_inputs;
    sub.m_outputs = base.m_outputs;
    sub.m_initially = base.m_initially;
    sub.m_require = base.m_require;
    sub.m_assume = base.m_assume;
    for (const CoreFormula& entry : subset) {
        append_by_section(sub, entry);
    }
    return sub;
}

bool is_conflict(const Specification& base,
                 const std::vector<CoreFormula>& active,
                 const RealizabilityOracle& is_realizable) {
    return !is_realizable(build_candidate_spec(base, active));
}

std::vector<CoreFormula> concat(std::vector<CoreFormula> lhs,
                                const std::vector<CoreFormula>& rhs) {
    lhs.insert(lhs.end(), rhs.begin(), rhs.end());
    return lhs;
}

// QuickXplain (Junker 2004). `background` is the guarantee-side subset assumed
// present but not under test; `candidates` is the set being minimised.
// `background_grew` records whether the caller just added to `background`, so
// the redundant conflict check on an unchanged background is skipped.
// Returns the minimal subset of `candidates` that, together with `background`,
// is still a conflict (unrealizable).
std::vector<CoreFormula> quickxplain(const Specification& base,
                                     const std::vector<CoreFormula>& background,
                                     bool background_grew,
                                     const std::vector<CoreFormula>& candidates,
                                     const RealizabilityOracle& is_realizable) {
    if (background_grew && is_conflict(base, background, is_realizable)) {
        return {};
    }
    if (candidates.size() == 1) {
        return candidates;
    }
    const auto mid = static_cast<std::ptrdiff_t>(candidates.size() / 2);
    const std::vector<CoreFormula> left(candidates.begin(),
                                        candidates.begin() + mid);
    const std::vector<CoreFormula> right(candidates.begin() + mid,
                                         candidates.end());
    const std::vector<CoreFormula> right_core = quickxplain(
        base, concat(background, left), !left.empty(), right, is_realizable);
    const std::vector<CoreFormula> left_core =
        quickxplain(base, concat(background, right_core), !right_core.empty(),
                    left, is_realizable);
    return concat(left_core, right_core);
}

// Screens every single-formula subset concurrently, returning the lowest index
// that is a conflict on its own. QuickXplain would reach such a formula only
// after log(n) halving rounds of serial probes; here the whole sweep is one
// concurrent region. Nothing is discarded when no singleton conflicts: the
// verdicts stay in the oracle's caches, so the walk that follows reads them
// instead of spawning a solver.
//
// Lowest index rather than first to answer, so concurrency cannot change which
// core comes back.
std::optional<std::size_t> screen_singletons(
    const Specification& base, const std::vector<CoreFormula>& candidates,
    const RealizabilityOracle& is_realizable, std::size_t max_in_flight) {
    if (max_in_flight <= 1 || candidates.size() < 2) {
        return std::nullopt;
    }
    std::vector<char> conflicting(candidates.size(), 0);
    run_bounded_async(
        candidates.size(), max_in_flight,
        [&base, &candidates, &is_realizable](std::size_t idx) {
            return [&base, &candidates, &is_realizable, idx] {
                return is_conflict(base, {candidates[idx]}, is_realizable);
            };
        },
        [&conflicting](std::size_t idx, bool conflicts) {
            conflicting[idx] = conflicts ? 1 : 0;
        });
    for (std::size_t idx = 0; idx < conflicting.size(); ++idx) {
        if (conflicting[idx] != 0) {
            return idx;
        }
    }
    return std::nullopt;
}

}  // namespace

MinimalUnrealizableCore extract_muc(const Specification& spec,
                                    const RealizabilityOracle& is_realizable) {
    return extract_muc(spec, is_realizable, /*max_in_flight=*/1);
}

MinimalUnrealizableCore extract_muc(const Specification& spec,
                                    const RealizabilityOracle& is_realizable,
                                    std::size_t max_in_flight) {
    assert(!is_realizable(spec) &&
           "extract_muc precondition: spec must be unrealizable");
    const std::vector<CoreFormula> candidates = guarantee_side_candidates(spec);
    MinimalUnrealizableCore result;
    if (candidates.empty()) {
        result.spec = build_candidate_spec(spec, {});
        return result;
    }
    if (const std::optional<std::size_t> single =
            screen_singletons(spec, candidates, is_realizable, max_in_flight)) {
        // A one-formula conflict is already minimal: no subset of it is a
        // conflict, because the only proper subset is empty.
        result.formulae = {candidates[*single]};
    } else {
        result.formulae =
            quickxplain(spec, /*background=*/{},
                        /*background_grew=*/false, candidates, is_realizable);
    }
    result.spec = build_candidate_spec(spec, result.formulae);
    return result;
}

MinimalUnrealizableCore extract_muc(const Specification& spec) {
    RealizabilityChecker& checker = global_real_checker();
    // Counted across the concurrent screen as well as the walk, so it is
    // atomic rather than a plain member.
    std::atomic<std::size_t> n_undecided{0};
    const RealizabilityOracle oracle =
        [&checker, &n_undecided](const Specification& candidate) {
            const std::optional<bool> verdict = checker.check_realizability_ltl(
                candidate.to_ltl(), candidate.m_inputs, candidate.m_outputs,
                tlsf::specification_sides(candidate));
            if (!verdict.has_value()) {
                n_undecided.fetch_add(1, std::memory_order_relaxed);
            }
            // Undecided reads as unrealizable, which keeps QuickXplain
            // shrinking the candidate set. What that costs -- a core resting on
            // a probe that never finished -- travels back in n_undecided rather
            // than being lost here, because at this budget on a large
            // specification it is the difference between a core and a guess.
            return verdict.value_or(false);
        };
    MinimalUnrealizableCore result =
        extract_muc(spec, oracle, dispatch_window());
    result.n_undecided = n_undecided.load(std::memory_order_relaxed);
    return result;
}

std::vector<CoreFormula> non_core_formulae(
    const Specification& spec, const std::vector<CoreFormula>& core) {
    std::vector<bool> consumed(core.size(), false);
    std::vector<CoreFormula> result;
    // Live conjuncts only, matching guarantee_side_candidates: a deleted
    // conjunct is in neither the core nor what is carried over around it.
    auto collect = [&](std::size_t section_id, const Section& section) {
        for (const Formula& formula : live_formulae(section)) {
            bool matched = false;
            for (std::size_t i = 0; i < core.size(); ++i) {
                if (!consumed[i] && core[i].section_id == section_id &&
                    core[i].formula == formula) {
                    consumed[i] = true;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                result.push_back({section_id, formula});
            }
        }
    };
    collect(k_preset, spec.m_preset);
    collect(k_assert, spec.m_assert);
    collect(k_guarantee, spec.m_guarantee);
    return result;
}

Specification reintegrate(const Specification& repaired_subspec,
                          const std::vector<CoreFormula>& non_core) {
    Specification result = repaired_subspec;
    for (const CoreFormula& entry : non_core) {
        append_by_section(result, entry);
    }
    return result;
}

const char* section_name(std::size_t section_id) {
    switch (section_id) {
        case 0:
            return "INITIALLY";
        case k_preset:
            return "PRESET";
        case 2:
            return "REQUIRE";
        case 3:
            return "ASSUME";
        case k_assert:
            return "ASSERT";
        case k_guarantee:
            return "GUARANTEE";
        default:
            return "UNKNOWN";
    }
}

}  // namespace tlsf
