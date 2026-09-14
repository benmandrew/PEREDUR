#include "profile.hpp"

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace profile {

namespace {

// Never destroyed, and that is the point. A worker thread can still be inside a
// Scope destructor when static destruction starts, so the registry and the
// Sites it owns both have to outlive it; a Site destroyed under a live scope
// would be a use-after-free in the one build (release, long campaign run) where
// the profiler is least welcome to crash. Holding them behind a pointer that
// lives to exit keeps them reachable as well as alive, which is the difference
// between an intentional allocation and one LeakSanitizer reports: destroying
// the vector would free its buffer and orphan every Site just before the leak
// check runs.
std::vector<Site*>& site_registry() {
    static auto* registry = new std::vector<Site*>();
    return *registry;
}

std::mutex& registry_mutex() {
    static std::mutex mutex;
    return mutex;
}

// Leaked for the same reason as site_registry above, and not optionally so. The
// report is registered with atexit on the first scope, so a registry
// constructed after that point is destroyed before the report reads it. Sites
// would survive that anyway because they are already leaked; these keys are
// std::strings, and a destroyed map has the report printing their freed buffers
// -- names short enough for the small-string optimisation come out as the
// pointers written over their storage, longer ones as a prefix of whatever
// replaced them. Silent, and only in the JSON's counters block.
std::map<std::string, std::uint64_t>& counter_registry() {
    static auto* counters = new std::map<std::string, std::uint64_t>();
    return *counters;
}

// PEREDUR_PROFILE's value, or nullptr when unset. Captured once: a run that
// re-read the environment per scope would let the answer change mid-run.
const char* profile_target() {
    static const char* target = std::getenv("PEREDUR_PROFILE");
    return target;
}

std::string format_ns(std::uint64_t nanos) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << (static_cast<double>(nanos) / 1e9) << "s";
    return out.str();
}

}  // namespace

bool enabled() {
    // Registering the report here rather than asking each main() to call it
    // means every binary -- compare, realize, mucs, ltl as well as peredur --
    // reports without further wiring. Reading the sites at exit is safe because
    // they are deliberately leaked, so nothing has destroyed them by then, and
    // report_if_enabled is idempotent, so PEREDUR's own explicit call at the
    // end of its timing report still prints exactly once.
    static const bool is_enabled = [] {
        const bool requested = profile_target() != nullptr;
        if (requested) {
            std::atexit([] { report_if_enabled(); });
        }
        return requested;
    }();
    return is_enabled;
}

std::uint64_t wall_ns() {
    struct timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ULL) +
           static_cast<std::uint64_t>(now.tv_nsec);
}

#ifdef __APPLE__
// macOS has no clock id for per-thread CPU, and the process-wide clock is not
// a substitute: the whole diagnostic this profiler exists for is that a scope
// with large wall and near-zero CPU is blocked on a child rather than
// computing, and that reading is only true of a per-thread clock. thread_info
// reports the same user and system time the Linux clock id does.
//
// mach_thread_self returns a send right the caller owns, so calling it per
// scope and dropping the reference would leak a port per scope. Held for the
// life of the thread instead, and released with it.
class ThreadPort {
   public:
    ThreadPort() : m_port(mach_thread_self()) {}
    ~ThreadPort() { mach_port_deallocate(mach_task_self(), m_port); }
    ThreadPort(const ThreadPort&) = delete;
    ThreadPort& operator=(const ThreadPort&) = delete;
    ThreadPort(ThreadPort&&) = delete;
    ThreadPort& operator=(ThreadPort&&) = delete;
    mach_port_t get() const { return m_port; }

   private:
    mach_port_t m_port;
};

