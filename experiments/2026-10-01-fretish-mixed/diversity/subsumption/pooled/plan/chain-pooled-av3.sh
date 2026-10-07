#!/bin/sh
# Pooled RQ3 pass (20 families, humanoid-742 excluded) on av3. Waits for the
# humanoid-742 cross-seed split to exit, then runs. Resumable: rerun to continue.
cd ~/subsum
export JOBS=${JOBS:-28}
while pgrep -f "run[.]py split-742" >/dev/null; do sleep 60; done
echo "split done $(date)"
python3 run.py pooled20-av3.csv results-pooled20.csv
echo "pooled: done $(date)"
