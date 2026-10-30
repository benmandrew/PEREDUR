#include "genetic/output_gate.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "config.hpp"
#include "filter/correctness.hpp"
#include "filter/implication.hpp"
#include "filter/implication_check.hpp"
#include "filter/streaming_maximal.hpp"
#include "fingerprint/prefilter.hpp"
#include "fitness/status.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/filter_report.hpp"
#include "genetic/generation.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/scored.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "status_line.hpp"
#include "thread_pool.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/fitness.hpp"
#include "tlsf/specification.hpp"

namespace {

// The grading is the run's on both paths: hard-coding one scale here made a
// run configured `status_grading = "aurus"` score its search on the six-level
// ladder and then judge its output on another. No admission order
// is passed: it changes which queries the MRS walk memoises, never whether the
// top tier is reached.
double gate_status(const Specification& spec, const Config& cfg) {
    return specification_status(
        spec, global_sat_checker(), global_real_checker(), cfg.status_grading,
        {}, ComponentCheck::Included, cfg.mrs_speculation_depth);
}

double gate_status(const tlsf::Specification& spec, const Config& cfg) {
    return tlsf_status(spec, cfg);
}

bool stream_implies(const Specification& lhs, const Specification& rhs,
                    SatisfiabilityChecker& checker) {
    return spec_implies(lhs, rhs, checker).value_or(false);
}

bool stream_implies(const tlsf::Specification& lhs,
                    const tlsf::Specification& rhs,
                    SatisfiabilityChecker& checker) {
    return tlsf_spec_implies(lhs, rhs, checker).value_or(false);
}

// The correctness checks, built once. Their predicates capture the global
// checkers, which outlive every caller.
template <typename Spec>
const std::vector<CorrectnessCheckT<Spec>>& gate_checks() {
    static const std::vector<CorrectnessCheckT<Spec>> checks =
        correctness_checks<Spec>(global_sat_checker(), global_real_checker());
    return checks;
}

}  // namespace

// Elites and the seed population reach this unscreened, and the per-generation
// flags can turn any correctness check off outright, so the whole table is
// re-applied here, unconditionally: those flags tune search pressure, never
// output correctness.
//
// Status leads. Most of a final population is unrealizable, and status is a
// scored objective, so its verdict is already memoised for anything the last
// generation scored, whereas the checks behind it are only warm where their
// per-generation stage ran.
template <typename Spec>
bool passes_output_gate(const Spec& spec, const Config& cfg,
                        const FitnessFunctionT<Spec>& status) {
    return (status ? status(spec) : gate_status(spec, cfg)) == 1.0 &&
           !first_failing_check(spec, gate_checks<Spec>()).has_value();
}

template <typename Spec>
std::vector<char> gate_verdicts(const std::vector<Scored<Spec>>& population,
                                const Config& cfg,
                                const FitnessFunctionT<Spec>& status) {
    // Each status check is an `ltlsynt` query and the whole population is
    // checked, so a serial sweep here costs a subprocess per distinct
    // candidate.
    const std::size_t max_in_flight = dispatch_window();
    std::vector<char> keep(population.size(), 0);
    if (max_in_flight <= 1) {
        for (std::size_t idx = 0; idx < population.size(); ++idx) {
            keep[idx] =
                passes_output_gate(population[idx].specification, cfg, status)
                    ? 1
                    : 0;
        }
    } else {
        run_bounded_async(
            population.size(), max_in_flight,
            [&population, &cfg, &status](std::size_t idx) {
                return [&spec = population[idx].specification, &cfg, &status] {
                    return passes_output_gate(spec, cfg, status);
                };
            },
            [&keep](std::size_t idx, bool realizable) {
                keep[idx] = realizable ? 1 : 0;
            });
    }
    return keep;
}

// Not free: nothing else in a generation asks the gate, so this sweep is a
// solver call per candidate, memoised from scoring.
template <typename Spec>
std::size_t accumulate_gate_passing(const std::vector<Scored<Spec>>& population,
                                    const Config& cfg, std::size_t generation,
                                    RepairAccumulator<Spec>& accumulator,
                                    const FitnessFunctionT<Spec>& status) {
    const std::vector<char> keep = gate_verdicts(population, cfg, status);
    std::size_t n_gate_passing = 0;
    for (std::size_t idx = 0; idx < population.size(); ++idx) {
        if (keep[idx] != 0) {
            ++n_gate_passing;
            accumulator.insert(population[idx].specification, generation);
        }
    }
    return n_gate_passing;
}

