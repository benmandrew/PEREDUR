#!/bin/sh
# The n = 20 size sweep: every frontier of a matched, cross-seed block
# subsampled to exactly 20 members, K = 3, at the 2 h cap. One tlsf_prc.py
# process per family over a per-family directory of symlinks, JOBS at a time.
#
# usage: size_sweep.sh <peredur-sidecar-root> <work-dir>
# <peredur-sidecar-root> holds one directory per host; the screened AuRUS
# sidecars are read from AURUS_SIDECARS (av2/, av3/).
#
# Vendored from 2026-10-01-fretish-mixed/diversity/ball-radii at 22ab586
# (blob d8c6bd0). Changed: PEREDUR's hosts are av1, av2 and av3; the AuRUS
# root is AURUS_SIDECARS instead of out/ws/aurus beside the script; JOBS (8)
# replaces 10 workers, for this box's 8-core cap. Each family is still one
# process over the same inputs, so the rows are unchanged.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
R=$1 WORK=$2
A=${AURUS_SIDECARS:?set AURUS_SIDECARS to the screened AuRUS sidecar root}
mkdir -p "$WORK/in" "$WORK/out"
specs=$(for h in av2 av3; do ls "$A/$h"; done | grep '\.members\.tsv$' |
    sed -E 's/^aurus_(.*)_seed[0-9]+\.members\.tsv$/\1/' | sort -u)
for sp in $specs; do
    for h in av1 av2 av3; do
        mkdir -p "$WORK/in/$sp/$h"
        for f in "$R/$h"/*log_"${sp}"_seed* "$A/$h"/aurus_"${sp}"_seed*; do
            [ -e "$f" ] && ln -sf "$(realpath "$f")" "$WORK/in/$sp/$h/"
        done
    done
done
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 SIZE=20 K=3 MATCH=1 XSEED=1
PY=${PYTHON:-python3}
printf '%s\n' $specs | xargs -P "${JOBS:-8}" -I{} "$PY" "$HERE/tlsf_prc.py" "$WORK/out/{}.csv" "$WORK/in/{}/av1" "$WORK/in/{}/av2" "$WORK/in/{}/av3" >/dev/null
first=$(ls "$WORK/out"/*.csv | head -1)
head -1 "$first" > "$WORK/size20.csv"
for f in "$WORK/out"/*.csv; do tail -n +2 "$f"; done >> "$WORK/size20.csv"
"$PY" "$HERE/tlsf_prc_report.py" "$WORK/size20.csv" > "$WORK/report_size20.txt"
