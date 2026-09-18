#pragma once

/// @file generation.hpp
/// @brief One generation of the genetic repair loop: scoring, filtering,
///        crossover, mutation, and the FilterFunction / ScoredSpecification
///        types.

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bounded_async.hpp"
#include "config.hpp"
#include "fitness/function.hpp"
#include "genetic/crossover.hpp"
#include "genetic/mutation.hpp"
#include "genetic/nsga2.hpp"
#include "genetic/operators.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "genetic/scored.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "thread_pool.hpp"

/// Whether a filter's rejects may be re-admitted to keep a generation alive.
///
/// Correctness rejects are unfit to breed from at all: a candidate with a false
/// condition, contradictory assumptions or a system strategy that falsifies its
/// own assumptions satisfies (A) -> (G) for free, so breeding from one chases a
/// repair that repairs nothing. Preference rejects are perfectly good
/// specifications that the chain would rather not carry -- a duplicate, or one
/// grown past the bloat cap -- and re-admitting one costs diversity or size,
/// not meaning.
///
/// Only stage_filter_fallback reads this: it decides which rejects can come
/// back when the chain empties the population. Filters that never run inside a
/// generation (the final weakening and implication screens) are still tagged,
/// so the distinction reads the same everywhere.
enum class FilterKind : std::uint8_t { Correctness, Preference };

/// A filter function transforms a population into a surviving subset.
/// Receives the entire population, enabling both per-element predicates and
/// population-level relations such as keeping only maximal elements under a
/// partial order.
///
/// Tracks the input and output population sizes of the most recent invocation
/// via n_in() and n_out(), for per-generation diagnostic output.
template <typename Spec>
class FilterFunctionT {
   public:
    /// Takes the population by value so a filter can move its survivors out
    /// of it rather than copying them. The chain runs in series and each
    /// stage owns what it was handed, so nothing downstream reads the input
    /// again.
    using Fn = std::function<std::vector<Spec>(std::vector<Spec>)>;

    /// A filter is Correctness unless it says otherwise: a new filter left
    /// untagged is then re-applied by the fallback rather than silently
    /// bypassed by it, which is the failure this default exists to avoid.
    FilterFunctionT(std::string name, Fn func,
                    FilterKind kind = FilterKind::Correctness)
        : m_name(std::move(name)), m_fn(std::move(func)), m_kind(kind) {}

    /// Implicit construction from any compatible callable (for tests and
    /// inline construction where a display name is not needed).
    template <
        typename Callable,
        std::enable_if_t<
            !std::is_same_v<std::decay_t<Callable>, FilterFunctionT>, int> = 0>
    FilterFunctionT(  // NOLINT(google-explicit-constructor,runtime/explicit)
        Callable&& func)
        : FilterFunctionT("", Fn(std::forward<Callable>(func))) {}

    std::vector<Spec> operator()(std::vector<Spec> pop) const {
        m_n_in = pop.size();
        std::vector<Spec> survivors = m_fn(std::move(pop));
        m_n_out = survivors.size();
        return survivors;
    }

    const std::string& name() const { return m_name; }
    FilterKind kind() const { return m_kind; }
    std::size_t n_in() const { return m_n_in; }
    std::size_t n_out() const { return m_n_out; }

   private:
    std::string m_name;
    Fn m_fn;
    FilterKind m_kind{FilterKind::Correctness};
    mutable std::size_t m_n_in{0};
    mutable std::size_t m_n_out{0};
};

/// The FRETISH filter function type.
using FilterFunction = FilterFunctionT<Specification>;

/// Callback invoked after each individual is produced during a generation.
/// @p done is the count produced so far; @p total is the generation size.
using GenerationProgressCallback =
    std::function<void(std::size_t done, std::size_t total)>;

/// A specification paired with its aggregated fitness score.
using ScoredSpecification = Scored<Specification>;

