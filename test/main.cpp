#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "config.hpp"
#include "runner/black.hpp"
#include "test_suite.hpp"

namespace {

// Handles the genetic-algorithm suites. Split out of run_suite for the same
// reason as run_tlsf_suite below.
bool run_genetic_suite(std::string_view suite_name) {
    if (suite_name == "accumulator") {
        run_accumulator_tests();
        return true;
    }
    if (suite_name == "crossover") {
        run_crossover_tests();
        return true;
    }
    if (suite_name == "generation") {
        run_generation_tests();
        return true;
    }
    if (suite_name == "determinism") {
        run_determinism_tests();
        return true;
    }
    if (suite_name == "fretish_monotone") {
        run_fretish_monotone_tests();
        return true;
    }
    if (suite_name == "pipeline") {
        run_pipeline_tests();
        return true;
    }
    if (suite_name == "termination") {
        run_termination_tests();
        return true;
    }
    if (suite_name == "nsga2") {
        run_nsga2_tests();
        return true;
    }
    if (suite_name == "mutation") {
        run_mutation_tests();
        return true;
    }
    return false;
}

// Handles the population-filter suites. Split out of run_suite for the same
// reason as run_tlsf_suite below.
bool run_filter_suite(std::string_view suite_name) {
    if (suite_name == "correctness") {
        run_correctness_tests();
        return true;
    }
    if (suite_name == "implication_filter") {
        run_implication_filter_tests();
        return true;
    }
    if (suite_name == "vacuity_filter") {
        run_vacuity_filter_tests();
        return true;
    }
    if (suite_name == "well_separation_filter") {
        run_well_separation_filter_tests();
        return true;
    }
    return false;
}

// Handles the TLSF-mode suites. Split out of run_suite so the latter stays
// under the clang-tidy cognitive-complexity threshold.
bool run_tlsf_suite(std::string_view suite_name) {
    if (suite_name == "tlsf_parser") {
        run_tlsf_parser_tests();
        return true;
    }
    if (suite_name == "tlsf_writer") {
        run_tlsf_writer_tests();
        return true;
    }
    if (suite_name == "tlsf_fitness") {
        run_tlsf_fitness_tests();
        return true;
    }
    if (suite_name == "tlsf_mucs") {
        run_tlsf_mucs_tests();
        run_tlsf_guarantee_parts_tests();
        return true;
    }
    if (suite_name == "tlsf_genetic") {
        run_tlsf_genetic_tests();
        return true;
    }
    if (suite_name == "tlsf_assumption") {
        run_tlsf_assumption_tests();
        return true;
    }
    if (suite_name == "tlsf_monotone") {
        run_tlsf_monotone_tests();
        return true;
    }
    if (suite_name == "tlsf_pipeline") {
        run_tlsf_pipeline_tests();
        return true;
    }
    if (suite_name == "tlsf_filter") {
        run_tlsf_filter_tests();
        return true;
    }
    return false;
}

// Handles the end-to-end suites, one per built driver. Each spawns the binary
// itself rather than calling into the library, so these are the only suites
// that cover src/main.cpp, src/repair/ and the standalone tools' own argument
// handling. Split out of run_suite for the same reason as run_tlsf_suite above.
bool run_driver_suite(std::string_view suite_name) {
    if (suite_name == "driver_peredur") {
        run_peredur_driver_tests();
        return true;
    }
    if (suite_name == "driver_realize") {
        run_realize_driver_tests();
        return true;
    }
    if (suite_name == "driver_ltl") {
        run_ltl_driver_tests();
        return true;
    }
    if (suite_name == "driver_mucs") {
        run_mucs_driver_tests();
        return true;
    }
    if (suite_name == "driver_compare") {
        run_compare_driver_tests();
        return true;
    }
    if (suite_name == "driver_maximal") {
        run_maximal_driver_tests();
        return true;
    }
    if (suite_name == "driver_fingerprint") {
        run_fingerprint_driver_tests();
        return true;
    }
    if (suite_name == "driver_lint_ideals") {
        run_lint_ideals_driver_tests();
        return true;
    }
    if (suite_name == "driver_signal_tracer") {
        run_signal_tracer_driver_tests();
        return true;
    }
    return false;
}

// Handles the suites over the process-wide plumbing rather than the repair
// itself. Split out of run_suite for the same reason as run_tlsf_suite above.
bool run_infrastructure_suite(std::string_view suite_name) {
    if (suite_name == "config_io") {
        run_config_io_tests();
        return true;
    }
    if (suite_name == "dashboard") {
        run_dashboard_tests();
        return true;
    }
    if (suite_name == "driver_support") {
        run_driver_support_tests();
        return true;
    }
    if (suite_name == "profile") {
        run_profile_tests();
        return true;
    }
    if (suite_name == "thread_pool") {
        run_thread_pool_tests();
        return true;
    }
    return false;
}

// The formula suites: the AST and its parser, printer, canonical form, CNF
// encoding and rewriter, plus the lasso evaluator that walks the same arena.
// Split out of run_suite for the reason run_tlsf_suite and run_genetic_suite
// are: one flat chain of every suite in the binary crosses the lint target's
// cognitive-complexity bound.
bool run_formula_suite(std::string_view suite_name) {
    if (suite_name == "prop_formula_ast") {
        run_prop_formula_ast_tests();
        return true;
    }
    if (suite_name == "prop_formula_canonical") {
        run_prop_formula_canonical_tests();
        return true;
    }
    if (suite_name == "prop_formula_cnf") {
        run_prop_formula_cnf_tests();
        return true;
    }
    if (suite_name == "prop_formula_rewrite") {
        run_prop_formula_rewrite_tests();
        return true;
    }
    if (suite_name == "prop_formula_temporal") {
        run_prop_formula_temporal_tests();
        return true;
    }
    if (suite_name == "prop_formula_similarity") {
        run_prop_formula_similarity_tests();
        return true;
    }
    if (suite_name == "fingerprint_lasso") {
        run_fingerprint_lasso_tests();
        return true;
    }
    return false;
}

void run_suite(std::string_view suite_name,
               const std::chrono::milliseconds& timeout) {
    if (suite_name == "transfer_matrix") {
        run_transfer_matrix_tests();
        return;
    }
    if (suite_name == "black_runner") {
        run_black_runner_tests(timeout);
        return;
    }
    if (suite_name == "formaliser_runner") {
        run_formaliser_runner_tests();
        return;
    }
    if (suite_name == "ganak_runner") {
        run_ganak_runner_tests();
        return;
    }
    if (suite_name == "ltlfilt_runner") {
        run_ltlfilt_runner_tests();
        return;
    }
    if (suite_name == "process_runner") {
        run_process_runner_tests();
        return;
    }
    if (suite_name == "spot_runner") {
        run_spot_runner_tests();
        return;
    }
    if (run_genetic_suite(suite_name)) {
        return;
    }
    if (run_formula_suite(suite_name)) {
        return;
    }
    if (suite_name == "semantic_similarity") {
        run_semantic_similarity_tests();
        return;
    }
    if (suite_name == "syntactic_similarity") {
        run_syntactic_similarity_tests();
        return;
    }
    if (suite_name == "fitness_function") {
        run_fitness_function_tests();
        return;
    }
    if (suite_name == "status") {
        run_status_tests();
        return;
    }
    if (run_filter_suite(suite_name)) {
        return;
    }
    if (suite_name == "requirement") {
        run_requirement_tests();
        return;
    }
    if (suite_name == "scope") {
        run_scope_tests();
        return;
    }
    if (suite_name == "serialisation") {
        run_serialisation_tests();
        return;
    }
    if (run_infrastructure_suite(suite_name)) {
        return;
    }
    if (run_tlsf_suite(suite_name)) {
        return;
    }
    if (run_driver_suite(suite_name)) {
        return;
    }
    throw std::invalid_argument("Unknown test suite: " +
                                std::string(suite_name));
}

}  // namespace

