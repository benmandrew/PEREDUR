find_program(CPPLINT_EXE NAMES cpplint cpplint.py)
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
find_program(RUN_CLANG_TIDY_EXE NAMES run-clang-tidy run-clang-tidy-14)
find_program(CPPCHECK_EXE NAMES cppcheck)

set(PEREDUR_LINT_GLOBS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/*.h
    # fuzz/ is compiled via a raw clang++ add_custom_command (see
    # fuzz/CMakeLists.txt), not a normal CMake target, so it has no
    # compile_commands.json entry: only cpplint/cppcheck (compile-database
    # independent) run on it, not clang-tidy below.
    ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

set(PEREDUR_LINT_CPP_GLOBS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/*.cpp
)

# Optional allow-list restricting linting to an explicit set of files, passed
# as an absolute-path list. CI sets this on pull requests to lint only the
# files a branch changed (the full sweep runs on pushes to main); local runs
# and main-branch CI leave it empty and lint everything. A header-only change
# yields an empty CPP subset, which the clang-tidy target below treats as a
# no-op rather than letting run-clang-tidy fall back to linting the whole
# compile database.
set(PEREDUR_LINT_FILES_OVERRIDE "" CACHE STRING
    "Absolute paths of files to lint; empty means lint all sources")

if(PEREDUR_LINT_FILES_OVERRIDE)
    set(PEREDUR_LINT_FILES ${PEREDUR_LINT_FILES_OVERRIDE})
    set(PEREDUR_LINT_CPP_FILES "")
    foreach(lint_file IN LISTS PEREDUR_LINT_FILES_OVERRIDE)
        if(lint_file MATCHES "^${CMAKE_CURRENT_SOURCE_DIR}/(src|test|bench)/.*\\.cpp$")
            list(APPEND PEREDUR_LINT_CPP_FILES ${lint_file})
        endif()
    endforeach()
else()
    file(GLOB_RECURSE PEREDUR_LINT_FILES CONFIGURE_DEPENDS ${PEREDUR_LINT_GLOBS})
    file(GLOB_RECURSE PEREDUR_LINT_CPP_FILES CONFIGURE_DEPENDS ${PEREDUR_LINT_CPP_GLOBS})
endif()

# --- cpplint ---

if(CPPLINT_EXE)
    add_custom_target(lint-cpplint
        COMMAND ${CPPLINT_EXE}
            --config=.cpplint.cfg
            --quiet
            ${PEREDUR_LINT_FILES}
        COMMENT "Running cpplint on C++ sources"
        VERBATIM
    )
else()
    add_custom_target(lint-cpplint
        COMMAND ${CMAKE_COMMAND} -E echo "WARNING: cpplint was not found on PATH, skipping"
        COMMENT "cpplint not available"
        VERBATIM
    )
endif()

# --- clang-tidy ---

# run-clang-tidy's positional args are source-file filter regexes matched
# against the compile database. With a changed-file allow-list, each .cpp path
# doubles as a regex matching exactly itself; otherwise use the tree-wide
# pattern.
if(PEREDUR_LINT_FILES_OVERRIDE)
    set(PEREDUR_CLANG_TIDY_FILTER ${PEREDUR_LINT_CPP_FILES})
else()
    set(PEREDUR_CLANG_TIDY_FILTER "^${CMAKE_CURRENT_SOURCE_DIR}/(src|test|bench)/.*\\.cpp$")
endif()

if(PEREDUR_LINT_FILES_OVERRIDE AND NOT PEREDUR_LINT_CPP_FILES)
    # Restricted to changed files, none of which are .cpp translation units:
    # skip rather than let run-clang-tidy fall back to the whole database.
    add_custom_target(lint-clang-tidy
        COMMAND ${CMAKE_COMMAND} -E echo "clang-tidy: no changed .cpp files, skipping"
        COMMENT "clang-tidy: nothing to do"
        VERBATIM
    )
elseif(RUN_CLANG_TIDY_EXE)
    # run-clang-tidy wrapper scripts default -clang-tidy-binary to a
    # version-suffixed name (e.g. clang-tidy-14) baked into the script
    # itself, which silently diverges from whichever "clang-tidy" resolves
    # to on PATH if multiple LLVM versions are installed (older versions can
    # be missing checks that newer .clang-tidy configs rely on). Pin it
    # explicitly to the same binary CLANG_TIDY_EXE resolved to.
    add_custom_target(lint-clang-tidy
        COMMAND ${CMAKE_COMMAND}
            -DRUN_CLANG_TIDY_EXE=${RUN_CLANG_TIDY_EXE}
            -DCLANG_TIDY_EXE=${CLANG_TIDY_EXE}
            -DBUILD_DIR=${CMAKE_BINARY_DIR}
            "-DFILES_PATTERN=${PEREDUR_CLANG_TIDY_FILTER}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_clang_tidy.cmake
        COMMENT "Running clang-tidy on C++ sources"
        VERBATIM
    )
elseif(CLANG_TIDY_EXE)
    add_custom_target(lint-clang-tidy
        COMMAND ${CLANG_TIDY_EXE}
            --quiet
            -p ${CMAKE_BINARY_DIR}
            ${PEREDUR_LINT_CPP_FILES}
        COMMENT "Running clang-tidy on C++ sources (single-threaded)"
        VERBATIM
    )
else()
    add_custom_target(lint-clang-tidy
        COMMAND ${CMAKE_COMMAND} -E echo "WARNING: clang-tidy was not found on PATH, skipping"
        COMMENT "clang-tidy not available"
        VERBATIM
    )
endif()

# --- cppcheck ---

if(CPPCHECK_EXE)
    add_custom_target(lint-cppcheck
        COMMAND ${CPPCHECK_EXE}
            --enable=warning,style,performance,portability
            --error-exitcode=1
            --suppressions-list=${CMAKE_CURRENT_SOURCE_DIR}/.cppcheck_suppressions.txt
            --quiet
            ${PEREDUR_LINT_FILES}
        COMMENT "Running cppcheck on C++ sources"
        VERBATIM
    )
else()
    add_custom_target(lint-cppcheck
        COMMAND ${CMAKE_COMMAND} -E echo "WARNING: cppcheck was not found on PATH, skipping"
        COMMENT "cppcheck not available"
        VERBATIM
    )
endif()

# --- config key parity ---

# Checks config_io.cpp's key spec, schemas/config-schema.json, and
# example-config.toml against each other. Unlike the linters above this ignores
# PEREDUR_LINT_FILES_OVERRIDE: it is a whole-tree consistency check costing
# milliseconds, and restricting it to changed files would miss exactly the
# case it exists for (one of the three edited without the others).
find_program(PYTHON3_EXE NAMES python3 python)

if(PYTHON3_EXE)
    add_custom_target(lint-config-schema
        COMMAND ${PYTHON3_EXE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_config_schema.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking TOML config key parity"
        VERBATIM
    )
else()
    add_custom_target(lint-config-schema
        COMMAND ${CMAKE_COMMAND} -E echo "WARNING: python3 was not found on PATH, skipping"
        COMMENT "python3 not available"
        VERBATIM
    )
endif()

# --- aggregate ---

add_custom_target(lint)
add_dependencies(lint lint-cpplint lint-clang-tidy lint-cppcheck lint-config-schema)
