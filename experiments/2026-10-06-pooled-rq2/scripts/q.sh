#!/bin/sh
# Work through ~/subsum/q-rq2/*.csv in name order, one run.py at a time,
# appending to ~/subsum/results-rq2.csv. Names sort sampling batches (0-*)
# ahead of stage-1 chunks (1-*) and stage-2 chunks (2-*). Idles when the
# queue is empty; touch q-rq2/STOP to exit after the current file.
cd ~/subsum
mkdir -p q-rq2 q-rq2-done
if [ -x "$HOME/subsum/av1/bin/compare" ]; then
    export CMP=$HOME/subsum/av1/bin/compare PEREDUR_BLACK_PATH=$HOME/subsum/av1/bin/black PEREDUR_SPOT_BIN_DIR=$HOME/subsum/av1/bin JOBS=${JOBS:-30}
fi
while [ ! -e q-rq2/STOP ]; do
    f=$(ls q-rq2/*.csv 2>/dev/null | sort | head -1)
    if [ -z "$f" ]; then sleep 30; continue; fi
    echo "$(date +%T) start $(basename "$f")" >> q-rq2.log
    python3 run.py "$f" results-rq2.csv >> run-rq2.log 2>&1
    rc=$?
    echo "$(date +%T) done $(basename "$f") rc=$rc" >> q-rq2.log
    mv "$f" q-rq2-done/
done
echo "$(date +%T) stopped" >> q-rq2.log
