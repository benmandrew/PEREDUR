// Tests over the scope profiler: that a Site records what a Scope measured,
// that repeated names resolve to one Site rather than accumulating duplicates,
// and that an interned name stays readable once the registry has grown past it.
//
// The interning test is the one worth having. Site holds a bare const char*
// into the interned string, so backing the intern table with a vector rather
// than a deque would leave every earlier Site pointing at freed memory the
// first time it reallocated -- and the report only reads those pointers at
// process exit, long after the corruption, where it would surface as garbage
// site names rather than as a crash anyone could trace back here.

#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "profile.hpp"
#include "runner/process.hpp"
#include "test_suite.hpp"
#include "test_support.hpp"

namespace {

void test_scope_records_a_call() {
    profile::Site& site = profile::site_interned("test/scope-records");
    const std::uint64_t before = site.m_calls.load();
    {
        const profile::Scope scope(site);
    }
    // Accumulation is conditional on the profiler being enabled, which is an
    // environment variable this suite deliberately does not set: assert the
    // two consistent outcomes rather than forcing one.
    const std::uint64_t after = site.m_calls.load();
    if (profile::enabled()) {
        expect(after == before + 1, "an executed scope records one call");
    } else {
        expect(after == before, "a disabled scope records nothing");
    }
}

void test_repeated_names_share_one_site() {
    // Named rather than passed as a literal: site_interned takes a
    // std::string and returns a reference, so a temporary argument makes gcc's
    // -Wdangling-reference suspect the result points into it. It does not --
    // the Site outlives the process on purpose -- and taking the address
    // rather than binding a reference says so without needing the warning
    // suppressed. The literals here are the point of the test, so they stay:
    // the third lookup below uses a distinct string object, and all three must
    // land on one Site for interning to be by value rather than by pointer.
    const profile::Site* first = &profile::site_interned("test/shared-name");
    const profile::Site* second = &profile::site_interned("test/shared-name");
    expect(first == second, "the same name resolves to the same Site");

    const std::string name = "test/shared-name";
    const profile::Site* third = &profile::site_interned(name);
    expect(first == third,
           "a name from a different string object still resolves to one Site");
}

// Interns enough distinct names to force the intern table to grow several
// times, then re-reads the name of the very first one.
void test_interned_names_survive_registry_growth() {
    std::vector<profile::Site*> sites;
    constexpr int k_count = 200;
    sites.reserve(k_count);
    for (int i = 0; i < k_count; ++i) {
        sites.push_back(
            &profile::site_interned("test/growth-" + std::to_string(i)));
    }
    for (int i = 0; i < k_count; ++i) {
        const std::string expected = "test/growth-" + std::to_string(i);
        expect(std::string(sites[i]->m_name) == expected,
               "an interned site name is still readable after the registry "
               "grew past it");
    }
}

void test_wall_and_cpu_are_measured_separately() {
    // A sleeping thread accrues wall time but almost no CPU time, which is the
    // distinction the whole profiler exists to draw. Only checkable when the
    // profiler is on, since otherwise nothing is recorded at all.
    if (!profile::enabled()) {
        return;
    }
    profile::Site& site = profile::site_interned("test/sleep");
    {
        const profile::Scope scope(site);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    expect(site.m_wall_ns.load() > 10'000'000ULL,
           "a 20ms sleep records at least 10ms of wall time");
    expect(site.m_cpu_ns.load() < site.m_wall_ns.load(),
           "a sleeping scope records less CPU time than wall time");
}

void test_record_max_keeps_the_largest() {
    // The counter exists for peak resident sets, where summing a thousand
    // calls' peaks would describe nothing that ever happened. Both orders are
    // exercised because a naive implementation that assigns rather than
    // compares passes the ascending one.
    if (!profile::enabled()) {
        return;
    }
    profile::record_max("test/max-ascending", 10);
    profile::record_max("test/max-ascending", 250);
    profile::record_max("test/max-descending", 250);
    profile::record_max("test/max-descending", 10);
    std::uint64_t ascending = 0;
    std::uint64_t descending = 0;
    for (const auto& [name, value] : profile::counts()) {
        if (name == "test/max-ascending") {
            ascending = value;
        } else if (name == "test/max-descending") {
            descending = value;
        }
    }
    expect(ascending == 250, "a rising sequence records its largest value");
    expect(descending == 250,
           "a falling sequence keeps the largest rather than the last, got " +
               std::to_string(descending));
}

void test_record_max_is_silent_when_disabled() {
    if (profile::enabled()) {
        return;
    }
    profile::record_max("test/max-disabled", 99);
    const auto counters = profile::counts();
    const bool present = std::any_of(
        counters.begin(), counters.end(),
        [](const auto& entry) { return entry.first == "test/max-disabled"; });
    expect(!present, "a disabled profiler registers no counter at all");
}

void test_clocks_advance_monotonically() {
    const std::uint64_t wall_before = profile::wall_ns();
    const std::uint64_t cpu_before = profile::thread_cpu_ns();
    std::uint64_t sink = 0;
    for (std::uint64_t i = 0; i < 2'000'000; ++i) {
        sink += i;
    }
    expect(sink > 0, "the busy loop was not optimised away");
    expect(profile::wall_ns() >= wall_before, "wall_ns does not go backwards");
    expect(profile::thread_cpu_ns() > cpu_before,
           "thread_cpu_ns advances across a busy loop");
}

// The above says the clocks move. It does not say thread_cpu_ns measures *this
// thread*, which is the profiler's whole diagnostic: a scope with large wall
// and near-zero CPU is blocked rather than working, and that reading is only
// true of a per-thread clock. Swapping CLOCK_THREAD_CPUTIME_ID for the
// process-wide clock would keep every other assertion in this file passing.
//
// So burn CPU on another thread while this one sleeps. A per-thread clock
// barely moves; a process-wide one picks up the whole burn. The margins are
// wide -- the burn is far longer than the amount allowed to leak through, and
// the sleep far longer than the wall time required -- so this does not depend
// on the two threads being scheduled in any particular way.
void test_thread_cpu_excludes_other_threads() {
    std::atomic<bool> stop{false};
    std::thread burner([&stop] {
        std::uint64_t sink = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            for (std::uint64_t i = 0; i < 100'000; ++i) {
                sink += i;
            }
        }
        expect(sink > 0, "the burner loop was not optimised away");
    });

    const std::uint64_t wall_before = profile::wall_ns();
    const std::uint64_t cpu_before = profile::thread_cpu_ns();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const std::uint64_t wall_ns = profile::wall_ns() - wall_before;
    const std::uint64_t cpu_ns = profile::thread_cpu_ns() - cpu_before;

    stop.store(true, std::memory_order_relaxed);
    burner.join();

    expect(wall_ns > 100'000'000,
           "the sleeping thread should still see wall time pass");
    expect(cpu_ns < 50'000'000,
           "thread_cpu_ns must not pick up another thread's CPU: a sleeping "
           "thread burns almost none of its own, whatever the process does");
}

// The running test binary's own path, for the re-exec below. /proc/self/exe is
// the Linux spelling and there is no procfs on macOS; _NSGetExecutablePath is
// the direct equivalent, and reports the buffer size it wants when the one it
// is given is too small.
std::string this_executable() {
#ifdef __APPLE__
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        return {};
    }
    path.resize(std::strlen(path.c_str()));
    return path;
#else
    return "/proc/self/exe";
#endif
}

