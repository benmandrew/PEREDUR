cd "${F4X_WORK:?set F4X_WORK}"
mkdir -p f4xres
for h in av2 av3; do for p in remove-exchange remove-exchange-seeded remove-no-blame-exchange-seeded; do scp -q $h:projects/peredur-full4-exchange/experiments/results-fretish-$p.csv f4xres/$p-$h.csv; done; done
