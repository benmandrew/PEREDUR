// Tests over the width of the global thread pool: the size a caller asks for,
// the default a caller who asks for nothing gets, and that a second request is
// ignored.
//
// global_thread_pool() is a function-local static, built on first use and never
// resized, so whichever of those a process exercises it can only exercise once.
// That is why the suite branches on PEREDUR_TEST_POOL_SIZE rather than testing
// both in sequence, and why CMake registers it twice -- one process per case --
// the same shape as the profile suite's two registrations.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "cpu_limits.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"
#include "thread_pool.hpp"

namespace {

constexpr std::string_view k_test_suite = "thread_pool";

// Returns 0 when the variable is unset or does not parse as a positive count,
// which is the "no size was requested" case rather than a request for zero.
std::size_t requested_pool_size() {
    const char* const value = std::getenv("PEREDUR_TEST_POOL_SIZE");
    if (value == nullptr) {
        return 0;
    }
    try {
        const std::int64_t parsed = std::stoll(value);
        return parsed > 0 ? static_cast<std::size_t>(parsed) : 0;
    } catch (const std::exception&) {
        return 0;
    }
}

// The default width, which the sized case must not accidentally ask for.
std::size_t default_pool_size() { return available_parallelism(); }

// A size that cannot coincide with the default. Asking for exactly the width
// the pool would have taken anyway makes both assertions below pass whatever
// set_thread_pool_size does, and the machine that would expose it is a CI
// runner with as many cores as the number CMake happens to pass in.
std::size_t distinct_from_default(std::size_t requested) {
    return requested == default_pool_size() ? requested + 1 : requested;
}

void test_pool_takes_the_requested_size(std::size_t requested) {
    set_thread_pool_size(requested);
    expect(global_thread_pool().size() == requested,
           "the global pool should run as many workers as were requested");

    // The header promises that a later call has no effect. Nothing enforces
    // that promise but this: g_pool_size is still writable afterwards, so a
    // refactor that read it per call rather than once, at construction, would
    // go unnoticed.
    set_thread_pool_size(requested + 1);
    expect(global_thread_pool().size() == requested,
           "a second set_thread_pool_size call should not resize the pool "
           "already built");
}

void test_pool_defaults_to_the_available_parallelism() {
    expect(global_thread_pool().size() == default_pool_size(),
           "a pool nobody sized should run one worker per usable CPU, or one "
           "worker where that is unknown");
}

TEST(test_pool_takes_the_requested_or_default_size) {
    const std::size_t requested = requested_pool_size();
    if (requested > 0) {
        test_pool_takes_the_requested_size(distinct_from_default(requested));
    } else {
        test_pool_defaults_to_the_available_parallelism();
    }
}

// Whatever the machine, the affinity mask and the cgroup quota, sizing a pool
// from this must never ask for zero workers.
TEST(test_available_parallelism_is_never_zero) {
    expect(available_parallelism() >= 1,
           "available_parallelism should always report at least one worker");
}

void expect_cpu_max(std::string_view content,
                    std::optional<std::size_t> expected, const char* message) {
    expect(parse_cgroup_v2_cpu_max(content) == expected, message);
}

TEST(test_cgroup_v2_cpu_max_parses) {
    expect_cpu_max("max 100000", std::nullopt,
                   "a cgroup v2 quota of \"max\" is unlimited, so it bounds "
                   "nothing");
    expect_cpu_max("400000 100000", std::size_t{4},
                   "a cgroup v2 quota of four periods should allow four "
                   "workers");
    expect_cpu_max("150000 100000", std::size_t{2},
                   "a fractional cgroup v2 quota should round up rather than "
                   "stranding the surplus");
    expect_cpu_max("100000 100000", std::size_t{1},
                   "a cgroup v2 quota of one period should allow one worker");
    expect_cpu_max("50000 100000", std::size_t{1},
                   "a sub-CPU cgroup v2 quota should still allow one worker");
    expect_cpu_max("400000 100000\n", std::size_t{4},
                   "the trailing newline the kernel writes should not defeat "
                   "the parse");
}

TEST(test_malformed_cgroup_v2_cpu_max_bounds_nothing) {
    expect_cpu_max("", std::nullopt, "an empty cpu.max should bound nothing");
    expect_cpu_max("400000", std::nullopt,
                   "a cpu.max missing its period should bound nothing");
    expect_cpu_max("400000 0", std::nullopt,
                   "a zero period should bound nothing rather than divide by "
                   "zero");
    expect_cpu_max("four 100000", std::nullopt,
                   "a non-numeric quota should bound nothing");
    expect_cpu_max("400000 ten", std::nullopt,
                   "a non-numeric period should bound nothing");
    expect_cpu_max("-1 100000", std::nullopt,
                   "a negative quota is not cgroup v2 syntax and should bound "
                   "nothing");
}

void expect_cfs_quota(std::string_view quota, std::string_view period,
                      std::optional<std::size_t> expected,
                      const char* message) {
    expect(parse_cgroup_v1_cpu_quota(quota, period) == expected, message);
}

TEST(test_cgroup_v1_cpu_quota_parses) {
    expect_cfs_quota("-1", "100000", std::nullopt,
                     "a cgroup v1 quota of -1 is unlimited, so it bounds "
                     "nothing");
    expect_cfs_quota("400000", "100000", std::size_t{4},
                     "a cgroup v1 quota of four periods should allow four "
                     "workers");
    expect_cfs_quota("150000", "100000", std::size_t{2},
                     "a fractional cgroup v1 quota should round up");
    expect_cfs_quota("100000\n", "100000\n", std::size_t{1},
                     "the trailing newlines the kernel writes should not "
                     "defeat the parse");
    expect_cfs_quota("", "100000", std::nullopt,
                     "an empty quota file should bound nothing");
    expect_cfs_quota("400000", "", std::nullopt,
                     "an empty period file should bound nothing");
    expect_cfs_quota("400000", "0", std::nullopt,
                     "a zero period should bound nothing rather than divide by "
                     "zero");
}

// size() is only worth asserting on if it describes a pool that runs work, so
// hand it more tasks than it has workers and wait for every one.
TEST(test_every_submitted_task_runs) {
    ThreadPool& pool = global_thread_pool();
    const std::size_t n_tasks = (pool.size() * 4) + 1;

    std::vector<char> ran(n_tasks, 0);
    std::vector<std::future<void>> results;
    results.reserve(n_tasks);
    for (std::size_t i = 0; i < n_tasks; ++i) {
        results.push_back(pool.submit([&ran, i] { ran[i] = 1; }));
    }
    for (std::future<void>& result : results) {
        result.get();
    }

    for (std::size_t i = 0; i < n_tasks; ++i) {
        expect(ran[i] == 1,
               "every task submitted to the pool should have run by the time "
               "its future is ready");
    }
}

// One worker, held busy while both queues fill, so the order the queued tasks
// run in is the order the worker chose them in and nothing else.
TEST(test_foreground_tasks_run_before_queued_background_ones) {
    ThreadPool pool(1);
    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    std::future<void> held = pool.submit([gate] { gate.wait(); });

    std::mutex order_mutex;
    std::vector<std::string> order;
    const auto record = [&order_mutex, &order](std::string name) {
        return [&order_mutex, &order, name = std::move(name)] {
            const std::scoped_lock lock(order_mutex);
            order.push_back(name);
        };
    };
    std::vector<std::future<void>> results;
    results.push_back(pool.submit(record("b1"), TaskPriority::Background));
    results.push_back(pool.submit(record("b2"), TaskPriority::Background));
    results.push_back(pool.submit(record("f1")));
    results.push_back(pool.submit(record("f2"), TaskPriority::Foreground));
    release.set_value();
    held.get();
    for (std::future<void>& result : results) {
        result.get();
    }

    const std::vector<std::string> expected{"f1", "f2", "b1", "b2"};
    expect(order == expected,
           "priority: a queued foreground task should run before every queued "
           "background one, each queue keeping its own submission order");
}

TEST(test_background_region_collects_every_item) {
    constexpr std::size_t k_n_items = 32;
    std::vector<std::size_t> values(k_n_items, 0);
    run_bounded_async(
        k_n_items, dispatch_window(),
        [](std::size_t idx) { return [idx] { return idx + 1; }; },
        [&values](std::size_t idx, std::size_t value) { values[idx] = value; },
        {}, TaskPriority::Background);
    for (std::size_t idx = 0; idx < k_n_items; ++idx) {
        expect(values[idx] == idx + 1,
               "priority: a background region should run and collect every "
               "item under its own index");
    }
}

TEST(test_dispatch_window_is_twice_the_pool) {
    expect(dispatch_window() >= 1,
           "the dispatch window should always admit at least one task");
    expect(dispatch_window() == global_thread_pool().size() * 2,
           "the dispatch window should be twice the pool's worker count");
}

// Longest-processing-time-first ordering, and the property that makes it safe
// to apply anywhere: permuting the launch order changes which item goes out
// first and nothing else.

TEST(test_cost_ordered_indices_is_longest_first) {
    const std::vector<double> costs{1.0, 5.0, 3.0, 5.0, 0.0};
    const std::vector<std::size_t> order = cost_ordered_indices(
        costs.size(), [&costs](std::size_t idx) { return costs[idx]; });
    const std::vector<std::size_t> expected{1, 3, 2, 0, 4};
    expect(order == expected,
           "cost order: items should launch by descending cost, with equal "
           "costs keeping index order so the permutation is a function of the "
           "estimates alone");
}

TEST(test_cost_ordered_dispatch_pairs_results_with_their_own_index) {
    constexpr std::size_t k_n_items = 64;
    // Deliberately not monotone in the index, so index order and cost order
    // disagree and a result filed under the launch slot would be caught.
    const auto cost = [](std::size_t idx) {
        return static_cast<double>((idx * 7) % 11);
    };
    std::vector<std::size_t> times_seen(k_n_items, 0);
    std::vector<std::size_t> values(k_n_items, 0);
    run_bounded_async(
        k_n_items, dispatch_window(),
        [](std::size_t idx) { return [idx] { return idx * 3; }; },
        [&times_seen, &values](std::size_t idx, std::size_t value) {
            ++times_seen[idx];
            values[idx] = value;
        },
        cost_ordered_indices(k_n_items, cost));
    for (std::size_t idx = 0; idx < k_n_items; ++idx) {
        expect(times_seen[idx] == 1,
               "cost order: every item should be launched and collected "
               "exactly once whatever order it went out in");
        expect(values[idx] == idx * 3,
               "cost order: a result should be delivered under its own item "
               "index, not under the slot it was launched from");
    }
}

}  // namespace
