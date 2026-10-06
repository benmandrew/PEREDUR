#!/bin/bash
# The FRETISH ablation's frontier pass on one host, queued behind the curve pass.
# usage: go.sh <seed-lo> <seed-hi> <sampled-subject>
set -u
lo=$1 hi=$2 samp=$3
F=~/fa-front C=~/projects/counter R=$C/experiments/results-fretish-ablation
cd $F
while pgrep -f 'scripts/score_curves.py' > /dev/null; do sleep 60; done
echo "scoring done $(date)"
cd $C && git fetch -q origin && git checkout -q campaign/fretish-ablation && git merge -q --ff-only origin/campaign/fretish-ablation \
  && cmake --build build-release --target compare maximal > $F/build.log 2>&1 || { echo "build failed"; exit 1; }
build-release/compare --version; build-release/maximal --version
cd $F
# The exact pass: the 16 cheap subjects' pairs for this host's seeds.
rm -rf exact && mkdir exact
for d in $R/sweep_O_*; do
  n=${d##*/}; s=$((10#${n##*_seed}))
  case $n in *_liquid-mixer_*|*_lpc-mini-core1_*|*_lpc-full-core1_*|*_valu3s-uc6_*) continue;; esac
  [ $s -ge $lo ] && [ $s -le $hi ] && ln -s $d exact/$n
done
# The sampled subject: every seed, from this host's results and the copies.
rm -rf samp && mkdir samp
for d in $R/sweep_O_*_${samp}_seed* copied/sweep_O_*_${samp}_seed*; do
  [ -d $d ] && ln -sfn $(realpath $d) samp/${d##*/}
done
echo "exact $(ls exact | wc -l) dirs, sampled $(ls samp | wc -l) dirs"
export MAXIMAL=$C/build-release/maximal CAP=1800 JOBS=4 WORKERS=4
nice -n 10 python3 paired.py exact paired.csv > paired.log 2>&1 &
COMPARE=$C/build-release/compare TIMEOUT=120 WORKERS=3 STOP=200 \
  nice -n 10 python3 sample_paired.py samp $samp sample > sample.log 2>&1 &
wait
echo "done $(date)"
