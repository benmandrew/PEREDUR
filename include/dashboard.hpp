#pragma once

/// @file dashboard.hpp
/// @brief Streams run progress to `<output-dir>/progress.jsonl` as the run
///        proceeds, for the live dashboard page to poll.

#include <cstddef>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "genetic/pipeline.hpp"

/// Appends one JSON object per line to `<dir>/progress.jsonl` while a run is in
/// progress, and drops the dashboard page beside it.
///
/// Every record is flushed as it is written: the page polls the file mid-run,
/// so a buffered line is a line the viewer cannot see. The cost is a handful of
/// flushes per generation, against generations that take seconds.
///
/// A writer that cannot open its file reports once and then does nothing.
/// Losing the progress log must never take a repair run with it.
class DashboardWriter {
   public:
    /// Opens (and truncates) `<dir>/progress.jsonl` when @p enabled, and
    /// otherwise touches nothing: every method below becomes a no-op, so a
    /// caller need not branch on whether progress was asked for. Never throws;
    /// check enabled() to learn whether the file actually opened.
    DashboardWriter(const std::string& dir, bool enabled);

    [[nodiscard]] bool enabled() const { return m_enabled; }

    /// The path written to, for reporting to the user.
    [[nodiscard]] const std::string& path() const { return m_path; }

    /// Copies the dashboard page next to the progress log, so serving the
    /// output directory is all the viewer needs. Returns the page's path, or an
    /// empty string in each of the three cases where no page is written: the
    /// writer is disabled, the build carries no dashboard page path
    /// (PEREDUR_DASHBOARD_PAGE_PATH is undefined), or the copy itself failed.
    ///
    /// The page is taken from `PEREDUR_DASHBOARD_PAGE` when that environment
    /// variable is set and non-empty, and from PEREDUR_DASHBOARD_PAGE_PATH
    /// otherwise. The environment is read once, on first use. A build without
    /// the compile-time definition writes no page whatever the environment
    /// says, the page being a property of the build.
    std::string write_page();

    /// Opens the log with the run's fixed facts, @p input being the path to
    /// the specification being repaired.
    ///
    /// @p objectives is in registration order. @p stages is every stage a
    /// generation can run, in pipeline order, so the page can lay out the full
    /// set before the first generation reports rather than growing it row by
    /// row as stages first appear.
    void run_start(const std::string& input, std::size_t generations,
                   std::size_t population, std::size_t seed,
                   const std::vector<std::string>& objectives,
                   const std::vector<std::string>& stages);

    /// @param gen         1-indexed generation
    /// @param index       Position of the stage within the generation's stage
    ///                    list
    /// @param observation The completed stage's name, population sizes and
    ///                    elapsed time, and `distinct`: how many of the
    ///                    survivors are distinct specifications, which is what
    ///                    no population size can show
    /// @param muc_iter    1-indexed MUC repair iteration, or 0 when the run is
    ///                    not MUC-guided; emitted only when non-zero
    void stage(std::size_t gen, std::size_t index,
               const StageObservation& observation, std::size_t muc_iter = 0);

    /// @param gen          1-indexed generation
    /// @param elapsed_s    Wall time this generation took
    /// @param best_fitness Highest weighted scalar in the population. Not the
    ///                     leading individual's: NSGA-II orders by front rank
    ///                     and crowding distance, so that one can sit below the
    ///                     mean reported beside it
    /// @param mean_fitness Mean weighted scalar across the population
    /// @param objectives   Per-objective means, labelled by objective name
    /// @param n_realizable Realizable survivors this generation, where the
    ///                     driver counts them; std::nullopt omits the key
    ///                     rather than reporting a zero the run never measured
    /// @param population   Population size at the end of the generation
    /// @param muc_iter     1-indexed MUC repair iteration, or 0 when the run is
    ///                     not MUC-guided; emitted only when non-zero
    void generation(
        std::size_t gen, double elapsed_s, double best_fitness,
        double mean_fitness,
        const std::vector<std::pair<std::string, double>>& objectives,
        std::optional<std::size_t> n_realizable, std::size_t population,
        std::size_t muc_iter = 0);

    void run_end(std::size_t generations_run, std::size_t n_realizable,
                 std::size_t n_maximal, double elapsed_s);

   private:
    void write_line(const std::string& line);

    std::string m_path;
    std::ofstream m_out;
    bool m_enabled = false;
};

/// Mean of each objective across @p population, paired with the objective names
/// in registration order. Returns one entry per name; a shorter objective
/// vector than @p names contributes nothing to the missing tail.
///
/// Kept here rather than in the fitness headers because it exists for
/// reporting, not for selection.
std::vector<std::pair<std::string, double>> mean_objectives(
    const std::vector<std::string>& names,
    const std::vector<std::vector<double>>& population_objectives);
