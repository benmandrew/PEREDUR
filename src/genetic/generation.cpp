#include "genetic/generation.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "bounded_async.hpp"
#include "filter/bloat.hpp"
#include "filter/correctness.hpp"
#include "filter/implication.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "thread_pool.hpp"
#include "tlsf/specification.hpp"

namespace {
// One predicate's answer, or the message it threw. Carried back as a value,
// as score_population's PartOutcome is, so a check that fails on one
// formula cannot unwind the whole filter.
struct PredicateOutcome {
    bool keep = false;
    std::string error;
};

template <typename Predicate, typename Spec>
PredicateOutcome evaluate_predicate(const Predicate& predicate,
                                    const Spec& spec) {
    PredicateOutcome outcome;
    try {
        outcome.keep = predicate(spec);
    } catch (const std::exception& exc) {
        outcome.error = exc.what();
        // A message is never empty, so the empty string can mean success.
        if (outcome.error.empty()) {
            outcome.error = "unnamed exception";
        }
    }
    return outcome;
}

}  // namespace

template <typename Spec>
FilterFunctionT<Spec> make_predicate_filter(
    std::string name,
    std::function<bool(const typename NonDeduced<Spec>::type&)> predicate,
    std::size_t max_in_flight, FilterKind kind, double max_failure_rate) {
    auto run = [name, predicate = std::move(predicate), max_in_flight,
                max_failure_rate](std::vector<Spec> pop) {
        std::vector<Spec> survivors;
        survivors.reserve(pop.size());
        // Predicates draw no randomness, so running them concurrently
        // leaves seed reproducibility unaffected.
        std::vector<PredicateOutcome> outcomes(pop.size());
        if (max_in_flight <= 1) {
            for (std::size_t idx = 0; idx < pop.size(); ++idx) {
                outcomes[idx] = evaluate_predicate(predicate, pop[idx]);
            }
        } else {
            run_bounded_async(
                pop.size(), max_in_flight,
                [&predicate, &pop](std::size_t idx) {
                    return [&predicate, &spec = pop[idx]] {
                        return evaluate_predicate(predicate, spec);
                    };
                },
                [&outcomes](std::size_t idx, PredicateOutcome outcome) {
                    outcomes[idx] = std::move(outcome);
                });
        }
        // Read in population order, so the first error named is the
        // same whichever worker finished first.
        std::vector<const std::string*> errors;
        for (const PredicateOutcome& outcome : outcomes) {
            if (!outcome.error.empty()) {
                errors.push_back(&outcome.error);
            }
        }
        const std::size_t tolerated =
            tolerated_failures(max_failure_rate, pop.size());
        if (errors.size() > tolerated) {
            throw std::runtime_error(
                "the " + name + " filter's check failed for " +
                std::to_string(errors.size()) + " of " +
                std::to_string(pop.size()) + " candidates (tolerating " +
                std::to_string(tolerated) +
                "); the tooling is broken rather than the formulae. "
                "First error: " +
                *errors.front());
        }
        for (const std::string* error : errors) {
            FilterErrorStats::record(name + ": " + *error);
        }
        for (std::size_t idx = 0; idx < pop.size(); ++idx) {
            if (outcomes[idx].keep && outcomes[idx].error.empty()) {
                survivors.push_back(std::move(pop[idx]));
            }
        }
        return survivors;
    };
    return {std::move(name), std::move(run), kind};
}

template FilterFunctionT<Specification> make_predicate_filter<Specification>(
    std::string, std::function<bool(const Specification&)>, std::size_t,
    FilterKind, double);
template FilterFunctionT<tlsf::Specification>
make_predicate_filter<tlsf::Specification>(
    std::string, std::function<bool(const tlsf::Specification&)>, std::size_t,
    FilterKind, double);

template <typename Spec>
std::vector<FilterFunctionT<Spec>> get_filter_functions(
    const Spec& original, SatisfiabilityChecker& checker,
    double max_failure_rate) {
    const std::size_t max_in_flight = dispatch_window();
    std::vector<FilterFunctionT<Spec>> filters;
    FilterFunctionT<Spec> dedup = make_dedup_filter<Spec>();
    filters.push_back(std::move(dedup));
    FilterFunctionT<Spec> bloat = make_bloat_cap_filter(original);
    filters.push_back(std::move(bloat));
    // Built from correctness_checks rather than listed here, so a property
    // cannot be enforced per generation without also being enforced by the
    // final gate and the input screen, which read the same table. Both stages
    // use the shared global checkers, so the queries a filter pays for are the
    // ones the gate later hits in cache.
    for (const CorrectnessCheckT<Spec>& check :
         correctness_checks<Spec>(checker, global_real_checker())) {
        if (check.per_generation) {
            filters.push_back(make_predicate_filter<Spec>(
                check.name, check.admissible, max_in_flight,
                FilterKind::Correctness, max_failure_rate));
        }
    }
    return filters;
}

void insert_parent_verdict_stage(
    std::vector<PipelineStage<Specification>>& filters) {
    // make_dedup_filter's name.
    const auto dedup =
        std::find_if(filters.begin(), filters.end(),
                     [](const PipelineStage<Specification>& stage) {
                         return stage.name() == "dedup";
                     });
    const auto position = dedup == filters.end() ? filters.begin() : dedup + 1;
    filters.insert(
        position,
        PipelineStage<Specification>(
            k_parent_verdict_stage, [](GenerationContext<Specification>& ctx) {
                pipeline_detail::stage_parent_verdict<Specification>(
                    ctx,
                    [](const Specification& parent) -> std::optional<bool> {
                        if (global_real_checker().proved_unrealizable(parent)) {
                            return false;
                        }
                        return std::nullopt;
                    });
            }));
}

template std::vector<FilterFunctionT<Specification>> get_filter_functions(
    const Specification&, SatisfiabilityChecker&, double);
template std::vector<FilterFunctionT<tlsf::Specification>> get_filter_functions(
    const tlsf::Specification&, SatisfiabilityChecker&, double);
