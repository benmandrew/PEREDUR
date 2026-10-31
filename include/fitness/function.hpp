#pragma once

/// @file function.hpp
/// @brief Fitness function types: FitnessFunction, WeightedFitnessFunction,
///        AggregateWeightedFitnessFunction, and the factory
///        get_fitness_function.

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config.hpp"
#include "profile.hpp"
#include "requirement.hpp"

/// Default weight for WeightedFitnessFunction when not explicitly specified.
inline constexpr double k_default_fitness_weight = 1.0;

/// A fitness function scores a specification element, returning a value in
/// [0, 1].
template <typename Spec>
using FitnessFunctionT = std::function<double(const Spec&)>;

/// Relative cost hints for ordering the launches of one scoring region, as the
/// number of external-tool calls a part makes. Only the order these induce is
/// read, never the magnitudes, so they are counts rather than times.
inline constexpr double k_part_cost_in_process = 0.0;
inline constexpr double k_part_cost_satisfiability = 1.0;
inline constexpr double k_part_cost_model_count = 3.0;
inline constexpr double k_part_cost_synthesis = 10.0;

/// One independently schedulable piece of an objective's work for one
/// candidate.
struct FitnessPart {
    std::function<double()> run;
    /// Launched before cheaper parts; see @ref cost_ordered_indices.
    double cost = k_part_cost_in_process;
};

/// An objective's work for one candidate: parts that may run in any order on
/// any thread, and the fold that turns their results back into the objective's
/// score.
///
/// This exists because scoring one candidate was a serial chain of subprocess
/// calls on a single pool worker -- a semantic term per requirement slot, then
/// a satisfiability call per component, then a synthesis walk -- so the last
/// candidate of a generation held one worker while the rest of the pool sat
/// idle, and no number of workers shortened it. The parts of every candidate
/// go into one flat dispatch region instead.
///
/// The parts capture the candidate by reference, so the specification they were
/// built from must outlive them. `combine` receives their results in part
/// order, whatever order they actually ran in, so the score is a function of
/// the candidate alone.
struct ObjectiveWork {
    std::vector<FitnessPart> parts;
    std::function<double(const std::vector<double>&)> combine;
    /// Optional. Given the same part values as `combine`, the guarantee slots
    /// the objective blames; see ObjectiveRecord::refused. Only the status
    /// objective sets it.
    std::function<std::vector<std::size_t>(const std::vector<double>&)>
        refused = nullptr;
};

/// What the fitness cache holds for one candidate: its objective vector, and
/// the guarantee slots its status walk refused.
///
/// The refused set is carried with the objectives rather than recomputed,
/// so that a cache hit hands breeding exactly what the walk that scored the
/// candidate found, without asking a single query again.
struct ObjectiveRecord {
    /// Per-objective scores, in registration order.
    std::vector<double> values;
    /// Indices into `m_guarantees` of the guarantees the MRS walk refused,
    /// ascending. Empty when the walk did not grade the candidate: a run
    /// without a status objective, StatusGrading::Aurus, a candidate the
    /// component or whole-specification screen scored 0, and the TLSF path,
    /// whose walk runs over conjuncts that no slot names.
    std::vector<std::size_t> refused;
};

/// Splits an objective's work for one candidate into parts. An objective
/// without one runs as a single part, which is what the aggregate substitutes.
template <typename Spec>
using FitnessDecompositionT = std::function<ObjectiveWork(const Spec&)>;

/// A fitness function paired with a weight for weighted-average aggregation.
/// The default weight is given by k_default_fitness_weight.
template <typename Spec>
struct WeightedFitnessFunctionT {
    /// The whole objective in one call. May be empty when `decompose` is set,
    /// and then the serial path runs the parts in order and folds them. The
    /// status objective keeps both, because its whole walk stops at the first
    /// unsatisfiable component and its parts cannot.
    FitnessFunctionT<Spec> function;
    double weight = k_default_fitness_weight;
    std::string name;
    /// Optional. Absent means `function` is indivisible and runs as one part.
    /// Defaulted rather than merely default-constructible so that the many
    /// three-field brace initialisations of this aggregate keep compiling under
    /// -Wmissing-field-initializers.
    FitnessDecompositionT<Spec> decompose = nullptr;
    /// Optional. `function` that also reports the guarantee slots it blames
    /// (see ObjectiveRecord::refused). Read in its place by the serial path
    /// where set.
    std::function<double(const Spec&, std::vector<std::size_t>&)>
        function_with_refused = nullptr;

