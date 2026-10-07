#!/bin/bash
# Build compare and maximal at origin/main (>= 5dc3472), reusing main's SPOT/black/ganak.
set -x
cd ~/fret-pooled/counter
mkdir -p build-release
ln -sfn ~/projects/counter/build-release/third_party build-release/third_party
which cmake ninja g++ clang++
cmake --preset release > ~/fret-pooled/build-configure.log 2>&1 || { echo CONFIGURE_FAIL; exit 1; }
nice -n 10 cmake --build build-release --target compare maximal -j 8 > ~/fret-pooled/build.log 2>&1 || { echo BUILD_FAIL; exit 1; }
build-release/compare --version
echo BUILD_DONE
