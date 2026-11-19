#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "climb_replay_slots.hpp"
#include "config.hpp"
#include "config_io.hpp"
#include "driver_support.hpp"
#include "filter/streaming_maximal.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/climb.hpp"
#include "genetic/output_gate.hpp"
#include "repair/manifest.hpp"
#include "repair_file.hpp"
#include "repair_steps.hpp"
#include "requirement.hpp"
#include "runner/spot.hpp"
#include "serialisation.hpp"
#include "thread_pool.hpp"

// Replays the output climb of a finished FRETISH run under a chosen number of
// step orders, for the climb-diversity study. It reads the run's config, its
// original specification and the repairs its search accumulated, leaves out
// the climb's own accumulated results, and runs the post-search tail the run
// ran: the climb over every accumulated repair, then the final filters over
// the accumulated repairs and the climbed ones together. At --orders 1 it
// should write the run's own repair set.

namespace {

namespace fs = std::filesystem;

void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog
        << " --run <run-dir> --orders <k> --out <dir> [--spec <spec.json>]\n"
           "\n"
           "Replays the output climb of the finished FRETISH run in <run-dir>\n"
           "with filters.climb_orders = <k>, and writes the maximal results "
           "to\n"
           "<dir> as repair_N.json, with summary.json beside them.\n"
           "\n"
           "  --spec <path>  the original specification; by default the input\n"
           "                 run.json names, or the file of the same name "
           "under\n"
           "                 this checkout's examples/ when that path is "
           "gone\n";
}

nlohmann::json read_json(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return nlohmann::json::parse(file);
}

// The run's input where it still exists, and otherwise the same path below
// the last `examples/` component, resolved against this checkout.
fs::path resolve_spec(const std::string& recorded) {
    if (fs::exists(recorded)) {
        return recorded;
    }
    const std::string marker = "/examples/";
    const std::size_t pos = recorded.rfind(marker);
    if (pos != std::string::npos) {
        const fs::path local = fs::path(PEREDUR_EXAMPLES_DIR) /
                               recorded.substr(pos + marker.size());
        if (fs::exists(local)) {
            return local;
        }
    }
    throw std::runtime_error("cannot find the run's input " + recorded +
                             "; pass --spec");
}

struct Member {
    std::string file;
    std::size_t generation = 0;
};

// The rows of accumulated/index.tsv, in file order, or none where it is
// absent.
std::vector<Member> read_index(const fs::path& run_dir) {
    const fs::path path =
        run_dir / AccumulatedRepairWriter::k_subdirectory / "index.tsv";
    std::vector<Member> rows;
    // A run that accumulated nothing writes no index.
    if (!fs::exists(path)) {
        return rows;
    }
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::string line;
    std::getline(file, line);  // header
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        Member row;
        std::string generation;
        if (!std::getline(fields, row.file, '\t') ||
            !std::getline(fields, generation, '\t')) {
            continue;
        }
        row.generation = std::stoul(generation);
        rows.push_back(std::move(row));
    }
    return rows;
}

// The config keys, as JSON pointers, on which @p replay and @p recorded
// disagree, other than the two the replay sets: a value either side holds and
// the other lacks, or a value both hold that differs. A key the run recorded
// as null is one its path never read, and is skipped.
std::vector<std::string> config_differences(const nlohmann::json& replay,
                                            const nlohmann::json& recorded) {
    std::vector<std::string> keys;
    const nlohmann::json flat_replay = replay.flatten();
    const nlohmann::json flat_recorded = recorded.flatten();
    const auto set_here = [](const std::string& key) {
        return key == "/filters/climb_orders" || key == "/filters/climb_output";
    };
    for (const auto& [key, value] : flat_replay.items()) {
        const auto found = flat_recorded.find(key);
        if (set_here(key) ||
            (found != flat_recorded.end() && found->is_null())) {
            continue;
        }
        if (found == flat_recorded.end()) {
            keys.push_back(key + " (not in the run)");
        } else if (*found != value) {
            keys.push_back(key + " (run " + found->dump() + ", replay " +
                           value.dump() + ")");
        }
    }
    for (const auto& [key, value] : flat_recorded.items()) {
        if (!set_here(key) && !value.is_null() && !flat_replay.contains(key)) {
            keys.push_back(key + " (not in this build, run " + value.dump() +
                           ")");
        }
    }
    return keys;
}

