cmake_minimum_required(VERSION 3.21)

project(counter VERSION 0.1.0 LANGUAGES CXX)

enable_testing()

if(NOT DEFINED CMAKE_BUILD_PARALLEL_LEVEL)
  include(ProcessorCount)
  ProcessorCount(NPROC)
  if(NPROC GREATER 1)
    set(CMAKE_BUILD_PARALLEL_LEVEL ${NPROC})
  else()
    set(CMAKE_BUILD_PARALLEL_LEVEL 1)
  endif()
endif()

# Spot, black and ganak are spawned as subprocesses and nothing links against
# them, so their trees carry none of a preset's flags -- every preset builds a
# byte-equivalent copy. Hoisting the location behind a variable lets a caller
# point every preset at one tree; CI does, so its four matrix legs share a
# single cached copy rather than building Spot four times under four keys. The
# default keeps each build directory self-contained.
set(COUNTER_THIRD_PARTY_DIR "${CMAKE_BINARY_DIR}/third_party" CACHE PATH
    "Directory holding the built Spot, black and ganak trees")

include(cmake/ganak.cmake)
include(cmake/spot.cmake)
include(cmake/black.cmake)
include(cmake/dependencies.cmake)
include(cmake/version.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(COUNTER_TSAN "Build with ThreadSanitizer" OFF)
if(COUNTER_TSAN)
    # setarch is util-linux. macOS disables ASLR for a child through the
    # debugger rather than the loader, so there is no equivalent wrapper to
    # substitute here; TSan runs without the shadow-memory guarantee that
    # -R buys, and reports it as a mapping failure when it matters.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(CMAKE_CROSSCOMPILING_EMULATOR
            "setarch" "${CMAKE_SYSTEM_PROCESSOR}" "-R"
            CACHE STRING "Disable ASLR for TSan shadow memory" FORCE)
        message(STATUS "TSan: wrapping tests with setarch ${CMAKE_SYSTEM_PROCESSOR} -R (ASLR disabled)")
    else()
        message(STATUS "TSan: no setarch on ${CMAKE_SYSTEM_NAME}; tests run with ASLR enabled")
    endif()
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(counter_core STATIC)
target_sources(counter_core PRIVATE
    src/formula_key.cpp
    src/prop_formula/canonical.cpp
    src/prop_formula/cnf.cpp
    src/prop_formula/core.cpp
    src/prop_formula/dimacs_api.cpp
    src/prop_formula/parser.cpp
    src/prop_formula/render.cpp
    src/prop_formula/rewrite.cpp
    src/fingerprint/lasso.cpp
    src/prop_formula/similarity.cpp
    src/prop_formula/simplify.cpp
    src/prop_formula/transform.cpp
    src/profile.cpp
    src/requirement.cpp
    src/tlsf/lexer.cpp
    src/tlsf/parser.cpp
    src/tlsf/specification.cpp
    src/tlsf/writer.cpp
    src/version.cpp
)
target_include_directories(counter_core PUBLIC include)
# src is on the path for counter_core's own sources alone, so an internal
# header can be named from another directory under src/ ("prop_formula/
# internal.hpp" from src/fingerprint/lasso.cpp resolves relative to src/ without
# it). include/ stays the published surface; nothing outside this target may
# reach src/ this way.
target_include_directories(counter_core PRIVATE src)
# Only version.cpp includes the generated header, so a new commit recompiles
# one translation unit rather than the whole tree.
target_include_directories(counter_core PRIVATE "${COUNTER_VERSION_HEADER_DIR}")
add_dependencies(counter_core counter_version_header)
target_link_libraries(counter_core PUBLIC nlohmann_json::nlohmann_json)
# Include nlohmann as a system directory so clang-tidy skips warnings inside it.
target_include_directories(counter_core SYSTEM PUBLIC ${nlohmann_json_SOURCE_DIR}/include)

add_library(counter_fitness STATIC)
target_sources(counter_fitness PRIVATE
    src/fitness/function.cpp
    src/runner/black.cpp
    src/runner/formaliser.cpp
    src/runner/ganak.cpp
    src/runner/ltlfilt.cpp
    src/runner/process.cpp
    src/runner/spot.cpp
    src/runner/tool_paths.cpp
    src/thread_pool.cpp
    src/filter/bloat.cpp
    src/filter/correctness.cpp
    src/filter/implication.cpp
    src/filter/implication_check.cpp
    src/filter/vacuity.cpp
    src/filter/well_separation.cpp
    src/fitness/exhaustive_count.cpp
    src/fitness/model_counter.cpp
    src/fitness/semantic_similarity.cpp
    src/fitness/status.cpp
    src/fitness/syntactic_similarity.cpp
    src/fitness/transfer_matrix.cpp
    src/genetic/crossover.cpp
    src/genetic/generation.cpp
    src/genetic/monotone.cpp
    src/genetic/mutation.cpp
    src/genetic/nsga2.cpp
    src/tlsf/fitness.cpp
    src/tlsf/mutation.cpp
    src/tlsf/crossover.cpp
    src/tlsf/operators.cpp
    src/tlsf/filter.cpp
    src/fingerprint/prefilter.cpp
    src/tlsf/mucs.cpp
    src/tlsf/guarantee_parts.cpp
    src/tlsf/pipeline.cpp
    src/tlsf/evolve.cpp
    src/tlsf/filter_report.cpp
    src/tlsf/repair_modes.cpp
    src/tlsf/repair_output.cpp
    src/tlsf/survivors.cpp
    src/config_io.cpp
    src/dashboard.cpp
    src/driver_support.cpp
)
target_include_directories(counter_fitness PUBLIC include)
# As counter_core does, and for the same reason: src/tlsf/filter.cpp names
# "fingerprint/lasso.hpp", which lives under src/ because it is not published.
target_include_directories(counter_fitness PRIVATE src)
target_link_libraries(counter_fitness PUBLIC counter_core tomlplusplus::tomlplusplus)
add_dependencies(counter_fitness spot_project)
# Include Eigen as a system directory so clang-tidy skips warnings inside it.
target_include_directories(counter_fitness SYSTEM PUBLIC ${eigen_SOURCE_DIR})
target_compile_definitions(counter_fitness PRIVATE
    GANAK_EXECUTABLE_PATH="${GANAK_EXECUTABLE}"
    SPOT_BIN_DIR="${SPOT_BIN_DIR}"
    BLACK_EXECUTABLE_PATH="${BLACK_EXECUTABLE}"
)
if(DEFINED COUNTER_DEFAULT_FITNESS_WEIGHT)
    target_compile_definitions(counter_fitness PUBLIC
        COUNTER_DEFAULT_FITNESS_WEIGHT=${COUNTER_DEFAULT_FITNESS_WEIGHT})
