#!/usr/bin/env bash

set -euo pipefail

mkdir -p build-release results

rm -f results/*

NPROC=$(nproc)

INPUT=examples/fsm-timing/spec.json
IDEALS_DIR=examples/fsm-timing/fixes

cmake --build --parallel "$NPROC" --preset release

./build-release/peredur \
    --input "$INPUT" \
    --output-dir results \
    --config example-config.toml

./build-release/compare \
    --repairs results \
    --ideals "$IDEALS_DIR"