void test_a_counter_name_survives_the_exit_report() {
    // Same defect as the interning one above, one registry over: the report is
    // registered with atexit on the first scope, so everything it reads has to
    // outlive static destruction. Sites are leaked for that reason and so is
    // the counter registry; without that, the report reads its keys' freed
    // buffers -- names short enough for the small-string optimisation come out
    // as the pointers written over them, longer ones as a prefix of whatever
    // replaced them.
    //
    // Nothing in this process can see that. The report it would have to read
    // is not written until after every test has finished, which is why this is
    // a second process reading the first's. The child skips the spawn by
    // finding PEREDUR_PROFILE already set, so it recurses exactly once.
    const bool is_the_child = std::getenv("PEREDUR_PROFILE") != nullptr;
    profile::add_count("test/counter-name-survives");
    if (is_the_child) {
        return;
    }

    const std::filesystem::path report =
        std::filesystem::temp_directory_path() /
        ("peredur-profile-" + std::to_string(getpid()) + ".json");
    // The parent latched PEREDUR_PROFILE as unset when the first scope above
    // ran, so setting it here reaches the child and cannot turn profiling on
    // in this process -- which would have it write over the file being read.
    setenv("PEREDUR_PROFILE", report.c_str(), 1);
    const ProcessResult child = execute_and_capture(
        {this_executable(), "profile"}, std::chrono::seconds(300));
    unsetenv("PEREDUR_PROFILE");
    expect(child.m_exit_code == 0,
           "profile: the child test run should pass, or the report below says "
           "nothing about the registry");
    std::ifstream file(report);
    const std::string json((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    std::filesystem::remove(report);
    expect(json.find(R"("test/counter-name-survives": 1)") != std::string::npos,
           "profile: the exit report should name the counter that was added, "
           "not the freed bytes of its key");
}

}  // namespace

void run_profile_tests() {
    test_scope_records_a_call();
    test_repeated_names_share_one_site();
    test_interned_names_survive_registry_growth();
    test_wall_and_cpu_are_measured_separately();
    test_record_max_keeps_the_largest();
    test_record_max_is_silent_when_disabled();
    test_clocks_advance_monotonically();
    test_thread_cpu_excludes_other_threads();
    // Last, so the scopes above have already latched PEREDUR_PROFILE as unset.
    test_a_counter_name_survives_the_exit_report();
}
