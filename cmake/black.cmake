# black-sat, from one of three places, chosen by PEREDUR_BLACK_FLAVOUR:
#
#   path    a black on PATH that reports BLACK_VERSION;
#   deb     the upstream Ubuntu 24.04 x86_64 package, extracted;
#   source  built from the release tarball, on every other host and on macOS.
#
# `auto` (the default) takes the first that applies. See cmake/third_party.cmake
# for the install protocol the deb and source flavours share. Each flavour and
# version installs into a directory of its own, so a version bump or a change
# of flavour builds afresh rather than finding another build in place, and a
# rebuild never deletes a black that another build tree is using.

include("${CMAKE_CURRENT_LIST_DIR}/third_party.cmake")

set(BLACK_VERSION "25.09.0")
set(_black_source_sha256 "e9e8527c1dd7fcdc791850c1e1c45bb12c0ce9fea7e21e7f3b6dd2e7af59fe95")
set(_black_deb_sha256 "228dd00fa666b5d7b0de0540e5981390ce7ffcacb4380f9020d2044b855ded52")
# Header-only dependencies of the source build, not reliably packaged.
set(_black_tsl_version "2.4.0")
set(_black_tsl_sha256 "c147d1f6af9559c0e91af3ecf62274404ce5fb35ce94d2234c080ccc7a5913de")
set(_black_json_version "3.12.0")
set(_black_json_sha256 "42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa")

set(PEREDUR_BLACK_FLAVOUR "auto" CACHE STRING
    "Where black-sat comes from: auto, path, deb or source")
set_property(CACHE PEREDUR_BLACK_FLAVOUR PROPERTY STRINGS auto path deb source)
if(NOT PEREDUR_BLACK_FLAVOUR MATCHES "^(auto|path|deb|source)$")
    message(FATAL_ERROR
        "PEREDUR_BLACK_FLAVOUR must be auto, path, deb or source, "
        "got '${PEREDUR_BLACK_FLAVOUR}'")
endif()

# Left set for cmake/install.cmake by the deb and source flavours. A black from
# PATH sets neither: this build never fetched it, and the host's copy is not
# ours to install.
set(BLACK_BIN "")
set(BLACK_LIB_DIR "")
set(_black_flavour "")

# --- path ---------------------------------------------------------------------

# Before 2026-11-05 this lookup was cached under the name below.
unset(_black_candidate CACHE)

if(PEREDUR_BLACK_FLAVOUR MATCHES "^(auto|path)$")
    # The name is shared with the Python code formatter, which answers
    # --version too, and another black-sat release answers it with another
    # number. Only the pinned release is taken.
    find_program(_black_on_path black NO_CACHE)
    if(_black_on_path)
        execute_process(
            COMMAND "${_black_on_path}" --version
            RESULT_VARIABLE _black_path_rc
            OUTPUT_VARIABLE _black_path_out
            ERROR_VARIABLE _black_path_out
            TIMEOUT 30
        )
        string(REPLACE "." "\\." _black_version_regex "${BLACK_VERSION}")
        if(_black_path_rc STREQUAL "0"
           AND _black_path_out MATCHES "version ${_black_version_regex}([^0-9]|$)")
            set(BLACK_EXECUTABLE "${_black_on_path}")
            set(_black_flavour "path")
        else()
            message(STATUS
                "Ignoring the black at ${_black_on_path}: it does not report "
                "black-sat ${BLACK_VERSION}")
        endif()
    endif()
    if(PEREDUR_BLACK_FLAVOUR STREQUAL "path" AND NOT _black_flavour)
        message(FATAL_ERROR
            "PEREDUR_BLACK_FLAVOUR=path, but no black on PATH reports "
            "black-sat ${BLACK_VERSION}")
    endif()
endif()

# --- choose deb or source -----------------------------------------------------

if(NOT _black_flavour)
    if(PEREDUR_BLACK_FLAVOUR MATCHES "^(deb|source)$")
        set(_black_flavour "${PEREDUR_BLACK_FLAVOUR}")
    else()
        # The prebuilt deb needs Ubuntu 24.04's glibc and libstdc++, and
        # upstream publishes it for x86_64 only. Every other Linux host, and
        # macOS on either architecture, builds from source.
        set(_black_flavour "source")
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
           AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
            execute_process(
                COMMAND lsb_release -cs
                OUTPUT_VARIABLE _ubuntu_codename
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_ubuntu_codename STREQUAL "noble")
                set(_black_flavour "deb")
            endif()
        endif()
    endif()
    if(_black_flavour STREQUAL "deb" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "PEREDUR_BLACK_FLAVOUR=deb needs a Linux host")
    endif()
endif()

