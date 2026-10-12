# 2026-10-12-maoz-coverage-rerun

## 1. Registration

This campaign repeats `2026-10-05-maoz-coverage` with one input changed. Its registration is section 11 of `experiments/2026-10-02-maoz-baselines/PLAN.md`, and the outcomes and readings are that section's. No search runs here.

## 2. What changes

The PEREDUR pool is built from the final frontiers of `2026-10-07-tlsf-rerun`, the run that the paper's other PEREDUR figures read. `2026-10-05-maoz-coverage` built its pool from `2026-09-14-paper-rerun`. The arm is the same, `sweep_G_mrs_nsga2-apportion_wkoff_log`, at 30 runs on each of the ten subjects.

The tool side is the adapted tree that keeps the `G` on safety assumptions written to `REQUIRE`, `experiments/results-maoz-baselines-gfix`, from 1b8073c. The re-score of 2026-10-12 read the same tree. The passes and every budget are those of `2026-10-05-maoz-coverage`.

## 3. The pool

```sh
python3 scripts/maoz_score.py --pass pool --out experiments/maoz-peredur-pool-tlsf-rerun \
    --curves <tlsf-rerun>/experiments/curves-tlsf-rerun \
    --curves-rescored <tlsf-rescore>/experiments/curves-tlsf-rerun-rescore \
    --rerun <tlsf-rerun>/experiments/results-tlsf-rerun
```

Each frontier is the set that the run's maximality curve holds at its last cut, read from the `.members.tsv` sidecars of `2026-10-07-tlsf-rerun-scoring`. Eleven of the 300 runs are among the 63 that `2026-10-09-tlsf-rerun-rescore` scored again from the last attempt in their accumulator index, and for those the re-score's sidecar is read. The run directories are spread over av1, av2 and av3, so the pool is built locally and copied to both hosts.

The pool holds 9,168 distinct repairs over 9,275 frontier rows. The pool of `2026-10-05-maoz-coverage` held 9,178 over 9,281.

## 4. The split

av2 takes `humanoid-742` and av3 takes the other nine subjects. `humanoid-742` cost 25,614 of the 29,224 seconds of `compare` time in the re-score of 2026-10-12, so no split by subject balances the hosts. Each host runs the screen, frontier and coverage passes over its own subjects at 16 workers.

The report pass reads both halves. It runs locally over the joined `screen.csv`, `frontier.csv`, `frontier-summary.csv` and `relations.csv`.
