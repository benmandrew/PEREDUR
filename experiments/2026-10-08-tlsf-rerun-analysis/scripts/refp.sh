#!/bin/bash
# Re-fingerprint one host's frontier members of the named families at a larger
# word count, keeping the separation recount's other sampling settings
# (seed 0, stems and loops of at most 8) so the word set is a superset draw.
# usage: refp.sh <members-dir> <out-dir> <words> <family>...
# env: JOBS (12), REPO (~/projects/counter)
set -euo pipefail
src=$1 out=$2 words=$3; shift 3
REPO=${REPO:-$HOME/projects/counter}
mkdir -p "$out"; out=$(realpath "$out"); src=$(realpath "$src")
one() {
    m=$1 out=$2 words=$3
    base=$(basename "$m" .members.tsv)
    [ -s "$out/$base.fingerprints.tsv" ] && return 0
    case $base in
        aurus_*) spec=${base#aurus_}; run=$REPO/experiments/results-aurus-curves/$base ;;
        *)       spec=$(sed -E 's/^sweep_G_[a-z]+_[a-z0-9-]+_wkoff_log_//' <<< "$base"); run=$REPO/experiments/results-rematch/$base ;;
    esac
    spec=${spec%_seed*}
    cp "$m" "$out/"
    cd "$run/accumulated"
    { printf 'file\tfingerprint\n'; cut -f2 "$m" | tail -n +2 | sort -u |
        xargs "$REPO/build-release/fingerprint" --signals "$REPO/examples/$spec/spec.tlsf" \
            --words "$words" --seed 0 --max-prefix 8 --max-cycle 8; } > "$out/$base.fingerprints.tsv.part"
    mv "$out/$base.fingerprints.tsv.part" "$out/$base.fingerprints.tsv"
    echo "$base $spec"
}
export -f one; export REPO
for f in "$@"; do ls "$src"/*_"$f"_seed*.members.tsv; done |
    xargs -P "${JOBS:-12}" -I{} bash -c 'one "$@"' _ {} "$out" "$words"
