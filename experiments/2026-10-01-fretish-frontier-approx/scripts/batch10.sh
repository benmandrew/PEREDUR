set -u
cd /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data
SEEDS="$(seq -s ' ' 0 29)"
B=$HOME/projects/counter-wt/fpcal/build-release
export MAXIMAL=$B/maximal COMPARE=$B/compare TIMEOUT=20
run() {  # subject ltl words list dirs...
  s=$1 ltl=$2 w=$3 lst=$4; shift 4
  mkdir -p fpw
  BLACK_TIMEOUT=20 WORKERS=8 python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/pairwit.py $ltl fpt32 $s words/$s-pw.words all $SEEDS -- "$@"
  cat $w words/$s-pw.words > words/$s-w.words
  $B/fpdraw eval words/$s-w.words $lst 8 > fpw/$s.tsv 2> fpw/$s.err
  LTL=$ltl WORKERS=6 python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/hybrid.py fpw $s hyb/$s.w.csv "$@" > hyb/$s.w.log 2>&1
  echo "$s done $(date)"
}
run valu3s-uc6 valu3s-uc6.ltl words/valu3s-uc6-32768.words all-valu3s-uc6.list /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/results /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/av2/results06
run lpc-mini-core1 lpcmini.ltl words/lpcmini-32768.words lpc-mini-core1.list /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/lpcmini/samp
echo done