/// Wraps a per-element predicate as a population-level filter, on either front
/// end: `Spec` defaults to the FRETISH Specification, and a TLSF caller names
/// tlsf::Specification.
///
/// A predicate that shells out to a solver costs a whole subprocess per
/// cache-missing candidate, and the miss rate rises with population diversity,
/// so those filters should pass a max_in_flight above 1. Structural predicates
/// are cheaper than a thread-pool dispatch and should leave it at the default.
/// Either way the survivors and their order are identical: verdicts are
/// collected by index and the survivors rebuilt in population order.
///
/// @param name          Display name used in diagnostic output
/// @param predicate     A predicate returning true for specifications to keep
/// @param max_in_flight Concurrent predicate evaluations; 1 evaluates serially
/// @param kind          Whether the fallback may re-admit this filter's rejects
/// @return              A filter that applies the predicate element-wise
template <typename Spec = Specification, typename Predicate>
FilterFunctionT<Spec> make_predicate_filter(
    std::string name, Predicate predicate, std::size_t max_in_flight = 1,
    FilterKind kind = FilterKind::Correctness) {
    return {std::move(name),
            [predicate = std::function<bool(const Spec&)>(std::move(predicate)),
             max_in_flight](std::vector<Spec> pop) {
                std::vector<Spec> survivors;
                survivors.reserve(pop.size());
                // Predicates draw no randomness, so running them concurrently
                // leaves seed reproducibility unaffected.
                std::vector<char> keep(pop.size(), 0);
                if (max_in_flight <= 1) {
                    for (std::size_t idx = 0; idx < pop.size(); ++idx) {
                        keep[idx] = predicate(pop[idx]) ? 1 : 0;
                    }
                } else {
                    run_bounded_async(
                        pop.size(), max_in_flight,
                        [&predicate, &pop](std::size_t idx) {
                            return [&predicate, &spec = pop[idx]] {
                                return predicate(spec);
                            };
                        },
                        [&keep](std::size_t idx, bool verdict) {
                            keep[idx] = verdict ? 1 : 0;
                        });
                }
                for (std::size_t idx = 0; idx < pop.size(); ++idx) {
                    if (keep[idx] != 0) {
                        survivors.push_back(std::move(pop[idx]));
                    }
                }
                return survivors;
            },
            kind};
}

/// An individual whose fitness scoring throws is dropped from the returned
/// population rather than aborting the run (see
/// Config::max_scoring_failure_rate). Every drop is tallied here so it is
/// reported at the end of a run: a silent drop must never be mistaken for a
/// clean sweep.
struct ScoringStats {
    /// Distinct error messages are capped: a message names the offending
    /// formula, so an uncapped tally would grow with the search.
    static constexpr std::size_t k_max_distinct_reasons = 8;

    inline static std::size_t n_dropped = 0;
    inline static std::size_t n_reasons_elided = 0;
    inline static std::map<std::string, std::size_t> reasons;

    static void record(const std::string& reason) {
        n_dropped++;
        const auto found = reasons.find(reason);
        if (found != reasons.end()) {
            found->second++;
        } else if (reasons.size() < k_max_distinct_reasons) {
            reasons.emplace(reason, 1);
        } else {
            n_reasons_elided++;
        }
    }
};

