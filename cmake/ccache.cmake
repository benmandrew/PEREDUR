# Compiles through ccache when it is on PATH, so that git worktrees of this
# repository share their object files.
#
# CCACHE_BASEDIR rewrites the absolute paths CMake passes (sources, -I) relative
# to the build directory, and -fdebug-prefix-map records that directory as `.`
# in the debug info, so a translation unit hashes the same in every worktree and
# no object file names another tree. A definition holding an absolute path into
# the build tree, such as SPOT_SERVER_PATH or PEREDUR_DRIVER_DIR, still differs
# per tree, so the targets carrying one miss across worktrees, which is correct.
#
# The launcher goes in only when nobody chose one: a CMAKE_CXX_COMPILER_LAUNCHER
# given on the command line or in the environment wins. The coverage preset
# turns it off, because coverage mapping records the compilation directory and
# a shared object would name another tree's.

include_guard(GLOBAL)

option(PEREDUR_CCACHE "Compile through ccache when it is on PATH" ON)

if(PEREDUR_CCACHE
   AND NOT CMAKE_CXX_COMPILER_LAUNCHER
   AND NOT DEFINED ENV{CMAKE_CXX_COMPILER_LAUNCHER})
    find_program(PEREDUR_CCACHE_PROGRAM ccache)
    if(PEREDUR_CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER
            "${CMAKE_COMMAND}" -E env "CCACHE_BASEDIR=${CMAKE_SOURCE_DIR}"
            "${PEREDUR_CCACHE_PROGRAM}")
        add_compile_options("-fdebug-prefix-map=${CMAKE_BINARY_DIR}=.")
        message(STATUS "Compiling through ${PEREDUR_CCACHE_PROGRAM}")
    endif()
endif()
