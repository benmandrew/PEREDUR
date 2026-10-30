#include "driver.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "crash/crash_handler.hpp"
#include "evolution.hpp"
#include "filter/streaming_maximal.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/filter_report.hpp"
#include "genetic/generation.hpp"
#include "genetic/generation_loop.hpp"
#include "genetic/output_gate.hpp"
#include "genetic/random_source.hpp"
#include "manifest.hpp"
#include "profile.hpp"
#include "repair_steps.hpp"
#include "reports.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "serialisation.hpp"
#include "tlsf/pipeline.hpp"

namespace {

std::string format_crash_metadata(std::size_t seed,
                                  const std::string& input_path,
                                  const Config& cfg) {
    std::ostringstream out;
    out << "Input:            " << input_path << "\n";
    out << "Config:\n";
    out << "  Seed:           " << seed << "\n";
    out << "  Generations:    " << cfg.generations << "\n";
    out << "  Population:     " << cfg.population_size << "\n";
    out << "  Crossover rate: " << cfg.crossover_rate << "\n";
    out << "  Mutation rate:  " << cfg.mutation_rate << "\n";
    out << "  p_trigger:      " << cfg.p_trigger << "\n";
    out << "  p_response:     " << cfg.p_response << "\n";
    out << "  p_timing:       " << cfg.p_timing << "\n";
    out << "  Weight syn:     " << cfg.fitness_weight_syntactic << "\n";
    out << "  Weight sem:     " << cfg.fitness_weight_semantic << "\n";
    out << "  Weight status:  " << cfg.fitness_weight_status;
    return out.str();
}

// Warned rather than rejected: campaign configs and example-config.toml set
// every key, so a shared config always carries the other path's keys, and only
// a changed value says the config asked for something this run will not do.
void warn_ignored_keys(const Config& cfg, RepairInput input) {
    const char* const reader = input == RepairInput::Tlsf ? "FRETISH" : "TLSF";
    const char* const run = input == RepairInput::Tlsf ? "TLSF" : "FRETISH";
    for (const std::string& key : ignored_non_default_keys(cfg, input)) {
        std::cerr << "warning: config key " << key << " is read only on the "
                  << reader << " path; this " << run << " run ignores it\n";
    }
}

// Warns about the other path's config keys, fixes the seed, and registers the
// crash metadata: what both runs do before reading their input. Returns the
// seed the run uses, drawn from the random device when @p seed is absent.
std::size_t begin_run(const Config& cfg, const std::string& input_path,
                      const std::optional<std::size_t>& seed,
                      RepairInput input) {
    warn_ignored_keys(cfg, input);
    std::size_t effective_seed = 0;
    if (seed.has_value()) {
        effective_seed = *seed;
    } else {
        std::random_device rng_dev;
        effective_seed = static_cast<std::size_t>(rng_dev());
    }
    register_crash_metadata(
        format_crash_metadata(effective_seed, input_path, cfg));
    return effective_seed;
}

// The reports both runs end with, and then the manifest and the closing
// `Done in` line. Every figure is read from @p budget's clock, which has the
// same origin as the manifest's wall_s.
void end_run(const Config& cfg, const std::vector<FilterRunStats>& filter_stats,
             const std::string& input_path, const std::string& output_dir,
             std::size_t seed, const SearchBudget& budget, RepairInput input) {
    print_scoring_report();
    if (cfg.report_diagnostics) {
        print_filter_report(filter_stats);
        print_diagnostics_report();
    }
    if (cfg.report_cpu_timing) {
        print_cpu_report(budget.elapsed_s());
    }
    // The per-tool rows say how long each tool took; the scope profile says
    // where inside a call that went. No-op unless PEREDUR_PROFILE is set, so
    // it is not tied to --diagnostics.
    profile::report_if_enabled();
    // After the reports, so the per-tool counts it records are the run's
    // totals.
    write_run_manifest(output_dir, input_path, seed, cfg, budget.elapsed_s(),
                       budget, input);
    // Last, so the figure covers the same work on both paths.
    std::cout << "Done in " << std::fixed << std::setprecision(2)
              << budget.elapsed_s() << "s\n";
}

}  // namespace

