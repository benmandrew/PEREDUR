#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "config.hpp"
#include "driver_support.hpp"
#include "filter/implication.hpp"
#include "filter/implication_check.hpp"
#include "filter/running_antichain.hpp"
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
    std::string curve_index;
    std::size_t jobs{0};
    std::size_t wave{0};
    std::int64_t timeout_s{20};
};

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " <dir-or-file>... [--jobs N] [--timeout S]\n"
        << "       " << prog
        << " --curve F [--jobs N] [--wave W] [--timeout S]\n"
        << "\n"
        << "Reports the maximal subset of a set of specifications under the\n"
        << "implication order (A dominates B when A implies B and B does not\n"
        << "imply A), then quotients the survivors by mutual implication.\n"
        << "The input format is basic-TLSF (.tlsf) or FRETISH JSON (.json),\n"
        << "chosen by the extensions present; the two cannot be mixed.\n"
        << "Directory arguments contribute every file of the chosen\n"
        << "extension in them, non-recursively.\n"
        << "\n"
        << "  --curve F    Walk the accumulator index F in timestamp order\n"
           "               and print the antichain's event log instead, one\n"
           "               row per admission, drop and removal. Files\n"
           "               resolve against F's own directory.\n"
        << "  --jobs N     Solver calls in flight (default: hardware "
           "concurrency).\n"
        << "  --wave W     Arrivals scanned concurrently under --curve\n"
           "               (default: twice the pool; 1 walks serially).\n"
        << "  --timeout S  Per-black-call budget in seconds (default: 20).\n"
        << "  --version    Print the git commit this binary was built from.\n";
}

enum class FlagStatus : std::uint8_t { NotAFlag, Consumed, Bad };

