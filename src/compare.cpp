#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
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
#include "spec_format.hpp"
#include "thread_pool.hpp"
#include "tlsf/specification.hpp"

namespace {

struct Args {
    std::string repairs_dir;
    std::string ideals_dir;
    std::int64_t timeout_s{20};
    bool rule_only{false};
    std::string pairs_file;
};

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog
        << " --repairs <dir> --ideals <dir> [--timeout S] [--rule-only]\n"
        << "       [--pairs FILE]\n"
        << "\n"
        << "Compares each repair in the repairs directory against every\n"
        << "ideal in the ideals directory and reports whether the found\n"
        << "repair is equivalent to, strictly stronger than, strictly\n"
        << "weaker than, or incomparable with the ideal, under the\n"
        << "assume-guarantee implication order. Both directories must hold\n"
        << "the same format: FRETISH JSON (.json) or basic-TLSF (.tlsf).\n"
        << "\n"
        << "Each pair's per-direction verdicts go to stderr as\n"
        << "DIR<TAB>repair<TAB>ideal<TAB>fwd<TAB>rev, with ? for undecided.\n"
        << "COMPARE_DIRECTIONS=fwd|rev asks one direction only; the other\n"
        << "reports 0.\n"
        << "\n"
        << "  --timeout S  Per-black-call budget in seconds (default: 20).\n"
        << "  --rule-only  FRETISH only: decide each direction by rule alone,\n"
        << "               launching no solver. A direction holds when every\n"
        << "               assumption of the stronger side is implied by one "
           "of\n"
        << "               the weaker side's, and every guarantee of the "
           "weaker\n"
        << "               side by one of the stronger side's, each by equal\n"
        << "               lowering or by the requirement rules the engine's\n"
        << "               filters use. Sound and incomplete: 0 means "
           "unproved,\n"
        << "               never refuted, and no direction reads ?. A pair\n"
        << "               with neither direction proved prints no DIR line.\n"
        << "  --pairs FILE With --rule-only, ask only the listed pairs, one\n"
        << "               a line: repair name, ideal name and fwd, rev or\n"
        << "               both, tab-separated. A direction not asked reads "
           "0.\n"
        << "  --version    Print the git commit this binary was built from.\n";
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
        } else if (arg == "--timeout") {
            if (i + 1 >= argc || argv[i + 1] == nullptr) {
                std::cerr << arg << " expects a value\n";
                return std::nullopt;
            }
            const std::optional<std::size_t> value = parse_seed(argv[++i]);
            if (!value.has_value() || *value == 0) {
                std::cerr << arg << " expects a positive integer\n";
                return std::nullopt;
            }
            args.timeout_s = static_cast<std::int64_t>(*value);
        } else if (arg == "--rule-only") {
            args.rule_only = true;
        } else if (arg == "--pairs" && i + 1 < argc && argv[i + 1] != nullptr) {
            args.pairs_file = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return std::nullopt;
        }
    }
    if (args.repairs_dir.empty() || args.ideals_dir.empty()) {
        return std::nullopt;
    }
    if (!args.pairs_file.empty() && !args.rule_only) {
        std::cerr << "--pairs needs --rule-only\n";
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

    // COMPARE_DIRECTIONS=fwd|rev skips the other direction, which the caller
    // (scripts/compare_pairs.py's dirs column) has already refuted on a
    // sampled word; a skipped direction reports 0. Every pair's per-direction
    // verdicts go to stderr as DIR lines, since the relation alone cannot say
    // which direction an undecided pair lacks.
    const char* dirs_env = std::getenv("COMPARE_DIRECTIONS");
    const std::string dirs = dirs_env != nullptr ? dirs_env : "both";
    const bool do_fwd = dirs != "rev";
    const bool do_rev = dirs != "fwd";
    std::mutex dir_mutex;
    const auto tri = [](std::optional<bool> verdict) {
        if (!verdict.has_value()) {
            return "?";
        }
        return *verdict ? "1" : "0";
    };

    run_bounded_async(
        n_tasks, max_in_flight,
        [&](std::size_t task_idx) {
            const std::size_t rep = task_idx / n_ideals;
            const std::size_t ide = task_idx % n_ideals;
            return [&, rep, ide] {
                const std::optional<bool> fwd =
                    do_fwd ? SpecFormat<Spec>::implies(
                                 repairs[rep].spec, ideals[ide].spec, checker)
                           : std::optional<bool>(false);
                const std::optional<bool> rev =
                    do_rev ? SpecFormat<Spec>::implies(
                                 ideals[ide].spec, repairs[rep].spec, checker)
                           : std::optional<bool>(false);
                {
                    const std::scoped_lock lock(dir_mutex);
                    std::cerr << "DIR\t" << repairs[rep].name << "\t"
                              << ideals[ide].name << "\t" << tri(fwd) << "\t"
                              << tri(rev) << "\n";
                }
                return classify(fwd, rev);
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

// Whole-specification implication by rule alone, for --rule-only. It asks
// spec_implies_by_rule's question without its slot pairing: a requirement may
// be covered by any live entry on the other side, not only the one in its own
// slot. JSON drops a removed requirement rather than tombstoning it, so the
// slots of two loaded repairs no longer pair by index once either removed one.
// Any cover is sound, since each step is an implication between two whole
// requirements. A grid over one pool asks millions of pairs of a few thousand
// distinct requirements, so each lowering is interned once and
// requirement_implies is memoised per pair of interned ids.
class RuleImplication {
   public:
    explicit RuleImplication(const std::vector<const Specification*>& specs) {
        for (const Specification* spec : specs) {
            m_sides.push_back(
                {intern(spec->m_assumptions), intern(spec->m_guarantees)});
        }
    }

    // Whether spec @p from is proved to imply spec @p dest, both indices
    // into the constructor's list.
    bool operator()(std::size_t from, std::size_t dest) {
        return covers(m_sides[dest].first, m_sides[from].first) &&
               covers(m_sides[from].second, m_sides[dest].second);
    }

   private:
    // The sorted, distinct ids of the live requirements of @p reqs.
    std::vector<std::uint32_t> intern(const std::vector<Requirement>& reqs) {
        std::vector<std::uint32_t> ids;
        for (const Requirement& req : reqs) {
            if (req.m_removed) {
                continue;
            }
            const auto found = m_ids.find(req.m_ltl);
            if (found != m_ids.end()) {
                ids.push_back(found->second);
                continue;
            }
            const auto id = static_cast<std::uint32_t>(m_reqs.size());
            m_reqs.push_back(&req);
            m_ids.emplace(req.m_ltl, id);
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }

    // Whether every requirement of @p weaker is implied by one of
    // @p stronger.
    bool covers(const std::vector<std::uint32_t>& stronger,
                const std::vector<std::uint32_t>& weaker) {
        for (const std::uint32_t req : weaker) {
            if (std::binary_search(stronger.begin(), stronger.end(), req)) {
                continue;
            }
            if (std::none_of(
                    stronger.begin(), stronger.end(),
                    [&](std::uint32_t cand) { return implies(cand, req); })) {
                return false;
            }
        }
        return true;
    }

    bool implies(std::uint32_t stronger, std::uint32_t weaker) {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(stronger) << 32U) | weaker;
        const auto found = m_memo.find(key);
        if (found != m_memo.end()) {
            return found->second;
        }
        const bool proved =
            requirement_implies(*m_reqs[stronger], *m_reqs[weaker]);
        m_memo.emplace(key, proved);
        return proved;
    }

    // The loaded specifications own the requirements and outlive this.
    std::vector<const Requirement*> m_reqs;
    std::unordered_map<std::string_view, std::uint32_t> m_ids;
    std::vector<
        std::pair<std::vector<std::uint32_t>, std::vector<std::uint32_t>>>
        m_sides;
    std::unordered_map<std::uint64_t, bool> m_memo;
};

// --rule-only: the grid run_and_report asks, each direction decided by rule
// on this thread, with no solver started.
// One asked pair of --rule-only: a repair, an ideal, and which directions.
struct AskedPair {
    std::size_t rep;
    std::size_t ide;
    bool fwd;
    bool rev;
};

// Every pair of the grid both ways, or the pairs @p path lists; nullopt
// with a message where a line names no loaded file.
std::optional<std::vector<AskedPair>> asked_pairs(
    const std::vector<Entry<Specification>>& repairs,
    const std::vector<Entry<Specification>>& ideals, const std::string& path) {
    std::vector<AskedPair> out;
    if (path.empty()) {
        for (std::size_t rep = 0; rep < repairs.size(); ++rep) {
            for (std::size_t ide = 0; ide < ideals.size(); ++ide) {
                out.push_back({rep, ide, true, true});
            }
        }
        return out;
    }
    std::unordered_map<std::string, std::size_t> rep_at;
    std::unordered_map<std::string, std::size_t> ide_at;
    for (std::size_t idx = 0; idx < repairs.size(); ++idx) {
        rep_at.emplace(repairs[idx].name, idx);
    }
    for (std::size_t idx = 0; idx < ideals.size(); ++idx) {
        ide_at.emplace(ideals[idx].name, idx);
    }
    std::istringstream lines(read_file_or_throw(path, "cannot open pairs"));
    for (std::string line; std::getline(lines, line);) {
        if (line.empty()) {
            continue;
        }
        const std::size_t tab1 = line.find('\t');
        const std::size_t tab2 = line.find('\t', tab1 + 1);
        if (tab1 == std::string::npos || tab2 == std::string::npos) {
            std::cerr << "--pairs: malformed line: " << line << "\n";
            return std::nullopt;
        }
        const auto rep = rep_at.find(line.substr(0, tab1));
        const auto ide = ide_at.find(line.substr(tab1 + 1, tab2 - tab1 - 1));
        const std::string dirs = line.substr(tab2 + 1);
        if (rep == rep_at.end() || ide == ide_at.end() ||
            (dirs != "fwd" && dirs != "rev" && dirs != "both")) {
            std::cerr << "--pairs: unknown name or direction: " << line << "\n";
            return std::nullopt;
        }
        out.push_back({rep->second, ide->second, dirs != "rev", dirs != "fwd"});
    }
    return out;
}

void run_rule_only(const std::vector<Entry<Specification>>& repairs,
                   const std::vector<Entry<Specification>>& ideals,
                   const std::vector<AskedPair>& pairs) {
    std::vector<const Specification*> specs;
    for (const auto& entry : repairs) {
        specs.push_back(&entry.spec);
    }
    for (const auto& entry : ideals) {
        specs.push_back(&entry.spec);
    }
    RuleImplication implies(specs);
    const std::size_t off = repairs.size();
    std::vector<std::pair<Relation, std::size_t>> best(repairs.size(),
                                                       {Relation::Timeout, 0});
    for (const AskedPair& pair : pairs) {
        const bool fwd = pair.fwd && implies(pair.rep, off + pair.ide);
        const bool rev = pair.rev && implies(off + pair.ide, pair.rep);
        if (!fwd && !rev) {
            // A grid over one pool has millions of unproved pairs, and an
            // absent line reads as unproved both ways.
            continue;
        }
        std::cerr << "DIR\t" << repairs[pair.rep].name << "\t"
                  << ideals[pair.ide].name << "\t" << (fwd ? "1" : "0") << "\t"
                  << (rev ? "1" : "0") << "\n";
        const Relation rel = classify(fwd, rev);
        if (rel > best[pair.rep].first) {
            best[pair.rep] = {rel, pair.ide};
        }
    }
    std::array<std::size_t, k_relation_info.size()> counts{};
    for (std::size_t idx = 0; idx < repairs.size(); ++idx) {
        const RelationInfo& info = relation_info(best[idx].first);
        std::cout << std::left << std::setw(24) << repairs[idx].name << " : "
                  << info.detail;
        if (info.names_ideal) {
            std::cout << ideals[best[idx].second].name;
        }
        std::cout << "\n";
        ++counts[static_cast<std::size_t>(best[idx].first)];
    }
    std::cout << "\nSummary: "
              << counts[static_cast<std::size_t>(Relation::Equivalent)]
              << " equivalent, "
              << counts[static_cast<std::size_t>(Relation::Stronger)]
              << " strictly stronger, "
              << counts[static_cast<std::size_t>(Relation::Weaker)]
              << " strictly weaker, "
              << counts[static_cast<std::size_t>(Relation::Incomparable)]
              << " unproved (by rule)\n";
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

    if (args.rule_only) {
        if (selects_tlsf({args.repairs_dir, args.ideals_dir})) {
            std::cerr << "--rule-only reads FRETISH repairs only\n";
            return 1;
        }
        std::vector<Entry<Specification>> repairs;
        std::vector<Entry<Specification>> ideals;
        if (!load_or_report(args.repairs_dir, true, repairs) ||
            !load_or_report(args.ideals_dir, false, ideals)) {
            return 1;
        }
        const std::optional<std::vector<AskedPair>> pairs =
            asked_pairs(repairs, ideals, args.pairs_file);
        if (!pairs.has_value()) {
            return 1;
        }
        run_rule_only(repairs, ideals, *pairs);
        return 0;
    }

    SatisfiabilityChecker& checker = configure_offline_checkers(
        std::chrono::milliseconds{args.timeout_s * 1000}, true);

    return selects_tlsf({args.repairs_dir, args.ideals_dir})
               ? run<tlsf::Specification>(args, checker)
               : run<Specification>(args, checker);
}
