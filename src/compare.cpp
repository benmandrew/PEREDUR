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
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "bounded_async.hpp"
#include "driver_support.hpp"
#include "filter/implication_check.hpp"
#include "repair/manifest.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "serialisation.hpp"
#include "thread_pool.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/parser.hpp"
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

// A repair's display name and its optional weighted fitness. Both the FRETISH
// and TLSF paths reduce their loaded repairs to this before reporting, so the
// report loop is agnostic to which format produced them.
struct RepairMeta {
    std::string name;
    std::optional<double> fitness;
};

std::vector<std::pair<std::string, serialisation::ScoredSpecification>>
load_repairs(const std::string& dir) {
    std::vector<std::pair<std::string, serialisation::ScoredSpecification>>
        repairs;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        // A repairs directory is a run's output directory, so it also holds the
        // run manifest write_run_manifest left there. Loading that as a
        // specification throws, and the throw aborts the whole comparison --
        // which reads downstream as implies_ideal = 0 for a run whose repairs
        // were never scored at all, not as an error. The TLSF loader below is
        // immune only because repairs are .tlsf there and the manifest is
        // .json.
        if (entry.path().filename() == k_run_manifest_name) {
            continue;
        }
        repairs.emplace_back(entry.path().filename().string(),
                             load_scored_specification(entry.path().string()));
    }
    std::sort(repairs.begin(), repairs.end(),
              [](const auto& lhs, const auto& rhs) {
                  const double lfit =
                      lhs.second.fitness ? lhs.second.fitness->total : 0.0;
                  const double rfit =
                      rhs.second.fitness ? rhs.second.fitness->total : 0.0;
                  return lfit > rfit;
              });
    return repairs;
}

std::vector<std::pair<std::string, Specification>> load_ideals(
    const std::string& dir) {
    std::vector<std::pair<std::string, Specification>> ideals;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        ideals.emplace_back(entry.path().filename().string(),
                            load_specification(entry.path().string()));
    }
    std::sort(
        ideals.begin(), ideals.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    return ideals;
}

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

struct TlsfRepair {
    std::string name;
    tlsf::Specification spec;
    std::optional<double> fitness;
};

std::vector<TlsfRepair> load_tlsf_repairs(const std::string& dir) {
    std::vector<TlsfRepair> repairs;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".tlsf") {
            continue;
        }
        repairs.push_back({entry.path().filename().string(),
                           tlsf::parse(read_file_or_throw(entry.path().string(),
                                                          "cannot open file")),
                           read_tlsf_fitness(entry.path())});
    }
    std::sort(repairs.begin(), repairs.end(),
              [](const TlsfRepair& lhs, const TlsfRepair& rhs) {
                  return lhs.fitness.value_or(0.0) > rhs.fitness.value_or(0.0);
              });
    return repairs;
}