namespace generation_detail {

/// One part's result: its value, or the message from the fitness function that
/// threw. Failure is carried back as a value rather than left in the future, so
/// a part that fails on one formula cannot unwind the whole scoring pass.
struct PartOutcome {
    double value = 0.0;
    std::string error;
};

/// Where one part of one candidate's score sits: which candidate, which of its
/// objectives, and which part of that objective.
struct PartAddress {
    std::size_t candidate = 0;
    std::size_t objective = 0;
    std::size_t part = 0;
};

/// A candidate the cache could not answer, with the work its score decomposes
/// into and the slots that work will fill.
///
/// One planned candidate answers for *every* population slot holding that
/// specification. A population reaching the score stage is largely repeats --
/// stage_pad cycles the survivors back up to the target size, and the whole
/// point of the apportion scheme is replication -- and planning runs before any
/// part is dispatched, so every repeat would otherwise miss the fitness cache
/// together and dispatch its own copy of the same queries.
template <typename Spec>
struct PlannedCandidate {
    std::vector<std::size_t> indices;
    std::vector<ObjectiveWork> work;
    std::vector<std::vector<double>> values;
    std::size_t outstanding = 0;
    std::string error;
};

/// Folds a finished candidate's part values back into an objective vector,
/// stores it against the specification, and records the scored individual.
///
/// Values are folded in part order whatever order the parts ran in, so the
/// result is a function of the candidate alone.
template <typename Spec, typename Fitness>
void commit_candidate(const PlannedCandidate<Spec>& candidate,
                      const std::vector<Spec>& population,
                      const Fitness& fitness_function,
                      std::vector<Scored<Spec>>& scored,
                      std::vector<bool>& succeeded) {
    std::vector<double> objectives;
    objectives.reserve(candidate.work.size());
    for (std::size_t obj = 0; obj < candidate.work.size(); ++obj) {
        objectives.push_back(
            candidate.work[obj].combine(candidate.values[obj]));
    }
    auto [stored, fitness] = fitness_function.store(
        population[candidate.indices.front()], std::move(objectives));
    for (const std::size_t idx : candidate.indices) {
        scored[idx].specification = population[idx];
        scored[idx].objectives = stored;
        scored[idx].fitness = fitness;
        succeeded[idx] = true;
    }
}

/// Records an already-cached score against the population slot it belongs to.
template <typename Spec>
void commit_cached(std::size_t idx, const std::vector<Spec>& population,
                   std::pair<std::vector<double>, double> cached,
                   std::vector<Scored<Spec>>& scored,
                   std::vector<bool>& succeeded) {
    scored[idx].specification = population[idx];
    scored[idx].objectives = std::move(cached.first);
    scored[idx].fitness = cached.second;
    succeeded[idx] = true;
}

/// Plans the whole population: candidates the fitness cache can answer are
/// recorded straight away, and the rest come back with the work their score
/// decomposes into.
///
/// Nothing here touches an external tool, so the whole population is planned
/// before a single part is dispatched. That is what lets one region hold every
/// part of every candidate rather than one bounded window per candidate.
template <typename Spec, typename Fitness>
std::vector<PlannedCandidate<Spec>> plan_population(
    const std::vector<Spec>& population, const Fitness& fitness_function,
    std::vector<Scored<Spec>>& scored, std::vector<bool>& succeeded,
    const std::function<void()>& report) {
    std::vector<PlannedCandidate<Spec>> planned;
    // The dispatched tasks hold references to the FitnessParts inside this
    // vector, so nothing may grow it once planning has returned. Reserving the
    // whole population up front also spares the planning loop its
    // reallocations.
    planned.reserve(population.size());
    std::unordered_map<Spec, std::size_t> planned_by_spec;
    planned_by_spec.reserve(population.size());
    for (std::size_t idx = 0; idx < population.size(); ++idx) {
        std::optional<std::vector<double>> cached =
            fitness_function.cached_objectives(population[idx]);
        if (cached.has_value()) {
            // Paired with its scalar directly rather than handed back through
            // store(), which would hash and compare the whole specification a
            // second time to re-emplace a key it just answered from.
            const double fitness = fitness_function.scalar(*cached);
            commit_cached(idx, population, {std::move(*cached), fitness},
                          scored, succeeded);
            report();
            continue;
        }
        const auto repeat = planned_by_spec.find(population[idx]);
        if (repeat != planned_by_spec.end()) {
            planned[repeat->second].indices.push_back(idx);
            continue;
        }
        PlannedCandidate<Spec> candidate;
        candidate.indices.push_back(idx);
        candidate.work = fitness_function.plan(population[idx]);
        candidate.values.reserve(candidate.work.size());
        for (const ObjectiveWork& objective : candidate.work) {
            candidate.values.emplace_back(objective.parts.size(), 0.0);
            candidate.outstanding += objective.parts.size();
        }
        // A candidate every objective decomposed to nothing has an answer
        // already -- a semantic-only score whose every slot matched, say -- and
        // would otherwise never reach the collector that folds it.
        if (candidate.outstanding == 0) {
            commit_candidate(candidate, population, fitness_function, scored,
                             succeeded);
            report();
            continue;
        }
        planned_by_spec.emplace(population[idx], planned.size());
        planned.push_back(std::move(candidate));
    }
    return planned;
}

/// Files one finished part against the candidate it belongs to, folding that
/// candidate's score once its last part has landed.
///
/// Returns how many population slots this part completed -- zero unless it was
/// the candidate's last -- so the caller reports progress in individuals rather
/// than in parts. Called from the dispatcher thread alone, so nothing here
/// needs a lock.
template <typename Spec, typename Fitness>
std::size_t collect_part(const PartAddress& address, PartOutcome outcome,
                         std::vector<PlannedCandidate<Spec>>& planned,
                         const std::vector<Spec>& population,
                         const Fitness& fitness_function,
                         std::vector<Scored<Spec>>& scored,
                         std::vector<bool>& succeeded,
                         std::vector<std::string>& errors) {
    PlannedCandidate<Spec>& candidate = planned[address.candidate];
    if (outcome.error.empty()) {
        candidate.values[address.objective][address.part] = outcome.value;
    } else if (candidate.error.empty()) {
        // The first message is kept and the rest discarded: a candidate is
        // dropped whole however many of its parts failed, and both the failure
        // tolerance and the progress report count individuals.
        candidate.error = std::move(outcome.error);
    }
    if (--candidate.outstanding > 0) {
        return 0;
    }
    if (candidate.error.empty()) {
        commit_candidate(candidate, population, fitness_function, scored,
                         succeeded);
    } else {
        // One error per slot the candidate answered for, so the failure
        // tolerance counts individuals dropped rather than distinct
        // specifications that failed.
        errors.insert(errors.end(), candidate.indices.size(), candidate.error);
    }
    return candidate.indices.size();
}

/// Every part of every planned candidate, addressed so the dispatcher can hand
/// a result back to the slot it belongs in.
template <typename Spec>
std::vector<PartAddress> part_addresses(
    const std::vector<PlannedCandidate<Spec>>& planned) {
    std::vector<PartAddress> addresses;
    for (std::size_t slot = 0; slot < planned.size(); ++slot) {
        for (std::size_t obj = 0; obj < planned[slot].work.size(); ++obj) {
            const std::size_t n_parts = planned[slot].work[obj].parts.size();
            for (std::size_t part = 0; part < n_parts; ++part) {
                addresses.push_back({slot, obj, part});
            }
        }
    }
    return addresses;
}

}  // namespace generation_detail

