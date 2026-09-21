#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "driver_support.hpp"
#include "filter/implication.hpp"
#include "filter/implication_check.hpp"
#include "fingerprint/lasso.hpp"
#include "fingerprint/prefilter.hpp"
#include "genetic/generation.hpp"
#include "repair/manifest.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "serialisation.hpp"
#include "thread_pool.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

// Runs PEREDUR's maximality filter over a directory of specifications that
// PEREDUR did not produce, so a foreign tool's output can be measured for
// semantic diversity on the same definition PEREDUR applies to its own. Both
// front ends are here: basic-TLSF text and FRETISH JSON, selected by extension
// the way `compare` selects, since a maximality curve over a FRETISH campaign
// reads the same accumulated candidates a TLSF one does.
//
// Two numbers, because they answer different questions and AuRUS's own
// MaximalSolutions filter conflates them. "maximal" is the filter PEREDUR runs:
// keep every spec no other spec strictly dominates, so a whole equivalence
// class survives together. "classes" quotients those survivors by mutual
// implication, which is the count of genuinely distinct strongest repairs. A
// filter that keeps one arbitrary member per class reports the second number
// while looking like the first.
//
// The two implication oracles ask the same question, so the numbers are
// comparable across the formats: each lowers a whole specification to one LTL
// formula and asks a complete query. `spec_implies` decomposed per requirement
// until 2026-09-11, which missed every implication holding only via several
// requirements together, so a FRETISH corpus reported more maximal members
// than the same corpus under the TLSF oracle.

namespace {

struct Args {
    std::vector<std::string> paths;
    std::size_t jobs{0};
    std::int64_t timeout_s{20};
};

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " <dir-or-file>... [--jobs N] [--timeout S]\n"
        << "\n"
        << "Reports the maximal subset of a set of specifications under the\n"
        << "implication order (A dominates B when A implies B and B does not\n"
        << "imply A), then quotients the survivors by mutual implication.\n"
        << "The input format is basic-TLSF (.tlsf) or FRETISH JSON (.json),\n"
        << "chosen by the extensions present; the two cannot be mixed.\n"
        << "Directory arguments contribute every file of the chosen\n"
        << "extension in them, non-recursively.\n"
        << "\n"
        << "  --jobs N     Solver calls in flight (default: hardware "
           "concurrency).\n"
        << "  --timeout S  Per-black-call budget in seconds (default: 20).\n"
        << "  --version    Print the git commit this binary was built from.\n";
}

// Hand-rolled rather than through find_unknown_arg and collect_argument_paths,
// which between them assume a driver whose paths are all flag values: the first
// reports every positional as unknown and the second collects the flags as
// paths. This one takes a variable number of positional arguments, so it walks
// argv once and still refuses an unrecognised flag rather than ignoring it.
std::optional<Args> parse_args(int argc, const char* const* argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        const std::string arg(argv[i]);
        const bool is_jobs = arg == "--jobs";
        if (is_jobs || arg == "--timeout") {
            if (i + 1 >= argc || argv[i + 1] == nullptr) {
                std::cerr << arg << " expects a value\n";
                return std::nullopt;
            }
            const std::optional<std::size_t> value = parse_seed(argv[++i]);
            if (!value.has_value() || *value == 0) {
                std::cerr << arg << " expects a positive integer\n";
                return std::nullopt;
            }
            if (is_jobs) {
                args.jobs = *value;
            } else {
                args.timeout_s = static_cast<std::int64_t>(*value);
            }
            continue;
        }
        if (arg.rfind("--", 0) == 0) {
            std::cerr << "unknown argument: " << arg << "\n";
            return std::nullopt;
        }
        args.paths.push_back(arg);
    }
    if (args.paths.empty()) {
        return std::nullopt;
    }
    return args;
}

// Everything one front end contributes, so the sweep, the quotient and the
// report below are written once. The pair of specialisations is the whole of
// the format split; adding a third format means adding one of these.
template <typename Spec>
struct SpecOps;

template <>
struct SpecOps<Specification> {
    static constexpr const char* k_extension = ".json";
    static constexpr const char* k_alphabet = "atom alphabets";

    static Specification parse(const std::string& text) {
        return parse_specification_json(text);
    }

    static bool same_alphabet(const Specification& lhs,
                              const Specification& rhs) {
        return lhs.m_in_atoms == rhs.m_in_atoms &&
               lhs.m_out_atoms == rhs.m_out_atoms && lhs.m_modes == rhs.m_modes;
    }

    static std::optional<bool> implies(const Specification& from,
                                       const Specification& dest,
                                       SatisfiabilityChecker& checker) {
        return spec_implies(from, dest, checker);
    }

    static FilterFunctionT<Specification> maximality_filter(
        SatisfiabilityChecker& checker,
        const GenerationProgressCallback& on_progress) {
        // No original specification here -- `maximal` takes a bare directory of
        // repairs -- so an equivalence class collapses on operator< with no
        // similarity to rank it.
        return make_implication_filter<Specification>(checker, nullptr,
                                                      on_progress);
    }
};