int replay(const fs::path& run_dir, std::size_t orders, const fs::path& out,
           const std::optional<std::string>& spec_arg) {
    const auto started = std::chrono::steady_clock::now();
    const nlohmann::json manifest = read_json(run_dir / k_run_manifest_name);
    Config cfg = config_from_toml(run_dir / "config.toml");
    cfg.climb_output = true;
    cfg.climb_orders = orders;
    apply_tool_timeouts(cfg);
    set_thread_pool_size(cfg.parallel);

    const fs::path spec_path =
        spec_arg.has_value()
            ? fs::path(*spec_arg)
            : resolve_spec(manifest.at("input").get<std::string>());
    Specification original = load_specification(spec_path.string());
    if (!screen_original(
            original, spec_path.string(), environment_signals(original),
            original.m_out_atoms,
            [&original] {
                return global_real_checker().check_realizability(original);
            },
            cfg)) {
        return 1;
    }
    const AggregateWeightedFitnessFunction fitness =
        get_fitness_function(original, cfg);

    // The climb's own results carry the generation after the search's last.
    const std::size_t last_search =
        manifest.at("generations_run").get<std::size_t>();
    std::vector<Specification> members;
    std::size_t n_realigned = 0;
    std::size_t n_climb_files = 0;
    for (const Member& row : read_index(run_dir)) {
        if (row.generation > last_search) {
            ++n_climb_files;
            continue;
        }
        Specification spec = load_specification(
            (run_dir / AccumulatedRepairWriter::k_subdirectory / row.file)
                .string());
        if (spec.m_guarantees.size() < original.m_guarantees.size()) {
            ++n_realigned;
            spec =
                Specification(spec.m_assumptions,
                              climb_replay::restore_removed_slots(
                                  spec.m_guarantees, original.m_guarantees),
                              spec.m_in_atoms, spec.m_out_atoms, spec.m_modes);
        }
        members.push_back(std::move(spec));
    }

    fs::create_directories(out / AccumulatedRepairWriter::k_subdirectory);
    const std::unique_ptr<StreamingMaximalFilter<Specification>> stream =
        make_maximal_stream(original, cfg, out.string());
    if (stream != nullptr) {
        for (const Specification& spec : members) {
            stream->push(spec, {});
        }
    }
    const RepairTransform<Specification> climb =
        [&original, orders,
         accepts = climb_gate(cfg, status_objective_of(fitness))](
            const std::vector<Specification>& repairs) {
            return climb_repairs(repairs, original, accepts, orders);
        };
    SearchResult<Specification> searched;
    const SearchOutput<Specification> output = finish_search(
        cfg, std::move(searched), members, fitness, stream.get(), climb);

    for (std::size_t i = 0; i < output.repairs.size(); ++i) {
        std::ofstream file = open_repair_file(
            (out / ("repair_" + std::to_string(i) + ".json")).string());
        const serialisation::ScoredSpecification scored{
            output.repairs[i].specification,
            repair_fitness_record(output.repairs[i], fitness)};
        const nlohmann::json jobj = scored;
        file << jobj.dump(2) << "\n";
    }

    std::size_t n_climbed = 0;
    for (const FilterRunStats& row : output.filter_stats) {
        if (row.name == "final/climb") {
            n_climbed = row.total_out;
        }
    }
    const double wall_s = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - started)
                              .count();
    const nlohmann::json summary = {
        {"run_dir", fs::absolute(run_dir).string()},
        {"spec", spec_path.string()},
        {"orders", orders},
        {"members_in", members.size()},
        {"members_realigned", n_realigned},
        {"climb_files_skipped", n_climb_files},
        {"climbed_distinct", n_climbed},
        {"repairs_out", output.repairs.size()},
        {"run_repairs", manifest.value("n_repairs", nlohmann::json())},
        {"climb",
         {{"climbed", ClimbStats::n_climbed.load()},
          {"queries", ClimbStats::n_queries.load()},
          {"steps_tried", ClimbStats::n_steps_tried.load()},
          {"steps_accepted", ClimbStats::n_steps_accepted.load()},
          {"ltlsynt_execs", ClimbStats::n_ltlsynt_execs.load()},
          {"gate_errors", ClimbStats::n_gate_errors.load()},
          {"wall_s", ClimbStats::wall_s}}},
        {"wall_s", wall_s},
        {"config_differences",
         config_differences(
             config_json(cfg),
             manifest.value("config", nlohmann::json::object()))},
    };
    std::ofstream(out / "summary.json") << summary.dump(2) << "\n";
    std::cout << "members " << members.size() << ", climbed " << n_climbed
              << ", repairs " << output.repairs.size() << " (run "
              << summary.at("run_repairs").dump() << "), climb queries "
              << ClimbStats::n_queries << ", " << wall_s << "s\n";
    return 0;
}

}  // namespace

// NOLINTNEXTLINE(bugprone-exception-escape): as in main.cpp.
int main(int argc, const char* const argv[]) {
    if (!has_program_name(argc, argv)) {
        return 1;
    }
    if (handle_info_flags(argc, argv, print_usage)) {
        return 0;
    }
    const std::optional<std::string> unknown =
        find_unknown_arg(argc, argv, {"--run", "--orders", "--out", "--spec"},
                         {"--version", "-h", "--help"});
    if (unknown.has_value()) {
        std::cerr << "Unknown argument: '" << *unknown << "'\n";
        return 1;
    }
    const std::optional<std::string> run_dir =
        parse_string_arg(argc, argv, "--run");
    const std::optional<std::string> orders_arg =
        parse_string_arg(argc, argv, "--orders");
    const std::optional<std::string> out =
        parse_string_arg(argc, argv, "--out");
    if (!run_dir.has_value() || !orders_arg.has_value() || !out.has_value()) {
        print_usage(argv[0]);
        return 1;
    }
    const std::optional<std::size_t> orders = parse_seed(*orders_arg);
    if (!orders.has_value() || *orders == 0) {
        std::cerr << "Invalid --orders value: '" << *orders_arg
                  << "' (expected a positive integer)\n";
        return 1;
    }
    try {
        fs::create_directories(*out);
        return replay(*run_dir, *orders, *out,
                      parse_string_arg(argc, argv, "--spec"));
    } catch (const std::exception& exc) {
        std::cerr << "fatal: " << exc.what() << "\n";
        return 1;
    }
}
