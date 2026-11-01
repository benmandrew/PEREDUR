#include "genetic/climb.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "config.hpp"
#include "fitness/syntactic_similarity.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/output_gate.hpp"
#include "requirement.hpp"
#include "runner/spot.hpp"
#include "thread_pool.hpp"

namespace {

// SplitMix64's finaliser, spelt out rather than std::hash so that the step
// priorities, and so the climbed repairs, agree across standard libraries.
std::uint64_t mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::uint64_t step_priority(const Step& step, std::size_t permutation) {
    const std::uint64_t field =
        step.m_field.has_value() ? static_cast<std::uint64_t>(*step.m_field) + 1
                                 : 0;
    auto identity = static_cast<std::uint64_t>(step.m_slot);
    identity = (identity << 2U) | static_cast<std::uint64_t>(step.m_kind);
    identity = (identity << 1U) | static_cast<std::uint64_t>(step.m_side);
    identity = (identity << 3U) | field;
    return mix(mix(static_cast<std::uint64_t>(permutation)) ^ identity);
}

// The timing chain a timing step crosses, without its destination, or empty
// where the step has no intermediate entries.
std::vector<Timing> chain_before_destination(const Specification& current,
                                             const Step& step) {
    std::vector<Timing> chain = timing_step_chain(current, step);
    if (!chain.empty()) {
        chain.pop_back();
    }
    return chain;
}

}  // namespace

void order_steps(std::vector<Step>& steps, const Specification& current,
                 const ClimbRule& rule) {
    if (rule.permutation > 0) {
        std::vector<std::pair<std::uint64_t, std::size_t>> keys;
        keys.reserve(steps.size());
        for (std::size_t i = 0; i < steps.size(); ++i) {
            keys.emplace_back(step_priority(steps[i], rule.permutation), i);
        }
        std::stable_sort(keys.begin(), keys.end());
        std::vector<Step> ordered;
        ordered.reserve(steps.size());
        for (const auto& [priority, index] : keys) {
            ordered.push_back(std::move(steps[index]));
        }
        steps = std::move(ordered);
        return;
    }
    if (rule.order == ClimbOrder::Slot || !rule.similarity) {
        return;
    }
    const double base = rule.similarity(current);
    std::vector<std::pair<double, std::size_t>> gains;
    gains.reserve(steps.size());
    for (std::size_t i = 0; i < steps.size(); ++i) {
        gains.emplace_back(rule.similarity(steps[i].m_result) - base, i);
    }
    std::stable_sort(
        gains.begin(), gains.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });
    std::vector<Step> ordered;
    ordered.reserve(steps.size());
    for (const auto& [gain, index] : gains) {
        ordered.push_back(std::move(steps[index]));
    }
    steps = std::move(ordered);
}

std::optional<Specification> furthest_accepted(const Specification& current,
                                               const Step& step,
                                               const ClimbPredicate& accepts) {
    if (accepts(step.m_result)) {
        return step.m_result;
    }
    const std::vector<Timing> chain = chain_before_destination(current, step);
    // Invariant: entry `low` is accepted (-1 standing for current itself) and
    // entry `high` is not (the destination, just refused, at chain.size()).
    std::ptrdiff_t low = -1;
    auto high = static_cast<std::ptrdiff_t>(chain.size());
    std::optional<Specification> best;
    while (high - low > 1) {
        const std::ptrdiff_t mid = low + ((high - low) / 2);
        std::optional<Specification> candidate =
            with_slot_timing(current, step.m_side, step.m_slot,
                             chain[static_cast<std::size_t>(mid)]);
        if (candidate.has_value() && accepts(*candidate)) {
            low = mid;
            best = std::move(candidate);
        } else {
            high = mid;
        }
    }
    return best;
}

