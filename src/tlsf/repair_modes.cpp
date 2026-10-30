#include "repair_modes.hpp"

#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "evolve.hpp"
#include "filter/streaming_maximal.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/filter_report.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "survivors.hpp"
#include "tlsf/specification.hpp"
#include "tlsf/writer.hpp"

namespace tlsf::internal {

std::vector<Scored<Specification>> run_monolithic(
    const Specification& original, const Config& cfg,
    const RandomSource& random_source,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    const DashboardProgress& progress, const std::string& output_dir,
    SearchBudget& budget, StreamingMaximalFilter<Specification>* stream) {
    std::vector<FilterRunStats> filter_stats;
    RepairAccumulator<Specification>::Sink sink;
    if (stream != nullptr) {
        sink = [stream](const Specification& spec, const std::string& name) {
            stream->push(spec, name);
        };
    }
    // The same serialiser repair_N.tlsf goes through, so an accumulated file
    // is a specification document and nothing else -- these are gate-passing
    // candidates, not the run's filtered output.
    RepairAccumulator<Specification> accumulator(
        AccumulatedRepairWriter<Specification>(
            output_dir, ".tlsf",
            [](const Specification& spec) { return write(spec); },
            [&budget] { return budget.elapsed_s(); }),
        std::move(sink));
    const std::vector<Scored<Specification>> population =
        evolve_population(original, cfg, random_source, fitness, filter_stats,
                          progress, accumulator, budget);
    std::vector<Scored<Specification>> survivors =
        realizable_survivors(population, cfg, fitness);
    survivors = merge_accumulated_survivors(
        std::move(survivors), accumulator.specifications(), cfg, fitness);
    if (cfg.report_diagnostics) {
        print_filter_report(filter_stats, EmptyFilterReport::Silent);
    }
    return survivors;
}

}  // namespace tlsf::internal
