#include "evolution.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "bounded_async.hpp"
#include "filter/correctness.hpp"
#include "filter/implication.hpp"
#include "filter/implication_check.hpp"
#include "fingerprint/prefilter.hpp"
#include "fitness/status.hpp"
#include "genetic/accumulator.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "serialisation.hpp"
#include "status_line.hpp"
#include "thread_pool.hpp"

namespace {

// The correctness checks, built once. Their predicates capture the global
// checkers, which outlive every caller.
const std::vector<CorrectnessCheck>& gate_checks() {
    static const std::vector<CorrectnessCheck> checks =
        correctness_checks(global_sat_checker(), global_real_checker());
    return checks;
}

// A specification counts as a realizable repair only if it is realizable and
// passes every correctness check. Elites and the seed population reach this
// unscreened, and the per-generation flags can turn any of those checks off
// outright, so both the live "real" counter and the final collection apply the
// whole table here, unconditionally.
//
// The grading is the run's, as it is on the TLSF path (is_tlsf_repair forwards
// cfg to tlsf_status): hard-coding Tiered here made a run configured
// `status_grading = "aurus"` score its search on the six-level ladder and then
// judge its output on the three-point one. The admitted set is the same either
// way, gate_checks() below applying the whole table whatever the scale folded
// in, so what the mismatch cost was a cold well-separation query per survivor
// under a grading whose search never asked one. No admission order is passed,
// again matching the TLSF gate: the order changes which queries the MRS walk
// memoises, never whether the top tier is reached.
//
// Status leads. Most of the final population is unrealizable -- that is why the
// collection drops most of it -- and status is a scored objective, so its
// verdict is already memoised for anything the last generation scored, whereas
// the checks behind it are only warm where their per-generation stage ran.
// Asking the warm question first short-circuits the common case for free.
bool is_realizable_repair(const Specification& spec, const Config& cfg) {
    return specification_status(spec, global_sat_checker(),
                                global_real_checker(),
                                cfg.status_grading) == 1.0 &&
           !first_failing_check(spec, gate_checks()).has_value();
}

// The gate asked of every candidate in the population, run only where there
// is somewhere to put what it admits. Nothing else in a FRETISH generation
// asks the gate, so with the accumulator off this sweep is a solver call per
// candidate that the run would not otherwise make; the status line's "real"
// column was the whole argument for paying it. The TLSF twin
// (`accumulate_gate_passing`, src/tlsf/evolve.cpp) has been gated this way
// since it gained the accumulator. Empty where it did not run.
std::optional<std::size_t> accumulate_gate_passing(
    const std::vector<ScoredSpecification>& population, const Config& cfg,
    std::size_t generation, RepairAccumulator<Specification>& accumulator) {
    if (!accumulator.enabled()) {
        return std::nullopt;
    }
    std::size_t n_gate_passing = 0;
    for (const ScoredSpecification& cand : population) {
        if (is_realizable_repair(cand.specification, cfg)) {
            ++n_gate_passing;
            accumulator.insert(cand.specification, generation);
        }
    }
    return n_gate_passing;
}

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
    std::vector<FilterRunStats> filter_stats;
    filter_stats.reserve(filter_functions.size());
    for (const FilterFunction& flt : filter_functions) {
        filter_stats.push_back({flt.name(), 0, 0});
    }
    StatusLine status;
    const std::size_t col_gen = status.add("gen");
    // Transient: within-generation progress is the reason the line updates at
    // all, and reads 100% on every committed line by construction.
    const std::size_t col_pct = status.add("%", true);
    const std::size_t col_time = status.add("time");
    const std::size_t col_best = status.add("best");
    // Only where the sweep below runs: a column that reads the same number
    // every generation because nothing measured it is worse than no column.
    const std::optional<std::size_t> col_real =
        accumulator.enabled() ? std::optional<std::size_t>(status.add("real"))
                              : std::nullopt;

