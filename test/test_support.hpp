#pragma once

#include <chrono>
#include <stdexcept>
#include <string>

[[noreturn]] inline void fail(const std::string& message) {
    throw std::runtime_error(message);
}

inline void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

/// The SAT budget every test runs under. The production default is tuned tight
/// for real runs, and CI has been slow enough to make it flaky for tests that
/// expect a definite SAT/UNSAT answer rather than a timeout.
inline constexpr std::chrono::milliseconds k_test_black_timeout{10000};
