#include <algorithm>
#include <cassert>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "config.hpp"
#include "driver_support.hpp"
#include "fitness/semantic_similarity.hpp"
#include "fitness/syntactic_similarity.hpp"
#include "requirement.hpp"
#include "runner/atom_names.hpp"
#include "serialisation.hpp"
#include "thread_pool.hpp"

// Prints, for every candidate FRETISH specification read on stdin, the terms
// that semantic similarity under `fitness.keyword_similarity = "semantic"`
// mixes, one row per changed slot pair, and the specification's syntactic
// score under both keyword modes. A weight sweep over `semantic_trace_weight`
// then needs no model count beyond the one per pair printed here.

namespace {

struct Args {
    std::string original;
    std::string pairs_path;
    std::string specs_path;
    std::size_t bound{Config{}.default_model_counting_bound};
    SimilarityMetric metric{Config{}.similarity_metric};
    std::size_t jobs{0};
};

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog
        << " --original <spec.json> --pairs <out.tsv> --specs <out.tsv>\n"
        << "       [--bound K] [--metric logarithmic|direct] [--jobs N]"
           " < candidates\n"
        << "\n"
        << "Reads candidate FRETISH spec JSON paths from stdin, one per line,\n"
        << "and scores each against the original.\n"
        << "--pairs gets one row per changed slot pair: its trace-count\n"
        << "score, the three keyword order terms and their mean. A slot\n"
        << "tombstoned on one side only is marked removed and scores 0.\n"
        << "--specs gets each candidate's syntactic similarity with the\n"
        << "keyword fields scored by order and by token, and how many\n"
        << "tombstones were restored to align it (the archive drops them).\n"
        << "\n"
        << "  --bound K    Model-counting bound (default "
        << Config{}.default_model_counting_bound << ").\n"
        << "  --metric M   logarithmic or direct (default logarithmic).\n"
        << "  --jobs N     Candidates scored at once (default: hardware "
           "concurrency).\n"
        << "  --version    Print the git commit this binary was built from.\n";
}

std::optional<Args> parse_args(int argc, const char* const* argv) {
    if (find_unknown_arg(argc, argv,
                         {"--original", "--pairs", "--specs", "--bound",
                          "--metric", "--jobs"},
                         {})) {
        return std::nullopt;
    }
    Args args;
    const std::optional<std::string> original =
        parse_string_arg(argc, argv, "--original");
    const std::optional<std::string> pairs =
        parse_string_arg(argc, argv, "--pairs");
    const std::optional<std::string> specs =
        parse_string_arg(argc, argv, "--specs");
    if (!original || !pairs || !specs) {
        return std::nullopt;
    }
    args.original = *original;
    args.pairs_path = *pairs;
    args.specs_path = *specs;
    if (const std::optional<std::string> bound =
            parse_string_arg(argc, argv, "--bound")) {
        const std::optional<std::size_t> value = parse_seed(*bound);
        if (!value || *value == 0) {
            return std::nullopt;
        }
        args.bound = *value;
    }
    if (const std::optional<std::string> jobs =
            parse_string_arg(argc, argv, "--jobs")) {
        const std::optional<std::size_t> value = parse_seed(*jobs);
        if (!value) {
            return std::nullopt;
        }
        args.jobs = *value;
    }
    if (const std::optional<std::string> metric =
            parse_string_arg(argc, argv, "--metric")) {
        if (*metric == "logarithmic") {
            args.metric = SimilarityMetric::Logarithmic;
        } else if (*metric == "direct") {
            args.metric = SimilarityMetric::Direct;
        } else {
            return std::nullopt;
        }
    }
    return args;
}

struct PairRow {
    bool is_guarantee{false};
    std::size_t slot{0};
    bool removed{false};
    double trace{0.0};
    double timing_order{0.0};
    double scope_order{0.0};
    double condition_type_order{0.0};
    double keyword{0.0};
};

struct CandidateResult {
    std::vector<PairRow> pairs;
    double syntactic_order{0.0};
    double syntactic_token{0.0};
    std::size_t restored{0};
};

// Throws with the path named when the file will not load or carries an atom
// name the tools would misread.
Specification load_screened(const std::string& path) {
    Specification spec;
    try {
        spec = load_specification(path);
    } catch (const std::exception& exc) {
        throw std::runtime_error(path + ": " + exc.what());
    }
    if (const std::optional<std::string> unsafe =
            runner::first_unsafe_atom_name(environment_signals(spec),
                                           spec.m_out_atoms)) {
        throw std::runtime_error(path + ": " + *unsafe);
    }
    return spec;
}

// One side of an archived candidate with its tombstones restored. The archive
// writes a candidate without its removed requirements, so a candidate that lost
// one reads a slot short, and every later slot would pair with the original
// slot before its own. Removal never reorders a side, so the survivors are a
// subsequence of the original's slots. The subsequence with the highest total
// syntactic similarity places them, and each skipped slot becomes a tombstone,
// which scores 0 as it did in the run.
std::vector<Requirement> realign_side(const std::vector<Requirement>& candidate,
                                      const std::vector<Requirement>& original,
                                      std::size_t& restored) {
    const std::size_t n_kept = candidate.size();
    const std::size_t n_slots = original.size();
    if (n_kept >= n_slots) {
        return candidate;
    }
    const Config cfg;
    const auto similarity = [&](std::size_t kept, std::size_t slot) {
        return syntactic_similarity(candidate[kept], original[slot], cfg);
    };
    // best[i][j]: the highest total placing the first i survivors among the
    // first j slots, defined for j >= i.
    std::vector<std::vector<double>> best(
        n_kept + 1, std::vector<double>(n_slots + 1, 0.0));
    for (std::size_t i = 1; i <= n_kept; ++i) {
        for (std::size_t j = i; j <= n_slots; ++j) {
            const double take = best[i - 1][j - 1] + similarity(i - 1, j - 1);
            best[i][j] = j > i ? std::max(best[i][j - 1], take) : take;
        }
    }
    // Walked back from the last slot, matching wherever the match reaches the
    // optimum, so a tie places a survivor in the latest slot it could take.
    std::vector<Requirement> aligned(original);
    for (Requirement& slot : aligned) {
        slot.m_removed = true;
    }
    std::size_t unplaced = n_kept;
    for (std::size_t slot = n_slots; slot > 0 && unplaced > 0; --slot) {
        if (best[unplaced - 1][slot - 1] + similarity(unplaced - 1, slot - 1) ==
            best[unplaced][slot]) {
            aligned[slot - 1] = candidate[unplaced - 1];
            --unplaced;
        }
    }
    assert(unplaced == 0);
    restored += n_slots - n_kept;
    return aligned;
}