    auto format_elapsed = [](double secs) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << secs << "s";
        return oss.str();
    };

    const std::size_t pop_size = population.size();
    const std::size_t selection_size = std::max(
        std::size_t{1}, static_cast<std::size_t>(static_cast<double>(pop_size) *
                                                 cfg.selection_rate));
    // Elites carry over verbatim; unlike selection there is no floor of 1, so
    // a small population or rate can legitimately yield no elites. The config
    // guarantees elitism_rate < selection_rate, keeping this below
    // selection_size.
    const auto elitism_size = static_cast<std::size_t>(
        static_cast<double>(pop_size) * cfg.elitism_rate);
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
            cfg, population, selection_size, elitism_size, fitness_function,
            filter_functions, random_source, on_progress, on_stage, &budget);
        budget.count_generation();

        // Each active filter copy carries this generation's in/out sizes; fold
        // them into the running per-filter totals reported at the end.
        for (const FilterFunction& flt : filter_functions) {
            for (FilterRunStats& stat : filter_stats) {
                if (stat.name == flt.name()) {
                    stat.total_in += flt.n_in();
                    stat.total_out += flt.n_out();
                    break;
                }
            }
        }

        const double elapsed = std::chrono::duration<double>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        status.set(col_time, format_elapsed(elapsed));

        // The maximum, not population[0]: under NSGA-II the population is
        // ordered by front rank and crowding distance, so the first individual
        // need not hold the highest weighted scalar. Reporting it as "best"
        // would put a number on the dashboard below the mean beside it -- and
        // put one on the status line that falls between generations while the
        // search is still improving, which reads as the search going backwards.
        // Computed before the status line rather than after so both report it.
        double fitness_total = 0.0;
        double fitness_best = 0.0;
        std::vector<std::vector<double>> objectives;
        objectives.reserve(population.size());
        for (const ScoredSpecification& cand : population) {
            fitness_total += cand.fitness;
            fitness_best = std::max(fitness_best, cand.fitness);
            objectives.push_back(cand.objectives);
        }
        if (!population.empty()) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3) << fitness_best;
            status.set(col_best, oss.str());
        }
        const std::optional<std::size_t> n_real =
            accumulate_gate_passing(population, cfg, gen_idx + 1, accumulator);
        // Both optionals are governed by accumulator.enabled(), so either
        // test decides the other; the pair is what lets the checker see it.
        if (col_real.has_value() && n_real.has_value()) {
            status.set(*col_real, std::to_string(*n_real));
        }
        status.finish();

        dashboard.generation(
            gen_idx + 1, elapsed, fitness_best,
            population.empty()
                ? 0.0
                : fitness_total / static_cast<double>(population.size()),
            mean_objectives(objective_names, objectives), n_real,
            population.size());
    }
    return {std::move(population), std::move(filter_stats),
            accumulator.specifications()};
}

std::vector<Specification> collect_realizable_specifications(
    const Config& cfg, const std::vector<ScoredSpecification>& population) {
    // Each check is a `black` and an `ltlsynt` query, and the whole final
    // population is checked, so a serial sweep here costs a subprocess per
    // distinct candidate -- the more diverse the population, the worse. Results
    // are collected by index and the survivors rebuilt in population order, so
    // the output matches a serial sweep exactly.
    const std::size_t max_in_flight = dispatch_window();
    std::vector<char> keep(population.size(), 0);
    // The per-generation filter only screens offspring during evolution, so a
    // vacuous result from the final generation would otherwise never be
    // re-screened before being reported here. Elites bypass the offspring
    // filters entirely, so one can reach the output unscreened either way.
    if (max_in_flight <= 1) {
        for (std::size_t idx = 0; idx < population.size(); ++idx) {
            keep[idx] = is_realizable_repair(population[idx].specification, cfg)
                            ? 1
                            : 0;
        }
    } else {
        run_bounded_async(
            population.size(), max_in_flight,
            [&population, &cfg](std::size_t idx) {
                return [&spec = population[idx].specification, &cfg] {
                    return is_realizable_repair(spec, cfg);
                };
            },
            [&keep](std::size_t idx, bool realizable) {
                keep[idx] = realizable ? 1 : 0;
            });
    }
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
    // The 500ms SPOT budget costs output as well as time, measured at 40
    // generations of 1000: takeoff declined 109 escalations, each an
    // ExpectUnsat query left undecided and so read as "does not imply", each
    // keeping a repair the filter had grounds to drop. It starts cold, but
    // these queries are between survivors rather than about one requirement,
    // so there is nothing in the search's cache to inherit.
    SatisfiabilityChecker final_checker;
    final_checker.set_timeout(cfg.black_timeout);
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

std::unique_ptr<StreamingMaximalFilter<Specification>> make_maximal_stream(
    const Config& cfg, const Specification& original,
    const std::string& output_dir) {
    if (!implication_streams(cfg)) {
        return nullptr;
    }
    MaximalStreamRules<Specification> rules;
    rules.implies = [](const Specification& lhs, const Specification& rhs,
                       SatisfiabilityChecker& checker) {
        return spec_implies(lhs, rhs, checker).value_or(false);
    };
    rules.similarity = syntactic_similarity_key(original, cfg);
    rules.fingerprints = [](const std::vector<Specification>& specs) {
        return fingerprint::prefilter::fingerprints_of(specs);
    };
    return std::make_unique<StreamingMaximalFilter<Specification>>(
        cfg, std::move(rules),
        (std::filesystem::path(output_dir) /
         AccumulatedRepairWriter<Specification>::k_subdirectory / "maximal.tsv")
            .string());
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
    const std::string& output_dir) {
    for (std::size_t i = 0; i < scored.size(); ++i) {
        const std::string path =
            output_dir + "/repair_" + std::to_string(i) + ".json";
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
