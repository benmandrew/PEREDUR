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

// The batch route to the final screen, taken wherever the stream is not.
void apply_final_filters(std::vector<Scored<Specification>>& survivors,
                         const Specification& original, const Config& cfg) {
    // The final filter asks whole-specification implications, `(A) & !(B)`
    // over the lowered specifications, which the search's checker is not
    // tuned for. Measured on humanoid-531 at 6 generations of 100: this stage
    // was 264s of a 312s run, and 96.7% of its solver time (4955s of 5122s
    // aggregate) was the `ltlfilt --simplify` pass, which settled nothing the
    // `--satisfiable` decision could not; 316 further calls hit the search's
    // ltlfilt budget. SPOT's 500ms budget is sized for single-requirement
    // queries, and an `ExpectUnsat` query it leaves undecided is never
    // escalated, so it reads as "keep both". A checker of the stage's own
    // keeps the search path byte-identical. It starts cold, but no query it
    // is asked was asked during the search, so there is nothing to inherit.
    SatisfiabilityChecker final_checker;
    final_checker.set_timeout(cfg.black_timeout);
    final_checker.set_simplify(false);
    final_checker.set_spot_budget(cfg.black_timeout);
    if (cfg.run_implication_filter && survivors.size() > 1) {
        survivors =
            internal::keep_maximal(survivors, original, cfg, final_checker);
    }
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
    // what the gate would otherwise only reveal at the end. MUC repair needs no
    // separate screen -- extraction leaves the environment side untouched, and
    // well-separation reads only that side.
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
    // Built from the original spec purely to name the stages. MUC repair
    // rebuilds these per core, but the filter set, and so the roster, is the
    // same whichever sub-specification is being evolved.
    dashboard.run_start(
        input_path, cfg.generations, cfg.population_size,
        maybe_seed.value_or(0), progress.objective_names,
        generation_stage_names(internal::build_per_gen_filters(original)));
    const auto wall_start = std::chrono::steady_clock::now();
    if (!dashboard.write_page().empty()) {
        std::cout << "Progress: " << dashboard.path() << "\n"
                  << "  view with: python3 -m http.server -d " << output_dir
                  << " 8000\n";
    }

    // Null unless the key is on, and then the final screens run while the
    // search does; destroyed unfinished if anything below throws.
    const std::unique_ptr<StreamingMaximalFilter<Specification>> stream =
        make_maximal_stream(original, cfg, output_dir);
    std::vector<Scored<Specification>> survivors =
        cfg.repair_mode == RepairMode::Muc
            ? internal::run_muc(original, cfg, random_source, fitness, progress,
                                output_dir, budget, stream.get())
            : internal::run_monolithic(original, cfg, random_source, fitness,
                                       progress, output_dir, budget,
                                       stream.get());
    const std::size_t n_realizable = survivors.size();
    if (stream) {
        survivors = internal::finish_maximal_stream(survivors, *stream);
    } else {
        apply_final_filters(survivors, original, cfg);
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
    return 0;
}

}  // namespace tlsf
