set -u
cd /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/data
mkdir -p fpb
BOUNDARY=1 python3 /tmp/claude-1001/-home-y19056ba-projects-counter/8430c7f3-2e75-44a1-93bc-79de8d1a2c69/scratchpad/fpcal/draw_black.py lpc-full-core1.b.ltl 32768 11 words/lpc-full-core1-b32768.words 8 2> words/lpc-full-core1-b.drawlog
cat words/lpc-full-core1-32768.words words/lpc-full-core1-b32768.words > words/lpc-full-core1-tb.words
~/projects/counter-wt/fpcal/build-release/fpdraw eval words/lpc-full-core1-tb.words all-lpc-full-core1.list 8 > fpb/lpc-full-core1.tsv 2> fpb/lpc-full-core1.err
echo done