Specification climb(const Specification& start, const Specification& original,
                    const ClimbPredicate& accepts, const ClimbRule& rule) {
    Specification current = start;
    std::unordered_set<Specification> visited{start};
    const ClimbPredicate unvisited = [&visited,
                                      &accepts](const Specification& spec) {
        if (visited.count(spec) != 0) {
            return false;
        }
        ++ClimbStats::n_queries;
        return accepts(spec);
    };
    for (;;) {
        std::vector<Step> steps = steps_towards(current, original);
        order_steps(steps, current, rule);
        std::optional<Specification> next;
        for (const Step& step : steps) {
            ++ClimbStats::n_steps_tried;
            next = furthest_accepted(current, step, unvisited);
            if (next.has_value()) {
                break;
            }
        }
        if (!next.has_value()) {
            return current;
        }
        ++ClimbStats::n_steps_accepted;
        visited.insert(*next);
        current = std::move(*next);
    }
}

ClimbPredicate climb_gate(const Config& cfg, FitnessFunction status) {
    return [&cfg, status = std::move(status)](const Specification& spec) {
        // The gate's status walk would ask every subset after the first part
        // a step touched, where one query on the whole settles most refusals,
        // and its verdict is the decided entry the memo's rules read.
        if (count_live(spec.m_guarantees) > 0 &&
            global_real_checker().check_realizability(spec) ==
                std::optional<bool>(false)) {
            return false;
        }
        return passes_output_gate(spec, cfg, status);
    };
}

std::vector<Specification> climb_repairs(
    const std::vector<Specification>& repairs, const Specification& original,
    const Config& cfg, const ClimbPredicate& accepts) {
    std::vector<Specification> starts;
    std::unordered_set<Specification> seen;
    for (const Specification& spec : repairs) {
        if (seen.insert(spec).second) {
            starts.push_back(spec);
        }
    }
    ClimbStats::n_climbed += starts.size();
    // Read without the checker's lock, as nothing else queries it while the
    // climb runs and the workers have joined by the second read.
    const auto checker_counts = [] {
        return std::array<std::size_t, 6>{
            RealizabilityChecker::n_cache_misses,
            RealizabilityChecker::n_cache_hits,
            RealizabilityChecker::n_subsumed,
            RealizabilityChecker::n_rule_subsumed,
            RealizabilityChecker::n_satisfiability_routed,
            ControllerReuseStats::n_hits};
    };
    const std::array<std::size_t, 6> before = checker_counts();

    const std::size_t n_orders = std::max<std::size_t>(cfg.climb_orders, 1);
    const auto climb_one = [&starts, &original, &cfg, &accepts,
                            n_orders](std::size_t idx) {
        ClimbRule rule;
        rule.order = cfg.climb_order;
        rule.similarity = [&original](const Specification& spec) {
            return syntactic_similarity(spec, original);
        };
        std::vector<Specification> climbed;
        for (std::size_t order = 0; order < n_orders; ++order) {
            rule.permutation = order;
            climbed.push_back(climb(starts[idx], original, accepts, rule));
        }
        return climbed;
    };
    std::vector<std::vector<Specification>> results(starts.size());
    // Serial at one worker, as gate_verdicts is: the gate's own status walk
    // dispatches to the pool, which a climb holding the only worker would
    // starve.
    const std::size_t max_in_flight = dispatch_window();
    if (max_in_flight <= 1) {
        for (std::size_t idx = 0; idx < starts.size(); ++idx) {
            results[idx] = climb_one(idx);
        }
    } else {
        run_bounded_async(
            starts.size(), max_in_flight,
            [&climb_one](std::size_t idx) {
                return [&climb_one, idx] { return climb_one(idx); };
            },
            [&results](std::size_t idx, std::vector<Specification> climbed) {
                results[idx] = std::move(climbed);
            });
    }

    const std::array<std::size_t, 6> after = checker_counts();
    ClimbStats::n_ltlsynt_execs += after[0] - before[0];
    ClimbStats::n_memo_hits += after[1] - before[1];
    ClimbStats::n_subsumed += after[2] - before[2];
    ClimbStats::n_rule_subsumed += after[3] - before[3];
    ClimbStats::n_satisfiability_routed += after[4] - before[4];
    ClimbStats::n_controller_hits += after[5] - before[5];
    std::vector<Specification> distinct;
    std::unordered_set<Specification> emitted;
    for (std::vector<Specification>& climbed : results) {
        for (Specification& spec : climbed) {
            if (emitted.insert(spec).second) {
                distinct.push_back(std::move(spec));
            }
        }
    }
    return distinct;
}
