#!/bin/sh
# The local half of 2026-10-08-fretish-rerun-analysis, after
#   python3 scripts/campaign.py collect --outputs analysis-fretish-rerun
# Run from the checkout root. Needs numpy and scipy: PYTHON defaults to
# /usr/bin/python3, which has both on this box and on the lab hosts.
set -eu
PY=${PYTHON:-/usr/bin/python3}
C=experiments/2026-10-08-fretish-rerun-analysis
S=$C/scripts
IN=experiments/analysis-fretish-rerun
OUT=$C/out

STRENGTH="fsm fsm-combined fsm-timing liquid-mixer lpc-mini-core1 mode-arbiter
rad-core-1-18 rad-core-10 rad-core-12-18 rad-core-17-18 rad-core-33 rad-core-45
rad-core-55 rad-core-61 rad-core-65 rad-core-68 takeoff"
BALL="fsm fsm-combined fsm-timing mode-arbiter rad-core-1-18 rad-core-10
rad-core-12-18 rad-core-17-18 rad-core-33 rad-core-45 rad-core-55 rad-core-61
rad-core-65 rad-core-68 takeoff"

# shellcheck disable=SC2086
"$PY" "$S/reduce.py" "$C/members" "$IN" "$OUT/strength" \
    --strength $STRENGTH --ball $BALL

mkdir -p "$OUT/ball-radii"
for k in 1 3 5; do
    DIST=jaccard K=$k MATCH=0 "$PY" "$S/fp_prc.py" "$OUT/ball-radii/j${k}m.csv" \
        "$OUT"/strength/ball/*.tsv
    "$PY" "$S/fp_prc_report.py" "$OUT/ball-radii/j${k}m.csv" \
        > "$OUT/ball-radii/report_j${k}m.txt"
done
"$PY" "$S/direction_ctrl.py" - "$OUT/ball-radii/j3m.csv" \
    > "$OUT/ball-radii/direction_j3m.txt"
cat "$OUT/strength/summary.txt" "$OUT/ball-radii/report_j3m.txt" \
    "$OUT/ball-radii/direction_j3m.txt"