CandidateResult score_candidate(const Specification& archived,
                                const Specification& original,
                                const Args& args) {
    CandidateResult result{};
    Specification candidate = archived;
    candidate.m_assumptions = realign_side(
        archived.m_assumptions, original.m_assumptions, result.restored);
    candidate.m_guarantees = realign_side(
        archived.m_guarantees, original.m_guarantees, result.restored);
    for (const ChangedRequirementPair& pair :
         changed_requirement_pairs(candidate, original)) {
        PairRow row{};
        row.is_guarantee = pair.m_is_guarantee;
        row.slot = pair.m_slot;
        row.removed = pair.m_removed;
        if (!pair.m_removed) {
            const Requirement& first = *pair.m_requirement;
            const Requirement& second = *pair.m_other_requirement;
            row.trace =
                semantic_similarity(first, second, args.bound, args.metric);
            row.timing_order =
                timing_order_similarity(first.m_timing, second.m_timing);
            row.scope_order =
                scope_order_similarity(first.m_scope, second.m_scope);
            row.condition_type_order = condition_type_order_similarity(
                first.m_condition_type, second.m_condition_type);
            row.keyword = keyword_order_similarity(first, second);
        }
        result.pairs.push_back(row);
    }
    Config cfg;
    cfg.keyword_similarity = KeywordSimilarity::Syntactic;
    result.syntactic_order = syntactic_similarity(candidate, original, cfg);
    cfg.keyword_similarity = KeywordSimilarity::Semantic;
    result.syntactic_token = syntactic_similarity(candidate, original, cfg);
    return result;
}

std::vector<std::string> read_candidate_paths() {
    std::vector<std::string> paths;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            paths.push_back(line);
        }
    }
    return paths;
}

void write_tables(const Args& args, const std::vector<std::string>& paths,
                  const std::vector<CandidateResult>& results) {
    std::vector<std::size_t> order(paths.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(),
                     [&paths](std::size_t lhs, std::size_t rhs) {
                         return paths[lhs] < paths[rhs];
                     });
    std::ofstream pairs_out(args.pairs_path);
    std::ofstream specs_out(args.specs_path);
    if (!pairs_out || !specs_out) {
        throw std::runtime_error("cannot open an output file");
    }
    pairs_out.precision(17);
    specs_out.precision(17);
    pairs_out << "candidate\tside\tslot\tremoved\ttrace\ttiming_order\t"
                 "scope_order\tcondition_type_order\tkeyword\n";
    specs_out << "candidate\tn_pairs\tsyntactic_order\tsyntactic_token\t"
                 "restored\n";
    for (const std::size_t idx : order) {
        std::vector<PairRow> rows = results[idx].pairs;
        std::stable_sort(rows.begin(), rows.end(),
                         [](const PairRow& lhs, const PairRow& rhs) {
                             return std::tie(lhs.is_guarantee, lhs.slot) <
                                    std::tie(rhs.is_guarantee, rhs.slot);
                         });
        for (const PairRow& row : rows) {
            pairs_out << paths[idx] << '\t' << (row.is_guarantee ? 'G' : 'A')
                      << '\t' << row.slot << '\t' << (row.removed ? 1 : 0);
            if (row.removed) {
                pairs_out << "\t0\t\t\t\t0\n";
            } else {
                pairs_out << '\t' << row.trace << '\t' << row.timing_order
                          << '\t' << row.scope_order << '\t'
                          << row.condition_type_order << '\t' << row.keyword
                          << '\n';
            }
        }
        specs_out << paths[idx] << '\t' << results[idx].pairs.size() << '\t'
                  << results[idx].syntactic_order << '\t'
                  << results[idx].syntactic_token << '\t'
                  << results[idx].restored << '\n';
    }
    if (!pairs_out || !specs_out) {
        throw std::runtime_error("cannot write an output file");
    }
}

int run(const Args& args) {
    const Specification original = load_screened(args.original);
    const std::vector<std::string> paths = read_candidate_paths();
    std::vector<CandidateResult> results(paths.size());
    set_thread_pool_size(args.jobs);
    run_bounded_async(
        paths.size(), dispatch_window(),
        [&paths, &original, &args](std::size_t idx) {
            return [&paths, &original, &args, idx] {
                const Specification candidate = load_screened(paths[idx]);
                return score_candidate(candidate, original, args);
            };
        },
        [&results](std::size_t idx, CandidateResult result) {
            results[idx] = std::move(result);
        });
    write_tables(args, paths, results);
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
    const std::optional<Args> args = parse_args(argc, argv);
    if (!args) {
        print_usage(argv[0]);
        return 1;
    }
    try {
        return run(*args);
    } catch (const std::exception& exc) {
        std::cerr << exc.what() << "\n";
        return 1;
    }
}
