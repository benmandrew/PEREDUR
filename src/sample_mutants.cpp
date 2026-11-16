// sample-mutants: draw random mutation chains from a FRETISH specification
// with the search's own mutation operator, and write the distinct results
// with how often each was drawn at each depth.
//
// The seeded corpus (experiments/2026-11-13-seeded-corpus) uses it to
// estimate how likely one or two undirected mutations of a seeded mutant are
// to land on a repair at least as strong as its base, which would make the
// subject a trivial reversion.

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "config_io.hpp"
#include "driver_support.hpp"
#include "genetic/mutation.hpp"
#include "genetic/random_source.hpp"
#include "requirement.hpp"
#include "serialisation.hpp"

namespace {

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " <spec.json> --config <cfg.toml> --out <dir>\n"
        << "       [--draws N] [--depth D] [--seed S] [--unrealizable-parent "
           "S]\n"
        << "\n"
        << "Draws N chains of D mutations (defaults 20000 and 2) from the\n"
        << "specification with mutate_specification under the config's\n"
        << "mutation keys, with no parent context unless one is given. Writes "
           "each distinct\n"
        << "result once as <dir>/s<k>.json and <dir>/index.tsv with\n"
        << "columns file, depth, count.\n"
        << "\n"
        << "  --unrealizable-parent S  Give every mutation the parent context\n"
        << "             of an unrealizable parent with status S and no "
           "refused\n"
        << "             slots, so that direction_gated and step_scaling act "
           "as\n"
        << "             they would on the specification's own offspring. The\n"
        << "             second mutation of a chain sees the same context.\n"
        << "\n"
        << "  --version  Print the git commit this binary was built from.\n";
}

// The command line, once parsed.
struct Options {
    std::string spec_path;
    std::string config_path;
    std::string out_dir;
    std::size_t draws = 20000;
    std::size_t depth = 2;
    std::size_t seed = 1;
    std::optional<double> parent_status;
};

// Parses argv into @p opts, false on an unknown flag, a missing value or a
// missing required argument.
bool parse_options(int argc, const char* const* argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--config" && has_value) {
            opts.config_path = argv[++i];
        } else if (arg == "--out" && has_value) {
            opts.out_dir = argv[++i];
        } else if (arg == "--draws" && has_value) {
            opts.draws = std::stoul(argv[++i]);
        } else if (arg == "--depth" && has_value) {
            opts.depth = std::stoul(argv[++i]);
        } else if (arg == "--seed" && has_value) {
            opts.seed = std::stoul(argv[++i]);
        } else if (arg == "--unrealizable-parent" && has_value) {
            opts.parent_status = std::stod(argv[++i]);
        } else if (opts.spec_path.empty() && arg.rfind("--", 0) != 0) {
            opts.spec_path = arg;
        } else {
            return false;
        }
    }
    return !opts.spec_path.empty() && !opts.config_path.empty() &&
           !opts.out_dir.empty() && opts.depth != 0;
}

// The distinct results, each with its file index, and how often each was
// drawn at each depth.
struct Samples {
    std::map<std::string, std::size_t> file_of;
    // Key: (depth, serialised result) -> count.
    std::map<std::pair<std::size_t, std::string>, std::size_t> counts;
};

Samples draw_samples(const Options& opts, const Specification& original,
                     const Config& cfg) {
    const RandomSource random_source = make_random_source_from_seed(opts.seed);
    ParentContext parent;
    parent.status = opts.parent_status;
    parent.realizable = false;
    parent.original = &original;
    const ParentContext* context = opts.parent_status ? &parent : nullptr;
    Samples samples;
    for (std::size_t draw = 0; draw < opts.draws; ++draw) {
        Specification current = original;
        for (std::size_t step = 1; step <= opts.depth; ++step) {
            current =
                mutate_specification(current, random_source, cfg, context);
            const nlohmann::json jobj = current;
            const std::string text = jobj.dump();
            if (samples.file_of.find(text) == samples.file_of.end()) {
                const std::size_t index = samples.file_of.size();
                samples.file_of.emplace(text, index);
            }
            ++samples.counts[{step, text}];
        }
    }
    return samples;
}

void write_samples(const std::string& out_dir, const Samples& samples) {
    std::filesystem::create_directories(out_dir);
    for (const auto& [text, index] : samples.file_of) {
        std::ofstream out(std::filesystem::path(out_dir) /
                          ("s" + std::to_string(index) + ".json"));
        out << nlohmann::json::parse(text).dump(2) << "\n";
    }
    std::ofstream index_out(std::filesystem::path(out_dir) / "index.tsv");
    index_out << "file\tdepth\tcount\n";
    for (const auto& [key, count] : samples.counts) {
        index_out << "s" << samples.file_of.at(key.second) << ".json\t"
                  << key.first << "\t" << count << "\n";
    }
}

}  // namespace

int main(int argc, const char* const argv[]) {
    if (!has_program_name(argc, argv)) {
        return 1;
    }
    if (handle_info_flags(argc, argv, print_usage)) {
        return 0;
    }
    Options opts;
    if (!parse_options(argc, argv, opts)) {
        print_usage(argv[0]);
        return 1;
    }
    try {
        const Specification original = load_specification(opts.spec_path);
        const Config cfg = config_from_toml(opts.config_path);
        const Samples samples = draw_samples(opts, original, cfg);
        write_samples(opts.out_dir, samples);
        std::cout << samples.file_of.size() << " distinct specifications from "
                  << opts.draws << " chains of depth " << opts.depth << "\n";
    } catch (const std::exception& exc) {
        std::cerr << exc.what() << "\n";
        return 1;
    }
    return 0;
}
