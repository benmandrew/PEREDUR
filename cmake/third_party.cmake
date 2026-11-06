# Shared machinery for the solvers fetched or built into PEREDUR_THIRD_PARTY_DIR
# by cmake/spot.cmake, cmake/black.cmake and cmake/ganak.cmake.
#
# Each tool is installed at configure time, under a lock, into a directory of
# its own, and the install finishes by writing a marker naming what was built.
# Configure trusts an installed tool only when the marker matches and the
# binary runs. Anything else is deleted and redone: an interrupted build, a
# version bump, or a library the binary links (Homebrew's z3, a /nix/store
# path) upgraded or garbage-collected underneath it. ExternalProject's done
# stamps could say none of this, and a stamp outlived every one of those
# failures.
#
# The lock is what lets several build trees share one PEREDUR_THIRD_PARTY_DIR:
# a second configure waits for the first and then finds the tool installed.
#
# Downloads land in a directory of their own, checked against a SHA-256, so a
# rebuild needs no network and a truncated or substituted archive is refused.

include_guard(GLOBAL)

set(PEREDUR_DOWNLOAD_DIR "${PEREDUR_THIRD_PARTY_DIR}/downloads")

# Builds run inside configure, where ninja's job pool is idle. An environment
# CMAKE_BUILD_PARALLEL_LEVEL wins, as it does for `cmake --build`.
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL}
   AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
    set(PEREDUR_TOOL_JOBS "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
elseif(CMAKE_BUILD_PARALLEL_LEVEL)
    set(PEREDUR_TOOL_JOBS "${CMAKE_BUILD_PARALLEL_LEVEL}")
else()
    include(ProcessorCount)
    ProcessorCount(PEREDUR_TOOL_JOBS)
    if(NOT PEREDUR_TOOL_JOBS GREATER 0)
        set(PEREDUR_TOOL_JOBS 1)
    endif()
endif()

# peredur_download(<url> <sha256> <file>)
#
# Downloads to <file>.part and renames on success, so an interrupted or failed
# download never leaves a file that a later configure takes for complete.
function(peredur_download url sha256 dest)
    if(EXISTS "${dest}")
        file(SHA256 "${dest}" have)
        if(have STREQUAL sha256)
            return()
        endif()
        message(STATUS "Discarding ${dest}: its SHA-256 is ${have}, not ${sha256}")
        file(REMOVE "${dest}")
    endif()
    get_filename_component(dir "${dest}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    message(STATUS "Downloading ${url}")
    # The hash is checked here, not with EXPECTED_HASH: a mismatch there is a
    # non-fatal error, after which configure would carry on to the rename.
    file(DOWNLOAD "${url}" "${dest}.part"
        STATUS status
        TLS_VERIFY ON)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        list(GET status 1 reason)
        file(REMOVE "${dest}.part")
        message(FATAL_ERROR "Failed to download ${url}: ${reason}")
    endif()
    file(SHA256 "${dest}.part" got)
    if(NOT got STREQUAL sha256)
        file(REMOVE "${dest}.part")
        message(FATAL_ERROR
            "${url} has SHA-256 ${got}, not the pinned ${sha256}. The release "
            "was replaced upstream, or the download was tampered with.")
    endif()
    file(RENAME "${dest}.part" "${dest}")
endfunction()

# peredur_lock_tool(<dir>)
#
# Held until the including cmake file finishes processing. Spot takes about 30
# minutes from cold, hence the two-hour wait.
macro(peredur_lock_tool dir)
    get_filename_component(_peredur_lock_parent "${dir}" DIRECTORY)
    file(MAKE_DIRECTORY "${_peredur_lock_parent}")
    file(LOCK "${dir}.lock" GUARD FILE TIMEOUT 7200
        RESULT_VARIABLE _peredur_lock_result)
    if(NOT _peredur_lock_result EQUAL 0)
        message(FATAL_ERROR
            "Could not lock ${dir}.lock: ${_peredur_lock_result}. Another "
            "configure is installing this tool, or a stale lock holder is "
            "still running.")
    endif()
endmacro()

# peredur_tool_runs(<out-var> <command>...)
function(peredur_tool_runs out)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE rc
        OUTPUT_QUIET
        ERROR_QUIET
        TIMEOUT 60
    )
    if(rc STREQUAL "0")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# peredur_tool_installed(<out-var> DIR <dir> STAMP <text> CHECK <command>...)
#
# TRUE when <dir>/.peredur-installed holds exactly <text> and <command> exits 0
# within the timeout.
function(peredur_tool_installed out)
    cmake_parse_arguments(ARG "" "DIR;STAMP" "CHECK" ${ARGN})
    set(${out} FALSE PARENT_SCOPE)
    set(marker "${ARG_DIR}/.peredur-installed")
    if(NOT EXISTS "${marker}")
        return()
    endif()
    file(READ "${marker}" have)
    if(NOT have STREQUAL ARG_STAMP)
        message(STATUS "${ARG_DIR} was built for '${have}', not '${ARG_STAMP}'; rebuilding")
        return()
    endif()
    peredur_tool_runs(runs ${ARG_CHECK})
    if(NOT runs)
        message(STATUS
            "${ARG_DIR} is installed but fails to run, likely because a library "
            "it links was upgraded or garbage-collected; rebuilding")
        return()
    endif()
    set(${out} TRUE PARENT_SCOPE)
endfunction()

# peredur_mark_installed(<dir> <stamp> CHECK <command>...)
#
# Runs the check once more and only then writes the marker, so a build that
# produced a binary which cannot run is reported here rather than accepted.
function(peredur_mark_installed dir stamp)
    cmake_parse_arguments(ARG "" "" "CHECK" ${ARGN})
    peredur_tool_runs(runs ${ARG_CHECK})
    if(NOT runs)
        message(FATAL_ERROR "The freshly installed tool in ${dir} fails to run: ${ARG_CHECK}")
    endif()
    file(WRITE "${dir}/.peredur-installed" "${stamp}")
endfunction()

# peredur_run_step(<log-file> COMMAND <command>... [WORKING_DIRECTORY <dir>])
#
# Runs one build step with its output in <log-file>, and on failure prints the
# log's last 40 lines before stopping configure.
function(peredur_run_step log)
    cmake_parse_arguments(ARG "" "WORKING_DIRECTORY" "COMMAND" ${ARGN})
    if(NOT ARG_WORKING_DIRECTORY)
        set(ARG_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    # execute_process cannot open a log in a missing directory, and then runs
    # nothing at all.
    get_filename_component(log_dir "${log}" DIRECTORY)
    file(MAKE_DIRECTORY "${log_dir}")
    execute_process(
        COMMAND ${ARG_COMMAND}
        WORKING_DIRECTORY "${ARG_WORKING_DIRECTORY}"
        RESULT_VARIABLE rc
        OUTPUT_FILE "${log}"
        ERROR_FILE "${log}"
    )
    if(NOT rc STREQUAL "0")
        file(STRINGS "${log}" lines)
        list(LENGTH lines n)
        if(n GREATER 40)
            math(EXPR first "${n} - 40")
            list(SUBLIST lines ${first} 40 lines)
        endif()
        list(JOIN lines "\n" tail)
        message(FATAL_ERROR "Step failed (${rc}): ${ARG_COMMAND}\n${tail}\nFull log: ${log}")
    endif()
endfunction()

# peredur_publish_tool_path(<var> <type> <doc>)
#
# Publishes a computed tool path as a cache entry, so `cmake -L` and the scripts
# that read CMakeCache.txt see the path the build compiles in. The path is not
# configurable here: a -D<var>=... is reported and replaced. A built binary
# takes the PEREDUR_* environment overrides at run time instead. A value this
# function published itself (recorded beside it) moves silently, as it does on
# a version bump; so does one from a tree configured before the record existed.
function(peredur_publish_tool_path var type doc)
    if(DEFINED CACHE{${var}}
       AND DEFINED CACHE{_PEREDUR_PUBLISHED_${var}}
       AND NOT "$CACHE{${var}}" STREQUAL "$CACHE{_PEREDUR_PUBLISHED_${var}}"
       AND NOT "$CACHE{${var}}" STREQUAL "${${var}}")
        message(WARNING
            "Ignoring the cached ${var}=$CACHE{${var}}; this build uses "
            "${${var}}. Tool paths follow PEREDUR_THIRD_PARTY_DIR, and a built "
            "binary is pointed elsewhere at run time with the PEREDUR_* "
            "variables (docs/dev/packaging.md).")
    endif()
    set(${var} "${${var}}" CACHE ${type} "${doc} (computed)" FORCE)
    set(_PEREDUR_PUBLISHED_${var} "${${var}}" CACHE INTERNAL "")
endfunction()
