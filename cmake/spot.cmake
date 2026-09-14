include(ExternalProject)

set(SPOT_VERSION "2.15.1")
set(SPOT_DOWNLOAD_URL
    "https://www.lre.epita.fr/dload/spot/spot-${SPOT_VERSION}.tar.gz")
set(SPOT_PREFIX "${COUNTER_THIRD_PARTY_DIR}/spot_src")
set(SPOT_INSTALL_DIR "${COUNTER_THIRD_PARTY_DIR}/spot")
set(SPOT_BIN_DIR "${SPOT_INSTALL_DIR}/bin")
set(SPOT_STAMP_DIR "${SPOT_PREFIX}/src/spot_project-stamp")

# ExternalProject_Add regenerates its per-step driver scripts on every
# configure, which get a newer mtime than a CI-cache-restored "-done" stamp.
# That tricks Make into rerunning the whole (30+ minute) configure/build even
# though the installed output is already present and valid. Skip
# re-declaring the external project entirely when a prior run already
# completed it, so a cache hit actually saves the rebuild.
if(EXISTS "${SPOT_STAMP_DIR}/spot_project-done")
    add_custom_target(spot_project)
    message(STATUS "Spot ${SPOT_VERSION} already installed at ${SPOT_INSTALL_DIR}")
else()
    # Autotools' ./configure doesn't know about CMAKE_CXX_COMPILER_LAUNCHER;
    # thread it through manually (e.g. sccache, when CI sets it) so a genuine
    # cache miss at least compiles fast on a subsequent run. This project only
    # enables CXX (project(... LANGUAGES CXX)), so CMAKE_C_COMPILER is never
    # populated — leave CC alone and let SPOT's configure auto-detect it,
    # same as before.
    set(_spot_cxx "${CMAKE_CXX_COMPILER}")
    if(CMAKE_CXX_COMPILER_LAUNCHER)
        set(_spot_cxx "${CMAKE_CXX_COMPILER_LAUNCHER} ${_spot_cxx}")
    endif()

    # Spot is an autotools project: its build tree is a Makefile regardless of
    # the generator this project uses, so it must be driven by make. Don't use
    # CMAKE_MAKE_PROGRAM here — under the Ninja generator that resolves to
    # ninja, which then fails to find a build.ninja in Spot's tree.
    find_program(SPOT_MAKE_PROGRAM NAMES gmake make REQUIRED)

    ExternalProject_Add(spot_project
        URL "${SPOT_DOWNLOAD_URL}"
        TLS_VERIFY ON
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        PREFIX "${SPOT_PREFIX}"
        INSTALL_DIR "${SPOT_INSTALL_DIR}"
        CONFIGURE_COMMAND
            <SOURCE_DIR>/configure
            --prefix=<INSTALL_DIR>
            --disable-python
            --disable-doc
            CXX=${_spot_cxx}
        BUILD_COMMAND
            ${SPOT_MAKE_PROGRAM} -j ${CMAKE_BUILD_PARALLEL_LEVEL}
        INSTALL_COMMAND
            ${SPOT_MAKE_PROGRAM} install
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
    message(STATUS "Spot ${SPOT_VERSION} will be built into ${SPOT_INSTALL_DIR}")
endif()

set(SPOT_BIN_DIR "${SPOT_BIN_DIR}" CACHE PATH "Path to Spot bin directory")
