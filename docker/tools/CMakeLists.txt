# A project that declares no targets of its own and exists only to run one of
# the fetch modules under cmake/. The Docker build gives each of Spot, black and
# Ganak a stage of its own that configures this file with a different
# PEREDUR_TOOL_MODULE, so bumping the version of one cannot invalidate the
# builds of the other two, and BuildKit is free to run all three at once.
#
# Each module installs its tool at configure time into
# ${CMAKE_BINARY_DIR}/third_party/<tool>, so the build directory has to be the
# one the real build will use: the real configure finds the install marker and
# a binary that runs, and reuses the work rather than repeating it.

cmake_minimum_required(VERSION 3.21)

project(peredur_tools LANGUAGES CXX)

# Mirrors the root CMakeLists. The fetch modules resolve their install
# locations against this, and this project includes one of them directly rather
# than through the root, so without the declaration here Spot would install into
# /spot and the stage would fail on a path the Dockerfile never copies.
set(PEREDUR_THIRD_PARTY_DIR "${CMAKE_BINARY_DIR}/third_party" CACHE PATH
    "Directory holding the built Spot, black and ganak trees")

if(NOT PEREDUR_TOOL_MODULE)
    message(FATAL_ERROR
        "-DPEREDUR_TOOL_MODULE=<module>.cmake is required "
        "(one of ganak.cmake, spot.cmake, black.cmake, dependencies.cmake)")
endif()

include("cmake/${PEREDUR_TOOL_MODULE}")
