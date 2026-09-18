#include "repair_modes.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <optional>
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
#include "runner/spot.hpp"
#include "survivors.hpp"
#include "tlsf/fitness.hpp"
#include "tlsf/mucs.hpp"
#include "tlsf/specification.hpp"
#include "tlsf/writer.hpp"

namespace tlsf::internal {

namespace {

bool is_realizable(const Specification& spec) {
    // Undecided reads as unrealizable: the repair loop keeps going rather than
    // declaring a specification repaired on a query that never finished.
    return global_real_checker()
        .check_realizability_ltl(spec.to_ltl(), spec.m_inputs, spec.m_outputs,
                                 tlsf::specification_sides(spec))
        .value_or(false);
}

// Every core survivor put back into its context, not just the best one. The
// core sub-population is where this mode's diversity is, and reintegrating
// only its front is what left MUC repair reporting a single repair where
// monolithic search reports the whole maximal antichain.
std::vector<Scored<Specification>> reintegrate_all(
    const std::vector<Scored<Specification>>& sub_survivors,
    const std::vector<CoreFormula>& carried) {
    std::vector<Scored<Specification>> rejoined;
    rejoined.reserve(sub_survivors.size());
    for (const Scored<Specification>& sub : sub_survivors) {
        Scored<Specification> candidate;
        candidate.specification = reintegrate(sub.specification, carried);
        rejoined.push_back(std::move(candidate));
    }
    return rejoined;
}

// Keeps each gate-passing repair once across the whole loop. The accumulator
// is handed every one of them regardless, since it holds its own seen-set and
// a repeat writes no second file.
void record_repairs(const std::vector<Scored<Specification>>& passed,
                    std::size_t gen_offset,
                    std::vector<Scored<Specification>>& repairs,
                    RepairAccumulator<Specification>& accumulator) {
    for (const Scored<Specification>& repair : passed) {
        const bool seen =
            std::any_of(repairs.begin(), repairs.end(),
                        [&repair](const Scored<Specification>& kept) {
                            return kept.specification == repair.specification;
                        });
        if (!seen) {
            repairs.push_back(repair);
        }
        accumulator.insert(repair.specification, gen_offset);
    }
}

}  // namespace

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
        cfg.accumulate_repairs,
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
    print_filter_report(filter_stats, EmptyFilterReport::Silent);
    return survivors;
}

std::vector<Scored<Specification>> run_muc(
    const Specification& original, const Config& cfg,
    const RandomSource& random_source,
    const AggregateWeightedFitnessFunctionT<Specification>& output_fitness,
    const DashboardProgress& progress, const std::string& output_dir,
    SearchBudget& budget, StreamingMaximalFilter<Specification>* stream) {
    std::vector<FilterRunStats> aggregate_stats;
    RepairAccumulator<Specification>::Sink sink;
    if (stream != nullptr) {
        sink = [stream](const Specification& spec, const std::string& name) {
            stream->push(spec, name);
        };
    }
    // What reaches this is a reintegrated whole specification that passed the
    // gate against the original, so it is a repair in the same sense the
    // monolithic path's accumulated candidates are, and the same serialiser
    // writes it.
    RepairAccumulator<Specification> accumulator(
        cfg.accumulate_repairs,
        AccumulatedRepairWriter<Specification>(
            output_dir, ".tlsf",
            [](const Specification& spec) { return write(spec); },
            [&budget] { return budget.elapsed_s(); }),
        std::move(sink));
    std::vector<Scored<Specification>> repairs;
    // Rebuilt only when the core changes: under status_grading = "mrs" with
    // the degree admission order, building one walks the core's parts
    // pairwise, and an iteration that extracts the same core again would pay
    // for that walk twice.
    std::optional<AggregateWeightedFitnessFunctionT<Specification>> sub_fitness;
    Specification fitness_core;
    Specification current = original;
    std::size_t gen_offset = 0;
    for (std::size_t iter = 0; iter < cfg.muc_max_iterations; ++iter) {
        if (is_realizable(current)) {
            break;
        }
        // The budget is the run's, not the core's. A core that opens with it
        // already spent would otherwise pay for an MUC extraction and a full
        // evolve_population call that breeds nothing.
        if (budget.active() && budget.exhausted()) {
            std::cout << "muc: search budget exhausted; stopping\n";
            break;
        }
        const MinimalUnrealizableCore muc = extract_muc(current);
        if (muc.formulae.empty()) {
            // No guarantee-side core: the environment side alone is
            // unrealizable, which this repair strategy cannot address.
            std::cout << "muc: no guarantee-side core to repair; stopping\n";
            break;
        }
        std::cout << "muc iteration " << (iter + 1) << "/"
                  << cfg.muc_max_iterations << ": core of "
                  << muc.formulae.size() << " formula(s)\n";
        if (muc.n_undecided > 0) {
            // Said out loud rather than folded into the core: the extraction
            // shrank across probes that never answered, so this core was never
            // confirmed unrealizable.
            std::cout << "muc: " << muc.n_undecided
                      << " undecided probe(s); core is provisional\n";
        }
        const std::vector<CoreFormula> carried =
            non_core_formulae(current, muc.formulae);
        if (!sub_fitness.has_value() || !(fitness_core == muc.spec)) {
            sub_fitness.emplace(tlsf_get_fitness_function(muc.spec, cfg));
            fitness_core = muc.spec;
        }
        std::vector<FilterRunStats> iter_stats;
        DashboardProgress iter_progress = progress;
        iter_progress.gen_offset = gen_offset;
        iter_progress.muc_iter = iter + 1;
        // Inert on the core population itself: a gate-passing candidate of a
        // core is realizable against the core alone, so emitting one would
        // report a fragment as a repair of the whole specification. The
        // reintegrated candidates below are what gets accumulated.
        RepairAccumulator<Specification> no_accumulation(false);
        const std::vector<Scored<Specification>> population = evolve_population(
            muc.spec, cfg, random_source, *sub_fitness, iter_stats,
            iter_progress, no_accumulation, budget);
        gen_offset += cfg.generations;
        accumulate_filter_stats(aggregate_stats, iter_stats);
        const std::vector<Scored<Specification>> sub_survivors =
            realizable_survivors(population, cfg, *sub_fitness);
        if (sub_survivors.empty()) {
            std::cout << "muc: core could not be made realizable; stopping\n";
            break;
        }
        const std::vector<Scored<Specification>> rejoined =
            reintegrate_all(sub_survivors, carried);
        // Gates, scores against the original, deduplicates and orders, exactly
        // as the monolithic path's collection does, and concurrently.
        const std::vector<Scored<Specification>> passed =
            realizable_survivors(rejoined, cfg, output_fitness);
        record_repairs(passed, gen_offset, repairs, accumulator);
        // The loop continues on a gate-passing repair where there is one, so
        // the next head check ends it; otherwise on the best reintegration, as
        // before, because the core is repaired but its context is not.
        current = passed.empty() ? rejoined.front().specification
                                 : passed.front().specification;
    }
    print_filter_report(aggregate_stats, EmptyFilterReport::Silent);
    if (repairs.empty()) {
        // Nothing passed the gate through a core. An input that was realizable
        // to begin with reaches here, and is the run's answer if it passes.
        if (!is_realizable(current)) {
            return {};
        }
        Scored<Specification> seed;
        seed.specification = current;
        return realizable_survivors({std::move(seed)}, cfg, output_fitness);
    }
    order_population(cfg, repairs);
    return merge_accumulated_survivors(
        std::move(repairs), accumulator.specifications(), cfg, output_fitness);
}

}  // namespace tlsf::internal
