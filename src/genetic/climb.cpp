#include "genetic/climb.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "config.hpp"
#include "genetic/order_moves.hpp"
#include "genetic/output_gate.hpp"
#include "requirement.hpp"
#include "runner/spot.hpp"
#include "thread_pool.hpp"

namespace {

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
                    const ClimbPredicate& accepts) {
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
        const std::vector<Step> steps = steps_towards(current, original);
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
        // A check that raises refuses the step. The climb then stays on the
        // repair it stood on, which has already passed the gate, so a tool
        // failure here costs one step and never writes an unchecked repair.
        // Left to propagate, it ended a run whose search had finished.
        try {
            // The gate's status walk would ask every subset after the first
            // part a step touched, where one query on the whole settles most
            // refusals, and its verdict is the decided entry the memo's rules
            // read.
            if (count_live(spec.m_guarantees) > 0 &&
                global_real_checker().check_realizability(spec) ==
                    std::optional<bool>(false)) {
                return false;
            }
            return passes_output_gate(spec, cfg, status);
        } catch (const std::exception&) {
            ClimbStats::n_gate_errors.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    };
}

std::vector<Specification> climb_repairs(
    const std::vector<Specification>& repairs, const Specification& original,
    const ClimbPredicate& accepts) {
    const auto started = std::chrono::steady_clock::now();
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

    const auto climb_one = [&starts, &original, &accepts](std::size_t idx) {
        return climb(starts[idx], original, accepts);
    };
    std::vector<Specification> results(starts.size());
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
            [&results](std::size_t idx, Specification climbed) {
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
    ClimbStats::wall_s += std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - started)
                              .count();
    std::vector<Specification> distinct;
    std::unordered_set<Specification> emitted;
    for (Specification& spec : results) {
        if (emitted.insert(spec).second) {
            distinct.push_back(std::move(spec));
        }
    }
    return distinct;
}
