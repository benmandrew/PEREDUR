#include "evolution.hpp"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "filter/implication_check.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/generation_loop.hpp"
#include "genetic/output_gate.hpp"
#include "runner/black.hpp"
#include "serialisation.hpp"
#include "status_line.hpp"

namespace {

// The committed line of the implication filter, the same on both routes to it
// so a log reads alike whichever ran. @p elapsed is the time the run waited on
// the filter: the whole sweep in batch, the last batch when streamed.
void print_implication_summary(double elapsed) {
    if (stdout_is_tty()) {
        std::cout << "\r\033[K";
    }
    std::cout << "Implication filter: 100%  " << std::fixed
              << std::setprecision(2) << elapsed << "s  ("
              << ImplicationFilterStats::n_comparisons << " cmp, "
              << ImplicationFilterStats::n_skipped << " skip, "
              << ImplicationFilterStats::n_duplicates << " dup, "
              << ImplicationFilterStats::n_equivalent_collapsed << " equiv, "
              << ImplicationFilterStats::n_timeouts << " timeout, "
              << ImplicationFilterStats::n_fingerprint_refuted << " refuted)\n";
}

}  // namespace

std::vector<std::string> fitness_objective_names(
    const AggregateWeightedFitnessFunction& fitness_function) {
    std::vector<std::string> names;
    for (const WeightedFitnessFunction& objective : fitness_function) {
        names.push_back(objective.name);
    }
    return names;
}

std::vector<ScoredSpecification> original_population(
    Specification& original_spec,
    const AggregateWeightedFitnessFunction& fitness_function,
    std::size_t population_size) {
    std::vector<ScoredSpecification> population;
    population.reserve(population_size);
    auto [objectives, fitness] =
        fitness_function.objectives_and_fitness(original_spec);
    for (std::size_t i = 0; i < population_size; ++i) {
        ScoredSpecification scored;
        scored.specification = original_spec;
        scored.fitness = fitness;
        scored.objectives = objectives;
        population.push_back(std::move(scored));
    }
    return population;
}

EvolutionResult run_evolution(
    const Config& cfg, std::vector<ScoredSpecification> population,
    const AggregateWeightedFitnessFunction& fitness_function,
    const std::vector<FilterFunction>& filter_functions,
    RandomSource& random_source, DashboardWriter& dashboard,
    const std::string& output_dir, SearchBudget& budget,
    RepairAccumulator<Specification>::Sink sink) {
    // The same serialiser repair_N.json goes through, so an accumulated file
    // is a specification document and nothing else -- no fitness record, since
    // these are gate-passing candidates rather than the run's filtered output.
    RepairAccumulator<Specification> accumulator(
        cfg.accumulate_repairs,
        AccumulatedRepairWriter<Specification>(
            output_dir, ".json",
            [](const Specification& spec) {
                const nlohmann::json jobj = spec;
                return jobj.dump(2) + "\n";
            },
            [&budget] { return budget.elapsed_s(); }),
        std::move(sink));
    const std::vector<std::string> objective_names =
        fitness_objective_names(fitness_function);
    std::vector<FilterRunStats> filter_stats =
        empty_filter_stats(filter_functions);
    StatusLine status;
    const std::size_t col_gen = status.add("gen");
    // Transient: within-generation progress is the reason the line updates at
    // all, and reads 100% on every committed line by construction.
    const std::size_t col_pct = status.add("%", true);
    const std::size_t col_time = status.add("time");
    const std::size_t col_best = status.add("best");
    // Only where the sweep below runs: a column that reads the same number
    // every generation because nothing measured it is worse than no column.
    //
    // A flag and a plain index rather than an optional index: gcc 11 at -O3
    // reports the optional's payload as maybe-uninitialized inside the
    // has_value() guard below, which -Werror turns into a failed build on the
    // lab hosts.
    const bool show_real = accumulator.enabled();
    const std::size_t col_real = show_real ? status.add("real") : 0;

    auto format_elapsed = [](double secs) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << secs << "s";
        return oss.str();
    };

    const GenerationSizes sizes = generation_sizes(cfg, population.size());
    const std::string total_str = std::to_string(cfg.generations);
    for (std::size_t gen_idx = 0; gen_idx < cfg.generations; ++gen_idx) {
        // Checked before the generation as well as between offspring, matching
        // checkTermination() at the head of AuRUS's evolve(count) loop. Without
        // it a spent budget still pays for a generation of filtering, scoring
        // and selection over an offspring set breeding left empty.
        if (budget.active() && budget.exhausted()) {
            break;
        }
        const auto start = std::chrono::steady_clock::now();
        const std::string gen_str =
            std::to_string(gen_idx + 1) + "/" + total_str;
        status.set(col_gen, gen_str);

        auto on_progress = [&](std::size_t done, std::size_t total) {
            const double elapsed = std::chrono::duration<double>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();
            status.set(col_pct, std::to_string(done * 100 / total) + "%");
            status.set(col_time, format_elapsed(elapsed));
            status.render();
        };

        // The stage index is assigned here rather than by the pipeline: the
        // observer sees stages in order, and the dashboard needs their order
        // without the pipeline having to number them.
        std::size_t stage_index = 0;
        auto on_stage = [&dashboard, &stage_index,
                         gen = gen_idx + 1](const StageObservation& obs) {
            dashboard.stage(gen, stage_index++, obs);
        };

        population = evolve_generation(
            cfg, population, sizes.selection, sizes.elitism, fitness_function,
            filter_functions, random_source, on_progress, on_stage, &budget);
        budget.count_generation();

        fold_filter_stats(filter_functions, filter_stats);

        const double elapsed = std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        status.set(col_time, format_elapsed(elapsed));

        // Computed before the status line rather than after so both report
        // the same best.
        const FitnessSummary summary = summarise_fitness(population);
        if (!population.empty()) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3) << summary.best;
            status.set(col_best, oss.str());
        }
        const std::optional<std::size_t> n_real =
            accumulate_gate_passing(population, cfg, gen_idx + 1, accumulator);
        // n_real and show_real are both governed by accumulator.enabled(), so
        // either test decides the other; the pair is what lets the checker
        // see it.
        if (show_real && n_real.has_value()) {
            status.set(col_real, std::to_string(*n_real));
        }
        status.finish();

        dashboard.generation(
            gen_idx + 1, elapsed, summary.best, summary.mean,
            mean_objectives(objective_names, objectives_of(population)), n_real,
            population.size());
    }
    return {std::move(population), std::move(filter_stats),
            accumulator.specifications()};
}