/// Scores each specification using a weighted average of all fitness functions:
///   fitness = sum(fn_i(spec) * w_i) / sum(w_i)
///
/// Every candidate's work goes into **one** dispatch region, split as far as
/// each objective can split it: a term per changed requirement slot on the
/// similarity objectives, a query per component plus the realizability walk on
/// the status one. Scoring a candidate used to be a single task, and a single
/// task is a serial chain of subprocess calls pinned to one pool worker, so the
/// slowest candidate of a generation held one worker while the rest of the pool
/// drained -- measured at 54% to 84% worker occupancy over the score stages of
/// a 20-worker run, with up to a third of a stage spent with two parts left.
/// Splitting the chain is the only thing that shortens it; more workers cannot.
///
/// Parts are launched longest-first (@ref cost_ordered_indices) over the whole
/// region, so a candidate's synthesis walk goes out ahead of every cheap
/// satisfiability query rather than behind whichever candidates happened to
/// have a lower index.
///
/// None of this reaches the result. Parts are collected by address and folded
/// in part order, candidates are compacted in population order, and no part
/// draws from the RandomSource, so the output is what a serial sweep would
/// give and a seeded run reproduces.
///
/// An individual whose scoring throws is dropped: the returned population is
/// shorter than @p population, in the same relative order. Above
/// Config::max_scoring_failure_rate of the population the failure is taken to
/// be systematic (a missing or broken external tool) rather than specific to
/// one formula, and the run aborts instead of evolving noise.
///
/// @param cfg               Algorithm configuration (max_scoring_failure_rate)
/// @param population        The population to score
/// @param fitness_function  Non-empty set of weighted fitness functions
/// @param on_progress       Optional callback invoked as individuals finish;
///                          receives (done, total) counts of individuals, not
///                          of parts
/// @return                  Successfully scored population paired with their
///                          aggregated fitness scores
/// @throws std::invalid_argument if fitness_function is empty or total weight
///                               is not positive
/// @throws std::runtime_error if more than max_scoring_failure_rate of the
///                            population failed to score
template <typename Spec, typename Fitness>
std::vector<Scored<Spec>> score_population(
    const Config& cfg, const std::vector<Spec>& population,
    const Fitness& fitness_function,
    const GenerationProgressCallback& on_progress = nullptr) {
    assert(!fitness_function.empty());
    std::vector<Scored<Spec>> scored(population.size());
    std::vector<bool> succeeded(population.size(), false);
    std::vector<std::string> errors;
    std::size_t done = 0;
    const std::size_t total = population.size();
    const std::function<void()> report = [&on_progress, &done, total] {
        if (on_progress) {
            on_progress(++done, total);
        }
    };

    std::vector<generation_detail::PlannedCandidate<Spec>> planned =
        generation_detail::plan_population(population, fitness_function, scored,
                                           succeeded, report);
    const std::vector<generation_detail::PartAddress> addresses =
        generation_detail::part_addresses(planned);
    // Stable for the whole region: planning is finished, so nothing reallocates
    // the vector these reference into.
    const auto part_at = [&addresses,
                          &planned](std::size_t item) -> const FitnessPart& {
        const generation_detail::PartAddress& address = addresses[item];
        return planned[address.candidate]
            .work[address.objective]
            .parts[address.part];
    };

    run_bounded_async(
        addresses.size(), dispatch_window(),
        [&part_at](std::size_t item) {
            return [&part = part_at(item)] {
                generation_detail::PartOutcome outcome;
                try {
                    outcome.value = part.run();
                } catch (const std::exception& exc) {
                    outcome.error = exc.what();
                }
                return outcome;
            };
        },
        [&addresses, &planned, &population, &fitness_function, &scored,
         &succeeded, &errors,
         &report](std::size_t item, generation_detail::PartOutcome outcome) {
            const std::size_t finished = generation_detail::collect_part(
                addresses[item], std::move(outcome), planned, population,
                fitness_function, scored, succeeded, errors);
            for (std::size_t i = 0; i < finished; ++i) {
                report();
            }
        },
        cost_ordered_indices(addresses.size(), [&part_at](std::size_t item) {
            return part_at(item).cost;
        }));

    // A single failure is tolerated whatever the population size, so a small
    // population is not held to a stricter standard than a large one -- but
    // never the whole population, since evolution cannot continue from nothing.
    const std::size_t tolerated =
        population.empty()
            ? 0
            : std::min(population.size() - 1,
                       std::max<std::size_t>(
                           1, static_cast<std::size_t>(
                                  cfg.max_scoring_failure_rate *
                                  static_cast<double>(population.size()))));
    if (errors.size() > tolerated) {
        throw std::runtime_error(
            "scoring failed for " + std::to_string(errors.size()) + " of " +
            std::to_string(population.size()) + " individuals (tolerating " +
            std::to_string(tolerated) +
            "); the fitness tooling is broken rather than the formulae. First "
            "error: " +
            errors.front());
    }
    for (const std::string& error : errors) {
        ScoringStats::record(error);
    }

    // Compacting by index keeps the surviving order independent of the order
    // the workers happened to finish in, so a fixed RNG seed stays
    // reproducible.
    std::vector<Scored<Spec>> survivors;
    survivors.reserve(population.size() - errors.size());
    for (std::size_t idx = 0; idx < scored.size(); ++idx) {
        if (succeeded[idx]) {
            survivors.push_back(std::move(scored[idx]));
        }
    }
    return survivors;
}

