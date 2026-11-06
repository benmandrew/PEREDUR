#!/usr/bin/env bash
# Runs a built PEREDUR image through one short call per tool it carries, so a
# broken tool wrapper, a wrong ENV path or a solver that cannot load its
# libraries fails here rather than in a user's first run.
#
# Usage: scripts/docker_smoke.sh <image> [expected commit sha]
#
# With a sha, --version must also report that commit as a clean build.
set -euo pipefail

image=${1:?usage: docker_smoke.sh <image> [expected commit sha]}
expected=${2:-}

fail() {
    echo "smoke test failed: $*" >&2
    exit 1
}

# Output is captured before it is matched: grep -q exits at the first match,
# and under pipefail the docker run it cut off would fail the pipeline.
run() {
    docker run --rm "$image" "$@"
}

echo "--- peredur --version"
version=$(run --version)
echo "$version"
if [ -n "$expected" ]; then
    grep -qx "commit=${expected}" <<<"$version" ||
        fail "--version does not name commit ${expected}"
    grep -qx "dirty=0" <<<"$version" || fail "--version reports a dirty build"
fi

# Each tool is reached through the ENV path the image sets for it, which is
# the path PEREDUR itself spawns, not through PATH.
echo "--- ltlsynt"
out=$(run sh -c '"$PEREDUR_SPOT_BIN_DIR/ltlsynt" --ins=a --outs=b \
    -f "G(a <-> b)" --realizability')
echo "$out"
grep -qx "REALIZABLE" <<<"$out" || fail "ltlsynt did not report REALIZABLE"

echo "--- black"
out=$(run sh -c 'echo "G(a -> F b)" | "$PEREDUR_BLACK_PATH" solve -')
echo "$out"
grep -qx "SAT" <<<"$out" || fail "black did not report SAT"

# (x1 or x2) over two variables has three models.
echo "--- ganak"
out=$(run sh -c 'printf "p cnf 2 1\n1 2 0\n" > /tmp/t.cnf \
    && "$PEREDUR_GANAK_PATH" /tmp/t.cnf')
grep -qx "c s exact arb int 3" <<<"$out" || {
    echo "$out"
    fail "ganak did not count 3 models"
}
echo "3 models"

echo "--- realize on a bundled example"
out=$(run realize /opt/peredur/share/peredur/examples/lily02/spec.tlsf)
echo "$out"
grep -qx "UNREALIZABLE" <<<"$out" || fail "realize did not report UNREALIZABLE"

# One generation of eight is under a second, and goes through ltl2tgba,
# ltlfilt, ltlsynt and black from inside the engine rather than from a shell.
echo "--- a one-generation repair"
run sh -c 'cd /tmp && mkdir out \
    && printf "[genetic]\ngenerations = 1\npopulation_size = 8\n" > smoke.toml \
    && peredur --input "$PEREDUR_EXAMPLES/lily02/spec.tlsf" --output-dir out \
        --config smoke.toml --seed 1' || fail "the repair run failed"

echo "Smoke test passed for ${image}."