set(BLACK_ROOT_DIR "${PEREDUR_THIRD_PARTY_DIR}/black/${BLACK_VERSION}-${_black_flavour}")

# A Linux black needs its own lib directory on the loader path, which a wrapper
# script supplies. Baked in rather than read from the shell's LD_LIBRARY_PATH,
# so the dev shell need not export it shell-wide and leak it into unrelated
# children. Empty entries are left out: ld.so reads one as the current
# directory, and black is spawned from the run's working directory.
function(_black_write_wrapper wrapper bin lib_dirs)
    list(JOIN lib_dirs ":" joined)
    file(WRITE "${wrapper}"
        "#!/bin/sh\n"
        "LD_LIBRARY_PATH=\"${joined}\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}\"\n"
        "export LD_LIBRARY_PATH\n"
        "exec \"${bin}\" \"$@\"\n")
    file(CHMOD "${wrapper}"
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
endfunction()

# --- deb ----------------------------------------------------------------------

if(_black_flavour STREQUAL "deb")
    set(_black_extract_dir "${BLACK_ROOT_DIR}/extracted")
    set(BLACK_BIN "${_black_extract_dir}/usr/bin/black")
    set(BLACK_LIB_DIR "${_black_extract_dir}/usr/lib")
    set(BLACK_EXECUTABLE "${BLACK_ROOT_DIR}/black")

    # The deb links libfmt.so.9 and does not bundle it. The dev shell provides
    # one (PEREDUR_FMT9_LIB_DIR, flake.nix); elsewhere the system's must do.
    set(_black_fmt9_lib_dir "$ENV{PEREDUR_FMT9_LIB_DIR}")
    set(_black_lib_dirs "${BLACK_LIB_DIR}")
    if(_black_fmt9_lib_dir)
        list(APPEND _black_lib_dirs "${_black_fmt9_lib_dir}")
    endif()
    set(_black_stamp "black ${BLACK_VERSION} deb fmt9=${_black_fmt9_lib_dir}")
    set(_black_check "${BLACK_EXECUTABLE}" --version)

    peredur_lock_tool("${BLACK_ROOT_DIR}")
    peredur_tool_installed(_black_ok
        DIR "${BLACK_ROOT_DIR}" STAMP "${_black_stamp}" CHECK ${_black_check})
    if(NOT _black_ok)
        file(REMOVE_RECURSE "${BLACK_ROOT_DIR}")
        file(MAKE_DIRECTORY "${_black_extract_dir}")
        set(_black_deb_name "black-sat-${BLACK_VERSION}.ubuntu24.04.x86_64.deb")
        set(_black_deb "${PEREDUR_DOWNLOAD_DIR}/${_black_deb_name}")
        peredur_download(
            "https://github.com/black-sat/black/releases/download/v${BLACK_VERSION}/${_black_deb_name}"
            "${_black_deb_sha256}" "${_black_deb}")
        find_program(_black_dpkg_deb dpkg-deb NO_CACHE REQUIRED)
        peredur_run_step("${PEREDUR_THIRD_PARTY_DIR}/logs/black-deb-extract.log"
            COMMAND "${_black_dpkg_deb}" -x "${_black_deb}" "${_black_extract_dir}")
        if(NOT EXISTS "${BLACK_BIN}")
            message(FATAL_ERROR "${_black_deb} holds no usr/bin/black")
        endif()
        _black_write_wrapper("${BLACK_EXECUTABLE}" "${BLACK_BIN}" "${_black_lib_dirs}")
        peredur_mark_installed("${BLACK_ROOT_DIR}" "${_black_stamp}" CHECK ${_black_check})
    endif()
    file(LOCK "${BLACK_ROOT_DIR}.lock" RELEASE)
endif()

# --- source -------------------------------------------------------------------

if(_black_flavour STREQUAL "source")
    set(_black_work_dir "${BLACK_ROOT_DIR}/work")
    set(_black_deps_dir "${BLACK_ROOT_DIR}/deps")
    set(_black_install_dir "${BLACK_ROOT_DIR}/install")
    set(BLACK_BIN "${_black_install_dir}/bin/black")
    set(BLACK_LIB_DIR "${_black_install_dir}/lib")

    # Inside the Nix dev shell the solver and fmt come from the flake. Before
    # 2026-11-05 macOS added Homebrew's prefix ahead of them, so black linked
    # Homebrew's z3 and fmt, and every `brew upgrade` that renamed a library
    # left the cached black unable to load. Turning off the PATH-derived
    # prefixes keeps /opt/homebrew/bin from leading the search back there.
    # Outside the shell, macOS still needs Homebrew to supply them.
    set(_black_env "")
    set(_black_platform_args "")
    set(_black_find_args NO_CACHE)
    if(DEFINED ENV{IN_NIX_SHELL})
        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            # That also hides install_name_tool, and black's install then
            # runs CMAKE_INSTALL_NAME_TOOL-NOTFOUND, ignores the failure and
            # leaves the binary an rpath into the deleted build tree.
            find_program(_black_install_name_tool install_name_tool NO_CACHE REQUIRED)
            list(APPEND _black_platform_args
                -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF
                "-DCMAKE_INSTALL_NAME_TOOL=${_black_install_name_tool}")
            list(APPEND _black_find_args NO_SYSTEM_ENVIRONMENT_PATH)
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        execute_process(
            COMMAND brew --prefix
            OUTPUT_VARIABLE _black_brew_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(NOT _black_brew_prefix)
            set(_black_brew_prefix "/opt/homebrew")
        endif()
        set(_black_env "CMAKE_PREFIX_PATH=${_black_brew_prefix}")
        list(APPEND _black_find_args HINTS "${_black_brew_prefix}/lib")
    endif()

    # The libraries the build will link go into the stamp by their resolved
    # paths, so a solver upgrade (a new Homebrew keg, a new /nix/store path)
    # rebuilds black at the next configure, before the old one stops loading.
    find_library(_black_z3_lib z3 ${_black_find_args})
    find_library(_black_cms_lib cryptominisat5 ${_black_find_args})
    find_library(_black_fmt_lib fmt ${_black_find_args})
    # black's own build leaves the installed binary an rpath into its build
    # tree, which is deleted once installed; the install rpath points beside it.
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        list(APPEND _black_platform_args
            -DCMAKE_MACOSX_RPATH=ON
            "-DCMAKE_INSTALL_RPATH=@loader_path/../lib"
            -DCMAKE_CXX_FLAGS=-Wno-nrvo)
        set(_black_solver_lib "${_black_z3_lib}")
    else()
        # z3 and cryptominisat5 are tried in that order.
        if(_black_z3_lib)
            list(APPEND _black_platform_args -DENABLE_Z3=ON)
            set(_black_solver_lib "${_black_z3_lib}")
        elseif(_black_cms_lib)
            list(APPEND _black_platform_args -DENABLE_CRYPTOMINISAT=ON)
            set(_black_solver_lib "${_black_cms_lib}")
        else()
            message(FATAL_ERROR
                "Building black-sat from source requires a SAT/SMT solver library.\n"
                "Install one with:\n"
                "  sudo apt install libz3-dev\n"
                "or:\n"
                "  sudo apt install libcryptominisat5-dev")
        endif()
        list(APPEND _black_platform_args
            "-DCMAKE_INSTALL_RPATH=$ORIGIN/../lib"
            "-DCMAKE_CXX_FLAGS=-Wno-nrvo -Wno-used-but-marked-unused")
    endif()
    set(_black_solver_real "${_black_solver_lib}")
    if(EXISTS "${_black_solver_lib}")
        file(REAL_PATH "${_black_solver_lib}" _black_solver_real)
    endif()
    set(_black_fmt_real "${_black_fmt_lib}")
    if(EXISTS "${_black_fmt_lib}")
        file(REAL_PATH "${_black_fmt_lib}" _black_fmt_real)
    endif()
    set(_black_stamp
        "black ${BLACK_VERSION} source solver=${_black_solver_real} fmt=${_black_fmt_real}")

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(BLACK_EXECUTABLE "${BLACK_BIN}")
    else()
        set(BLACK_EXECUTABLE "${BLACK_ROOT_DIR}/black")
    endif()
    set(_black_check "${BLACK_EXECUTABLE}" --version)

    peredur_lock_tool("${BLACK_ROOT_DIR}")
    peredur_tool_installed(_black_ok
        DIR "${BLACK_ROOT_DIR}" STAMP "${_black_stamp}" CHECK ${_black_check})
    if(NOT _black_ok)
        file(REMOVE_RECURSE "${BLACK_ROOT_DIR}")
        # Logs sit outside the work directory, which is deleted once installed.
        set(_black_log_dir "${PEREDUR_THIRD_PARTY_DIR}/logs/black-${BLACK_VERSION}-source")
        file(REMOVE_RECURSE "${_black_log_dir}")
        file(MAKE_DIRECTORY "${_black_work_dir}" "${_black_log_dir}")
        message(STATUS
            "Building black-sat ${BLACK_VERSION} into ${BLACK_ROOT_DIR} "
            "(logs in ${_black_log_dir})")

        set(_black_generator_args -G "${CMAKE_GENERATOR}")
        if(CMAKE_MAKE_PROGRAM)
            list(APPEND _black_generator_args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
        endif()
        # A genuine cache miss at least compiles fast (sccache, when CI sets it).
        set(_black_launcher_args
            "-DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}"
            "-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")

        set(_black_tsl_archive
            "${PEREDUR_DOWNLOAD_DIR}/hopscotch-map-${_black_tsl_version}.tar.gz")
        peredur_download(
            "https://github.com/Tessil/hopscotch-map/archive/refs/tags/v${_black_tsl_version}.tar.gz"
            "${_black_tsl_sha256}" "${_black_tsl_archive}")
        set(_black_json_archive
            "${PEREDUR_DOWNLOAD_DIR}/nlohmann-json-${_black_json_version}.tar.xz")
        peredur_download(
            "https://github.com/nlohmann/json/releases/download/v${_black_json_version}/json.tar.xz"
            "${_black_json_sha256}" "${_black_json_archive}")
        set(_black_archive "${PEREDUR_DOWNLOAD_DIR}/black-${BLACK_VERSION}.tar.gz")
        peredur_download(
            "https://github.com/black-sat/black/archive/refs/tags/v${BLACK_VERSION}.tar.gz"
            "${_black_source_sha256}" "${_black_archive}")

        foreach(_black_archive_file
                "${_black_tsl_archive}" "${_black_json_archive}" "${_black_archive}")
            file(ARCHIVE_EXTRACT INPUT "${_black_archive_file}"
                DESTINATION "${_black_work_dir}")
        endforeach()

        # The two header-only dependencies only need installing, for their
        # package config files.
        foreach(_black_dep
                "hopscotch-map-${_black_tsl_version}|tsl"
                "json|json")
            string(REPLACE "|" ";" _black_dep "${_black_dep}")
            list(GET _black_dep 0 _black_dep_src)
            list(GET _black_dep 1 _black_dep_name)
            peredur_run_step("${_black_log_dir}/${_black_dep_name}-configure.log"
                COMMAND "${CMAKE_COMMAND}"
                    -S "${_black_work_dir}/${_black_dep_src}"
                    -B "${_black_work_dir}/${_black_dep_name}-build"
                    ${_black_generator_args}
                    -DCMAKE_BUILD_TYPE=Release
                    "-DCMAKE_INSTALL_PREFIX=${_black_deps_dir}"
                    -DJSON_BuildTests=OFF
                    -DJSON_Install=ON)
            peredur_run_step("${_black_log_dir}/${_black_dep_name}-install.log"
                COMMAND "${CMAKE_COMMAND}" --install "${_black_work_dir}/${_black_dep_name}-build")
        endforeach()

        set(_black_build_dir "${_black_work_dir}/black-build")
        peredur_run_step("${_black_log_dir}/black-configure.log"
            COMMAND "${CMAKE_COMMAND}" -E env ${_black_env}
                "${CMAKE_COMMAND}"
                -S "${_black_work_dir}/black-${BLACK_VERSION}"
                -B "${_black_build_dir}"
                ${_black_generator_args}
                -DCMAKE_BUILD_TYPE=Release
                "-DCMAKE_INSTALL_PREFIX=${_black_install_dir}"
                "-DCMAKE_PREFIX_PATH=${_black_deps_dir}"
                -DENABLE_TESTS=OFF
                -DENABLE_FORMULAS_TESTS=OFF
                -DGIT_UPDATE_SUBMODULES=OFF
                ${_black_platform_args}
                ${_black_launcher_args})
        peredur_run_step("${_black_log_dir}/black-build.log"
            COMMAND "${CMAKE_COMMAND}" --build "${_black_build_dir}"
                --target frontend --parallel "${PEREDUR_TOOL_JOBS}")
        peredur_run_step("${_black_log_dir}/black-install.log"
            COMMAND "${CMAKE_COMMAND}" --install "${_black_build_dir}")

        if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            _black_write_wrapper("${BLACK_EXECUTABLE}" "${BLACK_BIN}" "${BLACK_LIB_DIR}")
        endif()
        # The extracted tarball and build tree come to about a gigabyte; the
        # archives stay in the download directory for the next rebuild. Removed
        # before the check, so the check sees what later runs will.
        file(REMOVE_RECURSE "${_black_work_dir}")
        peredur_mark_installed("${BLACK_ROOT_DIR}" "${_black_stamp}" CHECK ${_black_check})
    endif()
    file(LOCK "${BLACK_ROOT_DIR}.lock" RELEASE)
endif()

peredur_publish_tool_path(BLACK_EXECUTABLE FILEPATH "Path to the black executable")
message(STATUS "Using black (${_black_flavour}): ${BLACK_EXECUTABLE}")