/// FRETISH overload of score_population. Provided so callers can pass a
/// braced-init-list population (from which the Spec template parameter cannot
/// be deduced); forwards to the generic template.
inline std::vector<ScoredSpecification> score_population(
    const Config& cfg, const std::vector<Specification>& population,
    const AggregateWeightedFitnessFunction& fitness_function,
    const GenerationProgressCallback& on_progress = nullptr) {
    return score_population<Specification, AggregateWeightedFitnessFunction>(
        cfg, population, fitness_function, on_progress);
}

/// Applies filter functions sequentially; each filter receives the survivors
/// from the previous one.
///
/// @param population       The population to filter
/// @param filter_functions Filters applied in order; empty list keeps all
/// @return                 Surviving specifications
template <typename Spec>
std::vector<Spec> filter_population(
    std::vector<Spec> population,
    const std::vector<FilterFunctionT<Spec>>& filter_functions) {
    for (const FilterFunctionT<Spec>& filter_fn : filter_functions) {
        population = filter_fn(std::move(population));
    }
    return population;
}

/// Returns the standard set of filter functions used during evolution, in
/// order: deduplication, a bloat cap and the vacuity filter.
/// Every one runs on every generation; the implication screen is not here but
/// in get_final_filter_functions, which runs once over the survivors.
///
/// @param original  The reference specification the bloat cap is sized against
/// @param checker   Satisfiability checker; captured by reference, must
///                  outlive the returned filters
std::vector<FilterFunction> get_filter_functions(
    const Specification& original, SatisfiabilityChecker& checker);