int main(int argc, const char* const argv[]) {
    // Restore the larger pre-optimisation timeout for tests: the production
    // default in config.hpp is now tuned tight for real runs, but CI has
    // previously been slow enough to make that value flaky for tests that
    // expect a definite SAT/UNSAT answer rather than a timeout.
    Config cfg;
    cfg.black_timeout = std::chrono::milliseconds{10000};
    global_sat_checker().set_timeout(cfg.black_timeout);
    try {
        if (argc == 1) {
            run_transfer_matrix_tests();
            run_black_runner_tests(cfg.black_timeout);
            run_formaliser_runner_tests();
            run_ganak_runner_tests();
            run_ltlfilt_runner_tests();
            run_process_runner_tests();
            run_spot_runner_tests();
            run_accumulator_tests();
            run_crossover_tests();
            run_generation_tests();
            run_determinism_tests();
            run_pipeline_tests();
            run_termination_tests();
            run_nsga2_tests();
            run_mutation_tests();
            run_fretish_monotone_tests();
            run_prop_formula_ast_tests();
            run_prop_formula_canonical_tests();
            run_prop_formula_cnf_tests();
            run_prop_formula_rewrite_tests();
            run_prop_formula_similarity_tests();
            run_prop_formula_temporal_tests();
            run_fingerprint_lasso_tests();
            run_semantic_similarity_tests();
            run_syntactic_similarity_tests();
            run_fitness_function_tests();
            run_status_tests();
            run_correctness_tests();
            run_implication_filter_tests();
            run_vacuity_filter_tests();
            run_well_separation_filter_tests();
            run_requirement_tests();
            run_scope_tests();
            run_serialisation_tests();
            run_config_io_tests();
            run_dashboard_tests();
            run_driver_support_tests();
            run_profile_tests();
            run_tlsf_parser_tests();
            run_tlsf_writer_tests();
            run_tlsf_filter_tests();
            run_tlsf_fitness_tests();
            run_tlsf_mucs_tests();
            run_tlsf_guarantee_parts_tests();
            run_tlsf_genetic_tests();
            run_tlsf_monotone_tests();
            run_tlsf_assumption_tests();
            run_tlsf_pipeline_tests();
            run_peredur_driver_tests();
            run_realize_driver_tests();
            run_ltl_driver_tests();
            run_mucs_driver_tests();
            run_compare_driver_tests();
            run_maximal_driver_tests();
            run_fingerprint_driver_tests();
            run_lint_ideals_driver_tests();
            run_signal_tracer_driver_tests();
            // run_thread_pool_tests() is deliberately absent. It sizes the
            // global pool, which is a function-local static built on first use
            // and never resized, so running it here would pin the width of the
            // pool every later suite scores in. It runs as its own ctest
            // process instead: `peredur_tests thread_pool`.
            return 0;
        }
        if (argc != 2) {
            throw std::invalid_argument(
                "Expected zero arguments or exactly one test suite name.");
        }
        run_suite(argv[1], cfg.black_timeout);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
