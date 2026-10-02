set -u
cd /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data
B=$HOME/projects/counter-wt/fpcal/build-release
export MAXIMAL=$B/maximal COMPARE=$B/compare TIMEOUT=20
WORKERS=6 python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/hybrid.py fpw valu3s-uc6 hyb/valu3s-uc6.w.csv /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/results /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/av2/results06 > hyb/valu3s-uc6.w.log 2>&1 &
BLACK_TIMEOUT=20 WORKERS=6 python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/pairwit.py lpcmini.ltl fpt32 lpc-mini-core1 words/lpc-mini-core1-pw.words all $(seq -s ' ' 0 29) -- /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/lpcmini/samp
cat words/lpcmini-32768.words words/lpc-mini-core1-pw.words > words/lpc-mini-core1-w.words
$B/fpdraw eval words/lpc-mini-core1-w.words lpc-mini-core1.list 6 > fpw/lpc-mini-core1.tsv 2> fpw/lpc-mini-core1.err
LTL=lpcmini.ltl BLACK_TIMEOUT=20 WORKERS=6 python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/hybrid.py fpw lpc-mini-core1 hyb/lpc-mini-core1.w.csv /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data/lpcmini/samp > hyb/lpc-mini-core1.w.log 2>&1
wait
echo done $(date)
