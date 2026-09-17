add_executable(peredur_tests
    runner/black_tests.cpp
    runner/formaliser_tests.cpp
    runner/ganak_tests.cpp
    runner/ltlfilt_tests.cpp
    runner/process_tests.cpp
    runner/spot_tests.cpp
    filter/correctness_tests.cpp
    filter/implication_tests.cpp
    filter/running_antichain_tests.cpp
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
    peredur compare fingerprint lint-ideals ltl maximal mucs realize signal_tracer)
target_compile_definitions(peredur_tests PRIVATE
    PEREDUR_DRIVER_DIR="$<TARGET_FILE_DIR:peredur>")

if(MSVC)
    target_compile_options(peredur_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(peredur_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME peredur_tests.profile COMMAND peredur_tests profile)
# Again with the profiler switched on. The suite branches on
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
add_test(NAME peredur_tests.transfer_matrix COMMAND peredur_tests transfer_matrix)
add_test(NAME peredur_tests.black_runner COMMAND peredur_tests black_runner)
add_test(NAME peredur_tests.formaliser_runner COMMAND peredur_tests formaliser_runner)
add_test(NAME peredur_tests.ganak_runner COMMAND peredur_tests ganak_runner)
add_test(NAME peredur_tests.ltlfilt_runner COMMAND peredur_tests ltlfilt_runner)
add_test(NAME peredur_tests.process_runner COMMAND peredur_tests process_runner)
add_test(NAME peredur_tests.spot_runner COMMAND peredur_tests spot_runner)
add_test(NAME peredur_tests.accumulator COMMAND peredur_tests accumulator)
add_test(NAME peredur_tests.crossover COMMAND peredur_tests crossover)
add_test(NAME peredur_tests.generation COMMAND peredur_tests generation)
add_test(NAME peredur_tests.determinism COMMAND peredur_tests determinism)
add_test(NAME peredur_tests.pipeline COMMAND peredur_tests pipeline)
add_test(NAME peredur_tests.termination COMMAND peredur_tests termination)
add_test(NAME peredur_tests.nsga2 COMMAND peredur_tests nsga2)
add_test(NAME peredur_tests.mutation COMMAND peredur_tests mutation)
add_test(NAME peredur_tests.fretish_monotone COMMAND peredur_tests fretish_monotone)
add_test(NAME peredur_tests.prop_formula_ast COMMAND peredur_tests prop_formula_ast)
add_test(NAME peredur_tests.prop_formula_canonical COMMAND peredur_tests prop_formula_canonical)
add_test(NAME peredur_tests.prop_formula_cnf COMMAND peredur_tests prop_formula_cnf)
add_test(NAME peredur_tests.prop_formula_rewrite COMMAND peredur_tests prop_formula_rewrite)
add_test(NAME peredur_tests.prop_formula_similarity COMMAND peredur_tests prop_formula_similarity)
add_test(NAME peredur_tests.prop_formula_temporal COMMAND peredur_tests prop_formula_temporal)
add_test(NAME peredur_tests.fingerprint_lasso COMMAND peredur_tests fingerprint_lasso)
add_test(NAME peredur_tests.semantic_similarity COMMAND peredur_tests semantic_similarity)
add_test(NAME peredur_tests.syntactic_similarity COMMAND peredur_tests syntactic_similarity)
add_test(NAME peredur_tests.fitness_function COMMAND peredur_tests fitness_function)
add_test(NAME peredur_tests.status COMMAND peredur_tests status)
add_test(NAME peredur_tests.correctness COMMAND peredur_tests correctness)
add_test(NAME peredur_tests.implication_filter COMMAND peredur_tests implication_filter)
add_test(NAME peredur_tests.running_antichain COMMAND peredur_tests running_antichain)
add_test(NAME peredur_tests.vacuity_filter COMMAND peredur_tests vacuity_filter)
add_test(NAME peredur_tests.well_separation_filter COMMAND peredur_tests well_separation_filter)
add_test(NAME peredur_tests.requirement COMMAND peredur_tests requirement)
add_test(NAME peredur_tests.scope COMMAND peredur_tests scope)
add_test(NAME peredur_tests.serialisation COMMAND peredur_tests serialisation)
add_test(NAME peredur_tests.config_io COMMAND peredur_tests config_io)
add_test(NAME peredur_tests.dashboard COMMAND peredur_tests dashboard)
add_test(NAME peredur_tests.driver_support COMMAND peredur_tests driver_support)
add_test(NAME peredur_tests.tlsf_parser COMMAND peredur_tests tlsf_parser)
add_test(NAME peredur_tests.tlsf_writer COMMAND peredur_tests tlsf_writer)
add_test(NAME peredur_tests.tlsf_filter COMMAND peredur_tests tlsf_filter)
add_test(NAME peredur_tests.tlsf_fitness COMMAND peredur_tests tlsf_fitness)
add_test(NAME peredur_tests.tlsf_mucs COMMAND peredur_tests tlsf_mucs)
add_test(NAME peredur_tests.tlsf_genetic COMMAND peredur_tests tlsf_genetic)
add_test(NAME peredur_tests.tlsf_monotone COMMAND peredur_tests tlsf_monotone)
add_test(NAME peredur_tests.tlsf_assumption COMMAND peredur_tests tlsf_assumption)
add_test(NAME peredur_tests.tlsf_pipeline COMMAND peredur_tests tlsf_pipeline)

# One end-to-end entry per built driver, each running the binary the way a user
# would. They are registered separately so a failure names the driver rather
# than a suite covering eight of them.
add_test(NAME peredur_tests.driver_peredur COMMAND peredur_tests driver_peredur)
add_test(NAME peredur_tests.driver_realize COMMAND peredur_tests driver_realize)
add_test(NAME peredur_tests.driver_ltl COMMAND peredur_tests driver_ltl)
add_test(NAME peredur_tests.driver_mucs COMMAND peredur_tests driver_mucs)
add_test(NAME peredur_tests.driver_compare COMMAND peredur_tests driver_compare)
add_test(NAME peredur_tests.driver_maximal COMMAND peredur_tests driver_maximal)
add_test(NAME peredur_tests.driver_fingerprint
         COMMAND peredur_tests driver_fingerprint)
add_test(NAME peredur_tests.driver_lint_ideals
         COMMAND peredur_tests driver_lint_ideals)
add_test(NAME peredur_tests.driver_signal_tracer
         COMMAND peredur_tests driver_signal_tracer)

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