    /// Scores @p spec on the calling thread: `function` where there is one,
    /// otherwise every part of `decompose` in order, folded by its `combine`.
    double operator()(const Spec& spec) const {
        std::vector<std::size_t> refused;
        return evaluate(spec, refused);
    }

    /// As operator(), filling @p refused with the slots the objective blames,
    /// which is empty for every objective but status.
    double evaluate(const Spec& spec, std::vector<std::size_t>& refused) const {
        refused.clear();
        if (function_with_refused) {
            return function_with_refused(spec, refused);
        }
        if (function) {
            return function(spec);
        }
        assert(decompose);
        const ObjectiveWork work = decompose(spec);
        std::vector<double> values;
        values.reserve(work.parts.size());
        for (const FitnessPart& part : work.parts) {
            values.push_back(part.run());
        }
        if (work.refused) {
            refused = work.refused(values);
        }
        return work.combine(values);
    }
};

/// Aggregates multiple WeightedFitnessFunctionT instances into a single
/// function that computes a weighted average of their scores. Results are
/// memoised so each unique specification element is scored at most once per
/// instance.
///
/// The memoisation cache retains the individual per-objective scores (one per
/// aggregated function, in registration order), not just the collapsed
/// weighted average. Multi-objective selection (NSGA-II) reads the raw vector
/// via objectives(); the weighted scalar remains available through
/// operator().
/// The fitness cache's hit and miss totals, shared by every instantiation of
/// the template below.
///
/// They live outside it because `inline static` members of a class template
/// are per-instantiation, and the manifest reads one instantiation. FRETISH
/// runs are scored through `AggregateWeightedFitnessFunctionT<Specification>`
/// and TLSF ones through `AggregateWeightedFitnessFunctionT<tlsf::-
/// Specification>`, so every archived TLSF campaign records this cache as
/// `{hits: 0, misses: 0}` while the matching FRETISH runs record 16,047
/// against 3,955 -- the top-level cache, on the path PEREDUR is now
/// benchmarked against, reporting nothing at all.
struct FitnessCacheStats {
    inline static std::size_t n_hits = 0;
    inline static std::size_t n_misses = 0;
};

template <typename Spec>
class AggregateWeightedFitnessFunctionT {
   private:
    std::vector<WeightedFitnessFunctionT<Spec>> m_fitness_functions;
    mutable std::unordered_map<Spec, ObjectiveRecord> m_cache;
    mutable std::unique_ptr<std::mutex> m_cache_mutex =
        std::make_unique<std::mutex>();
    /// One profiler site per objective, resolved once at construction: the
    /// names are only known at run time, and interning them per call would
    /// charge the objective for the profiler's own string handling.
    std::vector<profile::Site*> m_profile_sites;
    const double m_total_weight;

    /// Returns the per-objective scores for @p spec, computing and caching
    /// them on the first request and reusing the cached vector thereafter.
    /// Scoring runs outside the lock, so two threads that miss on the same
    /// spec both score it; the second emplace finds the key taken and returns
    /// the winner's entry, which is the same value. The duplicated work is the
    /// price of not holding the mutex across an objective that shells out to
    /// black or ltlsynt, which would serialise the whole scoring pool.
    ObjectiveRecord objectives_cached(const Spec& spec) const {
        {
            std::scoped_lock lock(*m_cache_mutex);
            const auto cache_iter = m_cache.find(spec);
            if (cache_iter != m_cache.end()) {
                FitnessCacheStats::n_hits++;
                return cache_iter->second;
            }
            FitnessCacheStats::n_misses++;
        }
        ObjectiveRecord record;
        record.values.reserve(m_fitness_functions.size());
        std::vector<std::size_t> refused;
        for (std::size_t i = 0; i < m_fitness_functions.size(); ++i) {
            const profile::Scope scope(*m_profile_sites[i]);
            record.values.push_back(
                m_fitness_functions[i].evaluate(spec, refused));
            if (!refused.empty()) {
                record.refused = std::move(refused);
                refused.clear();
            }
        }
        std::scoped_lock lock(*m_cache_mutex);
        return m_cache.emplace(spec, std::move(record)).first->second;
    }

