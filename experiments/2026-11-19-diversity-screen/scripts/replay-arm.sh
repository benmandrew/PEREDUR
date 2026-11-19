#!/usr/bin/env bash
# Replay the output climb of every run of one arm at K orders.
# Usage: replay-arm.sh K JOBS ARM. Outputs go to replay/<ARM>/k<K>/<run>/ (untracked); a run with summary.json is skipped.
set -u
K=$1; JOBS=${2:-4}; ARM=${3:-remove-exchange}
BIN=$HOME/projects/peredur-div-climb/build-release/climb-replay
SRC=$HOME/projects/peredur-exchange-rerun/experiments/results-fretish-exchange-rerun
OUT=$(dirname "$0")/../replay/$ARM/k$K
mkdir -p "$OUT"
ls -d "$SRC"/sweep_D_${ARM}_nsga2-apportion_* | while read -r r; do
  n=$(basename "$r"); [ -f "$OUT/$n/summary.json" ] || echo "$r"
done | xargs -P "$JOBS" -I{} sh -c 'n=$(basename {}); mkdir -p "$0/$n"; "$1" --run {} --orders "$2" --out "$0/$n" > "$0/$n/replay.log" 2>&1 || echo "FAIL $n"' "$OUT" "$BIN" "$K"
echo "done k$K"
