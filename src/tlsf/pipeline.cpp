#include "tlsf/pipeline.hpp"

#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "dashboard.hpp"
#include "driver_support.hpp"
#include "filter/streaming_maximal.hpp"
#include "fitness/function.hpp"
#include "genetic/generation.hpp"
#include "genetic/generation_loop.hpp"
#include "genetic/output_gate.hpp"
#include "genetic/random_source.hpp"
#include "repair_modes.hpp"
#include "repair_output.hpp"
#include "repair_steps.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/fitness.hpp"
#include "tlsf/parser.hpp"
#include "tlsf/specification.hpp"

namespace tlsf {

std::optional<std::vector<FilterRunStats>> run_repair(
    const std::string& input_path, const std::string& output_dir,
    const Config& cfg, const RandomSource& random_source,
    SearchBudget& budget) {
    const std::optional<std::string> text = read_file_contents(input_path);
    if (!text.has_value()) {
        std::cerr << "cannot read input file: " << input_path << "\n";
        return std::nullopt;
    }
    Specification original;
    try {
        original = parse(*text);
    } catch (const std::invalid_argument& exc) {
        std::cerr << "parse error: " << exc.what() << "\n";
        return std::nullopt;
    }
    if (!screen_original(
            original, input_path, original.m_inputs, original.m_outputs,
            [&original] {
                return global_real_checker().check_realizability_ltl(
                    original.to_ltl(), original.m_inputs, original.m_outputs,
                    tlsf::specification_sides(original));
            },
            cfg)) {
        return std::nullopt;
    }

    const AggregateWeightedFitnessFunctionT<Specification> fitness =
        tlsf_get_fitness_function(original, cfg);
    DashboardWriter dashboard(output_dir, cfg.dashboard);
    // Built from the original spec purely to name the stages.
    announce_run(dashboard, input_path, output_dir, cfg, random_source.seed(),
                 objective_names(fitness),
                 generation_stage_names(
                     get_filter_functions(original, global_sat_checker())));

    // Null unless the implication filter is on, and then the final screens
    // run while the search does; destroyed unfinished if anything below
    // throws.
    const std::unique_ptr<StreamingMaximalFilter<Specification>> stream =
        make_maximal_stream(original, cfg, output_dir);
    SearchOutput<Specification> output =
        internal::run_monolithic(original, cfg, random_source, fitness,
                                 dashboard, output_dir, budget, stream.get());
    internal::write_survivors(output.repairs, fitness, output_dir);
    // budget.generations() rather than cfg.generations: the parameter is the
    // count the run actually completed, and a budget can end the run short of
    // its configured total.
    dashboard.run_end(budget.generations(), output.n_realizable,
                      output.repairs.size(), budget.elapsed_s());
    print_repair_summary(output.n_realizable, output.repairs.size(),
                         cfg.run_implication_filter, output_dir);
    if (cfg.report_repair_summary) {
        internal::print_repair_changes(original, output.repairs);
    }
    return std::move(output.filter_stats);
}

}  // namespace tlsf