// Consumes the value of a flag that takes one, or reports that @p arg is not
// such a flag. Split out of parse_args, which the cognitive-complexity check
// rejects once the flag table grows past a couple of entries.
FlagStatus take_valued_flag(const std::string& arg, int& index, int argc,
                            const char* const* argv, Args& args) {
    static const std::array<const char*, 4> k_valued = {"--jobs", "--timeout",
                                                        "--wave", "--curve"};
    if (std::none_of(k_valued.begin(), k_valued.end(),
                     [&arg](const char* name) { return arg == name; })) {
        return FlagStatus::NotAFlag;
    }
    if (index + 1 >= argc || argv[index + 1] == nullptr) {
        std::cerr << arg << " expects a value\n";
        return FlagStatus::Bad;
    }
    const std::string value(argv[++index]);
    if (arg == "--curve") {
        args.curve_index = value;
        return FlagStatus::Consumed;
    }
    const std::optional<std::size_t> count = parse_seed(value);
    if (!count.has_value() || *count == 0) {
        std::cerr << arg << " expects a positive integer\n";
        return FlagStatus::Bad;
    }
    if (arg == "--jobs") {
        args.jobs = *count;
    } else if (arg == "--wave") {
        args.wave = *count;
    } else {
        args.timeout_s = static_cast<std::int64_t>(*count);
    }
    return FlagStatus::Consumed;
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
        const FlagStatus status = take_valued_flag(arg, i, argc, argv, args);
        if (status == FlagStatus::Bad) {
            return std::nullopt;
        }
        if (status == FlagStatus::Consumed) {
            continue;
        }
        if (arg.rfind("--", 0) == 0) {
            std::cerr << "unknown argument: " << arg << "\n";
            return std::nullopt;
        }
        args.paths.push_back(arg);
    }
    // Curve mode takes its files from the index, so a positional there names a
    // set nothing reads; the batch mode has nothing to read without one.
    if (args.curve_index.empty() == args.paths.empty()) {
        std::cerr << (args.curve_index.empty()
                          ? "expected a directory or file, or --curve\n"
                          : "--curve takes its files from the index; drop the "
                            "positional argument\n");
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

    // load_specification reads the file itself, and this driver has already
    // read it to report an unreadable one uniformly across the two formats, so
    // the JSON half of that function is repeated here rather than the read.
    static Specification parse(const std::string& text) {
        const nlohmann::json jobj = nlohmann::json::parse(text);
        if (const std::optional<std::string> err =
                validate_specification_json(jobj)) {
            throw std::invalid_argument(*err);
        }
        return add_atom_prefix(jobj.get<Specification>());
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
        return make_implication_filter(checker, nullptr, on_progress);
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
        return tlsf_make_implication_filter(checker, nullptr, on_progress);
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

// One row of the accumulator's index.tsv: which file, and how many seconds into
// the search the run passed it through the output gate. The generation column
// sits between them and is not read here, the walk being over time.
using IndexRow = std::pair<std::string, double>;

std::optional<std::vector<IndexRow>> read_index(const std::string& path) {
    std::ifstream index(path);
    if (!index) {
        std::cerr << path << ": cannot read index\n";
        return std::nullopt;
    }
    std::vector<IndexRow> rows;
    std::string line;
    while (std::getline(index, line)) {
        const std::size_t first = line.find('\t');
        if (first == std::string::npos) {
            continue;
        }
        const std::size_t second = line.find('\t', first + 1);
        if (second == std::string::npos) {
            // A run killed mid-append leaves a partial last row. Dropping it
            // costs one candidate and keeps every row already flushed, which
            // is the reason the index is written a row at a time.
            continue;
        }
        double elapsed = 0.0;
        try {
            elapsed = std::stod(line.substr(second + 1));
        } catch (const std::exception&) {
            continue;  // The header, and any row whose time did not land.
        }
        rows.emplace_back(line.substr(0, first), elapsed);
    }
    return rows;
}

// The index names files the accumulator wrote, so their extension is the
// format, as a directory's contents are for the batch mode.
bool index_wants_tlsf(const std::vector<IndexRow>& rows) {
    return std::any_of(rows.begin(), rows.end(), [](const IndexRow& row) {
        return std::filesystem::path(row.first).extension() == ".tlsf";
    });
}

const char* event_name(antichain::AntichainEvent event) {
    switch (event) {
        case antichain::AntichainEvent::Admit:
            return "admit";
        case antichain::AntichainEvent::Drop:
            return "drop";
        case antichain::AntichainEvent::Remove:
            break;
    }
    return "remove";
}

// Walks the index in timestamp order and prints the antichain's event log.
//
// Every prefix's maximal set is recoverable from it: an admission adds, a
// removal takes away, and both are permanent, so membership at an instant is
// the admissions up to it less the removals up to it. That is what replaces
// one batch sweep per time cut, each re-deciding pairs the previous cut had
// already decided, in a fresh process with a cold solver cache.
template <typename Spec>
int run_curve(const Args& args, const std::vector<IndexRow>& index,
              SatisfiabilityChecker& checker) {
    const std::filesystem::path directory =
        std::filesystem::path(args.curve_index).parent_path();
    std::vector<antichain::Arrival<Spec>> arrivals;
    arrivals.reserve(index.size());
    std::size_t parse_failures = 0;
    for (const auto& [name, elapsed] : index) {
        const std::optional<std::string> contents =
            read_file_contents((directory / name).string());
        if (!contents.has_value()) {
            std::cerr << name << ": cannot read file\n";
            ++parse_failures;
            continue;
        }
        try {
            arrivals.push_back(
                {name, elapsed, SpecOps<Spec>::parse(*contents)});
        } catch (const std::exception& exc) {
            std::cerr << name << ": " << exc.what() << "\n";
            ++parse_failures;
        }
    }
    if (arrivals.empty()) {
        std::cerr << "no specifications parsed\n";
        return 1;
    }
    // Stable, so candidates sharing a timestamp keep the order the run found
    // them in and the walk stays a function of the index alone.
    std::stable_sort(arrivals.begin(), arrivals.end(),
                     [](const antichain::Arrival<Spec>& left,
                        const antichain::Arrival<Spec>& right) {
                         return left.m_elapsed_s < right.m_elapsed_s;
                     });

    // Written as the walk produces it rather than at the end, so a caller that
    // kills this on a deadline keeps the prefix: the early cuts of a log-spaced
    // curve are where its information is, and a hard run should yield a short
    // curve rather than none.
    antichain::AntichainStats stats;
    std::cout << "elapsed_s\tfile\tevent\tn_maximal\n";
    const std::vector<antichain::AntichainRow> rows =
        antichain::running_antichain(
            arrivals,
            [&checker](const Spec& from, const Spec& dest) {
                return SpecOps<Spec>::implies(from, dest, checker)
                    .value_or(false);
            },
            args.wave,
            [](std::size_t done, std::size_t total) {
                std::cerr << "\r  arrivals " << done << "/" << total
                          << std::flush;
                std::cout << std::flush;
            },
            [](const antichain::AntichainRow& row) {
                std::cout << std::fixed << std::setprecision(6)
                          << row.m_elapsed_s << "\t" << row.m_name << "\t"
                          << event_name(row.m_event) << "\t" << row.m_size
                          << "\n";
            },
            &stats);
    std::cout << std::flush;
    std::cerr << "\n"
              << "arrivals   " << arrivals.size() << "\n"
              << "maximal    "
              << antichain::members_at(rows, arrivals.back().m_elapsed_s).size()
              << "\n"
              << "queries    " << stats.m_solver_queries << "\n"
              << "refuted    " << stats.m_refuted_directions << "\n"
              << "shortcut   " << stats.m_short_circuited << "\n"
              << "reconciled " << stats.m_reconciled_pairs << "\n";
    if (parse_failures > 0) {
        std::cerr << "unparsed   " << parse_failures << "\n";
    }
    return 0;
}

int run_curve(const Args& args, SatisfiabilityChecker& checker) {
    const std::optional<std::vector<IndexRow>> index =
        read_index(args.curve_index);
    if (!index.has_value()) {
        return 1;
    }
    return index_wants_tlsf(*index)
               ? run_curve<tlsf::Specification>(args, *index, checker)
               : run_curve<Specification>(args, *index, checker);
}

// Both settings are measured over this tool's own queries, whole-spec
// implications of 1000-1600 characters. `ltlfilt --simplify` took 95.6% of
// solver wall time (1153.7s against 52.9s for the decision itself) and a
// 40-file batch went from 304s to 30s without it, with the same survivors;
// without the pass the unsimplified query runs about 2x longer, so the 500ms
// SPOT budget tuned for the search tips over under load and an undecided
// `ExpectUnsat` query keeps both sides. Giving SPOT black's budget instead
// restored agreement on 264 of 264 cut-values across a 10-run sample. The
// FRETISH path takes the same two settings, which is what its own final
// filters run under (src/repair/evolution.cpp). Its queries were per
// requirement rather than whole-spec until ed5413f, and measured at 40
// generations of 1000 the simplify pass was 59-61% of every ltlfilt exec a run
// made even at that shape; it now asks the whole-spec query this paragraph
// measures.
SatisfiabilityChecker& configure_checker(const Args& args) {
    Config cfg;
    cfg.parallel = args.jobs;
    cfg.black_timeout = std::chrono::milliseconds{args.timeout_s * 1000};
    // Bounds the `--remove-wm` rewrite check_satisfiability runs before black.
    // compare.cpp sized it at 300 s off amba.
    cfg.ltlfilt_timeout = std::chrono::milliseconds{300'000};
    apply_tool_timeouts(cfg);
    set_thread_pool_size(cfg.parallel);
    SatisfiabilityChecker& checker = global_sat_checker();
    // An undecided `ExpectUnsat` query keeps both sides, so SPOT takes black's
    // budget rather than the 500ms tuned for the search: that restored
    // agreement on 264 of 264 cut-values across a 10-run sample.
    checker.set_spot_budget(cfg.black_timeout);
    return checker;
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
    SatisfiabilityChecker& checker = configure_checker(args);
    if (!args.curve_index.empty()) {
        return run_curve(args, checker);
    }
    return wants_tlsf(args.paths) ? run<tlsf::Specification>(args, checker)
                                  : run<Specification>(args, checker);
}