    double weighted_average(const std::vector<double>& objectives) const {
        double weighted_sum = 0.0;
        for (std::size_t i = 0; i < m_fitness_functions.size(); ++i) {
            weighted_sum += m_fitness_functions[i].weight * objectives[i];
        }
        return m_total_weight > 0.0 ? weighted_sum / m_total_weight : 0.0;
    }

   public:
    explicit AggregateWeightedFitnessFunctionT(
        std::vector<WeightedFitnessFunctionT<Spec>> fitness_functions)
        : m_fitness_functions(std::move(fitness_functions)),
          m_total_weight([&]() {
              double total = 0.0;
              for (const auto& wff : m_fitness_functions) {
                  total += wff.weight;
              }
              return total;
          }()) {
        m_profile_sites.reserve(m_fitness_functions.size());
        for (const auto& wff : m_fitness_functions) {
            m_profile_sites.push_back(
                &profile::site_interned("fitness/" + wff.name));
        }
    }

    /// Computes the weighted average fitness score for a specification
    /// element.
    ///
    /// @return The weighted average fitness score, or 0.0 if total weight is
    /// not positive.
    double operator()(const Spec& spec) const {
        return weighted_average(objectives_cached(spec).values);
    }

    /// Returns the individual per-objective scores for @p spec, in the
    /// registration order of the aggregated fitness functions.
    std::vector<double> objectives(const Spec& spec) const {
        return objectives_cached(spec).values;
    }

    /// As objectives_and_fitness, with the refused guarantees alongside.
    std::pair<ObjectiveRecord, double> record_and_fitness(
        const Spec& spec) const {
        ObjectiveRecord record = objectives_cached(spec);
        const double scalar = weighted_average(record.values);
        return {std::move(record), scalar};
    }

    /// Scores @p spec once, returning both the raw per-objective vector and
    /// the weighted-average scalar derived from it, with a single cache
    /// lookup. Used by score_population so a scored element carries both.
    std::pair<std::vector<double>, double> objectives_and_fitness(
        const Spec& spec) const {
        std::vector<double> values = objectives_cached(spec).values;
        const double scalar = weighted_average(values);
        return {std::move(values), scalar};
    }

    /// The cached objective vector for @p spec, or nothing when it has not
    /// been scored yet. Counts against the same hit/miss totals the serial
    /// path reports, so a split scoring run's cache rate stays comparable.
    std::optional<std::vector<double>> cached_objectives(
        const Spec& spec) const {
        std::optional<ObjectiveRecord> record = cached_record(spec);
        if (!record.has_value()) {
            return std::nullopt;
        }
        return std::move(record->values);
    }

    /// As cached_objectives, with the refused guarantees alongside.
    std::optional<ObjectiveRecord> cached_record(const Spec& spec) const {
        const std::scoped_lock lock(*m_cache_mutex);
        const auto cache_iter = m_cache.find(spec);
        if (cache_iter != m_cache.end()) {
            FitnessCacheStats::n_hits++;
            return cache_iter->second;
        }
        FitnessCacheStats::n_misses++;
        return std::nullopt;
    }