std::vector<Specification> collect_realizable_specifications(
    const Config& cfg, const std::vector<ScoredSpecification>& population) {
    // The per-generation filter only screens offspring during evolution, so a
    // vacuous result from the final generation would otherwise never be
    // re-screened before being reported here. Elites bypass the offspring
    // filters entirely, so one can reach the output unscreened either way.
    const std::vector<char> keep = gate_verdicts(population, cfg);
    std::vector<Specification> realizable_vec;
    for (std::size_t idx = 0; idx < population.size(); ++idx) {
        if (keep[idx] != 0) {
            realizable_vec.push_back(population[idx].specification);
        }
    }
    return realizable_vec;
}

std::pair<std::vector<Specification>, std::vector<FilterRunStats>>
filter_maximal_specifications(
    const Config& cfg, const Specification& original,
    const std::vector<Specification>& realizable_vec) {
    const auto impl_start = std::chrono::steady_clock::now();
    auto on_impl_progress = [&impl_start](std::size_t done, std::size_t total) {
        // Off a terminal this frame is never overwritten, so it would land in
        // the log once per comparison; the committed line below says the same
        // thing once.
        if (!stdout_is_tty()) {
            return;
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          impl_start)
                .count();
        std::cout << "\r\033[KImplication filter: " << std::setw(3)
                  << (done * 100 / total) << "%  " << std::fixed
                  << std::setprecision(2) << elapsed << "s  ("
                  << ImplicationFilterStats::n_comparisons << " cmp, "
                  << ImplicationFilterStats::n_skipped << " skip, "
                  << ImplicationFilterStats::n_duplicates << " dup, "
                  << ImplicationFilterStats::n_equivalent_collapsed
                  << " equiv, " << ImplicationFilterStats::n_timeouts
                  << " timeout, "
                  << ImplicationFilterStats::n_fingerprint_refuted
                  << " refuted)" << std::flush;
    };
    // A checker of the stage's own, as on the TLSF path (tlsf/pipeline.cpp)
    // and in `maximal` and `compare`. Both final filters ask implications
    // between whole specifications, which is not the shape the search's
    // checker is tuned for, and the FRETISH path was left on the search's
    // settings only because its per-requirement queries had not been measured.
    // Measured at 40 generations of 1000: the `ltlfilt --simplify` pass is
    // 59-61% of every ltlfilt exec a run makes -- 37,171 of 61,100 on fsm,
    // 28,573 of 48,093 on takeoff -- and on takeoff 122 of those calls spent
    // the whole 10s ltlfilt budget and returned the formula unchanged, at
    // least 1,220s of that run's 2,523s of ltlfilt CPU. The 500ms SPOT budget
    // costs output as well as time: takeoff declined 109 escalations, each an
    // ExpectUnsat query left undecided and so read as "does not imply", each
    // keeping a repair the filter had grounds to drop. It starts cold, but
    // these queries are between survivors rather than about one requirement,
    // so there is nothing in the search's cache to inherit.
    SatisfiabilityChecker final_checker;
    final_checker.set_timeout(cfg.black_timeout);
    final_checker.set_simplify(false);
    final_checker.set_spot_budget(cfg.black_timeout);
    const std::vector<FilterFunction> filters = get_final_filter_functions(
        cfg, original, final_checker, on_impl_progress);
    const std::vector<Specification> result =
        filter_population(realizable_vec, filters);
    // A final filter drops repairs from the output, so it owes the same
    // accounting as a per-generation one; the report is built from the
    // per-generation list alone and would otherwise not mention these.
    std::vector<FilterRunStats> stats;
    stats.reserve(filters.size());
    for (const FilterFunction& flt : filters) {
        stats.push_back({"final/" + flt.name(), flt.n_in(), flt.n_out()});
    }
    if (cfg.run_implication_filter) {
        print_implication_summary(
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          impl_start)
                .count());
    }
    return {result, std::move(stats)};
}