endif()

# Defaults to the vendored copy (vendor/fretCLI.main.js, see vendor/README.md
# for provenance); override with
# `cmake -DFORMALISER_SCRIPT_PATH=/path/to/fretCLI.main.js` to point at a
# different copy. Still fails configure rather than deferring to the
# assert()/hang at runtime that a silently missing path causes (assert() is
# a no-op in NDEBUG builds, so the release and relwithdebinfo presets would
# otherwise spawn `node` with an empty script argument and hang indefinitely
# instead of failing fast) — this only fires now if the vendored file itself
# goes missing or an override is explicitly cleared.
if(NOT DEFINED FORMALISER_SCRIPT_PATH)
    set(FORMALISER_SCRIPT_PATH "${CMAKE_SOURCE_DIR}/vendor/fretCLI.main.js"
        CACHE FILEPATH "Path to fretCLI.main.js (formalize --batch)")
endif()
if(NOT FORMALISER_SCRIPT_PATH)
    message(FATAL_ERROR
        "FORMALISER_SCRIPT_PATH is not set. Configure with "
        "-DFORMALISER_SCRIPT_PATH=/path/to/fretCLI.main.js")
endif()
target_compile_definitions(counter_fitness PRIVATE
    FORMALISER_SCRIPT_PATH="${FORMALISER_SCRIPT_PATH}")

# The live dashboard page, copied beside progress.jsonl at run start so that
# serving the output directory is all a viewer needs. Resolved from the source
# tree the same way FORMALISER_SCRIPT_PATH is; a run whose page has gone missing
# still writes the progress log, it just does not drop the viewer next to it.
if(NOT DEFINED COUNTER_DASHBOARD_PAGE_PATH)
    set(COUNTER_DASHBOARD_PAGE_PATH "${CMAKE_SOURCE_DIR}/web/dashboard.html"
        CACHE FILEPATH "Path to the live dashboard HTML page")
endif()
target_compile_definitions(counter_fitness PRIVATE
    COUNTER_DASHBOARD_PAGE_PATH="${COUNTER_DASHBOARD_PAGE_PATH}")

add_executable(counter
    src/main.cpp
    src/crash/crash_handler.cpp
    src/repair/cli.cpp
    src/repair/driver.cpp
    src/repair/evolution.cpp
    src/repair/manifest.cpp
    src/repair/reports.cpp
)
target_link_libraries(counter PRIVATE counter_fitness cpptrace::cpptrace)
# The FRETISH driver reaches src/filter/streaming_maximal.hpp and the
# fingerprint prefilter behind it, as maximal does below.
target_include_directories(counter PRIVATE src)

add_executable(signal_tracer src/crash/signal_tracer.cpp)
target_link_libraries(signal_tracer PRIVATE cpptrace::cpptrace)

add_executable(compare src/compare.cpp)
target_link_libraries(compare PRIVATE counter_fitness)

add_executable(lint-ideals src/lint_ideals.cpp)
target_link_libraries(lint-ideals PRIVATE counter_fitness)

add_executable(realize src/realize.cpp)
target_link_libraries(realize PRIVATE counter_fitness)

add_executable(ltl src/ltl.cpp)
target_link_libraries(ltl PRIVATE counter_fitness)

add_executable(mucs src/mucs.cpp)
target_link_libraries(mucs PRIVATE counter_fitness)

add_executable(maximal src/maximal.cpp)
target_link_libraries(maximal PRIVATE counter_fitness)
# maximal reaches one internal header, src/fingerprint/prefilter.hpp, so
# that its equivalence quotient prefilters on the same words the pairwise sweep
# does. include/ stays the published surface.
target_include_directories(maximal PRIVATE src)

add_custom_target(ganak_binary ALL DEPENDS "${GANAK_EXECUTABLE}")
add_custom_target(black_binary ALL DEPENDS "${BLACK_EXECUTABLE}")
if(DEFINED BLACK_EXTERNAL_TARGET)
    add_dependencies(black_binary ${BLACK_EXTERNAL_TARGET})
endif()

add_subdirectory(test)

option(COUNTER_BENCH "Build and register benchmark tests" OFF)
if(COUNTER_BENCH)
    add_subdirectory(bench)
endif()

option(COUNTER_FUZZ
    "Build the LTL-equivalence libFuzzer target (requires a clang++ with libFuzzer support)"
    OFF)
if(COUNTER_FUZZ)
    add_subdirectory(fuzz)
endif()

include(cmake/install.cmake)

include(cmake/docs.cmake)
include(cmake/format.cmake)
include(cmake/lint.cmake)
include(cmake/githooks.cmake)

foreach(counter_target counter_core counter_fitness counter compare realize ltl maximal)
    if(MSVC)
        target_compile_options(${counter_target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${counter_target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

