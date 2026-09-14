# Use a non-cache variable so a broken PATH binary doesn't get permanently cached.
find_program(_black_candidate black)
if(_black_candidate)
    execute_process(
        COMMAND "${_black_candidate}" --version
        RESULT_VARIABLE _black_check
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(_black_check EQUAL 0)
        set(BLACK_EXECUTABLE "${_black_candidate}"
            CACHE FILEPATH "Path to black executable" FORCE)
        message(STATUS "Using black executable: ${BLACK_EXECUTABLE}")
        return()
    else()
        message(STATUS
            "black found at ${_black_candidate} but failed to run "
            "(likely a missing shared library such as libfmt.so.9); "
            "falling back to downloading a self-contained binary")
    endif()
endif()

set(BLACK_VERSION "25.09.0")
set(BLACK_ROOT_DIR "${COUNTER_THIRD_PARTY_DIR}/black")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Prebuilt debs require Ubuntu 24.04 (noble) glibc/libstdc++.  On older
    # releases (e.g. 22.04 jammy) the binary won't load, so fall back to a
    # source build there.
    execute_process(
        COMMAND lsb_release -cs
        OUTPUT_VARIABLE _ubuntu_codename
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # The architecture test belongs to the deb and nothing else: upstream
    # publishes one Linux binary and it is x86_64, so every other Linux
    # architecture takes the source build below. It guarded the whole Linux
    # branch until 2026-08-27, which failed configure outright on Linux arm64
    # rather than falling through to a path that has no architecture in it --
    # the same path macOS has always used, on Apple Silicon included.
    if(_ubuntu_codename STREQUAL "noble"
       AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        set(BLACK_DEB_NAME "black-sat-${BLACK_VERSION}.ubuntu24.04.x86_64.deb")
        set(BLACK_DOWNLOAD_URL
            "https://github.com/black-sat/black/releases/download/v${BLACK_VERSION}/${BLACK_DEB_NAME}")
        set(BLACK_ARCHIVE_PATH "${BLACK_ROOT_DIR}/${BLACK_DEB_NAME}")
        set(BLACK_EXTRACT_DIR "${BLACK_ROOT_DIR}/extracted")
        set(BLACK_BIN "${BLACK_EXTRACT_DIR}/usr/bin/black")
        set(BLACK_LIB_DIR "${BLACK_EXTRACT_DIR}/usr/lib")
        set(BLACK_WRAPPER "${BLACK_ROOT_DIR}/black")

        file(MAKE_DIRECTORY "${BLACK_ROOT_DIR}")

        if(NOT EXISTS "${BLACK_ARCHIVE_PATH}")
            message(STATUS "Downloading black-sat from ${BLACK_DOWNLOAD_URL}")
            file(DOWNLOAD
                "${BLACK_DOWNLOAD_URL}"
                "${BLACK_ARCHIVE_PATH}"
                STATUS black_download_status
                TLS_VERIFY ON
            )
            list(GET black_download_status 0 black_download_code)
            list(GET black_download_status 1 black_download_message)
            if(NOT black_download_code EQUAL 0)
                message(FATAL_ERROR "Failed to download black-sat: ${black_download_message}")
            endif()
        endif()

        if(NOT EXISTS "${BLACK_BIN}")
            file(MAKE_DIRECTORY "${BLACK_EXTRACT_DIR}")
            message(STATUS "Extracting black-sat package")
            execute_process(
                COMMAND dpkg-deb -x "${BLACK_ARCHIVE_PATH}" "${BLACK_EXTRACT_DIR}"
                RESULT_VARIABLE black_extract_result
            )
            if(NOT black_extract_result EQUAL 0)
                message(FATAL_ERROR "Failed to extract black-sat package.")
            endif()
        endif()

        if(NOT EXISTS "${BLACK_BIN}")
            message(FATAL_ERROR "black executable was not found after extraction: ${BLACK_BIN}")
        endif()

        # Baked in directly (rather than read from the shell's ambient
        # LD_LIBRARY_PATH) so the dev shell doesn't need to export it
        # shell-wide, which would leak into unrelated child processes.
        set(_black_fmt9_lib_dir "$ENV{COUNTER_FMT9_LIB_DIR}")
        file(WRITE "${BLACK_WRAPPER}"
            "#!/bin/sh\nexport LD_LIBRARY_PATH=\"${BLACK_LIB_DIR}:${_black_fmt9_lib_dir}:\${LD_LIBRARY_PATH}\"\nexec \"${BLACK_BIN}\" \"$@\"\n")
        file(CHMOD "${BLACK_WRAPPER}"
            PERMISSIONS
                OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
        )

        set(BLACK_EXECUTABLE "${BLACK_WRAPPER}" CACHE FILEPATH "Path to black executable" FORCE)

    else()
        # Build black from source.  Requires a SAT/SMT solver library; z3 and
        # cryptominisat5 are checked in that order.
        include(ExternalProject)

        find_library(_z3_lib z3)
        find_library(_cryptominisat5_lib cryptominisat5)
        if(_z3_lib)
            set(_black_solver_flag -DENABLE_Z3=ON)
            message(STATUS "black-sat source build: using z3 backend (${_z3_lib})")
        elseif(_cryptominisat5_lib)
            set(_black_solver_flag -DENABLE_CRYPTOMINISAT=ON)
            message(STATUS "black-sat source build: using cryptominisat5 backend (${_cryptominisat5_lib})")
        else()
            message(FATAL_ERROR
                "Building black-sat from source requires a SAT/SMT solver library.\n"
                "Install one with:\n"
                "  sudo apt install libz3-dev\n"
                "or:\n"
                "  sudo apt install libcryptominisat5-dev")
        endif()

        # Propagated into each sub-project's CMAKE_ARGS below so a genuine
        # cache miss at least compiles fast (e.g. sccache, when CI sets it).
        set(_black_launcher_args
            -DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}
            -DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER})

        # tsl-hopscotch-map: header-only hash map, not reliably packaged
        set(TSL_INSTALL_DIR "${BLACK_ROOT_DIR}/deps/tsl-hopscotch")
        set(TSL_PREFIX "${BLACK_ROOT_DIR}/deps/tsl-hopscotch-src")
        # See the comment above SPOT_STAMP_DIR in cmake/spot.cmake: skip
        # re-declaring the external project when a prior (possibly
        # CI-cache-restored) run already completed it, so a cache hit
        # actually avoids redoing the step.
        if(EXISTS "${TSL_PREFIX}/src/tsl_hopscotch_project-stamp/tsl_hopscotch_project-done")
            add_custom_target(tsl_hopscotch_project)
        else()
            ExternalProject_Add(tsl_hopscotch_project
                GIT_REPOSITORY "https://github.com/Tessil/hopscotch-map.git"
                GIT_TAG "v2.4.0"
                GIT_SHALLOW TRUE
                PREFIX "${TSL_PREFIX}"
                INSTALL_DIR "${TSL_INSTALL_DIR}"
                CMAKE_ARGS
                    -DCMAKE_BUILD_TYPE=Release
                    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                    ${_black_launcher_args}
                BUILD_COMMAND ""
                INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
                LOG_CONFIGURE TRUE
                LOG_INSTALL TRUE
                LOG_OUTPUT_ON_FAILURE TRUE
            )
        endif()

        # nlohmann_json: header-only JSON library
        set(NLOHMANN_INSTALL_DIR "${BLACK_ROOT_DIR}/deps/nlohmann")
        set(NLOHMANN_PREFIX "${BLACK_ROOT_DIR}/deps/nlohmann-src")
        if(EXISTS "${NLOHMANN_PREFIX}/src/nlohmann_json_project-stamp/nlohmann_json_project-done")
            add_custom_target(nlohmann_json_project)
        else()
            ExternalProject_Add(nlohmann_json_project
                GIT_REPOSITORY "https://github.com/nlohmann/json.git"
                GIT_TAG "v3.12.0"
                GIT_SHALLOW TRUE
                PREFIX "${NLOHMANN_PREFIX}"
                INSTALL_DIR "${NLOHMANN_INSTALL_DIR}"
                CMAKE_ARGS
                    -DCMAKE_BUILD_TYPE=Release
                    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                    -DJSON_BuildTests=OFF
                    -DJSON_Install=ON
                    ${_black_launcher_args}
                BUILD_COMMAND ""
                INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
                LOG_CONFIGURE TRUE
                LOG_INSTALL TRUE
                LOG_OUTPUT_ON_FAILURE TRUE
            )
        endif()

        set(BLACK_INSTALL_DIR "${BLACK_ROOT_DIR}/install")
        set(BLACK_LIB_DIR "${BLACK_INSTALL_DIR}/lib")
        set(BLACK_BIN "${BLACK_INSTALL_DIR}/bin/black")
        set(BLACK_WRAPPER "${BLACK_ROOT_DIR}/black")
        set(BLACK_PREFIX "${BLACK_ROOT_DIR}/src")

        if(EXISTS "${BLACK_PREFIX}/src/black_project-stamp/black_project-done")
            add_custom_target(black_project)
        else()
            ExternalProject_Add(black_project
                URL "https://github.com/black-sat/black/archive/refs/tags/v${BLACK_VERSION}.tar.gz"
                TLS_VERIFY ON
                DOWNLOAD_EXTRACT_TIMESTAMP ON
                PREFIX "${BLACK_PREFIX}"
                INSTALL_DIR "${BLACK_INSTALL_DIR}"
                LIST_SEPARATOR |
                CMAKE_ARGS
                    -DCMAKE_BUILD_TYPE=Release
                    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                    -DCMAKE_PREFIX_PATH=${TSL_INSTALL_DIR}|${NLOHMANN_INSTALL_DIR}
                    -DENABLE_TESTS=OFF
                    -DENABLE_FORMULAS_TESTS=OFF
                    -DGIT_UPDATE_SUBMODULES=OFF
                    "-DCMAKE_CXX_FLAGS=-Wno-nrvo -Wno-used-but-marked-unused"
                    ${_black_solver_flag}
                    ${_black_launcher_args}
                BUILD_COMMAND
                    ${CMAKE_COMMAND} --build <BINARY_DIR> --target frontend --parallel
                INSTALL_COMMAND
                    ${CMAKE_COMMAND} --install <BINARY_DIR>
                DEPENDS
                    tsl_hopscotch_project
                    nlohmann_json_project
                LOG_CONFIGURE TRUE
                LOG_BUILD TRUE
                LOG_INSTALL TRUE
                LOG_OUTPUT_ON_FAILURE TRUE
            )
        endif()

        file(WRITE "${BLACK_WRAPPER}"
            "#!/bin/sh\nexport LD_LIBRARY_PATH=\"${BLACK_LIB_DIR}:\${LD_LIBRARY_PATH}\"\nexec \"${BLACK_BIN}\" \"$@\"\n")
        file(CHMOD "${BLACK_WRAPPER}"
            PERMISSIONS
                OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
        )

        set(BLACK_EXECUTABLE "${BLACK_WRAPPER}" CACHE FILEPATH "Path to black executable" FORCE)
        set(BLACK_EXTERNAL_TARGET "black_project")
    endif()

elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    include(ExternalProject)

    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT HOMEBREW_PREFIX)
        set(HOMEBREW_PREFIX "/opt/homebrew")
    endif()

    # Propagated into each sub-project's CMAKE_ARGS below so a genuine
    # cache miss at least compiles fast (e.g. sccache, when CI sets it).
    set(_black_launcher_args
        -DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}
        -DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER})

    # tsl-hopscotch-map: header-only hash map, not in Homebrew
    set(TSL_INSTALL_DIR "${BLACK_ROOT_DIR}/deps/tsl-hopscotch")
    set(TSL_PREFIX "${BLACK_ROOT_DIR}/deps/tsl-hopscotch-src")
    # See the comment above SPOT_STAMP_DIR in cmake/spot.cmake: skip
    # re-declaring the external project when a prior (possibly
    # CI-cache-restored) run already completed it, so a cache hit actually
    # avoids redoing the step.
    if(EXISTS "${TSL_PREFIX}/src/tsl_hopscotch_project-stamp/tsl_hopscotch_project-done")
        add_custom_target(tsl_hopscotch_project)
    else()
        ExternalProject_Add(tsl_hopscotch_project
            GIT_REPOSITORY "https://github.com/Tessil/hopscotch-map.git"
            GIT_TAG "v2.4.0"
            GIT_SHALLOW TRUE
            PREFIX "${TSL_PREFIX}"
            INSTALL_DIR "${TSL_INSTALL_DIR}"
            CMAKE_ARGS
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                ${_black_launcher_args}
            BUILD_COMMAND ""
            INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
            LOG_CONFIGURE TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    endif()

    # nlohmann_json: header-only JSON library, not in Homebrew
    set(NLOHMANN_INSTALL_DIR "${BLACK_ROOT_DIR}/deps/nlohmann")
    set(NLOHMANN_PREFIX "${BLACK_ROOT_DIR}/deps/nlohmann-src")
    if(EXISTS "${NLOHMANN_PREFIX}/src/nlohmann_json_project-stamp/nlohmann_json_project-done")
        add_custom_target(nlohmann_json_project)
    else()
        ExternalProject_Add(nlohmann_json_project
            GIT_REPOSITORY "https://github.com/nlohmann/json.git"
            GIT_TAG "v3.12.0"
            GIT_SHALLOW TRUE
            PREFIX "${NLOHMANN_PREFIX}"
            INSTALL_DIR "${NLOHMANN_INSTALL_DIR}"
            CMAKE_ARGS
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                -DJSON_BuildTests=OFF
                -DJSON_Install=ON
                ${_black_launcher_args}
            BUILD_COMMAND ""
            INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
            LOG_CONFIGURE TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    endif()

    set(BLACK_INSTALL_DIR "${BLACK_ROOT_DIR}/install")
    set(BLACK_PREFIX "${BLACK_ROOT_DIR}/src")
    if(EXISTS "${BLACK_PREFIX}/src/black_project-stamp/black_project-done")
        add_custom_target(black_project)
    else()
        ExternalProject_Add(black_project
            URL "https://github.com/black-sat/black/archive/refs/tags/v${BLACK_VERSION}.tar.gz"
            TLS_VERIFY ON
            DOWNLOAD_EXTRACT_TIMESTAMP ON
            PREFIX "${BLACK_PREFIX}"
            INSTALL_DIR "${BLACK_INSTALL_DIR}"
            LIST_SEPARATOR |
            CMAKE_ARGS
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                -DCMAKE_PREFIX_PATH=${TSL_INSTALL_DIR}|${NLOHMANN_INSTALL_DIR}|${HOMEBREW_PREFIX}
                -DENABLE_TESTS=OFF
                -DENABLE_FORMULAS_TESTS=OFF
                -DGIT_UPDATE_SUBMODULES=OFF
                -DCMAKE_MACOSX_RPATH=ON
                -DCMAKE_CXX_FLAGS=-Wno-nrvo
                ${_black_launcher_args}
            BUILD_COMMAND
                ${CMAKE_COMMAND} --build <BINARY_DIR> --target frontend --parallel
            INSTALL_COMMAND
                ${CMAKE_COMMAND} --install <BINARY_DIR>
            # Unlike the Linux paths, BLACK_EXECUTABLE here is the installed
            # binary rather than a wrapper script written at configure time, so
            # nothing has created it by the time the generator runs.
            # ExternalProject declares only its stamps as outputs, leaving the
            # `DEPENDS "${BLACK_EXECUTABLE}"` on black_binary with no rule to
            # produce it -- ninja rejects the whole graph before compiling
            # anything.  add_dependencies alone does not fix that: it orders
            # targets, it does not name a producer for the file.
            BUILD_BYPRODUCTS "${BLACK_INSTALL_DIR}/bin/black"
            DEPENDS
                tsl_hopscotch_project
                nlohmann_json_project
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    endif()

    set(BLACK_EXECUTABLE "${BLACK_INSTALL_DIR}/bin/black"
        CACHE FILEPATH "Path to black executable" FORCE)
    set(BLACK_EXTERNAL_TARGET "black_project")

else()
    message(FATAL_ERROR "No black-sat support for ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "Using black executable: ${BLACK_EXECUTABLE}")
