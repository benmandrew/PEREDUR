# Run on every build by the tool_health target (root CMakeLists). Configure
# checks the solvers too, but a build reruns configure only when a CMake input
# changes, and `brew upgrade` or a Nix garbage collection is not one: after
# either, `cmake --build` would carry on with a black that cannot load and the
# test suite would report it as nineteen unrelated failures. Here it fails once,
# naming the tool, and touches STALE_FILE, a configure dependency, so the next
# build reruns configure and that rebuilds the tool.
#
#   cmake -DSTALE_FILE=<file> -DTOOLS=<name>=<path>|... -P check_tools.cmake
#
# The entries are joined with | because a ; would split the -D argument.

string(REPLACE "|" ";" TOOLS "${TOOLS}")
set(_broken "")
foreach(_entry IN LISTS TOOLS)
    string(FIND "${_entry}" "=" _eq)
    string(SUBSTRING "${_entry}" 0 ${_eq} _name)
    math(EXPR _start "${_eq} + 1")
    string(SUBSTRING "${_entry}" ${_start} -1 _path)
    execute_process(
        COMMAND "${_path}" --version
        RESULT_VARIABLE _rc
        OUTPUT_QUIET
        ERROR_VARIABLE _err
        TIMEOUT 60
    )
    if(NOT _rc STREQUAL "0")
        string(STRIP "${_err}" _err)
        string(APPEND _broken "\n  ${_name} (${_path}): ${_rc}\n    ${_err}")
    endif()
endforeach()

if(_broken)
    file(TOUCH "${STALE_FILE}")
    message(FATAL_ERROR
        "External tools fail to run:${_broken}\n"
        "A library they link was likely upgraded or garbage-collected. Run "
        "the build again: it reconfigures, and configure rebuilds them.")
endif()