/// Returns the set of filter functions applied to the final realizable
/// population after evolution: deduplication, then (if run_implication_filter)
/// the implication filter.
///
/// @param cfg              Algorithm configuration (run_implication_filter)
/// @param original         The specification under repair; the implication
///                         filter keeps the member of an equivalence class
///                         closest to it
/// @param checker          Satisfiability checker for the implication filter;
///                         captured by reference, must outlive the filters
/// @param on_impl_progress Optional progress callback forwarded to the
///                         implication filter
std::vector<FilterFunction> get_final_filter_functions(
    const Config& cfg, Specification original, SatisfiabilityChecker& checker,
    const GenerationProgressCallback& on_impl_progress = nullptr);

/// Scores each specification in @p specs and returns them ordered best-first
/// according to @p cfg's selection scheme: descending weighted fitness for
/// WeightedAverage, or the NSGA-II crowded-comparison order for Nsga2Truncate
/// and Nsga2Apportion.
std::vector<ScoredSpecification> score_and_sort_specifications(
    const Config& cfg, const std::vector<Specification>& specs,
    const AggregateWeightedFitnessFunction& fitness_function);

/// Wraps each filter as a named pipeline stage, so a consumer walking the stage
/// list sees one entry per active filter. The filters keep their own n_in/n_out
/// tallies, which the drivers still fold into their end-of-run reports.
///
/// @param filters Captured by reference; must outlive the returned stages.
template <typename Spec>
std::vector<PipelineStage<Spec>> filter_stages(
    const std::vector<FilterFunctionT<Spec>>& filters) {
    std::vector<PipelineStage<Spec>> stages;
    stages.reserve(filters.size());
    for (const FilterFunctionT<Spec>& filter : filters) {
        // Filters built inline for tests carry no display name.
        std::string name = filter.name().empty() ? "filter" : filter.name();
        stages.emplace_back(
            std::move(name), [&filter](GenerationContext<Spec>& ctx) {
                ctx.m_candidates = filter(std::move(ctx.m_candidates));
            });
    }
    return stages;
}

/// Builds the rescue stage_filter_fallback runs when the chain empties the
/// population: the correctness filters of @p filters alone, in their original
/// order, applied to the unfiltered offspring.
///
/// The chain runs in series, so the correctness filters only ever judge what
/// the preference filters passed on to them. A candidate the bloat cap dropped
/// is therefore not a candidate that failed well-separation; it is one whose
/// well-separation was never asked about. Re-running the correctness filters
/// over the whole unfiltered set asks, and what survives is exactly the
/// candidates that are correct but were culled for being duplicates or
/// oversized. Every candidate that already reached those filters is a hit in
/// the solver caches they share, so the rescue re-tests only what the chain
/// never tested.
///
/// Holds copies of the filters rather than references, so re-applying one does
/// not overwrite the n_in/n_out tallies the drivers accumulate per generation
/// from the main pass.
template <typename Spec>
PopulationRescue<Spec> correctness_rescue(
    const std::vector<FilterFunctionT<Spec>>& filters) {
    std::vector<FilterFunctionT<Spec>> correctness;
    for (const FilterFunctionT<Spec>& filter : filters) {
        if (filter.kind() == FilterKind::Correctness) {
            correctness.push_back(filter);
        }
    }
    return
        [correctness = std::move(correctness)](const std::vector<Spec>& pop) {
            return filter_population(pop, correctness);
        };
}

/// The names of every stage a generation can run, in pipeline order, counting
/// filters that only run on some generations.
///
/// A consumer needs the full roster up front to reserve a layout that does not
/// move between generations; which stages actually ran in a given generation
/// still comes from the stage reports. Built from the same pipeline the run
/// uses, so it cannot drift from it.
template <typename Spec>
std::vector<std::string> generation_stage_names(
    const std::vector<FilterFunctionT<Spec>>& filters) {
    std::vector<std::string> names;
    for (const PipelineStage<Spec>& stage : make_generation_pipeline<Spec>(
             filter_stages(filters), correctness_rescue(filters))) {
        names.push_back(stage.name());
    }
    return names;
}

