#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "bounded_async.hpp"
#include "driver_support.hpp"
#include "repair/manifest.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "serialisation.hpp"
#include "spec_format.hpp"
#include "thread_pool.hpp"
#include "tlsf/specification.hpp"

namespace {

struct Args {
    std::string repairs_dir;
    std::string ideals_dir;
};

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " --repairs <dir> --ideals <dir>\n"
        << "\n"
        << "Compares each repair in the repairs directory against every\n"
        << "ideal in the ideals directory and reports whether the found\n"
        << "repair is equivalent to, strictly stronger than, strictly\n"
        << "weaker than, or incomparable with the ideal, under the\n"
        << "assume-guarantee implication order. Both directories must hold\n"
        << "the same format: FRETISH JSON (.json) or basic-TLSF (.tlsf).\n"
        << "\n"
        << "  --version  Print the git commit this binary was built from.\n";
}

std::optional<Args> parse_args(int argc, const char* const* argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) {
            continue;
        }
        const std::string arg(argv[i]);
        if (arg == "--repairs" && i + 1 < argc && argv[i + 1] != nullptr) {
            args.repairs_dir = argv[++i];
        } else if (arg == "--ideals" && i + 1 < argc &&
                   argv[i + 1] != nullptr) {
            args.ideals_dir = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return std::nullopt;
        }
    }
    if (args.repairs_dir.empty() || args.ideals_dir.empty()) {
        return std::nullopt;
    }
    return args;
}

// A loaded repair or ideal. Only a repair carries a fitness, and only when its
// run recorded one.
template <typename Spec>
struct Entry {
    std::string name;
    Spec spec;
    std::optional<double> fitness;
};

// PEREDUR writes each TLSF repair as repair_N.tlsf alongside a
// repair_N.fitness.json carrying its weighted total. The fitness file is
// optional here: a missing or malformed one just leaves the fitness unset,
// which sorts the repair as if scored 0 and omits it from the printed line.
std::optional<double> read_tlsf_fitness(
    const std::filesystem::path& tlsf_path) {
    std::filesystem::path fitness_path = tlsf_path;
    fitness_path.replace_extension(".fitness.json");
    std::error_code err_code;
    if (!std::filesystem::exists(fitness_path, err_code)) {
        return std::nullopt;
    }
    try {
        const nlohmann::json jobj = nlohmann::json::parse(
            read_file_or_throw(fitness_path.string(), "cannot open file"));
        return jobj.at("total").get<double>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// FRETISH repairs carry their fitness inline, TLSF ones in a sibling file.
template <typename Spec>
Entry<Spec> load_repair(const std::filesystem::path& path) {
    if constexpr (std::is_same_v<Spec, Specification>) {
        serialisation::ScoredSpecification scored =
            load_scored_specification(path.string());
        std::optional<double> fitness;
        if (scored.fitness) {
            fitness = scored.fitness->total;
        }
        return {path.filename().string(), std::move(scored.spec), fitness};
    } else {
        return {path.filename().string(),
                SpecFormat<Spec>::load(path.string(), "cannot open file"),
                read_tlsf_fitness(path)};
    }
}

// Repairs sort best-first by fitness, ideals by name.
template <typename Spec>
std::vector<Entry<Spec>> load_entries(const std::string& dir, bool repairs) {
    std::vector<Entry<Spec>> entries;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != SpecFormat<Spec>::k_extension) {
            continue;
        }
        if (!repairs) {
            entries.push_back({entry.path().filename().string(),
                               SpecFormat<Spec>::load(entry.path().string(),
                                                      "cannot open file"),
                               std::nullopt});
            continue;
        }
        // A repairs directory is a run's output directory, so it also holds the
        // run manifest write_run_manifest left there. Loading that as a
        // specification throws, and the throw aborts the whole comparison --
        // which reads downstream as implies_ideal = 0 for a run whose repairs
        // were never scored at all, not as an error. TLSF is immune only
        // because repairs are .tlsf there and the manifest is .json.
        if (entry.path().filename() != k_run_manifest_name) {
            entries.push_back(load_repair<Spec>(entry.path()));
        }
    }
    std::sort(entries.begin(), entries.end(),
              [repairs](const Entry<Spec>& lhs, const Entry<Spec>& rhs) {
                  return repairs ? lhs.fitness.value_or(0.0) >
                                       rhs.fitness.value_or(0.0)
                                 : lhs.name < rhs.name;
              });
    return entries;
}

template <typename Spec>
bool load_or_report(const std::string& dir, bool repairs,
                    std::vector<Entry<Spec>>& entries) {
    try {
        entries = load_entries<Spec>(dir, repairs);
    } catch (const std::exception& exc) {
        std::cerr << "Error loading " << (repairs ? "repairs" : "ideals")
                  << ": " << exc.what() << "\n";
        return false;
    }
    if (entries.empty()) {
        std::cerr << "No " << SpecFormat<Spec>::k_extension
                  << " files found in: " << dir << "\n";
        return false;
    }
    return true;
}

// Priority: equivalent > stronger > weaker > incomparable > timeout
enum class Relation : std::uint8_t {
    Timeout,
    Incomparable,
    Weaker,
    Stronger,
    Equivalent
};

