#include "repair_modes.hpp"

#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "dashboard.hpp"
#include "filter/streaming_maximal.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/generation.hpp"
#include "genetic/generation_loop.hpp"
#include "genetic/output_gate.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "runner/black.hpp"
#include "tlsf/specification.hpp"
#include "tlsf/writer.hpp"

namespace tlsf::internal {

SearchOutput<Specification> run_monolithic(
    const Specification& original, const Config& cfg,
    const RandomSource& random_source,
    const AggregateWeightedFitnessFunctionT<Specification>& fitness,
    DashboardWriter& dashboard, const std::string& output_dir,
    SearchBudget& budget, StreamingMaximalFilter<Specification>* stream) {
    RepairAccumulator<Specification> accumulator =
        make_repair_accumulator<Specification>(
            output_dir, ".tlsf",
            [](const Specification& spec) { return write(spec); }, budget,
            stream);
    const std::vector<FilterFunctionT<Specification>> filters =
        get_filter_functions(original, global_sat_checker());
    const std::vector<Specification> seed_population(cfg.population_size,
                                                     original);
    SearchResult<Specification> searched = run_generations(
        cfg, score_population(cfg, seed_population, fitness), fitness, filters,
        random_source, dashboard, accumulator, budget);
    return finish_search(cfg, std::move(searched), accumulator.specifications(),
                         fitness, stream);
}

}  // namespace tlsf::internal