template <typename Spec>
std::unique_ptr<StreamingMaximalFilter<Spec>> make_maximal_stream(
    const Spec& original, const Config& cfg, const std::string& output_dir) {
    if (!implication_streams(cfg)) {
        return nullptr;
    }
    MaximalStreamRules<Spec> rules;
    rules.implies = [](const Spec& lhs, const Spec& rhs,
                       SatisfiabilityChecker& checker) {
        return stream_implies(lhs, rhs, checker);
    };
    rules.similarity = syntactic_similarity_key(original, cfg);
    // On the background queue with the stream's own checks, so its black
    // calls never hold up the search's regions.
    rules.fingerprints = [](const std::vector<Spec>& specs) {
        return fingerprint::prefilter::fingerprints_of(
            specs, TaskPriority::Background);
    };
    return std::make_unique<StreamingMaximalFilter<Spec>>(
        cfg, std::move(rules),
        (std::filesystem::path(output_dir) /
         AccumulatedRepairWriter::k_subdirectory / "maximal.tsv")
            .string());
}

namespace {

// The committed line of the implication filter. @p elapsed is the time the run
// waited on the filter, which is the last batch: the earlier ones ran during
// the search.
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
              << ImplicationFilterStats::n_fingerprint_refuted << " refuted, "
              << ImplicationFilterStats::n_model_refuted << " by model)\n";
}

// The maximal members of @p realizable in its own order, from @p stream, with
// a filter-report row each for deduplication and implication. Every
// accumulated specification is already in the stream, and the stream
// deduplicates, so pushing @p realizable adds only what the final population
// found that no generation had accumulated.
template <typename Spec>
std::vector<Spec> drain_maximal_stream(StreamingMaximalFilter<Spec>& stream,
                                       const std::vector<Spec>& realizable,
                                       std::vector<FilterRunStats>& stats) {
    const auto drain_start = std::chrono::steady_clock::now();
    for (const Spec& spec : realizable) {
        stream.push(spec, {});
    }
    const std::vector<Spec> streamed = stream.finish();
    // The stream returns push order, accumulated specifications first.
    // repair_N is numbered after a stable sort, so the order of fitness ties
    // is @p realizable's.
    const std::unordered_set<Spec> kept(streamed.begin(), streamed.end());
    std::unordered_set<Spec> emitted;
    std::vector<Spec> maximal;
    maximal.reserve(streamed.size());
    for (const Spec& spec : realizable) {
        if (kept.count(spec) != 0 && emitted.insert(spec).second) {
            maximal.push_back(spec);
        }
    }
    const MaximalStreamCounts& counts = stream.counts();
    stats.push_back({"final/dedup", realizable.size(), counts.n_distinct});
    stats.push_back({"final/implication", counts.n_distinct, maximal.size()});
    print_implication_summary(
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      drain_start)
            .count());
    return maximal;
}

}  // namespace

