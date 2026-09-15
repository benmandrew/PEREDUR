# The libFuzzer runtime (-fsanitize=fuzzer) is a clang/compiler-rt feature
# with no GCC equivalent, so this target is built with its own clang++
# invocation via add_custom_command rather than add_executable — the rest of
# the project keeps building with whichever compiler the active preset uses
# (GCC in the debug preset). Linking a clang+libFuzzer-compiled object
# against the GCC-compiled, ASAN/UBSAN-instrumented peredur_core/
# peredur_fitness static libraries works in practice (verified empirically)
# without needing a second, fully clang-built copy of the whole dependency
# tree (which would re-trigger SPOT's from-source ExternalProject build).
find_program(PEREDUR_FUZZ_CLANGXX NAMES clang++)
if(NOT PEREDUR_FUZZ_CLANGXX)
    message(FATAL_ERROR
        "PEREDUR_FUZZ requires a clang++ with libFuzzer support on PATH; "
        "none found. Install one (e.g. a system clang package) or disable "
        "with -DPEREDUR_FUZZ=OFF.")
endif()

set(PEREDUR_FUZZ_BINARY "${CMAKE_CURRENT_BINARY_DIR}/ltl_equivalence_fuzzer")
set(PEREDUR_FUZZ_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/ltl_equivalence_fuzzer.cpp")

add_custom_command(
    OUTPUT "${PEREDUR_FUZZ_BINARY}"
    COMMAND "${PEREDUR_FUZZ_CLANGXX}"
            -std=c++17 -O1 -g
            -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all
            "-I${CMAKE_SOURCE_DIR}/include"
            "${PEREDUR_FUZZ_SOURCE}"
            "$<TARGET_FILE:peredur_fitness>"
            "$<TARGET_FILE:peredur_core>"
            -o "${PEREDUR_FUZZ_BINARY}"
    DEPENDS "${PEREDUR_FUZZ_SOURCE}" peredur_fitness peredur_core
    COMMENT "Building ltl_equivalence_fuzzer with ${PEREDUR_FUZZ_CLANGXX} (libFuzzer)"
    VERBATIM
)
# Named apart from the binary: Ninja would otherwise emit a phony
# fuzz/ltl_equivalence_fuzzer that collides with the output file.
add_custom_target(fuzz_ltl_equivalence ALL DEPENDS "${PEREDUR_FUZZ_BINARY}")
