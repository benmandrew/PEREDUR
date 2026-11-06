# Wrapper invoked by the lint-clang-tidy target. Runs run-clang-tidy and
# strips the two noisy line types from its output before printing:
#   - "<clang-tidy binary> ... file.cpp" invocation echoes (run-clang-tidy
#     writes this unconditionally per file, regardless of -quiet -- that
#     flag only suppresses clang-tidy's own "N warnings generated" output)
#   - "N warnings generated." per-TU diagnostic summaries
# The binary run-clang-tidy invokes per translation unit. Normally the real
# clang-tidy, pinned explicitly (run-clang-tidy wrapper scripts otherwise
# default to a version-suffixed name baked into the script, e.g. clang-tidy-14,
# which can silently diverge from the clang-tidy CMake resolved on PATH).
set(CLANG_TIDY_INVOKED_BINARY "${CLANG_TIDY_EXE}")

# ctcache interposition, on whenever a clang-tidy-cache binary is on PATH:
# run-clang-tidy is pointed at a wrapper that routes each clang-tidy call
# through the cache, so a translation unit whose preprocessed content and
# .clang-tidy config are unchanged skips re-analysis. Without that binary the
# invocation below is byte-for-byte the uncached one (real clang-tidy, invoked
# directly).
#
# The cache directory is passed explicitly rather than left to be inherited,
# because ctcache's own default is a per-user directory under /tmp: shared
# across every checkout, and gone at boot on a machine that clears /tmp.
# Defaulting it under the build directory keeps it with the other generated
# artifacts, already gitignored and wiped by the same rm -rf that wipes a
# build, at the cost of one cold sweep per fresh build directory. An
# externally set CTCACHE_DIR still wins, which is how CI points it at a path
# its cache action can restore between runs.
set(RUN_CLANG_TIDY_LAUNCHER "")
find_program(CLANG_TIDY_CACHE_EXE NAMES clang-tidy-cache)
if(CLANG_TIDY_EXE AND CLANG_TIDY_CACHE_EXE)
    if(DEFINED ENV{CTCACHE_DIR})
        set(CTCACHE_DIR "$ENV{CTCACHE_DIR}")
    else()
        set(CTCACHE_DIR "${BUILD_DIR}/ctcache")
    endif()
    # ctcache's key covers the preprocessed source and the config, never the
    # clang-tidy binary, so after an LLVM upgrade it would replay the old
    # version's verdicts. One subdirectory per clang-tidy version keeps them
    # apart, CI's directory included.
    execute_process(
        COMMAND "${CLANG_TIDY_EXE}" --version
        OUTPUT_VARIABLE _clang_tidy_version_out
        ERROR_QUIET
    )
    if(_clang_tidy_version_out MATCHES "version ([0-9][0-9.]*)")
        set(CTCACHE_DIR "${CTCACHE_DIR}/clang-tidy-${CMAKE_MATCH_1}")
    else()
        message(FATAL_ERROR "Could not read a version from ${CLANG_TIDY_EXE} --version")
    endif()
    set(CLANG_TIDY_INVOKED_BINARY "${CMAKE_CURRENT_LIST_DIR}/clang_tidy_cache_wrapper.sh")
    set(RUN_CLANG_TIDY_LAUNCHER ${CMAKE_COMMAND} -E env
        "CTCACHE_DIR=${CTCACHE_DIR}"
        "CTCACHE_CACHE_BIN=${CLANG_TIDY_CACHE_EXE}"
        "CTCACHE_REAL_CLANG_TIDY=${CLANG_TIDY_EXE}"
        --)
endif()

set(CLANG_TIDY_BINARY_ARGS "")
if(CLANG_TIDY_INVOKED_BINARY)
    set(CLANG_TIDY_BINARY_ARGS "-clang-tidy-binary=${CLANG_TIDY_INVOKED_BINARY}")
endif()
execute_process(
    COMMAND ${RUN_CLANG_TIDY_LAUNCHER} ${RUN_CLANG_TIDY_EXE}
        -quiet
        -p ${BUILD_DIR}
        ${CLANG_TIDY_BINARY_ARGS}
        ${FILES_PATTERN}
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error_output
    RESULT_VARIABLE result
)
# The invocation echo starts with the exact binary path we pinned above
# (the real clang-tidy, or the cache wrapper when interposing), not the
# unqualified "clang-tidy-<version>" name the stripped pattern used to assume
# before pinning was added.
if(CLANG_TIDY_INVOKED_BINARY)
    string(REGEX REPLACE "${CLANG_TIDY_INVOKED_BINARY}[^\n]*\n?" "" output "${output}")
endif()
string(REGEX REPLACE "[0-9]+ warnings generated\\.[^\n]*\n?" "" output "${output}")
string(STRIP "${output}" output)
string(REGEX REPLACE "[0-9]+ warnings generated\\.[^\n]*\n?" "" error_output "${error_output}")
string(STRIP "${error_output}" error_output)
if(output)
    message("${output}")
endif()
if(error_output)
    message("${error_output}")
endif()
if(result)
    message(FATAL_ERROR "clang-tidy failed (exit ${result})")
endif()
