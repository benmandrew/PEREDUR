# A project that declares no targets of its own and exists only to run one of
# the fetch modules under cmake/. The Docker build gives each of Spot, black and
# Ganak a stage of its own that configures this file with a different
# COUNTER_TOOL_MODULE, so bumping the version of one cannot invalidate the
# builds of the other two, and BuildKit is free to run all three at once.
#
# Each module installs into ${CMAKE_BINARY_DIR}/third_party/<tool>, so the build
# directory has to be the one the real build will use: cmake/spot.cmake and
# cmake/black.cmake skip re-declaring their external project when its done-stamp
# is already there, and that is what lets the real build reuse this work rather
# than repeat it.

cmake_minimum_required(VERSION 3.21)

project(counter_tools LANGUAGES CXX)

# Mirrors the root CMakeLists. cmake/spot.cmake passes this straight to
# `make -j`, and an empty value there means unbounded parallelism rather than
# the default: Spot's build would fork a job per translation unit at once.
if(NOT DEFINED CMAKE_BUILD_PARALLEL_LEVEL)
    include(ProcessorCount)
    ProcessorCount(NPROC)
    if(NPROC GREATER 1)
        set(CMAKE_BUILD_PARALLEL_LEVEL ${NPROC})
    else()
        set(CMAKE_BUILD_PARALLEL_LEVEL 1)
    endif()
endif()

# Mirrors the root CMakeLists again. The fetch modules resolve their install
# locations against this, and this project includes one of them directly rather
# than through the root, so without the declaration here Spot would install into
# /spot and the stage would fail on a path the Dockerfile never copies.
set(COUNTER_THIRD_PARTY_DIR "${CMAKE_BINARY_DIR}/third_party" CACHE PATH
    "Directory holding the built Spot, black and ganak trees")

if(NOT COUNTER_TOOL_MODULE)
    message(FATAL_ERROR
        "-DCOUNTER_TOOL_MODULE=<module>.cmake is required "
        "(one of ganak.cmake, spot.cmake, black.cmake, dependencies.cmake)")
endif()

include("cmake/${COUNTER_TOOL_MODULE}")