int run_tlsf_repair(const Config& cfg, const std::string& input_path,
                    const std::string& output_dir,
                    const std::optional<std::size_t>& seed) {
    // Measured from process start, the same origin as the run's reported
    // wall_s: a deadline the manifest's own clock disagrees with would put a
    // run over its cap and record it under.
    SearchBudget budget(cfg, std::chrono::steady_clock::now());
    const std::size_t effective_seed =
        begin_run(cfg, input_path, seed, RepairInput::Tlsf);
    const RandomSource random_source =
        make_random_source_from_seed(effective_seed);
    try {
        const std::optional<std::vector<FilterRunStats>> filter_stats =
            tlsf::run_repair(input_path, output_dir, cfg, random_source,
                             budget);
        if (!filter_stats.has_value()) {
            return 1;
        }
        end_run(cfg, *filter_stats, input_path, output_dir, effective_seed,
                budget, RepairInput::Tlsf);
    } catch (const std::exception& exc) {
        std::cerr << "fatal: " << exc.what() << "\n";
        return 1;
    }
    return 0;
}

int run_fretish_repair(const Config& cfg, const std::string& input_path,
                       const std::string& output_dir,
                       const std::optional<std::size_t>& seed) {
    const std::size_t effective_seed =
        begin_run(cfg, input_path, seed, RepairInput::Fretish);
    const RandomSource random_source =
        make_random_source_from_seed(effective_seed);
    Specification original_spec;
    try {
        original_spec = load_specification(input_path);
    } catch (const std::exception& exc) {
        std::cerr << exc.what() << "\n";
        return 1;
    }
    if (!screen_original(
            original_spec, input_path, environment_signals(original_spec),
            original_spec.m_out_atoms,
            [&original_spec] {
                return global_real_checker().check_realizability(original_spec);
            },
            cfg)) {
        return 1;
    }
    AggregateWeightedFitnessFunction fitness_function =
        get_fitness_function(original_spec, cfg);
    const std::vector<FilterFunction> filter_functions =
        get_filter_functions(original_spec, global_sat_checker());
    std::vector<ScoredSpecification> population = original_population(
        original_spec, fitness_function, cfg.population_size);

    announce_seed(effective_seed);

    // Started here rather than at process start, matching AuRUS: its
    // startRunningTime is set on entry to evolve(count), after the initial
    // population has been built and scored, so neither tool's deadline is
    // charged for loading its input.
    SearchBudget budget(cfg, std::chrono::steady_clock::now());
    try {
        // Null unless the implication filter is on, and then the final screens
        // run while the search does; destroyed unfinished if anything below
        // throws.
        const std::unique_ptr<StreamingMaximalFilter<Specification>> stream =
            make_maximal_stream(original_spec, cfg, output_dir);
        RepairAccumulator<Specification> accumulator =
            make_repair_accumulator<Specification>(
                output_dir, ".json",
                [](const Specification& spec) {
                    const nlohmann::json jobj = spec;
                    return jobj.dump(2) + "\n";
                },
                budget, stream.get());
        SearchResult<Specification> evolved = run_generations(
            cfg, std::move(population), fitness_function, filter_functions,
            random_source, accumulator, budget);
        const SearchOutput<Specification> output =
            finish_search(cfg, std::move(evolved), accumulator.specifications(),
                          fitness_function, stream.get());
        write_specifications(output.repairs, fitness_function, output_dir);
        print_repair_summary(output.n_realizable, output.repairs.size(),
                             cfg.run_implication_filter, output_dir);
        if (cfg.report_repair_summary) {
            print_repair_changes(original_spec, output.repairs);
        }
        end_run(cfg, output.filter_stats, input_path, output_dir,
                effective_seed, budget, RepairInput::Fretish);
    } catch (const std::exception& exc) {
        std::cerr << "fatal: " << exc.what() << "\n";
        return 1;
    }
    return 0;
}
