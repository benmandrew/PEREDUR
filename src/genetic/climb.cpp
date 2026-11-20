#include "genetic/climb.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "config.hpp"
#include "genetic/ordered_language.hpp"
#include "genetic/output_gate.hpp"
#include "psp/ordered_language.hpp"
#include "psp/specification.hpp"
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

template <typename Spec, typename Field>
std::uint64_t step_priority(const OrderStep<Spec, Field>& step,
                            std::size_t permutation) {
    const std::uint64_t field =
        step.m_field.has_value() ? static_cast<std::uint64_t>(*step.m_field) + 1
                                 : 0;
    auto identity = static_cast<std::uint64_t>(step.m_slot);
    identity = (identity << 2U) | static_cast<std::uint64_t>(step.m_kind);
    identity = (identity << 1U) | static_cast<std::uint64_t>(step.m_side);
    identity = (identity << 3U) | field;
    return mix(mix(static_cast<std::uint64_t>(permutation)) ^ identity);
}

// The chain a step crosses, without its destination, or empty where the step
// has no intermediate entries.
template <typename Spec>
std::vector<typename OrderedLanguage<Spec>::ChainValue>
chain_before_destination(const Spec& current,
                         const typename OrderedLanguage<Spec>::Step& step) {
    std::vector<typename OrderedLanguage<Spec>::ChainValue> chain =
        OrderedLanguage<Spec>::step_chain(current, step);
    if (!chain.empty()) {
        chain.pop_back();
    }
    return chain;
}

// Whether @p spec has a live guarantee.
template <typename Spec>
bool has_live_guarantee(const Spec& spec) {
    using Language = OrderedLanguage<Spec>;
    const auto& guarantees = Language::guarantees(spec);
    return std::any_of(guarantees.begin(), guarantees.end(),
                       [](const auto& req) { return Language::is_live(req); });
}

}  // namespace

template <typename Spec, typename Field>
void order_steps(std::vector<OrderStep<Spec, Field>>& steps,
                 std::size_t permutation) {
    if (permutation == 0) {
        return;
    }
    std::vector<std::pair<std::uint64_t, std::size_t>> keys;
    keys.reserve(steps.size());
    for (std::size_t i = 0; i < steps.size(); ++i) {
        keys.emplace_back(step_priority(steps[i], permutation), i);
    }
    std::stable_sort(keys.begin(), keys.end());
    std::vector<OrderStep<Spec, Field>> ordered;
    ordered.reserve(steps.size());
    for (const auto& [priority, index] : keys) {
        ordered.push_back(std::move(steps[index]));
    }
    steps = std::move(ordered);
}

template <typename Spec>
std::optional<Spec> furthest_accepted(
    const Spec& current, const typename OrderedLanguage<Spec>::Step& step,
    const typename ClimbNonDeduced<ClimbPredicateT<Spec>>::type& accepts) {
    if (accepts(step.m_result)) {
        return step.m_result;
    }
    const std::vector<typename OrderedLanguage<Spec>::ChainValue> chain =
        chain_before_destination(current, step);
    // Invariant: entry `low` is accepted (-1 standing for current itself) and
    // entry `high` is not (the destination, just refused, at chain.size()).
    std::ptrdiff_t low = -1;
    auto high = static_cast<std::ptrdiff_t>(chain.size());
    std::optional<Spec> best;
    while (high - low > 1) {
        const std::ptrdiff_t mid = low + ((high - low) / 2);
        std::optional<Spec> candidate = OrderedLanguage<Spec>::with_slot_value(
            current, step.m_side, step.m_slot,
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

template <typename Spec>
Spec climb(const Spec& start, const Spec& original,
           const typename ClimbNonDeduced<ClimbPredicateT<Spec>>::type& accepts,
           std::size_t permutation) {
    Spec current = start;
    std::unordered_set<Spec> visited{start};
    const ClimbPredicateT<Spec> unvisited = [&visited,
                                             &accepts](const Spec& spec) {
        if (visited.count(spec) != 0) {
            return false;
        }
        ++ClimbStats::n_queries;
        return accepts(spec);
    };
    for (;;) {
        std::vector<typename OrderedLanguage<Spec>::Step> steps =
            OrderedLanguage<Spec>::steps_towards(current, original);
        order_steps(steps, permutation);
        std::optional<Spec> next;
        for (const auto& step : steps) {
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

template <typename Spec>
ClimbPredicateT<Spec> climb_gate(
    const Config& cfg,
    typename ClimbNonDeduced<FitnessFunctionT<Spec>>::type status) {
    return [&cfg, status = std::move(status)](const Spec& spec) {
        // A check that raises refuses the step. The climb then stays on the
        // repair it stood on, which has already passed the gate, so a tool
        // failure here costs one step and never writes an unchecked repair.
        // Left to propagate, it ended a run whose search had finished.
        try {
            // The gate's status walk would ask every subset after the first
            // part a step touched, where one query on the whole settles most
            // refusals, and its verdict is the decided entry the memo's rules
            // read.
            if (has_live_guarantee(spec) &&
                OrderedLanguage<Spec>::check_realizability(spec) ==
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

template <typename Spec>
std::vector<Spec> climb_repairs(
    const std::vector<Spec>& repairs, const Spec& original,
    const typename ClimbNonDeduced<ClimbPredicateT<Spec>>::type& accepts,
    std::size_t n_orders) {
    const auto started = std::chrono::steady_clock::now();
    std::vector<Spec> starts;
    std::unordered_set<Spec> seen;
    for (const Spec& spec : repairs) {
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

    n_orders = std::max<std::size_t>(n_orders, 1);
    const auto climb_one = [&starts, &original, &accepts,
                            n_orders](std::size_t idx) {
        std::vector<Spec> climbed;
        climbed.reserve(n_orders);
        for (std::size_t order = 0; order < n_orders; ++order) {
            climbed.push_back(climb(starts[idx], original, accepts, order));
        }
        return climbed;
    };
    std::vector<std::vector<Spec>> results(starts.size());
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
            [&results](std::size_t idx, std::vector<Spec> climbed) {
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
    std::vector<Spec> distinct;
    std::unordered_set<Spec> emitted;
    for (std::vector<Spec>& climbed : results) {
        for (Spec& spec : climbed) {
            if (emitted.insert(spec).second) {
                distinct.push_back(std::move(spec));
            }
        }
    }
    return distinct;
}

template void order_steps(std::vector<Step>&, std::size_t);
template std::optional<Specification> furthest_accepted(const Specification&,
                                                        const Step&,
                                                        const ClimbPredicate&);
template Specification climb(const Specification&, const Specification&,
                             const ClimbPredicate&, std::size_t);
template ClimbPredicate climb_gate(const Config&, FitnessFunction);
template std::vector<Specification> climb_repairs(
    const std::vector<Specification>&, const Specification&,
    const ClimbPredicate&, std::size_t);
template void order_steps(std::vector<psp::Step>&, std::size_t);
template std::optional<psp::Specification> furthest_accepted(
    const psp::Specification&, const psp::Step&,
    const ClimbPredicateT<psp::Specification>&);
template psp::Specification climb(const psp::Specification&,
                                  const psp::Specification&,
                                  const ClimbPredicateT<psp::Specification>&,
                                  std::size_t);
template ClimbPredicateT<psp::Specification> climb_gate(
    const Config&, FitnessFunctionT<psp::Specification>);
template std::vector<psp::Specification> climb_repairs(
    const std::vector<psp::Specification>&, const psp::Specification&,
    const ClimbPredicateT<psp::Specification>&, std::size_t);
