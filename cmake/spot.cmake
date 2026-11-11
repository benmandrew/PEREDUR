# Spot, built from source at configure time. See cmake/third_party.cmake for
# the install protocol: a lock, a marker, and a run check of ltlsynt.

include("${CMAKE_CURRENT_LIST_DIR}/third_party.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/spot_forkserver_objects.cmake")

set(SPOT_VERSION "2.16.1")
set(SPOT_SHA256 "182f4180da851588886a2e5f1540c3e2bb5cfddd228fd20689fa7ce498f57733")
set(SPOT_DOWNLOAD_URL
    "https://www.lre.epita.fr/dload/spot/spot-${SPOT_VERSION}.tar.gz")
set(SPOT_INSTALL_DIR "${PEREDUR_THIRD_PARTY_DIR}/spot")
set(SPOT_BIN_DIR "${SPOT_INSTALL_DIR}/bin")
set(_spot_work_dir "${PEREDUR_THIRD_PARTY_DIR}/spot_src")
set(_spot_stamp "spot ${SPOT_VERSION}")
# The fork server (tools/spot-forkserver) links tool objects from Spot's build
# tree, which is deleted once Spot is installed, so they are kept in the
# install directory and the stamp says so: an install made without them fails
# the stamp and is rebuilt once. Linux only for now; see docs/dev/packaging.md.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PEREDUR_SPOT_FORKSERVER ON)
    set(SPOT_FORKSERVER_OBJECT_DIR "${SPOT_INSTALL_DIR}/forkserver")
    string(APPEND _spot_stamp " forkserver 1")
else()
    set(PEREDUR_SPOT_FORKSERVER OFF)
endif()
set(_spot_check "${SPOT_BIN_DIR}/ltlsynt" --version)

# Before 2026-11-05 this cache entry held the make program, found by name
# order and so Homebrew's gmake ahead of the dev shell's make.
unset(SPOT_MAKE_PROGRAM CACHE)

peredur_lock_tool("${SPOT_INSTALL_DIR}")

# Adopts an install made by the ExternalProject this module used before
# 2026-11-05, so the move to the marker costs no rebuild: it wrote
# .spot-<version> after `make install`, beside ExternalProject's done stamp.
# Never under the fork server, since that install has no server objects.
if(NOT PEREDUR_SPOT_FORKSERVER
   AND NOT EXISTS "${SPOT_INSTALL_DIR}/.peredur-installed"
   AND EXISTS "${SPOT_INSTALL_DIR}/.spot-${SPOT_VERSION}"
   AND EXISTS "${_spot_work_dir}/src/spot_project-stamp/spot_project-done")
    peredur_tool_runs(_spot_legacy_runs ${_spot_check})
    if(_spot_legacy_runs)
        file(WRITE "${SPOT_INSTALL_DIR}/.peredur-installed" "${_spot_stamp}")
        message(STATUS "Adopted the Spot ${SPOT_VERSION} install at ${SPOT_INSTALL_DIR}")
    endif()
endif()

peredur_tool_installed(_spot_ok
    DIR "${SPOT_INSTALL_DIR}" STAMP "${_spot_stamp}" CHECK ${_spot_check})
if(_spot_ok AND PEREDUR_SPOT_FORKSERVER)
    set(_spot_objects libcommon.a libgnu.a)
    foreach(_spot_object IN LISTS SPOT_FORKSERVER_TOOLS)
        list(APPEND _spot_objects "${_spot_object}.o")
    endforeach()
    foreach(_spot_object IN LISTS _spot_objects)
        if(NOT EXISTS "${SPOT_FORKSERVER_OBJECT_DIR}/${_spot_object}")
            message(STATUS "${SPOT_FORKSERVER_OBJECT_DIR} lacks ${_spot_object}; rebuilding Spot")
            set(_spot_ok FALSE)
        endif()
    endforeach()
endif()

if(_spot_ok)
    message(STATUS "Spot ${SPOT_VERSION} already installed at ${SPOT_INSTALL_DIR}")
