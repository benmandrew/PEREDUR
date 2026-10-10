# TLSF re-run re-score: 63 two-attempt runs scored once, in 11.3 worker-hours

This campaign re-scores the 63 run directories of `2026-10-07-tlsf-rerun` whose accumulator index holds a killed attempt and its re-run. The scorer reads each index's last attempt only. The maximality phase writes the anytime curves, with ideals, and the `.members.tsv` sidecars; the recount phase reads those sidecars and counts epsilon-separated solutions at fingerprint seed 0 with 65,536 words. Both run at the settings of `2026-10-07-tlsf-rerun-scoring`, whose archive listed this re-score as owed. Each host scored the runs it searched: av3 31 runs of seeds 13 and 15-17, av2 16 of seed 19, av1 16 of seeds 25-26. Every figure below comes from `PROVENANCE.json`, `analysis-output.txt` and `splice-output.txt` beside this file.

## Scorer

The scorer is `c1c6e5b`: the parent archive's `df219b9` plus `1a0a539`, which makes `score_curves.py` read a re-run's index by its last attempt. The two commits differ in `scripts/` and `experiments/` alone, so the binaries build from the same source. All three (`maximal`, `compare`, `fingerprint`) are clean builds of `c1c6e5b` on every host. `provenance/tlsf-rerun-rescore` holds `c1c6e5b`.

## Cost

The maximality phase took 9.04 worker-hours at 4 cores a worker over its 63 finished attempts, with the longest run at 3237 s against a 7500 s deadline. The recount took 2.30 core-hours, the longest run 474 s against 900 s. Neither phase recorded a failure or a non-zero return code, and `failures.txt` is empty on every host. The ledger undercounts av3: its ticks were interrupted twice, and the 12 attempts those interruptions cut off left no line in `timings.txt`.

## Splice

`splice.py` drops each re-scored run's rows from the parent's merged file and writes the re-scored rows in their place. The curves file goes from 728,737 rows to 716,440 (34,220 dropped, 21,923 added) and the recount file from 921,401 to 909,152 (38,382 dropped, 26,133 added). No run outside `runs.txt` changes at the 7200 s cut. The final `solutions` count falls on all 63 runs, by 12,057 in total; the largest single fall is 723, on `humanoid-531` seed 26 under weighted/mrs, from 1446 to 723.

## Results

At the 7200 s cut, averaged over all 750 runs of an arm with a run lacking the metric counted as zero, original then spliced:

| Metric | nsga2-apportion/mrs | nsga2-apportion/aurus | weighted/mrs | weighted/aurus |
|---|---|---|---|---|
| solutions | 98.57, 97.47 | 66.11, 65.70 | 388.99, 379.77 | 265.08, 259.74 |
| ideal_solutions | 5.61, 5.58 | 5.10, 5.07 | 10.49, 10.45 | 9.09, 9.04 |
| maximal_solutions | 28.49, 28.49 | 19.49, 19.49 | 86.27, 85.52 | 54.76, 54.53 |
| maximal_ideal_solutions | 1.87, 1.87 | 1.75, 1.75 | 2.07, 2.07 | 1.68, 1.68 |
| eps_maximal_solutions_0.05 | 17.25, 17.25 | 13.10, 13.10 | 44.25, 44.13 | 31.46, 31.41 |
| eps_maximal_solutions_0.2 | 12.76, 12.76 | 9.66, 9.66 | 32.35, 32.27 | 23.33, 23.30 |
| eps_maximal_solutions_0.5 | 7.84, 7.84 | 5.97, 5.97 | 18.85, 18.81 | 13.74, 13.73 |

The `solutions` means fall by 1.10, 0.42, 9.23 and 5.34 a run, which are the over-counts the search archive measured. The over-count is gone from `solutions`. The maximal and epsilon counts move too, on 25 of the 63 runs for `maximal_solutions`, so the killed attempts' files did sit in some antichains. `analysis-output.txt` also gives the epsilon counts over all solutions, which fall by at most 0.11 a run.

## Deviations

One re-scored curve is partial. On av3 the antichain walk of `sweep_G_mrs_weighted_wkoff_log_humanoid-531_seed16` died with `SIGABRT`, and the scorer still exited 0, so the run is in neither `failures.txt` nor the manifest's failed count. Its curves hold no `maximal_solutions`, `maximal_ideal_solutions` or `eps_maximal_solutions` rows and no `.members.tsv`, where the original read 272, 1, and 25, 14 and 8 at 7200 s. The splice replaces the original rows, so in the spliced files this run counts zero on those five metrics. Of the 0.74 fall in weighted/mrs `maximal_solutions`, 0.36 is this run's missing rows (272 of 750). The cause of the abort was not recorded, and re-scoring that run is owed.

The first entry on each host was staged at `80ba483`, which lacks `runs.txt`; all three were cancelled before scoring anything and superseded by the `c1c6e5b` entries. av3's maximality phase logged "tick was interrupted mid-phase" at 15:05 and 15:10 on 2026-10-09 and resumed from the 25 curves already written; what interrupted the ticks was not recorded. The re-scored curve of `humanoid-742` seed 25 under weighted/mrs holds no `ideal_solutions` row where the original read 2, with no warning beyond the expected `run.json` one.

The spliced files are sound for `solutions`, `ideal_solutions` and the epsilon counts over all solutions. For the maximal metrics on weighted/mrs they are one run short until `humanoid-531` seed 16 is scored again.
