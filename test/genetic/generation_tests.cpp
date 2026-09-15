#include <algorithm>
#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "config.hpp"
#include "genetic/generation.hpp"
#include "prop_formula.hpp"
#include "requirement.hpp"
#include "test_suite.hpp"
#include "test_support.hpp"

namespace {

Requirement make_req(const std::string& trigger, const std::string& response,
                     Timing timing = timing::immediately()) {
    return Requirement{Formula(trigger), Formula(response), std::move(timing)};
}

Specification make_spec(const std::string& trigger, const std::string& response,
                        Timing timing = timing::immediately()) {
    return Specification({}, {make_req(trigger, response, std::move(timing))},
                         {}, {});
}

RandomSource make_source(std::vector<std::size_t> values,
                         std::size_t fallback) {
    return RandomSource(
        [values = std::move(values), fallback,
         index = std::size_t{0}](std::size_t upper_bound) mutable {
            if (index >= values.size()) {
                return fallback % upper_bound;
            }
            const std::size_t value = values[index];
            ++index;
            return value % upper_bound;
        });
}

std::string first_condition(const Specification& spec) {
    return spec.m_guarantees.begin()->m_condition.to_string();
}

// --- simplification ---

// The stop is simplified with the condition and response. `until false` is
// Always spelt another way and folds into it, so the two do not split a cache
// key; `before false` keeps its kind, being valid rather than a respelling.
void test_simplify_folds_until_false_into_always() {
    const auto simplified_timing = [](const Timing& timing) {
        return fretish_operators()
            .simplify(make_spec("a", "b", timing))
            .m_guarantees.front()
            .m_timing;
    };
    expect(std::holds_alternative<timing::Always>(
               simplified_timing(timing::until(Formula("false & a")))),
           "simplify: until false should fold into always");
    expect(simplified_timing(timing::until(Formula("a | false"))) ==
               timing::until(Formula("a")),
           "simplify: an until stop should be simplified in place");
    expect(simplified_timing(timing::before(Formula("false & a"))) ==
               timing::before(Formula("false")),
           "simplify: before false should keep its kind");
}

// --- score_population ---

void test_score_population_single_function() {
    const std::vector<Specification> pop = {make_spec("p", "q"),
                                            make_spec("r", "s")};
    const AggregateWeightedFitnessFunction fns =
        AggregateWeightedFitnessFunction(
            {{[](const Specification&) { return 0.5; }, 1.0, ""}});
    const auto scored = score_population(Config{}, pop, fns);
    expect(scored.size() == 2,
           "score_population: should score every specification");
    expect(scored[0].fitness == 0.5,
           "score_population: single-function score should match return value");
    expect(scored[1].fitness == 0.5,
           "score_population: all equal fitness with constant function");
}

void test_score_population_weighted_aggregation() {
    const std::vector<Specification> pop = {make_spec("p", "q")};
    // (0.0 * 1.0 + 1.0 * 3.0) / (1.0 + 3.0) == 0.75
    const AggregateWeightedFitnessFunction fns =
        AggregateWeightedFitnessFunction(
            {{[](const Specification&) { return 0.0; }, 1.0, ""},
             {[](const Specification&) { return 1.0; }, 3.0, ""}});
    const auto scored = score_population(Config{}, pop, fns);
    expect(scored.size() == 1,
           "score_population: should produce one entry for a single-element "
           "population");
    expect(scored[0].fitness == 0.75,
           "score_population: should compute weighted average correctly");
}

void test_score_population_equal_weights_give_average() {
    const std::vector<Specification> pop = {make_spec("p", "q")};
    // (0.2 * 1.0 + 0.8 * 1.0) / 2.0 == 0.5
    const AggregateWeightedFitnessFunction fns =
        AggregateWeightedFitnessFunction(
            {{[](const Specification&) { return 0.2; }, 1.0, ""},
             {[](const Specification&) { return 0.8; }, 1.0, ""}});
    const auto scored = score_population(Config{}, pop, fns);
    expect(scored[0].fitness == 0.5,
           "score_population: equal weights should give arithmetic average");
}

void test_score_population_drops_failing_individual() {
    const std::vector<Specification> pop = {
        make_spec("p", "q"), make_spec("boom", "q"), make_spec("r", "s")};
    // Mimics an external tool failing on one evolved formula: the individual
    // is dropped, the rest of the generation is scored as normal.
    const AggregateWeightedFitnessFunction fns =
        AggregateWeightedFitnessFunction({{[](const Specification& spec) {
                                               if (first_condition(spec) ==
                                                   "boom") {
                                                   throw std::runtime_error(
                                                       "tool exited with code "
                                                       "2");
                                               }
                                               return 0.5;
                                           },
                                           1.0, ""}});
    const auto scored = score_population(Config{}, pop, fns);
    expect(scored.size() == 2,
           "score_population: should drop the individual that threw");
    expect(first_condition(scored[0].specification) == "p" &&
               first_condition(scored[1].specification) == "r",
           "score_population: survivors should keep their relative order");
}

void test_score_population_circuit_breaker_trips() {
    const std::vector<Specification> pop = {
        make_spec("p", "q"), make_spec("r", "s"), make_spec("t", "u")};
    // Every individual fails, as it would with a missing or broken tool. That
    // must abort rather than quietly evolving an empty population.
    const AggregateWeightedFitnessFunction fns =
        AggregateWeightedFitnessFunction({{[](const Specification&) -> double {
                                               throw std::runtime_error(
                                                   "tool not found");
                                           },
                                           1.0, ""}});
    bool threw = false;
    try {
        score_population(Config{}, pop, fns);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw,
           "score_population: should abort when the whole generation fails");
}

// --- make_predicate_filter / filter_population ---

void test_make_predicate_filter_keeps_matching() {
    const std::vector<Specification> pop = {make_spec("p", "q"),
                                            make_spec("r", "s")};
    const FilterFunction filter = make_predicate_filter(
        "",
        [](const Specification& spec) { return first_condition(spec) == "p"; });
    const auto survivors = filter(pop);
    expect(survivors.size() == 1,
           "make_predicate_filter: should remove non-matching specifications");
    expect(first_condition(survivors[0]) == "p",
           "make_predicate_filter: should keep the matching specification");
}

void test_filter_population_empty_filter_list_keeps_all() {
    const std::vector<Specification> pop = {make_spec("p", "q"),
                                            make_spec("r", "s")};
    const auto survivors = filter_population(pop, {});
    expect(
        survivors.size() == 2,
        "filter_population: empty filter list should keep all specifications");
}

void test_filter_population_removes_failing() {
    const std::vector<Specification> pop = {make_spec("p", "q"),
                                            make_spec("r", "s")};
    const std::vector<FilterFunction> filters = {
        make_predicate_filter("", [](const Specification& spec) {
            return first_condition(spec) == "p";
        })};
    const auto survivors = filter_population(pop, filters);
    expect(survivors.size() == 1,
           "filter_population: should remove specifications failing the "
           "predicate");
    expect(first_condition(survivors[0]) == "p",
           "filter_population: should keep the passing specification");
}

void test_filter_population_applies_sequentially() {
    const std::vector<Specification> pop = {
        make_spec("p", "q"), make_spec("r", "s"), make_spec("t", "u")};
    // First filter removes t; second filter removes r — only p survives.
    const std::vector<FilterFunction> filters = {
        make_predicate_filter("",
                              [](const Specification& spec) {
                                  return first_condition(spec) != "t";
                              }),
        make_predicate_filter("", [](const Specification& spec) {
            return first_condition(spec) != "r";
        })};
    const auto survivors = filter_population(pop, filters);
    expect(survivors.size() == 1,
           "filter_population: filters should be applied sequentially");
    expect(first_condition(survivors[0]) == "p",
           "filter_population: sequential filters should leave only p");
}

void test_filter_population_population_level_maximal_elements() {
    // Keep only specs with the simplest (fewest-node) condition formula.
    const std::vector<Specification> pop = {make_spec("p", "q"),
                                            make_spec("p & r", "q")};
    const FilterFunction simplest_condition =
        [](const std::vector<Specification>& candidates) {
            if (candidates.empty()) {
                return candidates;
            }
            const std::size_t min_nodes =
                std::min_element(
                    candidates.begin(), candidates.end(),
                    [](const Specification& lhs, const Specification& rhs) {
                        return lhs.m_guarantees.begin()
                                   ->m_condition.n_subformulae() <
                               rhs.m_guarantees.begin()
                                   ->m_condition.n_subformulae();
                    })
                    ->m_guarantees.begin()
                    ->m_condition.n_subformulae();
            std::vector<Specification> result;
            for (const Specification& spec : candidates) {
                if (spec.m_guarantees.begin()->m_condition.n_subformulae() ==
                    min_nodes) {
                    result.push_back(spec);
                }
            }
            return result;
        };
    const auto survivors = filter_population(pop, {simplest_condition});
    expect(survivors.size() == 1,
           "filter_population: population-level filter should keep only "
           "maximal (simplest) elements");
    expect(first_condition(survivors[0]) == "p",
           "filter_population: should retain the specification with the "
           "simpler condition");
}

// --- evolve_generation ---

void test_evolve_generation_produces_target_size() {
    const AggregateWeightedFitnessFunction fns =
        AggregateWeightedFitnessFunction(
            {{[](const Specification&) { return 0.5; }, 1.0, ""}});

    const std::vector<ScoredSpecification> pop = score_population(
        Config{},
        {make_spec("p", "q"), make_spec("r", "s"), make_spec("t", "u")}, fns);
    const auto next_gen =
        evolve_generation(Config{}, pop, 2, 0, fns, {}, make_source({}, 0));
    expect(
        next_gen.size() == 2,
        "evolve_generation: should produce the requested number of offspring");
}

void test_evolve_generation_pads_up_to_target_size() {
    const AggregateWeightedFitnessFunction fns =
        AggregateWeightedFitnessFunction(
            {{[](const Specification&) { return 0.5; }, 1.0, ""}});
    const std::vector<ScoredSpecification> pop = score_population(
        Config{}, {make_spec("p", "q"), make_spec("r", "s")}, fns);
    const auto next_gen =
        evolve_generation(Config{}, pop, 5, 0, fns, {}, make_source({}, 0));
    expect(next_gen.size() == 5,
           "evolve_generation: should pad the next generation back to the "
           "requested target size");
}

void test_evolve_generation_selects_parents_before_offspring_filtering() {
    // The filter is applied after breeding, and the generation is then padded
    // back to the requested size if filtering shrinks the offspring pool.
    // Padding is truncation selection's path, so this pins WeightedAverage
    // rather than taking the Nsga2Apportion default: NSGA-II pools parents with
    // offspring to refill a generation, so it never reaches the duplication
    // below.
    Config cfg;
    cfg.selection_scheme = SelectionScheme::WeightedAverage;
    const AggregateWeightedFitnessFunction fns =
        AggregateWeightedFitnessFunction(
            {{[](const Specification&) { return 0.5; }, 1.0, ""}});
    const std::vector<ScoredSpecification> pop =
        score_population(cfg, {make_spec("p", "q"), make_spec("r", "s")}, fns);
    const std::vector<FilterFunction> filters = {
        [](const std::vector<Specification>& candidates) {
            if (candidates.empty()) {
                return candidates;
            }
            return std::vector<Specification>{candidates.front()};
        }};
    const auto next_gen =
        evolve_generation(cfg, pop, 2, 0, fns, filters, make_source({}, 0));
    expect(next_gen.size() == 2,
           "evolve_generation: the generation should be padded back to the "
           "requested target size after filtering");
    expect(first_condition(next_gen[0].specification) ==
               first_condition(next_gen[1].specification),
           "evolve_generation: padded offspring should duplicate the "
           "surviving specification");
}

void test_evolve_generation_elitism_preserves_best_through_filter() {
    // Fitness ranks "p" > "r" > "t", so ("p","q") is the top (elite) parent.
    const AggregateWeightedFitnessFunction fns =
        AggregateWeightedFitnessFunction({{[](const Specification& spec) {
                                               const std::string cond =
                                                   first_condition(spec);
                                               if (cond == "p") {
                                                   return 1.0;
                                               }
                                               if (cond == "r") {
                                                   return 0.5;
                                               }
                                               return 0.1;
                                           },
                                           1.0, ""}});
    // elitism_rate only drives truncation selection, so this pins
    // WeightedAverage rather than taking the Nsga2Apportion default: NSGA-II's
    // (mu+lambda) survivor pooling is already elitist, and keeps the top spec
    // alive at elitism_rate = 0 -- which is exactly why that is its natural
    // setting.
    Config cfg;
    cfg.selection_scheme = SelectionScheme::WeightedAverage;
    cfg.crossover_rate = 0.0;
    cfg.mutation_rate = 0.0;
    const std::vector<ScoredSpecification> pop = score_population(
        cfg, {make_spec("p", "q"), make_spec("r", "s"), make_spec("t", "u")},
        fns);
    // Drop every offspring whose condition is "p": the elite's own offspring is
    // removed, so only elitism can keep a "p" specification alive.
    const std::vector<FilterFunction> filters = {
        make_predicate_filter("", [](const Specification& spec) {
            return first_condition(spec) != "p";
        })};

    const auto with_elitism =
        evolve_generation(cfg, pop, 3, 1, fns, filters, make_source({}, 0));
    const bool elite_survived =
        std::any_of(with_elitism.begin(), with_elitism.end(),
                    [](const ScoredSpecification& scored) {
                        return first_condition(scored.specification) == "p";
                    });
    expect(elite_survived,
           "evolve_generation: the top spec survives verbatim as an elite even "
           "when a filter removes its offspring");

    // With no elitism the same filter leaves no "p" specification behind.
    const auto without_elitism =
        evolve_generation(cfg, pop, 3, 0, fns, filters, make_source({}, 0));
    const bool elite_absent =
        std::none_of(without_elitism.begin(), without_elitism.end(),
                     [](const ScoredSpecification& scored) {
                         return first_condition(scored.specification) == "p";
                     });
    expect(elite_absent,
           "evolve_generation: without elitism the filtered-out top spec does "
           "not survive");
}

// --- evolve_generation under the NSGA-II scheme ---

// Two objectives over the specs "p"/"r"/"t": objective A ranks p > r > t and
// objective B ranks t > r > p, so "p" Pareto-dominates both "r" and "t" while
// "r" and "t" trade off (mutually non-dominating).
AggregateWeightedFitnessFunction two_objective_fns() {
    return AggregateWeightedFitnessFunction({{[](const Specification& spec) {
                                                  const std::string cond =
                                                      first_condition(spec);
                                                  if (cond == "p") {
                                                      return 1.0;
                                                  }
                                                  if (cond == "r") {
                                                      return 0.6;
                                                  }
                                                  return 0.2;
                                              },
                                              1.0, "a"},
                                             {[](const Specification& spec) {
                                                  const std::string cond =
                                                      first_condition(spec);
                                                  if (cond == "p") {
                                                      return 1.0;
                                                  }
                                                  if (cond == "r") {
                                                      return 0.4;
                                                  }
                                                  return 0.8;
                                              },
                                              1.0, "b"}});
}

Config nsga2_truncate_config() {
    Config cfg;
    cfg.selection_scheme = SelectionScheme::Nsga2Truncate;
    return cfg;
}

void test_evolve_generation_nsga2_truncate_produces_target_size() {
    const Config cfg = nsga2_truncate_config();
    const AggregateWeightedFitnessFunction fns = two_objective_fns();
    const std::vector<ScoredSpecification> pop = score_population(
        cfg, {make_spec("p", "q"), make_spec("r", "s"), make_spec("t", "u")},
        fns);
    const auto next_gen =
        evolve_generation(cfg, pop, 2, 0, fns, {}, make_source({}, 0));
    expect(next_gen.size() == 2,
           "evolve_generation/nsga2-truncate: (mu+lambda) pooling still yields "
           "exactly "
           "target_size survivors");
}

void test_evolve_generation_nsga2_truncate_preserves_front_no_elitism() {
    Config cfg = nsga2_truncate_config();
    cfg.crossover_rate = 0.0;
    cfg.mutation_rate = 0.0;
    const AggregateWeightedFitnessFunction fns = two_objective_fns();
    const std::vector<ScoredSpecification> pop = score_population(
        cfg, {make_spec("p", "q"), make_spec("r", "s"), make_spec("t", "u")},
        fns);
    // Remove every "p" offspring: only NSGA-II's (mu+lambda) pooling, which
    // retains the original parent, can keep the Pareto-optimal "p" alive with
    // elitism_size = 0.
    const std::vector<FilterFunction> filters = {
        make_predicate_filter("", [](const Specification& spec) {
            return first_condition(spec) != "p";
        })};
    const auto next_gen =
        evolve_generation(cfg, pop, 3, 0, fns, filters, make_source({}, 0));
    const bool p_survived =
        std::any_of(next_gen.begin(), next_gen.end(),
                    [](const ScoredSpecification& scored) {
                        return first_condition(scored.specification) == "p";
                    });
    expect(p_survived,
           "evolve_generation/nsga2-truncate: the Pareto-optimal parent "
           "survives via "
           "(mu+lambda) pooling even with no elitism and its offspring "
           "filtered out");
}

void test_evolve_generation_nsga2_truncate_is_deterministic() {
    const Config cfg = nsga2_truncate_config();
    const AggregateWeightedFitnessFunction fns = two_objective_fns();
    const std::vector<ScoredSpecification> pop = score_population(
        cfg, {make_spec("p", "q"), make_spec("r", "s"), make_spec("t", "u")},
        fns);
    const auto first =
        evolve_generation(cfg, pop, 2, 0, fns, {}, make_source({1, 2, 3}, 0));
    const auto second =
        evolve_generation(cfg, pop, 2, 0, fns, {}, make_source({1, 2, 3}, 0));
    bool identical = first.size() == second.size();
    for (std::size_t i = 0; identical && i < first.size(); ++i) {
        identical = first_condition(first[i].specification) ==
                    first_condition(second[i].specification);
    }
    expect(
        identical,
        "evolve_generation/nsga2-truncate: identical inputs and RNG produce an "
        "identical, stably-ordered generation");
}

// --- dedup_by_specification / replicate_to_size ---

// int stands in for Spec: both helpers need only hashing, equality, and
// copying, so the real Specification would add nothing but construction cost.
Scored<int> make_scored(int specification, std::size_t rank) {
    Scored<int> scored;
    scored.specification = specification;
    scored.rank = rank;
    return scored;
}

std::vector<int> specifications_of(const std::vector<Scored<int>>& population) {
    std::vector<int> out;
    out.reserve(population.size());
    for (const Scored<int>& scored : population) {
        out.push_back(scored.specification);
    }
    return out;
}

/// Copies of each of @p distinct present in @p replicated, in the order of
/// @p distinct.
std::vector<std::size_t> copy_counts(const std::vector<Scored<int>>& replicated,
                                     const std::vector<Scored<int>>& distinct) {
    std::vector<std::size_t> counts(distinct.size(), 0);
    for (std::size_t i = 0; i < distinct.size(); ++i) {
        counts[i] = static_cast<std::size_t>(std::count_if(
            replicated.begin(), replicated.end(),
            [&distinct, i](const Scored<int>& scored) {
                return scored.specification == distinct[i].specification;
            }));
    }
    return counts;
}

void test_dedup_by_specification_keeps_first_occurrence_in_order() {
    const std::vector<Scored<int>> pool = {
        make_scored(7, 0), make_scored(3, 1), make_scored(7, 2),
        make_scored(9, 1), make_scored(3, 0), make_scored(7, 3)};
    const auto distinct = generation_detail::dedup_by_specification<int>(pool);
    expect(specifications_of(distinct) == std::vector<int>({7, 3, 9}),
           "dedup_by_specification: should keep the first occurrence of each "
           "specification, in first-occurrence order");
    expect(
        distinct[0].rank == 0 && distinct[1].rank == 1 && distinct[2].rank == 1,
        "dedup_by_specification: survivors should carry the metadata of the "
        "first occurrence, not a later duplicate");
}

void test_dedup_by_specification_all_distinct_is_identity() {
    const std::vector<Scored<int>> pool = {make_scored(1, 0), make_scored(2, 1),
                                           make_scored(3, 2)};
    expect(specifications_of(generation_detail::dedup_by_specification<int>(
               pool)) == std::vector<int>({1, 2, 3}),
           "dedup_by_specification: a pool with no repeats should pass through "
           "unchanged");
}

void test_replicate_to_size_produces_target_size() {
    const std::vector<Scored<int>> distinct = {
        make_scored(1, 0), make_scored(2, 1), make_scored(3, 2)};
    expect(generation_detail::replicate_to_size(distinct, 10).size() == 10,
           "replicate_to_size: output should be exactly target_size");
}

void test_replicate_to_size_keeps_every_individual() {
    const std::vector<Scored<int>> distinct = {
        make_scored(1, 0), make_scored(2, 1), make_scored(3, 5),
        make_scored(4, 9)};
    const auto counts = copy_counts(
        generation_detail::replicate_to_size(distinct, 12), distinct);
    expect(std::all_of(counts.begin(), counts.end(),
                       [](std::size_t n) { return n >= 1; }),
           "replicate_to_size: every distinct individual should keep at least "
           "one copy, however poor its rank");
}

void test_replicate_to_size_copies_non_increasing_in_rank() {
    const std::vector<Scored<int>> distinct = {
        make_scored(1, 0), make_scored(2, 1), make_scored(3, 2)};
    const auto counts = copy_counts(
        generation_detail::replicate_to_size(distinct, 10), distinct);
    expect(counts[0] >= counts[1] && counts[1] >= counts[2],
           "replicate_to_size: a better rank should never get fewer copies "
           "than a worse one");
    // Weights 1, 1/2 and 1/3 over the 7 spare slots give quotas of 3.818,
    // 1.909 and 1.273; the floors allocate 5 and the two largest remainders
    // (1 then 0) take the rest. Ignoring rank would instead give {4, 3, 3},
    // so the exact split is what pins the 1 / (1 + rank) weighting down.
    expect(counts == std::vector<std::size_t>({5, 3, 2}),
           "replicate_to_size: copies should follow the 1 / (1 + rank) "
           "apportionment exactly");
}

void test_replicate_to_size_is_deterministic() {
    const std::vector<Scored<int>> distinct = {
        make_scored(1, 0), make_scored(2, 0), make_scored(3, 1),
        make_scored(4, 4)};
    const auto first = generation_detail::replicate_to_size(distinct, 17);
    const auto second = generation_detail::replicate_to_size(distinct, 17);
    expect(specifications_of(first) == specifications_of(second),
           "replicate_to_size: it draws no random numbers, so repeated calls "
           "on identical input must agree");
}

void test_replicate_to_size_exact_fit_copies_once_each() {
    const std::vector<Scored<int>> distinct = {
        make_scored(1, 0), make_scored(2, 1), make_scored(3, 2)};
    const auto replicated = generation_detail::replicate_to_size(distinct, 3);
    expect(specifications_of(replicated) == std::vector<int>({1, 2, 3}),
           "replicate_to_size: with no spare slots every individual should get "
           "exactly one copy, in input order");
}

void test_replicate_to_size_single_individual_fills_population() {
    const std::vector<Scored<int>> distinct = {make_scored(42, 0)};
    const auto replicated = generation_detail::replicate_to_size(distinct, 5);
    expect(replicated.size() == 5 && specifications_of(replicated) ==
                                         std::vector<int>({42, 42, 42, 42, 42}),
           "replicate_to_size: a lone survivor should fill the whole "
           "population");
}

void test_replicate_to_size_equal_ranks_apportion_evenly() {
    const std::vector<Scored<int>> distinct = {
        make_scored(1, 0), make_scored(2, 0), make_scored(3, 0),
        make_scored(4, 0)};
    const auto even = copy_counts(
        generation_detail::replicate_to_size(distinct, 12), distinct);
    expect(even == std::vector<std::size_t>({3, 3, 3, 3}),
           "replicate_to_size: equal ranks dividing exactly should split the "
           "population evenly");

    // 10 slots over 4 equal ranks cannot divide, so the remainder decides:
    // counts may differ by one, never more.
    const auto ragged = copy_counts(
        generation_detail::replicate_to_size(distinct, 10), distinct);
    const auto bounds = std::minmax_element(ragged.begin(), ragged.end());
    expect(*bounds.second - *bounds.first <= 1,
           "replicate_to_size: with equal ranks the largest-remainder split "
           "should differ by at most one copy");
}

std::size_t distinct_specifications(
    const std::vector<ScoredSpecification>& population) {
    std::set<std::size_t> hashes;
    for (const ScoredSpecification& scored : population) {
        hashes.insert(std::hash<Specification>{}(scored.specification));
    }
    return hashes.size();
}

Config nsga2_apportion_config() {
    Config cfg;
    cfg.selection_scheme = SelectionScheme::Nsga2Apportion;
    return cfg;
}

void test_evolve_generation_nsga2_apportion_produces_target_size() {
    const Config cfg = nsga2_apportion_config();
    const AggregateWeightedFitnessFunction fns = two_objective_fns();
    // Every parent is the same specification, so the pool deduplicates well
    // below target_size and the replication branch is the one under test.
    const std::vector<ScoredSpecification> pop =
        score_population(cfg,
                         {make_spec("p", "q"), make_spec("p", "q"),
                          make_spec("p", "q"), make_spec("p", "q")},
                         fns);
    const auto next =
        evolve_generation(cfg, pop, 4, 0, fns, {}, make_source({1, 2, 3}, 0));
    expect(next.size() == 4,
           "evolve_generation/nsga2-apportion: a pool that deduplicates below "
           "target_size must still be replicated back up to it");
}

void test_evolve_generation_nsga2_apportion_retains_distinct_candidates() {
    const AggregateWeightedFitnessFunction fns = two_objective_fns();
    // "p" dominates both others on both objectives, so under nsga2-truncate its
    // three copies fill the rank-0 front and truncation keeps the copies in
    // preference to the distinct candidates behind them.
    const std::vector<Specification> specs = {
        make_spec("p", "q"), make_spec("p", "q"), make_spec("p", "q"),
        make_spec("r", "s"), make_spec("t", "u")};
    const Config truncating = nsga2_truncate_config();
    const Config apportioning = nsga2_apportion_config();
    const auto truncated_next =
        evolve_generation(truncating, score_population(truncating, specs, fns),
                          4, 0, fns, {}, make_source({1, 2, 3}, 0));
    const auto apportioned_next = evolve_generation(
        apportioning, score_population(apportioning, specs, fns), 4, 0, fns, {},
        make_source({1, 2, 3}, 0));
    expect(
        distinct_specifications(apportioned_next) >
            distinct_specifications(truncated_next),
        "evolve_generation/nsga2-apportion: deduplicating before truncation "
        "should hold strictly more distinct specifications than nsga2-truncate "
        "when duplicates crowd the rank-0 front");
}

void test_evolve_generation_nsga2_apportion_is_deterministic() {
    const Config cfg = nsga2_apportion_config();
    const AggregateWeightedFitnessFunction fns = two_objective_fns();
    const std::vector<ScoredSpecification> pop = score_population(
        cfg, {make_spec("p", "q"), make_spec("r", "s"), make_spec("t", "u")},
        fns);
    const auto first =
        evolve_generation(cfg, pop, 4, 0, fns, {}, make_source({1, 2, 3}, 0));
    const auto second =
        evolve_generation(cfg, pop, 4, 0, fns, {}, make_source({1, 2, 3}, 0));
    bool identical = first.size() == second.size();
    for (std::size_t i = 0; identical && i < first.size(); ++i) {
        identical = first_condition(first[i].specification) ==
                    first_condition(second[i].specification);
    }
    expect(identical,
           "evolve_generation/nsga2-apportion: apportionment draws no random "
           "numbers, so identical inputs and RNG produce an identical "
           "generation");
}

}  // namespace

