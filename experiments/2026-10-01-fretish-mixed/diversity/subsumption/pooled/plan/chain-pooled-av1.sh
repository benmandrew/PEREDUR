#!/bin/sh
# Pooled RQ3 pass (20 families, humanoid-742 excluded) on av1. Waits for the
# humanoid-742 cross-seed split to exit, then runs. Resumable: rerun to continue.
cd ~/subsum
export CMP=$HOME/subsum/av1/bin/compare PEREDUR_BLACK_PATH=$HOME/subsum/av1/bin/black PEREDUR_SPOT_BIN_DIR=$HOME/subsum/av1/bin JOBS=${JOBS:-30}
while pgrep -f "run[.]py split-742" >/dev/null; do sleep 60; done
echo "split done $(date)"
python3 run.py pooled20-av1.csv results-pooled20.csv
echo "pooled: done $(date)"
