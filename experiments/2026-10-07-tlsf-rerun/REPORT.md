# PEREDUR at `5d117f3`, TLSF re-run: parity holds at p = 0.6405 for 432.3 core-hours

This campaign repeats the 2026-09-14 paper re-run's search on the Temporal Logic Synthesis Format (TLSF) corpus, unchanged in design, at engine `5d117f3`. The design is the 2x2 of selection (`nsga2-apportion`, `weighted`) against status grading (`mrs`, `aurus`) over 25 families at 30 seeds, a 1000-individual budget and a 7200 s cap, 3000 rows in all. Every figure below comes from `PROVENANCE.json` and `analysis-output.txt` beside this file.

The registered primary compares `implies_ideal` per family for `nsga2-apportion/mrs` against the archived AuRUS arm, by an exact two-sided Wilcoxon test. PEREDUR is higher on 13 families, lower on 7 and tied on 5, with a mean difference of -0.003 and p = 0.6405. That is Outcome 3, parity, as in the paper re-run, which read 12 higher, 8 lower and p = 0.9345. Over scorable runs alone the contrast reads 12 higher, 7 lower and p = 0.7452, against 0.8983.

## Rows and binaries

All 3000 rows are clean (`dirty = 0`). 1321 ran at `43ab6c2` and 1679 at `953732b`. The two commits differ in `PLAN.md` and `campaign.toml` alone, so both binaries are built from the same engine source. av2 ran seeds 0-12 at `43ab6c2` overnight on 2026-10-07, when the av3 and av1 entries failed at the build step, and the remaining 17 seeds were re-split at `953732b`. 21 rows of seed 13 also stand at `43ab6c2`, finished by av3 before its entry was dequeued at 13:34 on 2026-10-08. `provenance/tlsf-rerun` holds `953732b`, whose parent is `43ab6c2`.

| Host | Compiler | Seeds | Rows | Core-hours | Killed |
|---|---|---|---|---|---|
| av2 | gcc 11 | 0-12, 19-24 | 1900 | 273.72 | 43 |
| av3 | gcc 11 | 13-18 | 600 | 80.80 | 13 |
| av1 | gcc 13 | 25-29 | 500 | 77.78 | 14 |

## Cost

The campaign cost 432.3 core-hours, against the paper re-run's 550.9, a fall of 21.5%. Finished runs cost 292.3 core-hours against 300.9, so almost all of the saving comes from kills at the cap: 70 against 125, taking 140.0 core-hours against 250.0. All 70 kills fall on the two humanoid families under `weighted` selection: `weighted/mrs` lost 30 runs on humanoid-742 and 26 on humanoid-531, and `weighted/aurus` lost 12 and 2. No `nsga2-apportion` run was killed.

| Arm | Yield | `implies_ideal` | Timeout | Mean wall (s) | Core-hours |
|---|---|---|---|---|---|
| nsga2-apportion/mrs | 1.000 (0.993) | 0.544 (0.549) | 0.000 (0.007) | 347.4 (516.4) | 72.4 |
| nsga2-apportion/aurus | 0.940 (0.940) | 0.533 (0.535) | 0.000 (0.000) | 211.8 (281.1) | 44.1 |
| weighted/mrs | 0.925 (0.908) | 0.483 (0.467) | 0.075 (0.091) | 924.5 (1058.0) | 192.6 |
| weighted/aurus | 0.935 (0.884) | 0.453 (0.441) | 0.019 (0.069) | 591.3 (788.7) | 123.2 |

The paper re-run's value is in brackets. Yield, `implies_ideal` and timeout are over 750 runs an arm, with a kill counting as a failure. Mean wall averages every run, a kill at its 7200 s, as the paper re-run's archive did.

## Verification

All five checks pass. `stopped_by` reads `individuals` on 2930 of 2930 manifests, and `individuals_bred` is 1000 on every one. `implies_ideal` holds on 1510 of 3000 rows, against `ideal_solutions > 0` on 1543 of 3000 curves. No `compare` call timed out.

## Calibration

av1 is the one host built with gcc 13. Its calibration ran seed 0 of six families across all four arms, and each of the 24 run directories was compared byte for byte with av2's seed-0 run of the same cell, wall-clock fields masked. 14 are identical and 10 differ. All 10 that differ hit tool timeouts on both hosts at different counts. All 8 runs with no timeout on either host (minepump and rg2) are identical, so the divergence follows the wall-clock budgets of the external tools. The output check passes on that reading.

The wall check does not pass. On the 14 identical runs av1 takes a median 1.089 times av2's wall time, slower on 12 of 14 (exact sign test p = 0.0129). On minepump and rg2, whose seed noise is a factor of 1.04, av1 is 1.069 to 1.340 times slower on all 8 runs. `PLAN.md` says that av1's seeds are then re-run on av2 and av3 before any wall-time figure is read. The decision of 2026-10-09 is to keep av1's seeds as collected, with no re-run, and the paper does not state the host difference. Contrasts between arms are unaffected, since all four arms of a seed ran on one host.

## Secondaries

Pooled over the four cells, PEREDUR is higher on 12 families and lower on 11, p = 0.7370, against the paper re-run's 0.5348. Post hoc, `nsga2-apportion` beats `weighted` on `implies_ideal` 173 to 67 discordant pairs and `mrs` beats `aurus` 111 to 81 (exact McNemar p = 0.0361), both in the paper re-run's direction. At 7200 s, `weighted/mrs` holds a mean 86.27 maximal solutions a run against 66.58 before, and `weighted/aurus` 57.44 against 47.42. The `nsga2-apportion` arms moved by under 0.5.

## Deviations

At about 16:57 on 2026-10-08 the running search entries on all three hosts were cancelled to move them behind another campaign, then re-enqueued. The runs in flight were killed and re-run under the resume key, which skips a finished run by its CSV key but never clears a directory. The accumulator appends to `accumulated/index.tsv`, so 63 of 3000 directories hold two attempts behind two headers, and the curve scorer counts both. 50 of the 63 repeat the killed attempt exactly. 13 diverge, leaving 1044 file names indexed by the killed attempt alone, whose files can sit in those runs' maximal antichains.

The double count inflates the 7200 s `solutions` mean by 9.23 a run on `weighted/mrs`, 5.34 on `weighted/aurus`, 1.10 on `nsga2-apportion/mrs` and 0.42 on `nsga2-apportion/aurus`. On `ideal_solutions` the inflation is 0.021 to 0.053 a run. The results CSV is unaffected, and so are the maximal and separation counts, which read each file once, apart from the 13 diverging runs. The data stand as collected. Re-scoring the 63 runs with each index cut to its last attempt is owed.
