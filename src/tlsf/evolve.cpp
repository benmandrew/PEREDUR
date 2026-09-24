#include "evolve.hpp"

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "dashboard.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/generation.hpp"
#include "genetic/generation_loop.hpp"
#include "genetic/output_gate.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "runner/black.hpp"
#include "status_line.hpp"
#include "tlsf/operators.hpp"
#include "tlsf/specification.hpp"

namespace tlsf::internal {

namespace {

std::string format_fixed(double value, int precision) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         start)
        .count();
}

// The per-generation status line, with the same columns as the FRETISH one
// (src/repair/evolution.cpp) so a log reads alike whichever path wrote it.
// A class of its own to keep evolve_population's loop about the search.
class GenerationStatus {
   public:
    // @p with_real adds the gate count column, only where the gate sweep runs,
    // as on the FRETISH path: a column that reads the same number every
    // generation because nothing measured it is worse than no column.
    GenerationStatus(std::size_t generations, bool with_real)
        : m_total(std::to_string(generations)),
          m_col_gen(m_status.add("gen")),
          // Transient: within-generation progress is the reason the line
          // updates at all, and reads 100% on every committed line by
          // construction.
          m_col_pct(m_status.add("%", true)),
          m_col_time(m_status.add("time")),
          m_col_best(m_status.add("best")),
          m_col_real(with_real
                         ? std::optional<std::size_t>(m_status.add("real"))
                         : std::nullopt) {}

    void begin(std::size_t gen) {
        m_start = std::chrono::steady_clock::now();
        m_status.set(m_col_gen, std::to_string(gen + 1) + "/" + m_total);
    }

    // Redraws the live line as breeding reports progress through a generation.
    void progress(std::size_t done, std::size_t total) {
        m_status.set(m_col_pct, std::to_string(done * 100 / total) + "%");
        m_status.set(m_col_time, format_fixed(seconds_since(m_start), 2) + "s");
        m_status.render();
    }

    // Commits the generation's line and returns its elapsed seconds. @p best
    // is empty for an empty population, which has no best to report.
    double commit(std::optional<double> best,
                  std::optional<std::size_t> n_real) {
        const double elapsed = seconds_since(m_start);
        m_status.set(m_col_time, format_fixed(elapsed, 2) + "s");
        if (best.has_value()) {
            m_status.set(m_col_best, format_fixed(*best, 3));
        }
        if (m_col_real.has_value() && n_real.has_value()) {
            m_status.set(*m_col_real, std::to_string(*n_real));
        }
        m_status.finish();
        return elapsed;
    }

   private:
    StatusLine m_status;
    std::string m_total;
    std::size_t m_col_gen;
    std::size_t m_col_pct;
    std::size_t m_col_time;
    std::size_t m_col_best;
    std::optional<std::size_t> m_col_real;
    std::chrono::steady_clock::time_point m_start;
};

}  // namespace

std::vector<Scored<Specification>> evolve_population(
    const Specification& spec, const Config& cfg,
    const RandomSource& random_source,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    std::vector<FilterRunStats>& filter_stats_out,
    const DashboardProgress& progress,
    RepairAccumulator<Specification>& accumulator_out, SearchBudget& budget) {
    const std::vector<FilterFunctionT<Specification>> per_gen_filters =
        get_filter_functions(spec, global_sat_checker());

    const std::vector<Specification> seed_population(cfg.population_size, spec);
    std::vector<Scored<Specification>> population =
        score_population(cfg, seed_population, fitness);

    // Sized on cfg.population_size, the seed population's size. Which
    // candidates count as "best" is not truncation on the weighted scalar:
    // order_population applies cfg.selection_scheme, which defaults to NSGA-II.
    const GenerationSizes sizes = generation_sizes(cfg, cfg.population_size);
    filter_stats_out = empty_filter_stats(per_gen_filters);

    GenerationStatus status(cfg.generations, accumulator_out.enabled());

    for (std::size_t gen = 0; gen < cfg.generations; ++gen) {
        // Before the generation as well as between offspring, matching
        // checkTermination() at the head of AuRUS's evolve(count) loop. The
        // budget spans the whole run, so under MUC repair a core that opens
        // with it already spent evolves nothing rather than restarting it.
        if (budget.active() && budget.exhausted()) {
            break;
        }
        status.begin(gen);
        auto on_progress = [&status](std::size_t done, std::size_t total) {
            status.progress(done, total);
        };
        // MUC repair restarts its generation count on every core it evolves, so
        // the dashboard is given a number that keeps climbing across
        // iterations; muc_iter carries the structure that flattens away.
        const std::size_t dashboard_gen = progress.gen_offset + gen + 1;
        std::size_t stage_index = 0;
        auto on_stage = [&progress, &stage_index,
                         dashboard_gen](const StageObservation& obs) {
            if (progress.writer != nullptr) {
                progress.writer->stage(dashboard_gen, stage_index++, obs,
                                       progress.muc_iter);
            }
        };
        population = evolve_generation_generic(
            cfg, population, sizes.selection, sizes.elitism, fitness,
            per_gen_filters, tlsf_operators(), random_source, on_progress,
            on_stage, &budget);
        budget.count_generation();
        fold_filter_stats(per_gen_filters, filter_stats_out);
        const FitnessSummary summary = summarise_fitness(population);
        const std::optional<std::size_t> n_real = accumulate_gate_passing(
            population, cfg, dashboard_gen, accumulator_out);
        const double elapsed = status.commit(
            population.empty() ? std::nullopt
                               : std::optional<double>(summary.best),
            n_real);
        if (progress.writer != nullptr) {
            progress.writer->generation(
                dashboard_gen, elapsed, summary.best, summary.mean,
                // The gate count where the accumulator swept the population,
                // nothing otherwise: without the sweep this path checks
                // realizability once, after evolution, so any number here
                // would be one the run never measured.
                mean_objectives(progress.objective_names,
                                objectives_of(population)),
                n_real, population.size(), progress.muc_iter);
        }
    }
    return population;
}

}  // namespace tlsf::internal