std::uint64_t thread_cpu_ns() {
    static thread_local ThreadPort port;
    thread_basic_info_data_t info{};
    mach_msg_type_number_t info_count = THREAD_BASIC_INFO_COUNT;
    const kern_return_t status =
        thread_info(port.get(), THREAD_BASIC_INFO,
                    reinterpret_cast<thread_info_t>(&info), &info_count);
    if (status != KERN_SUCCESS) {
        return 0;
    }
    const auto to_ns = [](const time_value_t& value) {
        return (static_cast<std::uint64_t>(value.seconds) * 1'000'000'000ULL) +
               (static_cast<std::uint64_t>(value.microseconds) * 1'000ULL);
    };
    return to_ns(info.user_time) + to_ns(info.system_time);
}
#else
std::uint64_t thread_cpu_ns() {
    struct timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return (static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ULL) +
           static_cast<std::uint64_t>(now.tv_nsec);
}
#endif

Site& site(const char* name) {
    const std::scoped_lock lock(registry_mutex());
    for (Site* existing : site_registry()) {
        if (std::strcmp(existing->m_name, name) == 0) {
            return *existing;
        }
    }
    auto* fresh = new Site(name);
    site_registry().push_back(fresh);
    return *fresh;
}

Site& site_interned(const std::string& name) {
    // Interned in a deque, not a vector: Site holds a bare pointer into these
    // strings, and a vector would invalidate every one of them when it grew.
    // Never destroyed, for the same reason as the registry above -- the names
    // it holds are what every Site's m_name points at.
    static auto& interned = *new std::deque<std::string>();
    const char* stable = nullptr;
    {
        const std::scoped_lock lock(registry_mutex());
        for (Site* existing : site_registry()) {
            if (name == existing->m_name) {
                return *existing;
            }
        }
        interned.push_back(name);
        // Taken here rather than after the lock: reading back() outside it
        // would race with another thread's push_back, and would hand this
        // caller whichever name that thread had just appended. The pointer
        // itself stays good once taken, since a deque never moves an element
        // it already holds.
        stable = interned.back().c_str();
    }
    return site(stable);
}

void add_count(const char* name, std::uint64_t n) {
    if (!enabled()) {
        return;
    }
    const std::scoped_lock lock(registry_mutex());
    counter_registry()[name] += n;
}

void record_max(const std::string& name, std::uint64_t value) {
    if (!enabled()) {
        return;
    }
    const std::scoped_lock lock(registry_mutex());
    std::uint64_t& slot = counter_registry()[name];
    slot = std::max(slot, value);
}

const std::vector<Site*>& sites() { return site_registry(); }

std::vector<std::pair<std::string, std::uint64_t>> counts() {
    const std::scoped_lock lock(registry_mutex());
    return {counter_registry().begin(), counter_registry().end()};
}

void report(std::ostream& out) {
    std::vector<Site*> ordered;
    {
        const std::scoped_lock lock(registry_mutex());
        ordered = site_registry();
    }
    ordered.erase(std::remove_if(ordered.begin(), ordered.end(),
                                 [](const Site* entry) {
                                     return entry->m_calls.load() == 0;
                                 }),
                  ordered.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const Site* lhs, const Site* rhs) {
                  return lhs->m_wall_ns.load() > rhs->m_wall_ns.load();
              });

    out << "\nScope profile (PEREDUR_PROFILE):\n";
    out << std::left << std::setw(34) << "site" << std::right << std::setw(10)
        << "calls" << std::setw(12) << "wall" << std::setw(12) << "cpu"
        << std::setw(9) << "cpu/wall" << std::setw(12) << "wall/call"
        << std::setw(12) << "max call" << "\n";
    for (const Site* entry : ordered) {
        const std::uint64_t calls = entry->m_calls.load();
        const std::uint64_t wall = entry->m_wall_ns.load();
        const std::uint64_t cpu = entry->m_cpu_ns.load();
        std::ostringstream ratio;
        ratio << std::fixed << std::setprecision(2)
              << (wall == 0
                      ? 0.0
                      : static_cast<double>(cpu) / static_cast<double>(wall));
        std::ostringstream per_call;
        per_call << std::fixed << std::setprecision(3)
                 << (static_cast<double>(wall) / static_cast<double>(calls) /
                     1e6)
                 << "ms";
        out << std::left << std::setw(34) << entry->m_name << std::right
            << std::setw(10) << calls << std::setw(12) << format_ns(wall)
            << std::setw(12) << format_ns(cpu) << std::setw(9) << ratio.str()
            << std::setw(12) << per_call.str() << std::setw(12)
            << format_ns(entry->m_max_wall_ns.load()) << "\n";
    }

    const auto counters = counts();
    if (!counters.empty()) {
        out << "\nScope counters:\n";
        for (const auto& [name, value] : counters) {
            out << std::left << std::setw(34) << name << std::right
                << std::setw(14) << value << "\n";
        }
    }
    out << std::flush;
}

void report_json(const std::string& path) {
    std::ofstream file(path);
    if (!file) {
        std::cerr << "profile: cannot write " << path << "\n";
        return;
    }
    std::vector<Site*> ordered;
    {
        const std::scoped_lock lock(registry_mutex());
        ordered = site_registry();
    }
    file << R"({)"
            "\n"
            R"(  "sites": [)"
            "\n";
    bool first = true;
    for (const Site* entry : ordered) {
        if (entry->m_calls.load() == 0) {
            continue;
        }
        if (!first) {
            file << ",\n";
        }
        first = false;
        file << R"(    {"name": ")" << entry->m_name << R"(", "calls": )"
             << entry->m_calls.load() << R"(, "wall_ns": )"
             << entry->m_wall_ns.load() << R"(, "cpu_ns": )"
             << entry->m_cpu_ns.load() << R"(, "max_wall_ns": )"
             << entry->m_max_wall_ns.load() << "}";
    }
    file << "\n"
            R"(  ],)"
            "\n"
            R"(  "counters": {)"
            "\n";
    first = true;
    for (const auto& [name, value] : counts()) {
        if (!first) {
            file << ",\n";
        }
        first = false;
        file << R"(    ")" << name << R"(": )" << value;
    }
    file << "\n  }\n}\n";
}

void report_if_enabled() {
    if (!enabled()) {
        return;
    }
    static std::once_flag once;
    std::call_once(once, [] {
        report(std::cerr);
        const char* target = profile_target();
        // "1" and "-" ask for the stderr table only; anything else is taken as
        // a path for the machine-readable copy.
        if (target != nullptr && std::strcmp(target, "1") != 0 &&
            std::strcmp(target, "-") != 0) {
            report_json(target);
        }
    });
}

}  // namespace profile