/// Generic one-generation evolution loop, templated on the specification
/// element type @p Spec and any callable fitness type @p Fitness. The three
/// genetic operators (crossover, mutation, simplification) are injected via
/// @p ops rather than hardcoded, so different Spec types supply their own.
///
/// See evolve_generation() for the algorithm; the behaviour is identical.
template <typename Spec, typename Fitness>
std::vector<Scored<Spec>> evolve_generation_generic(
    const Config& cfg, const std::vector<Scored<Spec>>& population,
    std::size_t target_size, std::size_t elitism_size,
    const Fitness& fitness_functions,
    const std::vector<FilterFunctionT<Spec>>& filter_functions,
    const GeneticOperators<Spec>& ops, const RandomSource& random_source,
    const GenerationProgressCallback& on_progress = nullptr,
    const StageObserver& on_stage = nullptr, SearchBudget* budget = nullptr) {
    assert(random_source);
    assert(!fitness_functions.empty());
    assert(cfg.crossover_rate >= 0.0 && cfg.crossover_rate <= 1.0);
    assert(cfg.mutation_rate >= 0.0 && cfg.mutation_rate <= 1.0);
    assert(!population.empty());

    GenerationContext<Spec> ctx(
        cfg, population, target_size, elitism_size, ops, random_source,
        [&cfg, &fitness_functions,
         &on_progress](const std::vector<Spec>& candidates) {
            return score_population(cfg, candidates, fitness_functions,
                                    on_progress);
        },
        budget);
    const std::vector<PipelineStage<Spec>> stages =
        make_generation_pipeline<Spec>(filter_stages(filter_functions),
                                       correctness_rescue(filter_functions));
    return run_generation_pipeline(ctx, stages, on_stage);
}

/// Returns the bundle of FRETISH genetic operators wiring
/// crossover_specifications, mutate_specification, and simplify_offspring for
/// use with evolve_generation_generic.
const GeneticOperators<Specification>& fretish_operators();

/// Evolves a population for one generation:
///   1. Order the population best-first under @p cfg's selection scheme —
///      descending weighted fitness for WeightedAverage, the NSGA-II
///      crowded-comparison order for Nsga2Truncate and Nsga2Apportion — and
///      take the top target_size as parents
///   2. Carry the best elitism_size parents over verbatim as elites (they skip
///      crossover, mutation, and the offspring filters)
///   3. For the remaining parents, apply crossover and mutation to produce
///      offspring
///   4. Apply filter functions sequentially to the offspring to produce
///      survivors, then add the elites back
///   5. Pad survivors back to target_size by duplicating them if filtering
///      reduced the population
///   6. Score the resulting population with fitness functions
///   7. Choose the survivors: under Nsga2Truncate and Nsga2Apportion the
///      parents are pooled with the scored offspring and ranked under the
///      crowded-comparison order ((mu+lambda) elitism), Nsga2Truncate keeping
///      the best target_size of that union and Nsga2Apportion deduplicating
///      the pool before ranking it and apportioning the target_size slots
///      over the distinct survivors; under WeightedAverage the scored
///      population is re-sorted by the weighted scalar
///
/// If the population is smaller than target_size, all of it is used as
/// parents. elitism_size is clamped to the number of parents.
///
/// @param cfg               Algorithm configuration (rates and filter flags)
/// @param population        Current generation's specifications
/// @param target_size       Number of offspring to produce
/// @param elitism_size      Number of top parents carried over verbatim;
///                          must be less than target_size
/// @param fitness_function  Non-empty weighted fitness function for scoring
/// @param filter_functions  Filters applied to the offspring after crossover
///                          and mutation, before scoring
/// @param random_source     Random source for crossover and mutation
/// @param on_progress       Optional callback invoked after each individual is
///                          scored; receives (done, total) counts
/// @param on_stage          Optional callback invoked after each pipeline stage
///                          completes; receives the stage's name, population
///                          sizes, and elapsed time
/// @param budget            Optional run-level budget; breeding stops part-way
///                          through the generation when it runs out, so the
///                          result can then be smaller than target_size
/// @return                  Next generation of target_size specifications
/// @throws std::invalid_argument if random_source is not callable, if
///                               fitness_function is empty, if rates are
///                               outside [0, 1]
std::vector<ScoredSpecification> evolve_generation(
    const Config& cfg, const std::vector<ScoredSpecification>& population,
    std::size_t target_size, std::size_t elitism_size,
    const AggregateWeightedFitnessFunction& fitness_function,
    const std::vector<FilterFunction>& filter_functions,
    const RandomSource& random_source,
    const GenerationProgressCallback& on_progress = nullptr,
    const StageObserver& on_stage = nullptr, SearchBudget* budget = nullptr);
