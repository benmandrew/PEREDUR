#include "genetic/output_gate.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
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
#include "genetic/scored.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "thread_pool.hpp"
#include "tlsf/filter.hpp"
#include "tlsf/fitness.hpp"
#include "tlsf/specification.hpp"

namespace {

// The grading is the run's on both paths: hard-coding Tiered here made a run
// configured `status_grading = "aurus"` score its search on the six-level
// ladder and then judge its output on the three-point one. No admission order
// is passed: it changes which queries the MRS walk memoises, never whether the
// top tier is reached.
double gate_status(const Specification& spec, const Config& cfg) {
    return specification_status(spec, global_sat_checker(),
                                global_real_checker(), cfg.status_grading);
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
bool passes_output_gate(const Spec& spec, const Config& cfg) {
    return gate_status(spec, cfg) == 1.0 &&
           !first_failing_check(spec, gate_checks<Spec>()).has_value();
}

template <typename Spec>
std::vector<char> gate_verdicts(const std::vector<Scored<Spec>>& population,
                                const Config& cfg) {
    // Each status check is an `ltlsynt` query and the whole population is
    // checked, so a serial sweep here costs a subprocess per distinct
    // candidate.
    const std::size_t max_in_flight = dispatch_window();
    std::vector<char> keep(population.size(), 0);
    if (max_in_flight <= 1) {
        for (std::size_t idx = 0; idx < population.size(); ++idx) {
            keep[idx] =
                passes_output_gate(population[idx].specification, cfg) ? 1 : 0;
        }
    } else {
        run_bounded_async(
            population.size(), max_in_flight,
            [&population, &cfg](std::size_t idx) {
                return [&spec = population[idx].specification, &cfg] {
                    return passes_output_gate(spec, cfg);
                };
            },
            [&keep](std::size_t idx, bool realizable) {
                keep[idx] = realizable ? 1 : 0;
            });
    }
    return keep;
}

// Not free: nothing else in a generation asks the gate, so with the accumulator
// off this sweep would be a solver call per candidate the run would not
// otherwise make. Hence the early return rather than a caller-side branch.
template <typename Spec>
std::optional<std::size_t> accumulate_gate_passing(
    const std::vector<Scored<Spec>>& population, const Config& cfg,
    std::size_t generation, RepairAccumulator<Spec>& accumulator) {
    if (!accumulator.enabled()) {
        return std::nullopt;
    }
    const std::vector<char> keep = gate_verdicts(population, cfg);
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
    rules.fingerprints = [](const std::vector<Spec>& specs) {
        return fingerprint::prefilter::fingerprints_of(specs);
    };
    return std::make_unique<StreamingMaximalFilter<Spec>>(
        cfg, std::move(rules),
        (std::filesystem::path(output_dir) /
         AccumulatedRepairWriter<Spec>::k_subdirectory / "maximal.tsv")
            .string());
}

template bool passes_output_gate(const Specification&, const Config&);
template bool passes_output_gate(const tlsf::Specification&, const Config&);
template std::vector<char> gate_verdicts(
    const std::vector<Scored<Specification>>&, const Config&);
template std::vector<char> gate_verdicts(
    const std::vector<Scored<tlsf::Specification>>&, const Config&);
template std::optional<std::size_t> accumulate_gate_passing(
    const std::vector<Scored<Specification>>&, const Config&, std::size_t,
    RepairAccumulator<Specification>&);
template std::optional<std::size_t> accumulate_gate_passing(
    const std::vector<Scored<tlsf::Specification>>&, const Config&, std::size_t,
    RepairAccumulator<tlsf::Specification>&);
template std::unique_ptr<StreamingMaximalFilter<Specification>>
make_maximal_stream(const Specification&, const Config&, const std::string&);
template std::unique_ptr<StreamingMaximalFilter<tlsf::Specification>>
make_maximal_stream(const tlsf::Specification&, const Config&,
                    const std::string&);
