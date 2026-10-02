set -u
cd /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data
SEEDS="$(seq -s ' ' 0 29)"
DIRS="/tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/results /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/av2/results06"
mkdir -p fpw1 fpw2
export BLACK_TIMEOUT=20 WORKERS=8
python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/pairwit.py lpc-full-core1.b.ltl fpb lpc-full-core1 words/lpc-full-pw1.words 15000 $SEEDS -- $DIRS
cat words/lpc-full-core1-tb.words words/lpc-full-pw1.words > words/lpc-full-w1.words
~/projects/counter-wt/fpcal/build-release/fpdraw eval words/lpc-full-w1.words all-lpc-full-core1.list 8 > fpw1/lpc-full-core1.tsv 2> fpw1/lpc-full-core1.err
python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/pairwit.py lpc-full-core1.b.ltl fpw1 lpc-full-core1 words/lpc-full-pw2.words all $SEEDS -- $DIRS
cat words/lpc-full-w1.words words/lpc-full-pw2.words > words/lpc-full-w2.words
~/projects/counter-wt/fpcal/build-release/fpdraw eval words/lpc-full-w2.words all-lpc-full-core1.list 8 > fpw2/lpc-full-core1.tsv 2> fpw2/lpc-full-core1.err
echo witnesses done $(date)
export MAXIMAL=$HOME/projects/counter-wt/fpcal/build-release/maximal COMPARE=$HOME/projects/counter-wt/fpcal/build-release/compare TIMEOUT=20 WORKERS=6
python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/hybrid.py fpw2 lpc-full-core1 hyb/lpc-full-core1.w.csv $DIRS > hyb/lpc-full-core1.w.log 2>&1
echo done $(date)
