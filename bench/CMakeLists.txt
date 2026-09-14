include(FetchContent)

FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
    GIT_SHALLOW TRUE
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(peredur_bench
    bench_copy.cpp
    bench_formula.cpp
    bench_implication.cpp
    bench_model_counter.cpp
    bench_mutation.cpp
    main.cpp
)

target_include_directories(peredur_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(peredur_bench PRIVATE peredur_fitness benchmark::benchmark)

if(MSVC)
    target_compile_options(peredur_bench PRIVATE /W4 /permissive-)
else()
    target_compile_options(peredur_bench PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Regression-detection test: asserts black-call counts are correct.
add_test(NAME peredur_bench.assertions
         COMMAND peredur_bench --check-assertions)
set_tests_properties(peredur_bench.assertions PROPERTIES LABELS bench)

# Timing test: writes Google Benchmark JSON for CI perf tracking.
add_test(NAME peredur_bench.timing
         COMMAND peredur_bench
                 --benchmark_format=json
                 --benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/bench_results.json
                 --benchmark_time_unit=ns)
set_tests_properties(peredur_bench.timing PROPERTIES LABELS bench)
