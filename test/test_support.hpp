#pragma once

#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

[[noreturn]] inline void fail(const std::string& message) {
    throw std::runtime_error(message);
}

inline void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

/// Fails with `message` unless `action` throws an `Exception`; any other
/// exception propagates. When `needle` is non-empty the error's `what()` must
/// also contain it. Returns `what()` for any further checks.
template <typename Exception = std::exception, typename Action>
std::string expect_throws(Action&& action, const std::string& message,
                          std::string_view needle = {}) {
    try {
        std::forward<Action>(action)();
    } catch (const Exception& error) {
        std::string what = error.what();
        expect(what.find(needle) != std::string::npos,
               message + ": the error should mention \"" + std::string(needle) +
                   "\", got \"" + what + "\"");
        return what;
    }
    fail(message);
}

/// The first seed below `n_seeds` for which `attempt(seed)` returns true, for
/// tests that try seeds until an outcome shows up.
template <typename Attempt>
std::optional<std::size_t> first_seed(std::size_t n_seeds, Attempt&& attempt) {
    for (std::size_t seed = 0; seed < n_seeds; ++seed) {
        if (attempt(seed)) {
            return seed;
        }
    }
    return std::nullopt;
}

/// Fails with `message` unless some seed below `n_seeds` makes `attempt`
/// return true.
template <typename Attempt>
void expect_some_seed(std::size_t n_seeds, Attempt&& attempt,
                      const std::string& message) {
    expect(first_seed(n_seeds, std::forward<Attempt>(attempt)).has_value(),
           message);
}

/// A fresh directory unique to `name` and to this process, removed on scope
/// exit so a failing test cannot leave the next run reading stale files.
class TempDir {
   public:
    explicit TempDir(const std::string& name)
        : m_path(std::filesystem::temp_directory_path() /
                 ("peredur_" + name + "_" + std::to_string(getpid()))) {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }
    [[nodiscard]] std::string string() const { return m_path.string(); }

   private:
    std::filesystem::path m_path;
};

/// Writes `contents` to `path`, creating its parent directories, and returns
/// `path`.
inline std::filesystem::path write_text(const std::filesystem::path& path,
                                        const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    expect(file.good(), "could not open " + path.string() + " for writing");
    file << contents;
    return path;
}

/// The SAT budget every test runs under. The production default is tuned tight
/// for real runs, and CI has been slow enough to make it flaky for tests that
/// expect a definite SAT/UNSAT answer rather than a timeout.
inline constexpr std::chrono::milliseconds k_test_black_timeout{10000};
