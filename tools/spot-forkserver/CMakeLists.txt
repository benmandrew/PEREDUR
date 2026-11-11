# spot-forkserver: Spot's ltlsynt, ltlfilt, ltl2tgba and autfilt linked into
# one binary that forks a child per request. See spot_forkserver.cpp, and
# "Tool subprocesses" in docs/dev/performance.md.

# Built without the sanitizers the debug and tsan presets put in the global
# flags. The binary is almost entirely Spot, which is not instrumented, and
# LeakSanitizer would report Spot's deliberate leaks at the exit of every tool
# child and turn its exit status into 23. Directory-scoped, so peredur's own
# targets keep them.
set(CMAKE_CXX_FLAGS "")
set(CMAKE_EXE_LINKER_FLAGS "")

find_package(Threads REQUIRED)

add_executable(spot-forkserver spot_forkserver.cpp)
# Spot 2.16.1's headers need C++20; nothing of PEREDUR's is compiled here.
set_target_properties(spot-forkserver PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_include_directories(spot-forkserver SYSTEM PRIVATE
    "${SPOT_INSTALL_DIR}/include")
set(_spot_fs_objects "")
foreach(_spot_fs_tool IN LISTS SPOT_FORKSERVER_TOOLS)
    list(APPEND _spot_fs_objects "${SPOT_FORKSERVER_OBJECT_DIR}/${_spot_fs_tool}.o")
endforeach()
# Plain files, not targets: the configure step in cmake/spot.cmake made them.
# libgnu appears twice because libcommon and libspot both call into it.
target_link_libraries(spot-forkserver PRIVATE
    ${_spot_fs_objects}
    "${SPOT_FORKSERVER_OBJECT_DIR}/libcommon.a"
    "${SPOT_FORKSERVER_OBJECT_DIR}/libgnu.a"
    "${SPOT_INSTALL_DIR}/lib/libspot.a"
    "${SPOT_INSTALL_DIR}/lib/libbddx.a"
    "${SPOT_FORKSERVER_OBJECT_DIR}/libgnu.a"
    atomic
    Threads::Threads)
set_property(TARGET spot-forkserver APPEND PROPERTY LINK_DEPENDS
    ${_spot_fs_objects})

# The round trip: an ltlfilt and an autfilt request, answered in one session.
set(_spot_fs_empty "HOA: v1\nStates: 1\nStart: 0\nAP: 0\nAcceptance: 0 f\n--BODY--\nState: 0\n--END--\n")
string(LENGTH "${_spot_fs_empty}" _spot_fs_empty_length)
set(_spot_fs_request "${CMAKE_CURRENT_BINARY_DIR}/round_trip.request")
file(WRITE "${_spot_fs_request}"
    "3\nltlfilt\n-F\n-\n2\na\n"
    "4\nautfilt\n--is-empty\n-F\n-\n${_spot_fs_empty_length}\n${_spot_fs_empty}")
add_custom_command(TARGET spot-forkserver POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DSERVER=$<TARGET_FILE:spot-forkserver>"
        "-DREQUEST=${_spot_fs_request}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/round_trip.cmake"
    COMMENT "Checking spot-forkserver answers an ltlfilt and an autfilt request"
    VERBATIM)

# The duplicate weak-symbol check that cmake/spot.cmake runs once, at Spot's
# install, run on every ctest over objects of known outcome.
add_test(NAME spot_forkserver_symbol_check
    COMMAND "${CMAKE_COMMAND}" "-DCXX=${CMAKE_CXX_COMPILER}" "-DCMAKE_NM=${CMAKE_NM}"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/symbol_check"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/symbol_check_test.cmake")
