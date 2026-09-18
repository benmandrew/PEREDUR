#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "runner/black.hpp"
#include "test_registry.hpp"
#include "test_support.hpp"

namespace {

struct Suite {
    std::string_view m_name;
    bool m_in_full_run = true;
    // A second registry suite run after the first under the same name.
    std::string_view m_also;
};

Suite suite_named(std::string_view name) { return Suite{name, true, {}}; }

// In the order the no-argument run takes them. thread_pool sizes the global
// pool, a function-local static built on first use and never resized, so
// running it there would pin the width every later suite scores in; it runs as
// its own ctest process instead.
const std::vector<Suite>& suites() {
    static const std::vector<Suite> table = {
        suite_named("transfer_matrix"),
        suite_named("black_runner"),
        suite_named("formaliser_runner"),
        suite_named("ganak_runner"),
        suite_named("ltlfilt_runner"),
        suite_named("process_runner"),
        suite_named("spot_runner"),
        suite_named("accumulator"),
        suite_named("crossover"),
        suite_named("generation"),
        suite_named("determinism"),
        suite_named("pipeline"),
        suite_named("termination"),
        suite_named("nsga2"),
        suite_named("mutation"),
        suite_named("fretish_monotone"),
        suite_named("prop_formula_ast"),
        suite_named("prop_formula_canonical"),
        suite_named("prop_formula_cnf"),
        suite_named("prop_formula_rewrite"),
        suite_named("prop_formula_similarity"),
        suite_named("prop_formula_temporal"),
        suite_named("fingerprint_lasso"),
        suite_named("semantic_similarity"),
        suite_named("syntactic_similarity"),
        suite_named("fitness_function"),
        suite_named("status"),
        suite_named("correctness"),
        suite_named("implication_filter"),
        suite_named("vacuity_filter"),
        suite_named("well_separation_filter"),
        suite_named("requirement"),
        suite_named("scope"),
        suite_named("serialisation"),
        suite_named("config_io"),
        suite_named("dashboard"),
        suite_named("driver_support"),
        suite_named("profile"),
        suite_named("tlsf_parser"),
        suite_named("tlsf_writer"),
        suite_named("tlsf_filter"),
        suite_named("tlsf_fitness"),
        Suite{"tlsf_mucs", true, "tlsf_guarantee_parts"},
        suite_named("tlsf_genetic"),
        suite_named("tlsf_monotone"),
        suite_named("tlsf_assumption"),
        suite_named("tlsf_pipeline"),
        suite_named("driver_peredur"),
        suite_named("driver_realize"),
        suite_named("driver_ltl"),
        suite_named("driver_mucs"),
        suite_named("driver_compare"),
        suite_named("driver_maximal"),
        suite_named("driver_lint_ideals"),
        suite_named("driver_signal_tracer"),
        Suite{"thread_pool", false, {}},
    };
    return table;
}

// A registered test whose suite no table entry names would never run.
void expect_every_test_is_reachable() {
    for (const TestCase& test : test_registry()) {
        const bool named = std::any_of(
            suites().begin(), suites().end(), [&test](const Suite& suite) {
                return suite.m_name == test.m_suite ||
                       suite.m_also == test.m_suite;
            });
        if (!named) {
            throw std::logic_error("Test " + std::string(test.m_name) +
                                   " is in unlisted suite " +
                                   std::string(test.m_suite));
        }
    }
}

void run_registered(std::string_view registry_suite) {
    for (const TestCase& test : test_registry()) {
        if (test.m_suite == registry_suite) {
            test.m_run();
        }
    }
}

void run_suite(const Suite& suite) {
    run_registered(suite.m_name);
    if (!suite.m_also.empty()) {
        run_registered(suite.m_also);
    }
}

void run_named_suite(std::string_view name) {
    const auto found = std::find_if(
        suites().begin(), suites().end(),
        [name](const Suite& suite) { return suite.m_name == name; });
    if (found == suites().end()) {
        throw std::invalid_argument("Unknown test suite: " + std::string(name));
    }
    run_suite(*found);
}

}  // namespace

int main(int argc, const char* const argv[]) {
    // The production default in config.hpp is tuned tight for real runs; tests
    // that expect a definite SAT/UNSAT answer need the larger budget.
    global_sat_checker().set_timeout(k_test_black_timeout);
    try {
        expect_every_test_is_reachable();
        if (argc == 1) {
            for (const Suite& suite : suites()) {
                if (suite.m_in_full_run) {
                    run_suite(suite);
                }
            }
            return 0;
        }
        if (argc != 2) {
            throw std::invalid_argument(
                "Expected zero arguments or exactly one test suite name.");
        }
        run_named_suite(argv[1]);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
