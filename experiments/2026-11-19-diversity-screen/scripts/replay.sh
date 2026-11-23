#!/usr/bin/env bash
# Replay the output climb of every remove-exchange run at K orders.
# Usage: replay.sh K JOBS. Outputs go to replay/k<K>/<run>/ (untracked); a run with summary.json is skipped.
set -u
K=$1; JOBS=${2:-4}
BIN=$HOME/projects/peredur-div-climb/build-release/climb-replay
SRC=$HOME/projects/peredur-exchange-rerun/experiments/results-fretish-exchange-rerun
OUT=$(dirname "$0")/../replay/k$K
mkdir -p "$OUT"
ls -d "$SRC"/sweep_D_remove-exchange_nsga2-apportion_* | while read -r r; do
  n=$(basename "$r"); [ -f "$OUT/$n/summary.json" ] || echo "$r"
done | xargs -P "$JOBS" -I{} sh -c 'n=$(basename {}); mkdir -p "$0/$n"; "$1" --run {} --orders "$2" --out "$0/$n" > "$0/$n/replay.log" 2>&1 || echo "FAIL $n"' "$OUT" "$BIN" "$K"
echo "done k$K"
