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
