add_executable(peredur_tests
    runner/black_tests.cpp
    runner/formaliser_tests.cpp
    runner/ganak_tests.cpp
    runner/ltlfilt_tests.cpp
    runner/process_tests.cpp
    runner/spot_tests.cpp
    filter/correctness_tests.cpp
    filter/implication_tests.cpp
    filter/vacuity_tests.cpp
    filter/well_separation_tests.cpp
    genetic/accumulator_tests.cpp
    genetic/crossover_tests.cpp
    genetic/generation_tests.cpp
    genetic/determinism_tests.cpp
    genetic/pipeline_tests.cpp
    genetic/termination_tests.cpp
    genetic/mutation_tests.cpp
    genetic/monotone_arm_tests.cpp
    genetic/nsga2_tests.cpp
    main.cpp
    fitness/function_tests.cpp
    fitness/status_tests.cpp
    fitness/semantic_similarity_tests.cpp
    fitness/syntactic_similarity_tests.cpp
    fitness/transfer_matrix_tests.cpp
    requirement_tests.cpp
    scope_tests.cpp
    prop_formula/ast_tests.cpp
    prop_formula/canonical_tests.cpp
    prop_formula/cnf_tests.cpp
    prop_formula/rewrite_tests.cpp
    prop_formula/similarity_tests.cpp
    prop_formula/temporal_tests.cpp
    serialisation_tests.cpp
    config_io_tests.cpp
    dashboard_tests.cpp
    driver_support_tests.cpp
    profile_tests.cpp
    thread_pool_tests.cpp
    tlsf/parser_tests.cpp
    tlsf/writer_tests.cpp
    tlsf/filter_tests.cpp
    tlsf/fitness_tests.cpp
    tlsf/mucs_tests.cpp
    tlsf/guarantee_parts_tests.cpp
    tlsf/genetic_tests.cpp
    tlsf/assumption_tests.cpp
    tlsf/monotone_tests.cpp
    tlsf/pipeline_tests.cpp
    fingerprint/lasso_tests.cpp
    drivers/e2e_tests.cpp
    test_registry.cpp
)

# src/ as well as test/, so suites can reach the private headers next to the
# implementation they cover (driver_support.hpp and the like). Those are
# deliberately not in include/, which is the published API surface.
target_include_directories(peredur_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(peredur_tests PRIVATE peredur_fitness)

# The end-to-end suites spawn the drivers rather than calling into the library,
# so the binaries have to exist before ctest runs and the suite has to know
# where they landed. Every driver is built into the same directory, so one
# generator expression covers all of them.
add_dependencies(peredur_tests
    peredur compare lint-ideals ltl maximal mucs realize signal_tracer)
target_compile_definitions(peredur_tests PRIVATE
    PEREDUR_DRIVER_DIR="$<TARGET_FILE_DIR:peredur>")

if(MSVC)
    target_compile_options(peredur_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(peredur_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

# One entry per suite in the table in main.cpp, which also fixes which tests
# each runs. The drivers' end-to-end suites are separate entries so a failure
# names the driver rather than a suite covering eight of them.
set(peredur_test_suites
    profile
    transfer_matrix
    black_runner
    formaliser_runner
    ganak_runner
    ltlfilt_runner
    process_runner
    spot_runner
    accumulator
    crossover
    generation
    determinism
    pipeline
    termination
    nsga2
    mutation
    fretish_monotone
    prop_formula_ast
    prop_formula_canonical
    prop_formula_cnf
    prop_formula_rewrite
    prop_formula_similarity
    prop_formula_temporal
    fingerprint_lasso
    semantic_similarity
    syntactic_similarity
    fitness_function
    status
    correctness
    implication_filter
    vacuity_filter
    well_separation_filter
    requirement
    scope
    serialisation
    config_io
    dashboard
    driver_support
    tlsf_parser
    tlsf_writer
    tlsf_filter
    tlsf_fitness
    tlsf_mucs
    tlsf_genetic
    tlsf_monotone
    tlsf_assumption
    tlsf_pipeline
    driver_peredur
    driver_realize
    driver_ltl
    driver_mucs
    driver_compare
    driver_maximal
    driver_lint_ideals
    driver_signal_tracer
)
foreach(suite IN LISTS peredur_test_suites)
    add_test(NAME peredur_tests.${suite} COMMAND peredur_tests ${suite})
endforeach()

# The profile suite again with the profiler switched on. The suite branches on
# profile::enabled(), so registering it only once means every assertion about
# actually recording anything is skipped -- the suite would pass by testing that
# a disabled profiler does nothing, which is the half that cannot regress.
add_test(NAME peredur_tests.profile_enabled COMMAND peredur_tests profile)
set_tests_properties(peredur_tests.profile_enabled PROPERTIES
    ENVIRONMENT "PEREDUR_PROFILE=1")
add_test(NAME peredur_tests.thread_pool_default COMMAND peredur_tests thread_pool)
# And again with a size requested. The global pool is built once per process and
# never resized, so the default and the requested width cannot both be observed
# in one run -- each case needs a process of its own, which is what two
# registrations buy. For the same reason the suite is not in the no-argument
# run-everything path in main.cpp.
add_test(NAME peredur_tests.thread_pool_sized COMMAND peredur_tests thread_pool)
set_tests_properties(peredur_tests.thread_pool_sized PROPERTIES
    ENVIRONMENT "PEREDUR_TEST_POOL_SIZE=3")

# The dashboard page ships as one self-contained HTML file, so its script is
# tested by extracting it from the page and running it under node -- the same
# node the FRET formaliser already needs at run time. It is not a build
# dependency, so a missing one skips these rather than failing configuration.
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    file(GLOB dashboard_page_tests CONFIGURE_DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/web/*.test.mjs)
    add_test(NAME dashboard_page
             COMMAND ${NODE_EXECUTABLE} --test ${dashboard_page_tests})
else()
    message(STATUS "node not found; skipping the dashboard page's tests")
endif()

add_custom_target(tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS peredur_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building and running all tests"
    VERBATIM
)
