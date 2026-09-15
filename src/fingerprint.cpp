#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "driver_support.hpp"
#include "fingerprint/lasso.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace {

struct Args {
    std::string signals_path;
    std::size_t n_words = 64;
    std::uint64_t seed = 0;
    std::size_t max_prefix = 2;
    std::size_t max_cycle = 3;
    std::vector<std::string> paths;
};

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog
        << " --signals <spec.tlsf> [options] <file-or-dir>...\n"
        << "\n"
        << "Prints one tab-separated row per input .tlsf: the file's name\n"
        << "and its behavioural fingerprint, a hex bit per sampled word set\n"
        << "where the specification's lowering holds at the word's first\n"
        << "position. Two fingerprints are comparable only when they were\n"
        << "drawn from the same --signals, --words, --seed, --max-prefix and\n"
        << "--max-cycle; the Hamming distance between them then estimates\n"
        << "the measure of the two specifications' symmetric difference.\n"
        << "\n"
        << "  --signals <spec.tlsf>  Specification the words are drawn over.\n"
        << "                         Required, and normally the family's\n"
        << "                         original: a candidate's own signal list\n"
        << "                         would let two candidates be scored on\n"
        << "                         different words.\n"
        << "  --words <n>            Words to sample (default 64).\n"
        << "  --seed <n>             Word-sampling seed (default 0).\n"
        << "  --max-prefix <n>       Longest lasso stem (default 2).\n"
        << "  --max-cycle <n>        Longest lasso loop (default 3).\n"
        << "  --version              Print the git commit this binary was\n"
        << "                         built from.\n";
}

/// Reads the value belonging to @p flag into @p args, or reports why not.
///
/// Split out of parse_args because the four counted flags differ only in
/// which member they land in, and folding them into the loop put that
/// function over the cognitive-complexity bound the lint target enforces.
bool assign_count(Args& args, const std::string& flag, const char* text) {
    const std::optional<std::size_t> value = parse_seed(text);
    if (!value.has_value()) {
        std::cerr << flag << " expects a non-negative integer\n";
        return false;
    }
    if (flag == "--words") {
        args.n_words = *value;
    } else if (flag == "--seed") {
        args.seed = *value;
    } else if (flag == "--max-prefix") {
        args.max_prefix = *value;
    } else {
        args.max_cycle = *value;
    }
    return true;
}

/// The bounds a word set has to satisfy before any file is read.
bool check_args(const Args& args) {
    if (args.n_words == 0) {
        std::cerr << "--words expects a positive integer\n";
        return false;
    }
    if (args.max_cycle == 0) {
        std::cerr << "--max-cycle expects a positive integer\n";
        return false;
    }
    if (args.max_prefix + args.max_cycle > fingerprint::k_max_positions) {
        std::cerr << "--max-prefix plus --max-cycle must not exceed "
                  << fingerprint::k_max_positions << "\n";
        return false;
    }
    return true;
}

std::optional<Args> parse_args(int argc, const char* const* argv) {
    static const std::vector<std::string> k_counted{
        "--words", "--seed", "--max-prefix", "--max-cycle"};
    Args args;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        const std::string arg(argv[i]);
        const bool counted = std::find(k_counted.begin(), k_counted.end(),
                                       arg) != k_counted.end();
        if (arg == "--signals" || counted) {
            if (i + 1 >= argc || argv[i + 1] == nullptr) {
                std::cerr << arg << " expects a value\n";
                return std::nullopt;
            }
            const char* const text = argv[++i];
            if (!counted) {
                args.signals_path = text;
            } else if (!assign_count(args, arg, text)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg.rfind("--", 0) == 0) {
            std::cerr << "unknown argument: " << arg << "\n";
            return std::nullopt;
        }
        args.paths.push_back(arg);
    }
    if (args.signals_path.empty() || args.paths.empty()) {
        return std::nullopt;
    }
    return check_args(args) ? std::optional<Args>(args) : std::nullopt;
}

std::vector<std::string> expand_paths(const std::vector<std::string>& paths) {
    std::vector<std::string> files;
    for (const std::string& path : paths) {
        if (std::filesystem::is_directory(path)) {
            for (const auto& entry :
                 std::filesystem::directory_iterator(path)) {
                if (entry.path().extension() == ".tlsf") {
                    files.push_back(entry.path().string());
                }
            }
        } else {
            files.push_back(path);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::optional<tlsf::Specification> read_spec(const std::string& path) {
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

}  // namespace

int main(int argc, const char* const argv[]) {
    if (argc == 0 || argv == nullptr || argv[0] == nullptr) {
        std::cerr << "fatal: missing argv[0]\n";
        return 1;
    }
    if (handle_info_flags(argc, argv, print_usage)) {
        return 0;
    }
    const std::optional<Args> maybe_args = parse_args(argc, argv);
    if (!maybe_args.has_value()) {
        print_usage(argv[0]);
        return 1;
    }
    const Args& args = *maybe_args;

    const std::optional<tlsf::Specification> reference =
        read_spec(args.signals_path);
    if (!reference.has_value()) {
        return 1;
    }
    std::vector<std::string> signals = reference->m_inputs;
    signals.insert(signals.end(), reference->m_outputs.begin(),
                   reference->m_outputs.end());
    const std::vector<fingerprint::LassoWord> words = fingerprint::sample_words(
        signals, args.n_words, args.seed, args.max_prefix, args.max_cycle);

    const std::vector<std::string> files = expand_paths(args.paths);
    if (files.empty()) {
        std::cerr << "no .tlsf files found\n";
        return 1;
    }

    std::size_t failures = 0;
    for (const std::string& file : files) {
        const std::optional<tlsf::Specification> spec = read_spec(file);
        if (!spec.has_value()) {
            ++failures;
            continue;
        }
        try {
            std::cout << std::filesystem::path(file).filename().string() << "\t"
                      << fingerprint::to_hex(fingerprint::fingerprint_of(
                             spec->to_ltl_formula(), words))
                      << "\n";
        } catch (const std::exception& exc) {
            std::cerr << file << ": " << exc.what() << "\n";
            ++failures;
        }
    }
    // A failed file is reported and skipped rather than aborting the run: a
    // scoring pass over thousands of candidates should not lose a whole run's
    // curve to one unreadable file. The exit status still carries the fact.
    return failures == 0 ? 0 : 1;
}
