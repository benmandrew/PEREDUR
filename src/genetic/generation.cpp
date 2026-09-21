#include "genetic/generation.hpp"

#include <cassert>
#include <cstddef>
#include <functional>
#include <optional>
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

// A stop timing with its stop simplified. `until false` is Always spelt another
// way, so it folds into Always rather than splitting the cache keys of one
// obligation. `until true` and `before true` gut or contradict their
// requirement and are left to the filters, which see the lowered formula.
Timing simplify_stop_timing(const Timing& timing) {
    const Formula* stop = timing_stop(timing);
    assert(stop != nullptr);
    Formula simplified = *stop;
    simplified.simplify();
    if (std::holds_alternative<timing::Before>(timing)) {
        return timing::before(std::move(simplified));
    }
    if (simplified.atom_name() == std::optional<std::string>("false")) {
        return timing::always();
    }
    return timing::until(std::move(simplified));
}

}  // namespace

Specification simplify_offspring(Specification offspring) {
    Specification pre_simplify = offspring;
    // Removed requirements are left alone alongside locked ones. Their content
    // is never read again, so simplifying it buys nothing, and rewriting it
    // could collapse two tombstones onto the same shape and hand the dedup
    // below a size change that discards the whole offspring.
    const auto simplify_all = [](std::vector<Requirement>& reqs) {
        for (auto& req : reqs) {
            if (!req.m_weakenable || req.m_removed) {
                continue;
            }
            req.m_condition.simplify();
            req.m_response.simplify();
            if (timing_stop(req.m_timing) != nullptr) {
                req.m_timing = simplify_stop_timing(req.m_timing);
            }
            req.m_ltl = requirement_to_ltl(req);
        }
    };
    simplify_all(offspring.m_assumptions);
    simplify_all(offspring.m_guarantees);
    Specification rededuped(offspring.m_assumptions, offspring.m_guarantees,
                            offspring.m_in_atoms, offspring.m_out_atoms,
                            offspring.m_modes);
    if (rededuped.m_assumptions.size() != pre_simplify.m_assumptions.size() ||
        rededuped.m_guarantees.size() != pre_simplify.m_guarantees.size()) {
        return pre_simplify;
    }
    return rededuped;
}

const GeneticOperators<Specification>& fretish_operators() {
    static const GeneticOperators<Specification> ops{
        [](const Specification& first, const Specification& second,
           const RandomSource& random_source, const Config&) {
            return crossover_specifications(first, second, random_source);
        },
        [](const Specification& spec, const RandomSource& random_source,
           const Config& cfg) {
            return mutate_specification(spec, random_source, cfg);
        },
        [](Specification spec) { return simplify_offspring(std::move(spec)); }};
    return ops;
}

template <typename Spec>
FilterFunctionT<Spec> make_predicate_filter(
    std::string name,
    std::function<bool(const typename NonDeduced<Spec>::type&)> predicate,
    std::size_t max_in_flight, FilterKind kind) {
    return {std::move(name),
            [predicate = std::move(predicate),
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

template FilterFunctionT<Specification> make_predicate_filter<Specification>(
    std::string, std::function<bool(const Specification&)>, std::size_t,
    FilterKind);
template FilterFunctionT<tlsf::Specification>
make_predicate_filter<tlsf::Specification>(
    std::string, std::function<bool(const tlsf::Specification&)>, std::size_t,
    FilterKind);

std::vector<ScoredSpecification> evolve_generation(
    const Config& cfg, const std::vector<ScoredSpecification>& population,
    std::size_t target_size, std::size_t elitism_size,
    const AggregateWeightedFitnessFunction& fitness_functions,
    const std::vector<FilterFunction>& filter_functions,
    const RandomSource& random_source,
    const GenerationProgressCallback& on_progress,
    const StageObserver& on_stage, SearchBudget* budget) {
    return evolve_generation_generic<Specification>(
        cfg, population, target_size, elitism_size, fitness_functions,
        filter_functions, fretish_operators(), random_source, on_progress,
        on_stage, budget);
}

template <typename Spec>
std::vector<FilterFunctionT<Spec>> get_filter_functions(
    const Spec& original, SatisfiabilityChecker& checker) {
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
                check.name, check.admissible, max_in_flight));
        }
    }
    return filters;
}

template std::vector<FilterFunctionT<Specification>> get_filter_functions(
    const Specification&, SatisfiabilityChecker&);
template std::vector<FilterFunctionT<tlsf::Specification>> get_filter_functions(
    const tlsf::Specification&, SatisfiabilityChecker&);

std::vector<FilterFunction> get_final_filter_functions(
    const Config& cfg, Specification original, SatisfiabilityChecker& checker,
    const GenerationProgressCallback& on_impl_progress) {
    std::vector<FilterFunction> filters;
    filters.push_back(make_dedup_filter());
    if (cfg.run_implication_filter) {
        filters.push_back(make_implication_filter(
            checker, syntactic_similarity_key(std::move(original), cfg),
            on_impl_progress));
    }
    return filters;
}
