// Tests over the argument helpers the CLI drivers share.
//
// parse_seed carries most of the weight here: --seed is what makes a run
// reproducible, so the interesting cases are the ones a laxer parser would
// accept and quietly turn into a seed the caller never asked for.

#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>

#include "driver_support.hpp"
#include "test_suite.hpp"
#include "test_support.hpp"

namespace {

void expect_seed(const std::string& text, std::size_t expected) {
    const std::optional<std::size_t> parsed = parse_seed(text);
    // fail() is [[noreturn]], so the dereferences below are guarded in a way
    // clang-tidy can follow; it cannot see through a call to expect().
    if (!parsed.has_value()) {
        fail("expected '" + text + "' to parse as a seed");
    }
    expect(*parsed == expected, "expected '" + text + "' to parse as " +
                                    std::to_string(expected) + ", got " +
                                    std::to_string(*parsed));
}

void expect_rejected(const std::string& text) {
    expect(!parse_seed(text).has_value(),
           "expected '" + text + "' to be rejected as a seed");
}

void test_parse_seed_accepts_decimal_digits() {
    expect_seed("0", 0);
    expect_seed("1", 1);
    expect_seed("42", 42);
    // Leading zeros are still an unambiguous decimal spelling, not octal.
    expect_seed("007", 7);
    const std::size_t max_seed = std::numeric_limits<std::size_t>::max();
    expect_seed(std::to_string(max_seed), max_seed);
}

// The regression this suite exists for: each of these used to reach
// std::stoull unguarded, which either aborted the process through an uncaught
// exception or returned a number bearing no relation to what was typed.
void test_parse_seed_rejects_malformed_values() {
    expect_rejected("");
    expect_rejected("abc");
    // stoull stops at the first non-digit and would report 12.
    expect_rejected("12abc");
    // stoull wraps a negative round to the top of the range.
    expect_rejected("-1");
    expect_rejected("+1");
    expect_rejected(" 12");
    expect_rejected("12 ");
    expect_rejected("1.5");
    expect_rejected("0x10");
    // Past the top of the range, where stoull throws out_of_range.
    expect_rejected("99999999999999999999999999");
}

void test_has_flag_matches_whole_arguments() {
    const std::array<const char* const, 4> argv = {"peredur", "--dashboard",
                                                   "--seed", "7"};
    const int argc = static_cast<int>(argv.size());
    expect(has_flag(argc, argv.data(), "--dashboard"),
           "expected --dashboard found");
    expect(!has_flag(argc, argv.data(), "--dash"),
           "expected a prefix of a flag not to match");
    expect(!has_flag(argc, argv.data(), "peredur"),
           "expected argv[0] to be skipped");
}

void test_parse_string_arg_reads_the_following_argument() {
    const std::array<const char* const, 4> argv = {"peredur", "--seed", "7",
                                                   "--trailing"};
    const int argc = static_cast<int>(argv.size());
    const std::optional<std::string> seed =
        parse_string_arg(argc, argv.data(), "--seed");
    expect(seed.has_value() && *seed == "7", "expected --seed to read 7");
    // A flag in the final position has no value after it.
    expect(!parse_string_arg(argc, argv.data(), "--trailing").has_value(),
           "expected a trailing flag to yield no value");
    expect(!parse_string_arg(argc, argv.data(), "--absent").has_value(),
           "expected an absent flag to yield no value");
}

}  // namespace

void run_driver_support_tests() {
    test_parse_seed_accepts_decimal_digits();
    test_parse_seed_rejects_malformed_values();
    test_has_flag_matches_whole_arguments();
    test_parse_string_arg_reads_the_following_argument();
}
