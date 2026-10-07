#!/bin/bash
# Run the pooled pass on the cheap subjects, each once its prep has written subset.npy.
# Resumable: verdicts are cached per subject, and finished subjects are skipped.
cd ~/fret-pooled
for s in "$@"; do
  [ -f subjects/$s/result.json ] && continue
  until [ -f subjects/$s/subset.npy ]; do sleep 20; done
  WORKERS=${WORKERS:-4} nice python3 pooled.py run $s
done
echo RUN_DONE "$@"