struct RelationInfo {
    const char* detail;   // printed after the repair name
    bool names_ideal;     // whether `detail` is followed by the ideal name
    const char* summary;  // label used in the summary line
};

// Indexed by the numeric value of Relation.
constexpr std::array<RelationInfo, 5> k_relation_info = {{
    {"timeout", false, "timeout"},
    {"incomparable", false, "incomparable"},
    {"strictly weaker than ", true, "strictly weaker"},
    {"strictly stronger than ", true, "strictly stronger"},
    {"equivalent to ", true, "equivalent"},
}};

const RelationInfo& relation_info(Relation rel) {
    return k_relation_info[static_cast<std::size_t>(rel)];
}

Relation classify(std::optional<bool> fwd, std::optional<bool> rev) {
    if (fwd.value_or(false) && rev.value_or(false)) {
        return Relation::Equivalent;
    }
    if (fwd.value_or(false)) {
        return Relation::Stronger;
    }
    if (rev.value_or(false)) {
        return Relation::Weaker;
    }
    if (fwd.has_value() && rev.has_value()) {
        return Relation::Incomparable;
    }
    return Relation::Timeout;
}

// Runs the n_repairs x n_ideals implication grid and prints, for each repair,
// its strongest relation to any ideal plus a summary.
template <typename Spec>
void run_and_report(const std::vector<Entry<Spec>>& repairs,
                    const std::vector<Entry<Spec>>& ideals,
                    SatisfiabilityChecker& checker) {
    const std::size_t n_repairs = repairs.size();
    const std::size_t n_ideals = ideals.size();
    const std::size_t n_tasks = n_repairs * n_ideals;
    const std::size_t max_in_flight = dispatch_window();

    struct BestResult {
        Relation rel = Relation::Timeout;
        std::size_t ideal_idx = 0;
    };
    std::vector<BestResult> best_per_repair(n_repairs);

    run_bounded_async(
        n_tasks, max_in_flight,
        [&](std::size_t task_idx) {
            const std::size_t rep = task_idx / n_ideals;
            const std::size_t ide = task_idx % n_ideals;
            return [&, rep, ide] {
                return classify(
                    SpecFormat<Spec>::implies(repairs[rep].spec,
                                              ideals[ide].spec, checker),
                    SpecFormat<Spec>::implies(ideals[ide].spec,
                                              repairs[rep].spec, checker));
            };
        },
        [&](std::size_t task_idx, Relation rel) {
            const std::size_t rep = task_idx / n_ideals;
            const std::size_t ide = task_idx % n_ideals;
            if (rel > best_per_repair[rep].rel) {
                best_per_repair[rep] = {rel, ide};
            }
        });

    std::array<std::size_t, k_relation_info.size()> counts{};
    for (std::size_t idx = 0; idx < n_repairs; ++idx) {
        const Relation best = best_per_repair[idx].rel;
        const RelationInfo& info = relation_info(best);
        std::cout << std::left << std::setw(24) << repairs[idx].name << " : ";
        std::cout << info.detail;
        if (info.names_ideal) {
            std::cout << ideals[best_per_repair[idx].ideal_idx].name;
        }
        ++counts[static_cast<std::size_t>(best)];
        const std::optional<double>& fitness = repairs[idx].fitness;
        if (fitness) {
            std::cout << std::fixed << std::setprecision(4)
                      << "  [fitness: " << *fitness << "]";
        }
        std::cout << "\n";
    }

    // Summary lists relations best-first, unlike the enum's timeout-first
    // order.
    constexpr std::array<Relation, k_relation_info.size()> summary_order = {
        Relation::Equivalent, Relation::Stronger, Relation::Weaker,
        Relation::Incomparable, Relation::Timeout};
    std::cout << "\nSummary:";
    for (std::size_t i = 0; i < summary_order.size(); ++i) {
        const Relation rel = summary_order[i];
        std::cout << (i == 0 ? " " : ", ")
                  << counts[static_cast<std::size_t>(rel)] << ' '
                  << relation_info(rel).summary;
    }
    std::cout << "\n";
}

template <typename Spec>
int run(const Args& args, SatisfiabilityChecker& checker) {
    std::vector<Entry<Spec>> repairs;
    std::vector<Entry<Spec>> ideals;
    if (!load_or_report(args.repairs_dir, true, repairs) ||
        !load_or_report(args.ideals_dir, false, ideals)) {
        return 1;
    }
    run_and_report(repairs, ideals, checker);
    return 0;
}

}  // namespace

// Only libc++'s allocation failures (length_error, bad_array_new_length) can
// escape, and terminating on them is intended.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, const char* const argv[]) {
    if (!has_program_name(argc, argv)) {
        return 1;
    }
    if (handle_info_flags(argc, argv, print_usage)) {
        return 0;
    }
    const auto maybe_args = parse_args(argc, argv);
    if (!maybe_args.has_value()) {
        print_usage(argv[0]);
        return 1;
    }
    const Args& args = *maybe_args;

    SatisfiabilityChecker& checker =
        configure_offline_checkers(std::chrono::milliseconds{20'000}, true);

    return selects_tlsf({args.repairs_dir, args.ideals_dir})
               ? run<tlsf::Specification>(args, checker)
               : run<Specification>(args, checker);
}