    /// The work of scoring @p spec, one entry per objective in registration
    /// order.
    ///
    /// Enumerates parts without running any of them, so a caller may plan a
    /// whole population and dispatch every part of it as one region. The parts
    /// hold @p spec by reference and must not outlive it.
    ///
    /// An objective with no decomposition contributes one part wrapping its
    /// whole function, so a caller never has to ask which kind it got.
    [[nodiscard]] std::vector<ObjectiveWork> plan(const Spec& spec) const {
        std::vector<ObjectiveWork> work;
        work.reserve(m_fitness_functions.size());
        for (std::size_t i = 0; i < m_fitness_functions.size(); ++i) {
            const WeightedFitnessFunctionT<Spec>& wff = m_fitness_functions[i];
            ObjectiveWork objective;
            if (wff.decompose) {
                objective = wff.decompose(spec);
            } else {
                // Costed as a synthesis call: an undecomposed objective is
                // opaque, and launching an unknown ahead of a known-cheap part
                // is the safer half of the guess.
                objective.parts.push_back(
                    {[&wff, &spec] { return wff.function(spec); },
                     k_part_cost_synthesis});
                objective.combine = [](const std::vector<double>& values) {
                    return values.front();
                };
            }
            // Charged to the objective's own profiler site, as the serial path
            // charges its one call, so the site still reports the objective's
            // total wall rather than losing it to the dispatcher.
            for (FitnessPart& part : objective.parts) {
                part.run = [site = m_profile_sites[i],
                            run = std::move(part.run)] {
                    const profile::Scope scope(*site);
                    return run();
                };
            }
            work.push_back(std::move(objective));
        }
        return work;
    }

    /// Records @p objectives as @p spec's score and returns it with the
    /// weighted scalar.
    ///
    /// A concurrent scorer that stored first keeps its entry, which is the
    /// same value: the objectives are a function of the specification, and
    /// duplicated work is the price the cache already pays for not holding its
    /// mutex across a subprocess.
    std::pair<std::vector<double>, double> store(
        const Spec& spec, std::vector<double> values) const {
        auto [record, scalar] =
            store(spec, ObjectiveRecord{std::move(values), {}});
        return {std::move(record.values), scalar};
    }

    /// As store, for a whole record.
    std::pair<ObjectiveRecord, double> store(const Spec& spec,
                                             ObjectiveRecord record) const {
        const std::scoped_lock lock(*m_cache_mutex);
        const ObjectiveRecord& stored =
            m_cache.emplace(spec, std::move(record)).first->second;
        return {stored, weighted_average(stored.values)};
    }

    /// The position of the objective named @p name in an objective vector, or
    /// nothing when the run does not score it (its weight was 0).
    [[nodiscard]] std::optional<std::size_t> objective_index(
        const std::string& name) const {
        for (std::size_t i = 0; i < m_fitness_functions.size(); ++i) {
            if (m_fitness_functions[i].name == name) {
                return i;
            }
        }
        return std::nullopt;
    }

    /// Number of aggregated objectives (per-element vector length).
    [[nodiscard]] std::size_t n_objectives() const {
        return m_fitness_functions.size();
    }

    double total_weight() const { return m_total_weight; }

    /// The weighted-average scalar of an objective vector. Exposed so a caller
    /// holding a cached vector can pair it with its scalar without a second
    /// pass through the cache.
    [[nodiscard]] double scalar(const std::vector<double>& values) const {
        return weighted_average(values);
    }

    /// Checks if the collection of fitness functions is empty.
    [[nodiscard]] bool empty() const { return m_fitness_functions.empty(); }

    [[nodiscard]] auto begin() const { return m_fitness_functions.begin(); }
    [[nodiscard]] auto end() const { return m_fitness_functions.end(); }
};

/// The FRETISH fitness function type aliases.
using FitnessFunction = FitnessFunctionT<Specification>;
using WeightedFitnessFunction = WeightedFitnessFunctionT<Specification>;
using AggregateWeightedFitnessFunction =
    AggregateWeightedFitnessFunctionT<Specification>;

/// Builds the standard set of weighted fitness functions from @p cfg weights.
/// Functions with a zero weight are omitted. The caller owns the returned
/// object and may invoke it repeatedly; results are memoised internally.
///
/// @param original_spec  Reference specification for similarity scoring
/// @param cfg            Configuration providing fitness weights
AggregateWeightedFitnessFunction get_fitness_function(
    const Specification& original_spec, const Config& cfg);
