#!/bin/sh
# The local half of 2026-10-08-tlsf-rerun-analysis, after
#   python3 scripts/campaign.py collect --outputs analysis-tlsf-rerun
#   python3 scripts/campaign.py collect --outputs analysis-tlsf-rerun-work
# The second pulls each host's work tree, whose strength/<spec>/result.json
# the frontier phase wrote, into experiments/analysis-tlsf-rerun-work/<host>/;
# this script files them beside the local plan. Run from the checkout root, inside the 8 GB scope
# PLAN.md gives. Needs numpy, scipy and pandas: PYTHON defaults to the
# main checkout's .venv, which has them on this box.
#
# STEPS picks a subset: strength rq3 coverage ball size (default: all). The
# ball radii and the size sweep read only the collected sidecars, so they can
# run before the host phases finish.
set -eu
# The ball-radii scripts read these from the environment; a stray one
# changes the measure.
unset SIZE CUT MATCH XSEED K METRIC WORDS
PY=${PYTHON:-$HOME/projects/counter/.venv/bin/python}
C=experiments/2026-10-08-tlsf-rerun-analysis
S=$C/scripts
W=experiments/analysis-tlsf-rerun-work
IN=experiments/analysis-tlsf-rerun
OUT=$C/out
STEPS=${STEPS:-strength rq3 coverage ball size}
# The archived well-separation-screened AuRUS sidecars (av2/, av3/), which the
# archived ball-radii pass read, and the archived Maoz coverage inputs.
AURUS=${AURUS_SIDECARS:-$HOME/projects/counter-wt/repair-diversity/experiments/2026-10-01-fretish-mixed/diversity/ball-radii/out/ws/aurus}
MAOZ=${MAOZ_ROOT:-$HOME/projects/counter-wt/maoz}
JOBS=${JOBS:-8}
R=${R:-1000}
mkdir -p "$OUT"

has() { case " $STEPS " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }
csvs() { for f in "$@"; do [ -f "$f" ] && printf '%s\n' "$f"; done; }

if has strength; then
    for h in av1 av2 av3; do
        for r in "$W/$h"/strength/*/result.json; do
            [ -f "$r" ] || continue
            spec=$(basename "$(dirname "$r")")
            cp "$r" "$W/strength/$spec/result.json"
        done
    done
    # shellcheck disable=SC2046
    "$PY" scripts/pooled_frontier.py split-fallbacks "$W/strength" \
        $(csvs "$IN"/av1/strength-fallback-av1.csv "$IN"/av2/strength-fallback-av2.csv "$IN"/av3/strength-fallback-av3.csv)
    # humanoid-742 is a sampled estimate (PLAN.md, deviations).
    if [ -f "$IN/av3/sample-742-av3.csv" ]; then
        "$PY" "$S/sample_742.py" score "$IN/av3/sample-742-av3.csv"
    fi
    "$PY" scripts/pooled_frontier.py score "$W/strength" "$OUT/strength"
fi

if has rq3; then
    # shellcheck disable=SC2046
    "$PY" "$S/score_rq3.py" "$W" "$C/data/aurus-frontier.csv" \
        $(csvs "$IN"/av2/rq3-av2.csv "$IN"/av3/rq3-av3.csv) "$OUT/rq3"
fi

if has coverage; then
    # maoz_score.py's report pass over relations.csv rebuilt from the per-pair
    # compare output, with the archived screen and frontier.
    "$PY" "$S/coverage_relations.py" "$OUT/coverage/relations.csv" "$IN"/av*/coverage-*.csv
    "$PY" "$S/maoz_score.py" --pass report --out "$OUT/coverage/report" \
        --pool "$W/coverage/pool" \
        --results "$MAOZ/experiments/results-maoz-baselines" \
        --maoz-out "$MAOZ/experiments/maoz-baselines-out" \
        --screen "$MAOZ/experiments/maoz-coverage-screen" \
        --frontier "$MAOZ/experiments/maoz-coverage-frontier" \
        --coverage "$OUT/coverage"
fi

# The ball radii read the rerun's sidecars where tlsf_prc.py expects them:
# members.tsv and fingerprints.tsv side by side (plan.py links them).
P1=$W/sidecars/av1 P2=$W/sidecars/av2 P3=$W/sidecars/av3
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
if has ball; then
    B=$OUT/ball-radii
    mkdir -p "$B/perm"
    for k in 1 3 5; do
        K=$k MATCH=1 XSEED=1 "$PY" "$S/tlsf_prc.py" "$B/match_k$k.csv" \
            "$P1" "$P2" "$P3" "$AURUS/av2" "$AURUS/av3" > /dev/null
        "$PY" "$S/tlsf_prc_report.py" "$B/match_k$k.csv" > "$B/report_match_k$k.txt"
    done
    K=3 R=$R JOBS=$JOBS "$PY" "$S/perm_null.py" "$B/perm/cap.csv" \
        "$P1" "$P2" "$P3" "$AURUS/av2" "$AURUS/av3" > "$B/perm/cap.log"
    K=3 R=$R JOBS=$JOBS CUT=equal "$PY" "$S/perm_null.py" "$B/perm/eqtime.csv" \
        "$P1" "$P2" "$P3" "$AURUS/av2" "$AURUS/av3" > "$B/perm/eqtime.log"
    "$PY" "$S/perm_report.py" "$B/perm/cap.csv" > "$B/perm/report_cap.txt"
    "$PY" "$S/perm_report.py" "$B/perm/eqtime.csv" > "$B/perm/report_eqtime.txt"
    "$PY" "$S/direction_ctrl.py" "$B/perm/cap.csv" > "$B/perm/direction_cap.txt"
    "$PY" "$S/direction_ctrl.py" "$B/perm/eqtime.csv" > "$B/perm/direction_eqtime.txt"
    "$PY" "$S/hl_ctrl.py" "$B/perm/cap.csv" > "$B/perm/hl_cap.txt"
    "$PY" "$S/effect_sizes.py" "$B/perm/cap.csv" > "$B/perm/effect_sizes_cap.txt"
fi

if has size; then
    AURUS_SIDECARS=$AURUS PYTHON=$PY sh "$S/size_sweep.sh" "$W/sidecars" "$OUT/size20"
fi
