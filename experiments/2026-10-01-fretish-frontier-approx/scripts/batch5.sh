set -u
cd /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data
export MAXIMAL=$HOME/projects/counter-wt/fpcal/build-release/maximal COMPARE=$HOME/projects/counter-wt/fpcal/build-release/compare TIMEOUT=20 WORKERS=4
mkdir -p fpfull
for s in fsm fsm-combined fsm-lmcps fsm-timing mode-arbiter takeoff rad-core-10 rad-core-1-18 rad-core-12-18 rad-core-17-18 rad-core-33 rad-core-45 rad-core-55 rad-core-61 rad-core-65 rad-core-68; do
  : > full-$s.list
  for r in $(ls -d results/sweep_O_*_${s}_seed* av2/all/sweep_O_*_${s}_seed* 2>/dev/null); do
    [ -f $r/accumulated/maximal.tsv ] && tail -n +2 $r/accumulated/maximal.tsv | sed "/^$/d; s#^#/tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/$r/accumulated/#" >> full-$s.list
  done
  sort -u -o full-$s.list full-$s.list
  if [ -s full-$s.list ]; then
    ~/projects/counter-wt/fpcal/build-release/fpdraw ltl examples/$s/spec.json full-$s.list > full-$s.ltl
    python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/draw_black.py full-$s.ltl 32768 3 words/full-$s.words 6 2> words/full-$s.drawlog
    ~/projects/counter-wt/fpcal/build-release/fpdraw eval words/full-$s.words full-$s.list 6 > fpfull/$s.tsv 2> fpfull/$s.err
  else
    : > fpfull/$s.tsv
  fi
  python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/hybrid.py fpfull $s hyb/full-$s.csv /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/results /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/av2/all > hyb/full-$s.log 2>&1
  echo "$s $(wc -l < full-$s.list) members $(tail -1 words/full-$s.drawlog 2>/dev/null) $(date)"
done
echo done