std::pair<std::vector<Specification>, std::vector<FilterRunStats>>
finish_maximal_stream(StreamingMaximalFilter<Specification>& stream,
                      const std::vector<Specification>& realizable_vec) {
    const auto drain_start = std::chrono::steady_clock::now();
    // Every accumulated specification is already in the stream, and the
    // stream deduplicates, so this adds only what the final population's own
    // collection found that no generation's sweep had accumulated.
    for (const Specification& spec : realizable_vec) {
        stream.push(spec, {});
    }
    const std::vector<Specification> streamed = stream.finish();
    // The stream returns push order, accumulated specifications first. The
    // batch filters return @p realizable_vec's, and repair_N is numbered after
    // a sort that keeps the order of fitness ties, so the order is restored.
    const std::unordered_set<Specification> kept(streamed.begin(),
                                                 streamed.end());
    std::unordered_set<Specification> emitted;
    std::vector<Specification> maximal;
    maximal.reserve(streamed.size());
    for (const Specification& spec : realizable_vec) {
        if (kept.count(spec) != 0 && emitted.insert(spec).second) {
            maximal.push_back(spec);
        }
    }
    const MaximalStreamCounts& counts = stream.counts();
    std::vector<FilterRunStats> stats;
    stats.push_back({"final/dedup", realizable_vec.size(), counts.n_distinct});
    stats.push_back({"final/implication", counts.n_distinct, maximal.size()});
    print_implication_summary(
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      drain_start)
            .count());
    return {std::move(maximal), std::move(stats)};
}

void write_specifications(
    const std::vector<ScoredSpecification>& scored,
    const AggregateWeightedFitnessFunction& fitness_function,
    const std::string& output_dir, const std::string& prefix) {
    for (std::size_t i = 0; i < scored.size(); ++i) {
        std::string path = output_dir;
        path += "/";
        path += prefix;
        path += std::to_string(i);
        path += ".json";
        std::ofstream file(path);
        if (!file) {
            throw std::runtime_error("cannot open output file: " + path);
        }
        serialisation::FitnessRecord record;
        record.total = scored[i].fitness;
        for (const WeightedFitnessFunction& wff : fitness_function) {
            record.components.push_back(
                {wff.name, wff.function(scored[i].specification), wff.weight});
        }
        const serialisation::ScoredSpecification ssc{scored[i].specification,
                                                     record};
        nlohmann::json jobj = ssc;
        file << jobj.dump(2) << "\n";
    }
}
