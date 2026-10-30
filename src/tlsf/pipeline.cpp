#include "tlsf/pipeline.hpp"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.hpp"
#include "dashboard.hpp"
#include "evolve.hpp"
#include "filter/correctness.hpp"
#include "filter/streaming_maximal.hpp"
#include "fitness/function.hpp"
#include "genetic/generation.hpp"
#include "genetic/output_gate.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "repair_modes.hpp"
#include "repair_output.hpp"
#include "runner/atom_names.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "survivors.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/fitness.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace tlsf {

namespace {

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

}  // namespace

int run_repair(const std::string& input_path, const std::string& output_dir,
               const Config& cfg, const RandomSource& random_source,
               SearchBudget& budget) {
    const std::optional<std::string> text = read_file(input_path);
    if (!text.has_value()) {
        std::cerr << "cannot read input file: " << input_path << "\n";
        return 1;
    }
    Specification original;
    try {
        original = parse(*text);
    } catch (const std::invalid_argument& exc) {
        std::cerr << "parse error: " << exc.what() << "\n";
        return 1;
    }

    // Before the screens, because an unsafe name makes every realizability
    // verdict below meaningless rather than merely suspect.
    if (const std::optional<std::string> unsafe =
            runner::first_unsafe_atom_name(original.m_inputs,
                                           original.m_outputs)) {
        std::cerr << "fatal: " << input_path << ": " << *unsafe << "\n";
        return 1;
    }

    // As on the FRETISH path: the seed population is copies of this
    // specification and no filter screens them, so the run says at the start
    // what the gate would otherwise only reveal at the end.
    std::cerr << screen_input(
        original, correctness_checks<Specification>(global_sat_checker(),
                                                    global_real_checker()));
    // After the input screen and before anything is scored: the query is
    // memoised, so this is the call the first scoring pass was about to make
    // anyway, timed.
    std::cerr << ltlsynt_budget_screen(
        [&original] {
            return global_real_checker().check_realizability_ltl(
                original.to_ltl(), original.m_inputs, original.m_outputs,
                tlsf::specification_sides(original));
        },
        cfg.ltlsynt_timeout);

    const std::optional<std::size_t> maybe_seed = random_source.seed();
    if (maybe_seed.has_value()) {
        std::cout << "Seed: " << *maybe_seed << "\n";
    }

    const AggregateWeightedFitnessFunctionT<Specification> fitness =
        tlsf_get_fitness_function(original, cfg);

    internal::DashboardProgress progress;
    DashboardWriter dashboard(output_dir, cfg.dashboard);
    progress.writer = &dashboard;
    for (const WeightedFitnessFunctionT<Specification>& objective : fitness) {
        progress.objective_names.push_back(objective.name);
    }
    // Built from the original spec purely to name the stages.
    dashboard.run_start(input_path, cfg.generations, cfg.population_size,
                        maybe_seed.value_or(0), progress.objective_names,
                        generation_stage_names(get_filter_functions(
                            original, global_sat_checker())));
    const auto wall_start = std::chrono::steady_clock::now();
    if (!dashboard.write_page().empty()) {
        std::cout << "Progress: " << dashboard.path() << "\n"
                  << "  view with: python3 -m http.server -d " << output_dir
                  << " 8000\n";
    }

    // Null unless the implication filter is on, and then the final screens
    // run while the search does; destroyed unfinished if anything below
    // throws.
    const std::unique_ptr<StreamingMaximalFilter<Specification>> stream =
        make_maximal_stream(original, cfg, output_dir);
    std::vector<Scored<Specification>> survivors =
        internal::run_monolithic(original, cfg, random_source, fitness,
                                 progress, output_dir, budget, stream.get());
    const std::size_t n_realizable = survivors.size();
    if (stream) {
        survivors = internal::finish_maximal_stream(survivors, *stream);
    }
    internal::write_survivors(survivors, fitness, output_dir);
    // budget.generations() rather than cfg.generations: the parameter is the
    // count the run actually completed, and a budget can end the run short of
    // its configured total.
    dashboard.run_end(budget.generations(), n_realizable, survivors.size(),
                      std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - wall_start)
                          .count());
    internal::print_repair_summary(n_realizable, survivors.size(),
                                   cfg.run_implication_filter, output_dir);
    if (cfg.report_repair_summary) {
        internal::print_repair_changes(original, survivors);
    }
    return 0;
}

}  // namespace tlsf