template <>
struct SpecOps<tlsf::Specification> {
    static constexpr const char* k_extension = ".tlsf";
    static constexpr const char* k_alphabet = "signal alphabets";

    static tlsf::Specification parse(const std::string& text) {
        return tlsf::parse(text);
    }

    static bool same_alphabet(const tlsf::Specification& lhs,
                              const tlsf::Specification& rhs) {
        return lhs.m_inputs == rhs.m_inputs && lhs.m_outputs == rhs.m_outputs;
    }

    static std::optional<bool> implies(const tlsf::Specification& from,
                                       const tlsf::Specification& dest,
                                       SatisfiabilityChecker& checker) {
        return tlsf_spec_implies(from, dest, checker);
    }

    static FilterFunctionT<tlsf::Specification> maximality_filter(
        SatisfiabilityChecker& checker,
        const GenerationProgressCallback& on_progress) {
        return make_implication_filter<tlsf::Specification>(checker, nullptr,
                                                            on_progress);
    }
};

// Route by input format, as compare.cpp does. A .tlsf extension on any
// argument, or any .tlsf file in any directory argument, selects the TLSF
// path; otherwise FRETISH JSON. Mixing the formats across the arguments is not
// supported, and the FRETISH side then finds no .json files and says so.
bool wants_tlsf(const std::vector<std::string>& paths) {
    for (const std::string& path : paths) {
        if (std::filesystem::path(path).extension() == ".tlsf") {
            return true;
        }
        std::error_code err_code;
        const std::filesystem::directory_iterator iter(path, err_code);
        const bool found =
            std::any_of(std::filesystem::begin(iter),
                        std::filesystem::end(iter), [](const auto& entry) {
                            return entry.path().extension() == ".tlsf";
                        });
        if (found) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> expand_paths(const std::vector<std::string>& paths,
                                      const std::string& extension) {
    std::vector<std::string> files;
    for (const std::string& path : paths) {
        if (std::filesystem::is_directory(path)) {
            for (const auto& entry :
                 std::filesystem::directory_iterator(path)) {
                if (entry.path().extension() != extension) {
                    continue;
                }
                // A FRETISH directory of repairs is a run's output directory,
                // so it also holds the run manifest write_run_manifest left
                // there. Reading that as a specification fails validation and
                // is reported as an unparsed file, which would put a number in
                // the report that is about the manifest rather than about the
                // repairs. compare.cpp's FRETISH loader skips it for the same
                // reason; the TLSF path is immune only because repairs are
                // .tlsf there and the manifest is .json.
                if (entry.path().filename() == k_run_manifest_name) {
                    continue;
                }
                files.push_back(entry.path().string());
            }
        } else {
            files.push_back(path);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Structural duplicates cost nothing to remove and would each pay for a full
// row of the pairwise sweep, so they are collapsed before any solver call.
// The implication filter does this internally too; doing it here as well is
// what lets the report name the files behind each survivor.
template <typename Spec>
struct Corpus {
    std::vector<Spec> m_distinct;
    std::vector<std::vector<std::string>> m_members;
    std::unordered_map<Spec, std::size_t> m_position_of;
    std::size_t m_parse_failures = 0;
};

template <typename Spec>
Corpus<Spec> load_corpus(const std::vector<std::string>& files) {
    Corpus<Spec> corpus;
    for (const std::string& file : files) {
        const std::optional<std::string> contents = read_file_contents(file);
        if (!contents.has_value()) {
            std::cerr << file << ": cannot read file\n";
            ++corpus.m_parse_failures;
            continue;
        }
        Spec spec;
        try {
            spec = SpecOps<Spec>::parse(*contents);
        } catch (const std::exception& exc) {
            std::cerr << file << ": " << exc.what() << "\n";
            ++corpus.m_parse_failures;
            continue;
        }
        const auto [iter, inserted] =
            corpus.m_position_of.try_emplace(spec, corpus.m_distinct.size());
        if (inserted) {
            corpus.m_distinct.push_back(std::move(spec));
            corpus.m_members.push_back({file});
        } else {
            corpus.m_members[iter->second].push_back(file);
        }
    }
    return corpus;
}

// The survivors' partition into mutual-implication classes, and how many there
// are. Separate from `main` because the pairwise sweep is the one part of this
// tool that is an algorithm rather than plumbing.
struct Quotient {
    std::vector<std::size_t> m_class_of;
    std::size_t m_n_classes = 0;
};

// Quotient the survivors. They are pairwise non-dominating by construction, so
// a mutual implication here is an equivalence and nothing else, and the sweep
// is over the survivors alone rather than the whole input.
//
// This loop is serial and used to be free: every query it asks, the pairwise
// sweep had already asked and cached. The sweep's fingerprint prefilter took
// that away, since the pairs reaching here are the mutually non-implying ones
// and those are exactly the ones a sampled word refutes, so the sweep now
// skips them and this loop pays a fresh subprocess for each. Measured over 80
// candidates that was 1030 serial ltlfilt calls and 20.4s of a 21s run. The
// same prefilter applies here for the same reason and restores it: a word one
// survivor accepts and another rejects rules out equivalence outright.
template <typename Spec>
Quotient quotient_by_equivalence(
    const std::vector<Spec>& maximal, SatisfiabilityChecker& checker,
    const std::vector<fingerprint::PackedFingerprint>& prints) {
    Quotient out;
    out.m_class_of.assign(maximal.size(), 0);
    const bool have_prints = prints.size() == maximal.size();
    for (std::size_t i = 0; i < maximal.size(); ++i) {
        bool placed = false;
        for (std::size_t j = 0; j < i && !placed; ++j) {
            if (have_prints &&
                (fingerprint::refutes_implication(prints[i], prints[j]) ||
                 fingerprint::refutes_implication(prints[j], prints[i]))) {
                continue;
            }
            if (SpecOps<Spec>::implies(maximal[i], maximal[j], checker)
                    .value_or(false) &&
                SpecOps<Spec>::implies(maximal[j], maximal[i], checker)
                    .value_or(false)) {
                out.m_class_of[i] = out.m_class_of[j];
                placed = true;
            }
        }
        if (!placed) {
            out.m_class_of[i] = out.m_n_classes++;
        }
    }
    return out;
}

// `m_members` is indexed by position in the distinct corpus, not by position in
// `maximal`, so every lookup goes through `m_position_of`.
template <typename Spec>
void print_report(const std::vector<Spec>& maximal, const Quotient& quotient,
                  const Corpus<Spec>& corpus) {
    std::size_t n_files = 0;
    for (const std::vector<std::string>& group : corpus.m_members) {
        n_files += group.size();
    }
    std::cout << "files      " << n_files << "\n"
              << "distinct   " << corpus.m_distinct.size() << "\n"
              << "maximal    " << maximal.size() << "\n"
              << "classes    " << quotient.m_n_classes << "\n";
    if (corpus.m_parse_failures > 0) {
        std::cout << "unparsed   " << corpus.m_parse_failures << "\n";
    }
    std::cout << "\n";
    for (std::size_t i = 0; i < maximal.size(); ++i) {
        const std::size_t position = corpus.m_position_of.at(maximal[i]);
        std::cout << "class " << quotient.m_class_of[i] << "  "
                  << corpus.m_members[position].front();
        if (corpus.m_members[position].size() > 1) {
            std::cout << "  (+" << corpus.m_members[position].size() - 1
                      << " identical)";
        }
        std::cout << "\n";
    }
}

template <typename Spec>
int run(const Args& args, SatisfiabilityChecker& checker) {
    const std::vector<std::string> files =
        expand_paths(args.paths, SpecOps<Spec>::k_extension);
    if (files.empty()) {
        std::cerr << "no " << SpecOps<Spec>::k_extension << " files found\n";
        return 1;
    }
    const Corpus<Spec> corpus = load_corpus<Spec>(files);
    if (corpus.m_distinct.empty()) {
        std::cerr << "no specifications parsed\n";
        return 1;
    }
    // Implication between specs over different alphabets is not the relation
    // this reports, so say so rather than printing a number that means nothing.
    for (const Spec& spec : corpus.m_distinct) {
        if (!SpecOps<Spec>::same_alphabet(spec, corpus.m_distinct.front())) {
            std::cerr << "warning: the input set mixes "
                      << SpecOps<Spec>::k_alphabet
                      << "; implication across them is not meaningful\n";
            break;
        }
    }

    std::size_t reported = 0;
    const std::vector<Spec> maximal = SpecOps<Spec>::maximality_filter(
        checker, [&reported](std::size_t done, std::size_t total) {
            reported = done;
            if (done % 500 == 0 || done == total) {
                std::cerr << "\r  pairs " << done << "/" << total << std::flush;
            }
        })(corpus.m_distinct);
    if (reported > 0) {
        std::cerr << "\n";
    }

    const Quotient quotient = quotient_by_equivalence<Spec>(
        maximal, checker, fingerprint::prefilter::fingerprints_of(maximal));
    print_report(maximal, quotient, corpus);
    return 0;
}

}  // namespace

int main(int argc, const char* const argv[]) {
    if (!has_program_name(argc, argv)) {
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

    SatisfiabilityChecker& checker = configure_offline_checkers(
        std::chrono::milliseconds{args.timeout_s * 1000}, true);
    set_thread_pool_size(args.jobs);

    return wants_tlsf(args.paths) ? run<tlsf::Specification>(args, checker)
                                  : run<Specification>(args, checker);
}