else()
    # The install prefix is baked into the binaries (libtool RUNPATH on Linux,
    # absolute install names on macOS), so Spot builds straight into its final
    # location, and the marker written last is what makes it complete.
    file(REMOVE_RECURSE "${SPOT_INSTALL_DIR}" "${_spot_work_dir}")
    # Logs sit outside the work directory, which is deleted once installed.
    set(_spot_log_dir "${PEREDUR_THIRD_PARTY_DIR}/logs/spot-${SPOT_VERSION}")
    file(REMOVE_RECURSE "${_spot_log_dir}")
    file(MAKE_DIRECTORY "${SPOT_INSTALL_DIR}" "${_spot_work_dir}/build" "${_spot_log_dir}")

    set(_spot_archive "${PEREDUR_DOWNLOAD_DIR}/spot-${SPOT_VERSION}.tar.gz")
    peredur_download("${SPOT_DOWNLOAD_URL}" "${SPOT_SHA256}" "${_spot_archive}")
    file(ARCHIVE_EXTRACT INPUT "${_spot_archive}" DESTINATION "${_spot_work_dir}")
    set(_spot_source_dir "${_spot_work_dir}/spot-${SPOT_VERSION}")

    # Autotools' ./configure does not know CMAKE_CXX_COMPILER_LAUNCHER, so it is
    # threaded through CXX (sccache, when CI sets it). This project enables CXX
    # only, so CMAKE_C_COMPILER is never populated and Spot's configure picks
    # its own C compiler.
    set(_spot_cxx "${CMAKE_CXX_COMPILER}")
    if(CMAKE_CXX_COMPILER_LAUNCHER)
        set(_spot_cxx "${CMAKE_CXX_COMPILER_LAUNCHER} ${_spot_cxx}")
    endif()

    # Spot's build tree is a Makefile whatever generator this project uses, so
    # it needs make, never CMAKE_MAKE_PROGRAM (ninja under the Ninja
    # generator). NAMES_PER_DIR keeps PATH order, so the dev shell's make wins
    # over a Homebrew gmake further down PATH.
    find_program(_spot_make NAMES make gmake NAMES_PER_DIR NO_CACHE REQUIRED)

    message(STATUS
        "Building Spot ${SPOT_VERSION} into ${SPOT_INSTALL_DIR} with "
        "${PEREDUR_TOOL_JOBS} jobs (about 30 minutes from cold; logs in "
        "${_spot_log_dir})")
    set(_spot_build_dir "${_spot_work_dir}/build")
    peredur_run_step("${_spot_log_dir}/configure.log"
        COMMAND "${_spot_source_dir}/configure"
            "--prefix=${SPOT_INSTALL_DIR}"
            --disable-python
            --disable-doc
            "CXX=${_spot_cxx}"
        WORKING_DIRECTORY "${_spot_build_dir}")
    peredur_run_step("${_spot_log_dir}/build.log"
        COMMAND "${_spot_make}" -j "${PEREDUR_TOOL_JOBS}"
        WORKING_DIRECTORY "${_spot_build_dir}")
    peredur_run_step("${_spot_log_dir}/install.log"
        COMMAND "${_spot_make}" install
        WORKING_DIRECTORY "${_spot_build_dir}")
    if(PEREDUR_SPOT_FORKSERVER)
        peredur_stash_spot_forkserver_objects(
            "${_spot_build_dir}" "${SPOT_FORKSERVER_OBJECT_DIR}" "${_spot_log_dir}")
    endif()
    # The build tree is about a gigabyte and nothing reads it once installed,
    # the fork server's objects having been copied out above. Removed before
    # the check, so the check sees what later runs will.
    file(REMOVE_RECURSE "${_spot_work_dir}")
    peredur_mark_installed("${SPOT_INSTALL_DIR}" "${_spot_stamp}" CHECK ${_spot_check})
    message(STATUS "Spot ${SPOT_VERSION} installed at ${SPOT_INSTALL_DIR}")
endif()

file(LOCK "${SPOT_INSTALL_DIR}.lock" RELEASE)

peredur_publish_tool_path(SPOT_BIN_DIR PATH "Spot bin directory")
