#include "driver_support.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "config_io.hpp"
#include "runner/black.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"
#include "version.hpp"

bool has_program_name(int argc, const char* const* argv) {
    if (argc == 0 || argv == nullptr || argv[0] == nullptr) {
        std::cerr << "fatal: missing argv[0]\n";
        return false;
    }
    return true;
}

bool has_flag(int argc, const char* const* argv, const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::string(argv[i]) == flag) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> parse_string_arg(int argc, const char* const* argv,
                                            const char* flag) {
    for (int i = 1; i < argc - 1; ++i) {
        if (argv[i] != nullptr && std::string(argv[i]) == flag) {
            if (argv[i + 1] != nullptr) {
                return std::string(argv[i + 1]);
            }
        }
    }
    return std::nullopt;
}

bool handle_info_flags(int argc, const char* const* argv,
                       void (*print_usage)(const char*)) {
    if (has_flag(argc, argv, "--version")) {
        version::print(std::cout);
        return true;
    }
    if (has_flag(argc, argv, "-h") || has_flag(argc, argv, "--help")) {
        print_usage(argv[0]);
        return true;
    }
    return false;
}

std::vector<std::string> collect_argument_paths(int argc,
                                                const char* const* argv) {
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr) {
            paths.emplace_back(argv[i]);
        }
    }
    return paths;
}

std::optional<std::string> read_file_contents(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

std::string read_file_or_throw(const std::string& path, const char* failure,
                               bool name_path) {
    std::optional<std::string> contents = read_file_contents(path);
    if (!contents.has_value()) {
        throw std::runtime_error(name_path ? std::string(failure) + ": " + path
                                           : std::string(failure));
    }
    return std::move(*contents);
}

std::optional<tlsf::Specification> load_tlsf_or_report(
    const std::string& path) {
    const std::optional<std::string> contents = read_file_contents(path);
    if (!contents.has_value()) {
        std::cerr << path << ": cannot read file\n";
        return std::nullopt;
    }
    try {
        return tlsf::parse(*contents);
    } catch (const std::exception& exc) {
        std::cerr << path << ": " << exc.what() << "\n";
        return std::nullopt;
    }
}

std::optional<std::string> find_unknown_arg(
    int argc, const char* const* argv,
    const std::vector<std::string>& value_flags,
    const std::vector<std::string>& bare_flags) {
    auto contains = [](const std::vector<std::string>& flags,
                       const std::string& arg) {
        return std::find(flags.begin(), flags.end(), arg) != flags.end();
    };
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        std::string arg(argv[i]);
        if (contains(value_flags, arg)) {
            // Skip the value, so a path or a seed that happens to look like a
            // flag is not itself reported as unknown.
            ++i;
            continue;
        }
        if (contains(bare_flags, arg)) {
            continue;
        }
        return arg;
    }
    return std::nullopt;
}

SatisfiabilityChecker& configure_offline_checkers(
    std::chrono::milliseconds black_timeout, bool whole_spec_queries) {
    // Set through apply_tool_timeouts rather than one at a time: compare used
    // to set only black's, which left ltlsynt (reachable through the TLSF
    // filters) unbounded, so a single hard query could spend the whole
    // per-invocation budget the experiment harness allows.
    Config cfg;
    cfg.black_timeout = black_timeout;
    cfg.ltlsynt_timeout = std::chrono::milliseconds{60'000};
    cfg.ltl2tgba_timeout = std::chrono::milliseconds{60'000};
    // ltlfilt's default is 10 s, and config.hpp justifies it by "an abandoned
    // call costs only a missed simplification, never an individual". That
    // premise holds inside a run and fails here. check_satisfiability answers
    // from a simplification of "0" or "1" before black is spawned, so on this
    // path the fold IS the verdict: losing it hands the query to a solver
    // black.cpp:155 records as unsound on W under negation, and the driver
    // prints whatever comes back as the relation. That is not hypothetical --
    // at 10 s, compare reported examples/amba as incomparable with itself
    // minus three GUARANTEES, with 0 timeouts.
    //
    // Sized off amba, the only subject big enough to cross the old default at
    // 44 requirements and 16 atomic propositions. Its four spec-against-ideal
    // queries need 80 s, 83 s, 90 s and 145 s; the two that fold to "0" decide
    // there, and black clears the two satisfiable ones in 0.02 s once the fold
    // has ruled out the cheap answer. 300 s leaves headroom over the 145 s
    // worst case. A pair can now cost minutes, which is affordable offline and
    // would not be inside a run.
    cfg.ltlfilt_timeout = std::chrono::milliseconds{300'000};
    apply_tool_timeouts(cfg);
    SatisfiabilityChecker& checker = global_sat_checker();
    if (whole_spec_queries) {
        // Both measured over maximal's own queries, whole-spec implications of
        // 1000-1600 characters. `ltlfilt --simplify` took 95.6% of solver wall
        // time (1153.7s against 52.9s for the decision itself) and a 40-file
        // batch went from 304s to 30s without it, with the same survivors;
        // without the pass the unsimplified query runs about 2x longer, so the
        // 500ms SPOT budget tuned for the search tips over under load and an
        // undecided `ExpectUnsat` query keeps both sides. Giving SPOT black's
        // budget instead restored agreement on 264 of 264 cut-values across a
        // 10-run sample. The FRETISH path takes the same two settings, which is
        // what its own final filters run under (src/repair/evolution.cpp). Its
        // queries were per requirement rather than whole-spec until ed5413f,
        // and measured at 40 generations of 1000 the simplify pass was 59-61%
        // of every ltlfilt exec a run made even at that shape; it now asks the
        // whole-spec query this paragraph measures. On compare the pass put 205
        // of 3000 runs past the harness's 600 s cap, 118 of 120 on
        // humanoid-742, and a timed-out compare reads as no ideal relation at
        // all.
        checker.set_simplify(false);
        checker.set_spot_budget(black_timeout);
    }
    return checker;
}

std::optional<std::size_t> parse_seed(const std::string& text) {
    // Checked before std::stoull rather than after, because stoull is happy to
    // stop at the first non-digit and to wrap a leading '-' round to the top of
    // the range; neither reports anything the caller could notice.
    const bool all_digits =
        !text.empty() &&
        std::all_of(text.begin(), text.end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        });
    if (!all_digits) {
        return std::nullopt;
    }
    try {
        const auto value = std::stoull(text);
        if constexpr (sizeof(value) > sizeof(std::size_t)) {
            if (value > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
        }
        return static_cast<std::size_t>(value);
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}
