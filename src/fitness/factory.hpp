#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "fitness/function.hpp"
#include "fitness/mean_or_perfect.hpp"
#include "fitness/status.hpp"
#include "runner/black.hpp"

// The status objective of make_fitness_function below.
template <typename Spec, typename Scorers>
WeightedFitnessFunctionT<Spec> status_objective(const Spec& original,
                                                const Config& cfg) {
    struct StatusContext {
        Config cfg;
        std::vector<std::size_t> order;
    };
    // Computed here because this is the one place that sees the
    // specification being evolved and is entered once, before anything is
    // scored. Under MrsAdmissionOrder::Spec it costs nothing and stays
    // empty, which the walk reads as index order.
    std::vector<std::size_t> order;
    if (cfg.status_grading == StatusGrading::Mrs &&
        cfg.mrs_admission_order == MrsAdmissionOrder::Degree) {
        order = Scorers::admission_order(original);
    }
    const auto status_ctx = std::make_shared<const StatusContext>(
        StatusContext{cfg, std::move(order)});
    auto status = [status_ctx](const Spec& spec) -> double {
        return Scorers::status(spec, status_ctx->cfg, status_ctx->order,
                               ComponentCheck::Included);
    };
    // One part per component satisfiability query, plus the realizability
    // walk. The walk is handed ComponentCheck::Skipped and the fold applies
    // the component tier from those parts, so no query is asked twice.
    //
    // What that gives up is the walk's short circuit: a candidate with an
    // unsatisfiable component now pays its synthesis queries rather than
    // being graded before they start. The guard cannot be kept without
    // either duplicating every component query or serialising the walk
    // behind them, and the queries it would have saved are the ones
    // RealizabilityChecker memoises most heavily.
    auto split = [status_ctx](const Spec& spec) {
        ObjectiveWork work;
        std::vector<std::string> components = Scorers::status_components(spec);
        const std::size_t n_components = components.size();
        for (std::string& component : components) {
            work.parts.push_back(
                {[formula = std::move(component)] {
                     return global_sat_checker()
                                    .check_satisfiability(formula)
                                    .value_or(false)
                                ? 1.0
                                : k_status_component_unsatisfiable;
                 },
                 k_part_cost_satisfiability});
        }
        work.parts.push_back({[status_ctx, &spec] {
                                  return Scorers::status(
                                      spec, status_ctx->cfg, status_ctx->order,
                                      ComponentCheck::Skipped);
                              },
                              Scorers::status_walk_cost(spec)});
        work.combine = [n_components](const std::vector<double>& values) {
            for (std::size_t i = 0; i < n_components; ++i) {
                if (values[i] == k_status_component_unsatisfiable) {
                    return k_status_component_unsatisfiable;
                }
            }
            return values.back();
        };
        return work;
    };
    return {status, cfg.fitness_weight_status, "status", split};
}

// The fitness factory both front ends build from. What differs between them is
// how a specification is scored, which @p Scorers supplies as static members:
//
//   syntactic(spec, original, cfg), semantic(spec, original, cfg)
//   semantic_terms(spec, original, cfg)  one term per changed slot
//   status(spec, cfg, order, ComponentCheck)
//   status_components(spec)              the component satisfiability queries
//   status_walk_cost(spec)               cost hint for the realizability walk
//   admission_order(original)            conflict_degree_order over its parts
//
// Objective order and names are read by the manifest and the dashboard.
template <typename Spec, typename Scorers>
AggregateWeightedFitnessFunctionT<Spec> make_fitness_function(
    const Spec& original, const Config& cfg) {
    // Held once for the run rather than copied into every part: the original is
    // a whole Specification and Config is large, and a scoring region builds
    // parts for every candidate in the population.
    struct SimilarityContext {
        Spec original;
        Config cfg;
    };
    std::vector<WeightedFitnessFunctionT<Spec>> functions;
    const auto ctx = std::make_shared<const SimilarityContext>(
        SimilarityContext{original, cfg});
    if (cfg.fitness_weight_syntactic > 0.0) {
        auto synsim = [ctx](const Spec& spec) -> double {
            return Scorers::syntactic(spec, ctx->original, ctx->cfg);
        };
        // One part, and the cheapest kind there is: this objective never leaves
        // the process, so splitting its arithmetic across workers would cost
        // more in dispatch than it could save. It is still declared rather than
        // left undecomposed, so the launch order knows it is free.
        auto split = [synsim](const Spec& spec) {
            ObjectiveWork work;
            work.parts.push_back({[synsim, &spec] { return synsim(spec); },
                                  k_part_cost_in_process});
            work.combine = [](const std::vector<double>& values) {
                return values.front();
            };
            return work;
        };
        functions.push_back(
            {synsim, cfg.fitness_weight_syntactic, "syntactic", split});
    }
    if (cfg.fitness_weight_semantic > 0.0) {
        auto semsim = [ctx](const Spec& spec) -> double {
            return Scorers::semantic(spec, ctx->original, ctx->cfg);
        };
        // One part per changed slot, each three bounded model counts over a
        // formula pair independent of every other slot's.
        auto split = [ctx](const Spec& spec) {
            ObjectiveWork work;
            for (std::function<double()>& term :
                 Scorers::semantic_terms(spec, ctx->original, ctx->cfg)) {
                work.parts.push_back(
                    {std::move(term), k_part_cost_model_count});
            }
            work.combine = mean_or_perfect;
            return work;
        };
        functions.push_back(
            {semsim, cfg.fitness_weight_semantic, "semantic", split});
    }
    if (cfg.fitness_weight_status > 0.0) {
        functions.push_back(status_objective<Spec, Scorers>(original, cfg));
    }
    return AggregateWeightedFitnessFunctionT<Spec>(std::move(functions));
}
