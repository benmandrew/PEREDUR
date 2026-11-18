set -e
S=${F4X_WORK:?set F4X_WORK}
mkdir -p $S/f4xdirs/small $S/f4xdirs/seeded $S/ctl/nb $S/f4xres
for h in av2 av3; do scp -q $h:projects/counter/experiments/results-fretish-seeded-remove.csv $S/ctl/seeded-remove-$h.csv; rsync -a $h:projects/counter/experiments/results-fretish-seeded-remove/ $S/ctl/nb/ --include="sweep_D_remove-no-blame_*/***" --exclude="*"; done
for h in av2 av3; do
  rsync -a $h:projects/peredur-full4-exchange/experiments/results-fretish-remove-exchange/ $S/f4xdirs/small/
  rsync -a $h:projects/peredur-full4-exchange/experiments/results-fretish-remove-exchange-seeded/ $S/f4xdirs/seeded/
  rsync -a $h:projects/peredur-full4-exchange/experiments/results-fretish-remove-no-blame-exchange-seeded/ $S/f4xdirs/seeded/ 2>/dev/null || true
done