template <typename Spec>
SearchOutput<Spec> finish_search(
    const Config& cfg, SearchResult<Spec> searched,
    const std::vector<Spec>& accumulated,
    const AggregateWeightedFitnessFunctionT<Spec>& fitness,
    StreamingMaximalFilter<Spec>* stream) {
    // The per-generation filters screen only offspring, and elites bypass
    // them, so the final population is gated here whatever the search did.
    const std::vector<Scored<Spec>>& population = searched.population;
    const std::vector<char> keep =
        gate_verdicts(population, cfg, status_objective_of(fitness));
    std::vector<Spec> realizable;
    for (std::size_t idx = 0; idx < population.size(); ++idx) {
        if (keep[idx] != 0) {
            realizable.push_back(population[idx].specification);
        }
    }
    // The accumulated candidates passed this same gate in the generation they
    // were collected in, so they are merged rather than re-checked; the final
    // filters below screen the union as one set.
    AccumulatorStats::n_contributed +=
        merge_accumulated(realizable, accumulated);

    SearchOutput<Spec> output;
    output.filter_stats = std::move(searched.filter_stats);
    std::vector<Spec> repairs;
    if (stream != nullptr) {
        repairs =
            drain_maximal_stream(*stream, realizable, output.filter_stats);
        output.n_realizable = stream->counts().n_distinct;
    } else {
        const std::vector<FilterFunctionT<Spec>> filters{
            make_dedup_filter<Spec>()};
        repairs = filter_population(realizable, filters);
        output.filter_stats.push_back({"final/" + filters[0].name(),
                                       filters[0].n_in(), filters[0].n_out()});
        output.n_realizable = repairs.size();
    }

    // Serial on purpose: every repair was scored during the search, so each
    // rescore is a cache hit, and fanning it out would nest solver dispatch
    // inside solver dispatch for nothing.
    output.repairs.reserve(repairs.size());
    for (Spec& spec : repairs) {
        auto [objectives, scalar] = fitness.objectives_and_fitness(spec);
        Scored<Spec> scored;
        scored.specification = std::move(spec);
        scored.fitness = scalar;
        scored.objectives = std::move(objectives);
        output.repairs.push_back(std::move(scored));
    }
    order_population(cfg, output.repairs);
    return output;
}

void print_repair_summary(std::size_t n_realizable, std::size_t n_written,
                          bool implication_filter_run,
                          const std::string& output_dir) {
    std::cout << "Realizable specifications: " << n_realizable;
    if (implication_filter_run) {
        std::cout << " (" << n_written << " maximal";
        // The sweep short-circuits, so a class of k members produces exactly
        // k-1 collapse events: this is the number of repairs that were the
        // same repair written another way, which no other figure reports.
        const std::size_t collapsed =
            ImplicationFilterStats::n_equivalent_collapsed.load(
                std::memory_order_relaxed);
        if (collapsed > 0) {
            std::cout << ", " << collapsed << " equivalent";
        }
        std::cout << ")";
    }
    // Only when something was written: the line otherwise names an output
    // directory that the run left empty, which reads as success.
    if (n_written > 0) {
        std::cout << ", written to " << output_dir << "/";
    }
    std::cout << "\n";
}

template bool passes_output_gate(const Specification&, const Config&,
                                 const FitnessFunctionT<Specification>&);
template bool passes_output_gate(const tlsf::Specification&, const Config&,
                                 const FitnessFunctionT<tlsf::Specification>&);
template std::vector<char> gate_verdicts(
    const std::vector<Scored<Specification>>&, const Config&,
    const FitnessFunctionT<Specification>&);
template std::vector<char> gate_verdicts(
    const std::vector<Scored<tlsf::Specification>>&, const Config&,
    const FitnessFunctionT<tlsf::Specification>&);
template std::size_t accumulate_gate_passing(
    const std::vector<Scored<Specification>>&, const Config&, std::size_t,
    RepairAccumulator<Specification>&, const FitnessFunctionT<Specification>&);
template std::size_t accumulate_gate_passing(
    const std::vector<Scored<tlsf::Specification>>&, const Config&, std::size_t,
    RepairAccumulator<tlsf::Specification>&,
    const FitnessFunctionT<tlsf::Specification>&);
template std::unique_ptr<StreamingMaximalFilter<Specification>>
make_maximal_stream(const Specification&, const Config&, const std::string&);
template std::unique_ptr<StreamingMaximalFilter<tlsf::Specification>>
make_maximal_stream(const tlsf::Specification&, const Config&,
                    const std::string&);
template SearchOutput<Specification> finish_search(
    const Config&, SearchResult<Specification>,
    const std::vector<Specification>&,
    const AggregateWeightedFitnessFunctionT<Specification>&,
    StreamingMaximalFilter<Specification>*);
template SearchOutput<tlsf::Specification> finish_search(
    const Config&, SearchResult<tlsf::Specification>,
    const std::vector<tlsf::Specification>&,
    const AggregateWeightedFitnessFunctionT<tlsf::Specification>&,
    StreamingMaximalFilter<tlsf::Specification>*);