std::vector<std::pair<std::string, tlsf::Specification>> load_tlsf_ideals(
    const std::string& dir) {
    std::vector<std::pair<std::string, tlsf::Specification>> ideals;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".tlsf") {
            continue;
        }
        ideals.emplace_back(entry.path().filename().string(),
                            tlsf::parse(read_file_or_throw(
                                entry.path().string(), "cannot open file")));
    }
    std::sort(
        ideals.begin(), ideals.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    return ideals;
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
// its strongest relation to any ideal plus a summary. `spawn(rep, ide)` returns
// a callable that computes that pair's relation when the dispatcher runs it, so
// the FRETISH and TLSF paths differ only in which spec type and implication
// function they close over.
template <typename Spawn>
void run_and_report(const std::vector<RepairMeta>& repairs,
                    const std::vector<std::string>& ideal_names, Spawn spawn) {
    const std::size_t n_repairs = repairs.size();
    const std::size_t n_ideals = ideal_names.size();
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
            return spawn(rep, ide);
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
            std::cout << ideal_names[best_per_repair[idx].ideal_idx];
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

bool dir_has_extension(const std::string& dir, const std::string& ext) {
    std::error_code err_code;
    const std::filesystem::directory_iterator iter(dir, err_code);
    return std::any_of(
        std::filesystem::begin(iter), std::filesystem::end(iter),
        [&ext](const auto& entry) { return entry.path().extension() == ext; });
}

int run_tlsf(const Args& args, SatisfiabilityChecker& checker) {
    std::vector<TlsfRepair> repairs;
    try {
        repairs = load_tlsf_repairs(args.repairs_dir);
    } catch (const std::exception& exc) {
        std::cerr << "Error loading repairs: " << exc.what() << "\n";
        return 1;
    }
    if (repairs.empty()) {
        std::cerr << "No .tlsf files found in: " << args.repairs_dir << "\n";
        return 1;
    }

    std::vector<std::pair<std::string, tlsf::Specification>> ideals;
    try {
        ideals = load_tlsf_ideals(args.ideals_dir);
    } catch (const std::exception& exc) {
        std::cerr << "Error loading ideals: " << exc.what() << "\n";
        return 1;
    }
    if (ideals.empty()) {
        std::cerr << "No .tlsf files found in: " << args.ideals_dir << "\n";
        return 1;
    }

    std::vector<RepairMeta> repair_meta;
    repair_meta.reserve(repairs.size());
    for (const TlsfRepair& repair : repairs) {
        repair_meta.push_back({repair.name, repair.fitness});
    }
    std::vector<std::string> ideal_names;
    ideal_names.reserve(ideals.size());
    for (const auto& ideal : ideals) {
        ideal_names.push_back(ideal.first);
    }

    run_and_report(
        repair_meta, ideal_names, [&](std::size_t rep, std::size_t ide) {
            return [&, rep, ide] {
                return classify(tlsf_spec_implies(repairs[rep].spec,
                                                  ideals[ide].second, checker),
                                tlsf_spec_implies(ideals[ide].second,
                                                  repairs[rep].spec, checker));
            };
        });
    return 0;
}

int run_fretish(const Args& args, SatisfiabilityChecker& checker) {
    std::vector<std::pair<std::string, serialisation::ScoredSpecification>>
        repairs;
    try {
        repairs = load_repairs(args.repairs_dir);
    } catch (const std::exception& exc) {
        std::cerr << "Error loading repairs: " << exc.what() << "\n";
        return 1;
    }
    if (repairs.empty()) {
        std::cerr << "No .json files found in: " << args.repairs_dir << "\n";
        return 1;
    }

    std::vector<std::pair<std::string, Specification>> ideals;
    try {
        ideals = load_ideals(args.ideals_dir);
    } catch (const std::exception& exc) {
        std::cerr << "Error loading ideals: " << exc.what() << "\n";
        return 1;
    }
    if (ideals.empty()) {
        std::cerr << "No .json files found in: " << args.ideals_dir << "\n";
        return 1;
    }

    std::vector<RepairMeta> repair_meta;
    repair_meta.reserve(repairs.size());
    for (const auto& repair : repairs) {
        repair_meta.push_back(
            {repair.first,
             repair.second.fitness
                 ? std::optional<double>(repair.second.fitness->total)
                 : std::nullopt});
    }
    std::vector<std::string> ideal_names;
    ideal_names.reserve(ideals.size());
    for (const auto& ideal : ideals) {
        ideal_names.push_back(ideal.first);
    }

    run_and_report(repair_meta, ideal_names,
                   [&](std::size_t rep, std::size_t ide) {
                       return [&, rep, ide] {
                           return classify(
                               spec_implies(repairs[rep].second.spec,
                                            ideals[ide].second, checker),
                               spec_implies(ideals[ide].second,
                                            repairs[rep].second.spec, checker));
                       };
                   });
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
    const auto maybe_args = parse_args(argc, argv);
    if (!maybe_args.has_value()) {
        print_usage(argv[0]);
        return 1;
    }
    const Args& args = *maybe_args;

    SatisfiabilityChecker& checker =
        configure_offline_checkers(std::chrono::milliseconds{20'000}, true);

    // Route by input format. A .tlsf extension on either directory path, or any
    // .tlsf file in either directory, selects the TLSF path; otherwise FRETISH
    // JSON. Mixing formats across the two directories is not supported, and the
    // per-path loaders reject a directory holding none of their extension.
    const bool tlsf =
        std::filesystem::path(args.repairs_dir).extension() == ".tlsf" ||
        std::filesystem::path(args.ideals_dir).extension() == ".tlsf" ||
        dir_has_extension(args.repairs_dir, ".tlsf") ||
        dir_has_extension(args.ideals_dir, ".tlsf");

    return tlsf ? run_tlsf(args, checker) : run_fretish(args, checker);
}
