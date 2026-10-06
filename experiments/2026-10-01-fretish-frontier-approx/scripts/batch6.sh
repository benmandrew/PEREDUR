set -u
cd /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data
export MAXIMAL=$HOME/projects/counter-wt/fpcal/build-release/maximal COMPARE=$HOME/projects/counter-wt/fpcal/build-release/compare TIMEOUT=20 WORKERS=4 FPDRAW_UNGLUE=1
mkdir -p fpfix
for s in mode-arbiter rad-core-10 rad-core-1-18 rad-core-12-18 rad-core-17-18 rad-core-45 rad-core-55 rad-core-68 rad-core-65 rad-core-61 rad-core-33; do
  ~/projects/counter-wt/fpcal/build-release/fpdraw eval words/full-$s.words full-$s.list 4 > fpfix/$s.tsv 2> fpfix/$s.err
  python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/hybrid.py fpfix $s hyb/fix-$s.csv /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/results /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/av2/all > hyb/fix-$s.log 2>&1
  echo "$s $(date)"
done
echo done
