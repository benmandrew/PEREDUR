# Point git at the tracked .githooks/ directory.
#
# Git never clones hooks -- doing so would let any remote run code on clone --
# so core.hooksPath has to be set locally, once per clone, by something the
# developer already runs. Configure is that something: every hook in .githooks/
# shells out to `cmake --build`, so none of them can work before this point
# anyway.
#
# The path is deliberately relative. Git chdirs to the top of the working tree
# before invoking a hook, so ".githooks" resolves per worktree, while the
# setting itself lives in the shared .git/config and is written only once.

if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.githooks)
    return()
endif()

find_package(Git QUIET)

if(NOT GIT_FOUND)
    return()
endif()

# A source tarball or a vendored copy is not a repository; nothing to set.
execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --is-inside-work-tree
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE PEREDUR_INSIDE_WORK_TREE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE PEREDUR_GIT_RESULT
)

if(NOT PEREDUR_GIT_RESULT EQUAL 0 OR NOT PEREDUR_INSIDE_WORK_TREE STREQUAL "true")
    return()
endif()

execute_process(
    COMMAND ${GIT_EXECUTABLE} config --local --get core.hooksPath
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE PEREDUR_HOOKS_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(PEREDUR_HOOKS_PATH STREQUAL ".githooks")
    return()
endif()

# Someone has pointed this repository elsewhere on purpose. Say so rather than
# overwriting a deliberate choice.
if(NOT PEREDUR_HOOKS_PATH STREQUAL "")
    message(STATUS
        "core.hooksPath is '${PEREDUR_HOOKS_PATH}', not '.githooks'; "
        "leaving it alone. The tracked hooks will not run.")
    return()
endif()

execute_process(
    COMMAND ${GIT_EXECUTABLE} config --local core.hooksPath .githooks
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE PEREDUR_HOOKS_SET_RESULT
    ERROR_QUIET
)

if(PEREDUR_HOOKS_SET_RESULT EQUAL 0)
    message(STATUS "Set core.hooksPath to .githooks (tracked git hooks enabled)")
else()
    message(STATUS "Could not set core.hooksPath; tracked git hooks are not enabled")
endif()