void run_generation_tests() {
    test_simplify_folds_until_false_into_always();
    test_score_population_single_function();
    test_score_population_weighted_aggregation();
    test_score_population_equal_weights_give_average();
    test_score_population_drops_failing_individual();
    test_score_population_circuit_breaker_trips();
    test_make_predicate_filter_keeps_matching();
    test_filter_population_empty_filter_list_keeps_all();
    test_filter_population_removes_failing();
    test_filter_population_applies_sequentially();
    test_filter_population_population_level_maximal_elements();
    test_evolve_generation_produces_target_size();
    test_evolve_generation_pads_up_to_target_size();
    test_evolve_generation_selects_parents_before_offspring_filtering();
    test_evolve_generation_elitism_preserves_best_through_filter();
    test_evolve_generation_nsga2_truncate_produces_target_size();
    test_evolve_generation_nsga2_truncate_preserves_front_no_elitism();
    test_evolve_generation_nsga2_truncate_is_deterministic();
    test_dedup_by_specification_keeps_first_occurrence_in_order();
    test_dedup_by_specification_all_distinct_is_identity();
    test_replicate_to_size_produces_target_size();
    test_replicate_to_size_keeps_every_individual();
    test_replicate_to_size_copies_non_increasing_in_rank();
    test_replicate_to_size_is_deterministic();
    test_replicate_to_size_exact_fit_copies_once_each();
    test_replicate_to_size_single_individual_fills_population();
    test_replicate_to_size_equal_ranks_apportion_evenly();
    test_evolve_generation_nsga2_apportion_produces_target_size();
    test_evolve_generation_nsga2_apportion_retains_distinct_candidates();
    test_evolve_generation_nsga2_apportion_is_deterministic();
}
