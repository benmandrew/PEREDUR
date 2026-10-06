#!/bin/bash
cd "$(dirname "$0")"
export MAXIMAL=$PWD/bin/maximal CAP=1800 JOBS=2 WORKERS=2
export PEREDUR_BLACK_PATH=/home/y19056ba/projects/counter/build-release/third_party/black/black
export PEREDUR_SPOT_BIN_DIR=/home/y19056ba/projects/counter/build-release/third_party/spot/bin
systemd-run --user --scope --quiet -p MemoryMax=12G -p MemorySwapMax=0 choom -n 1000 -- \
  nice -n 10 python3 paired.py res paired.csv > paired.log 2>&1
echo "done rc=$? $(date)" >> paired.log
