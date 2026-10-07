#!/bin/bash
# Run the pooled pass on the cheap subjects, each once its prep has written subset.npy.
cd ~/fret-pooled
for s in takeoff fsm-combined fsm-timing rad-core-10 rad-core-68 rad-core-1-18 rad-core-45 rad-core-12-18 rad-core-17-18 rad-core-55 rad-core-65 rad-core-61 rad-core-33; do
  until [ -f subjects/$s/subset.npy ]; do sleep 20; done
  WORKERS=8 nice python3 pooled.py run $s
done
echo RUN_CHEAP_DONE
