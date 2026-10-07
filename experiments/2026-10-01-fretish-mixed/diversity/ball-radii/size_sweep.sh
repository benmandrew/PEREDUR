#!/bin/sh
# The n = 20 size sweep: every frontier of a matched, cross-seed block
# subsampled to exactly 20 members, K = 3, at the 2 h cap. One tlsf_prc.py
# process per family over a per-family directory of symlinks, 10 at a time.
#
# usage: size_sweep.sh <rematch-sidecar-root> <work-dir>
# <rematch-sidecar-root> holds separation-recount-rematch-s0's av2/ and av3/;
# the screened AuRUS sidecars are read from out/ws/aurus/ beside this script.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
R=$1 WORK=$2
mkdir -p "$WORK/in" "$WORK/out"
specs=$(for h in av2 av3; do ls "$HERE/out/ws/aurus/$h"; done | grep '\.members\.tsv$' |
    sed -E 's/^aurus_(.*)_seed[0-9]+\.members\.tsv$/\1/' | sort -u)
for sp in $specs; do
    for h in av2 av3; do
        mkdir -p "$WORK/in/$sp/$h"
        for f in "$R/$h"/*log_"${sp}"_seed* "$HERE/out/ws/aurus/$h"/aurus_"${sp}"_seed*; do
            [ -e "$f" ] && ln -sf "$f" "$WORK/in/$sp/$h/"
        done
    done
done
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 SIZE=20 K=3 MATCH=1 XSEED=1
printf '%s\n' $specs | xargs -P 10 -I{} python3 "$HERE/tlsf_prc.py" "$WORK/out/{}.csv" "$WORK/in/{}/av2" "$WORK/in/{}/av3" >/dev/null
first=$(ls "$WORK/out"/*.csv | head -1)
head -1 "$first" > "$WORK/size20.csv"
for f in "$WORK/out"/*.csv; do tail -n +2 "$f"; done >> "$WORK/size20.csv"
python3 "$HERE/tlsf_prc_report.py" "$WORK/size20.csv" > "$WORK/report_size20.txt"
