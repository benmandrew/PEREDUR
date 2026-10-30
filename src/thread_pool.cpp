#include "thread_pool.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "cpu_limits.hpp"

#ifdef __linux__
#include <sched.h>
#endif

ThreadPool::ThreadPool(std::size_t n_workers) {
    m_workers.reserve(n_workers);
    for (std::size_t i = 0; i < n_workers; ++i) {
        m_workers.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::scoped_lock lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for (std::thread& worker : m_workers) {
        worker.join();
    }
}

void ThreadPool::worker_loop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(m_mutex);
            ++m_waiting;
            m_cv.wait(lock, [this] {
                return m_stop || !m_tasks.empty() || !m_background.empty();
            });
            --m_waiting;
            std::queue<std::function<void()>>& queue =
                m_tasks.empty() ? m_background : m_tasks;
            if (queue.empty()) {
                if (m_stop) {
                    return;
                }
                continue;
            }
            task = std::move(queue.front());
            queue.pop();
        }
        task();
    }
}

std::size_t ThreadPool::idle_workers() {
    const std::scoped_lock lock(m_mutex);
    const std::size_t queued = m_tasks.size() + m_background.size();
    return m_waiting > queued ? m_waiting - queued : 0;
}

namespace {

// 0 means "use available_parallelism()", which is what a run that never calls
// set_thread_pool_size gets. Read once, when the pool below is first touched.
std::atomic<std::size_t> g_pool_size{0};

std::string_view trim(std::string_view text) {
    const std::size_t begin = text.find_first_not_of(" \t\n\r");
    if (begin == std::string_view::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(" \t\n\r") - begin + 1);
}

// Digits only, so a negative quota, a keyword and a truncated read all read as
// "no bound" rather than as some number.
std::optional<std::uint64_t> parse_digits(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    constexpr std::uint64_t k_max = std::numeric_limits<std::uint64_t>::max();
    for (const char digit_char : text) {
        if (digit_char < '0' || digit_char > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint64_t>(digit_char - '0');
        if (value > (k_max - digit) / 10) {
            return std::nullopt;
        }
        value = (value * 10) + digit;
    }
    return value;
}

// Both cgroup versions express the bound as a share of a period, and both round
// up: a 1.5-CPU quota is served better by two workers each idle half the time
// than by one that cannot use the surplus half at all.
std::optional<std::size_t> quota_to_workers(std::string_view quota,
                                            std::string_view period) {
    const std::optional<std::uint64_t> quota_us = parse_digits(trim(quota));
    const std::optional<std::uint64_t> period_us = parse_digits(trim(period));
    if (!quota_us.has_value() || !period_us.has_value() || *period_us == 0) {
        return std::nullopt;
    }
    // Divide-then-adjust rather than the (a + b - 1) / b idiom, which would
    // wrap on a quota near the type's maximum and round a nonsense file down to
    // a small worker count instead of a large one.
    const std::uint64_t whole = *quota_us / *period_us;
    const std::uint64_t workers = whole + (*quota_us % *period_us == 0 ? 0 : 1);
    constexpr std::uint64_t k_size_max =
        std::numeric_limits<std::size_t>::max();
    if (workers >= k_size_max) {
        return std::numeric_limits<std::size_t>::max();
    }
    return std::max<std::size_t>(1, static_cast<std::size_t>(workers));
}

#ifdef __linux__

std::optional<std::string> read_whole_file(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) {
        return std::nullopt;
    }
    return contents.str();
}

// Docker with cgroup v2 mounts the container's own cgroup at /sys/fs/cgroup, so
// reading these paths directly is right for the case this exists for. A process
// in a nested or delegated cgroup can read a parent's limit rather than its
// own; resolving that means walking /proc/self/cgroup, which is not worth it to
// size a thread pool.
std::optional<std::size_t> cgroup_cpu_limit() {
    if (const std::optional<std::string> cpu_max =
            read_whole_file("/sys/fs/cgroup/cpu.max")) {
        // v2 present means v1 is not mounted, so an unlimited quota here is the
        // answer rather than a reason to look further.
        return parse_cgroup_v2_cpu_max(*cpu_max);
    }
    const std::optional<std::string> quota =
        read_whole_file("/sys/fs/cgroup/cpu/cpu.cfs_quota_us");
    const std::optional<std::string> period =
        read_whole_file("/sys/fs/cgroup/cpu/cpu.cfs_period_us");
    if (!quota.has_value() || !period.has_value()) {
        return std::nullopt;
    }
    return parse_cgroup_v1_cpu_quota(*quota, *period);
}

std::optional<std::size_t> affinity_limit() {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        return std::nullopt;
    }
    const int n_allowed = CPU_COUNT(&allowed);
    if (n_allowed <= 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(n_allowed);
}

#endif  // __linux__

}  // namespace

std::optional<std::size_t> parse_cgroup_v2_cpu_max(std::string_view content) {
    const std::string_view fields = trim(content);
    const std::size_t split = fields.find_first_of(" \t");
    if (split == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view quota = fields.substr(0, split);
    if (quota == "max") {
        return std::nullopt;
    }
    return quota_to_workers(quota, fields.substr(split + 1));
}

std::optional<std::size_t> parse_cgroup_v1_cpu_quota(std::string_view quota,
                                                     std::string_view period) {
    return quota_to_workers(quota, period);
}

std::size_t available_parallelism() {
    std::optional<std::size_t> limit;
    const auto tighten = [&limit](std::optional<std::size_t> bound) {
        if (bound.has_value()) {
            limit = limit.has_value() ? std::min(*limit, *bound) : *bound;
        }
    };

    const unsigned int hw_threads = std::thread::hardware_concurrency();
    if (hw_threads > 0) {
        tighten(static_cast<std::size_t>(hw_threads));
    }
#ifdef __linux__
    // hardware_concurrency() reports the host's online CPUs on Linux, so it
    // sees neither of these: a container given --cpus=4 on a 64-core box would
    // otherwise run 64 workers, each of which also spawns solver subprocesses.
    tighten(affinity_limit());
    tighten(cgroup_cpu_limit());
#endif
    return std::max<std::size_t>(1, limit.value_or(1));
}

void set_thread_pool_size(std::size_t size) { g_pool_size.store(size); }

ThreadPool& global_thread_pool() {
    static ThreadPool pool([] {
        const std::size_t requested = g_pool_size.load();
        if (requested > 0) {
            return requested;
        }
        return available_parallelism();
    }());
    return pool;
}

std::size_t dispatch_window() {
    return std::max<std::size_t>(1, global_thread_pool().size() * 2);
}
