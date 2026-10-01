# Per-solution anytime verdicts and well-separation for the un-censored AuRUS arm: 291,037 and 296,300 rows, no failure

`2026-09-22-aurus-rerun-curves` gives one curve per run, and two things the paper reads need one row per solution. The *well-separation* screen joins a verdict to each AuRUS solution on `(spec, repeat, index)`, and the archive's verdicts measured a different sample: AuRUS draws from an unseeded `Math.random()`, so its repeats do not pair across runs of the tool. The anytime pass supplies the other half of that join, a `compare` verdict against the ideals for every solution with its discovery time. It also yields the per-run columns the paper's `aurus_full.csv` carries.

Both ran as `kind = "aurus-score"` phases from `campaign/aurus-rerun-anytime` at `5891b06`, through queue entries av2 035 and av3 033, av2 scoring repeats 0 to 14 and av3 15 to 29. The branch sits four commits above `3cd6efb`. `5da7d00` keeps `compare`'s relation map beside each curve, `b81df3e` adds the phase kind and its host runner `aurus_score_campaign.py`, `a2da7bb` dates AuRUS solutions from the fork's `solution-times.csv`, and `5891b06` declares the campaign. None of the four touches `src/`, `include/` or `CMakeLists.txt`, so the `compare` binary here, recorded at `5891b06` with `dirty = 0`, is source-identical to the one that scored both arms' curves.

## The anytime pass

The anytime pass reads the raw tree `experiments/aurus-rerun-out`, one `<spec>/repeat-NN` directory per run, because it dates each solution from that repeat's own record. The fork's `solution-times.csv` gives microsecond times and is preferred, and upstream's `run.log` is the fall-back. The scorer that ran is the port in `a2da7bb`. The copy of `score_aurus_anytime.py` at `3cd6efb` on `campaign/aurus-rerun-curves` predates the fork and has no `solution-times.csv` reader, so it is the wrong vintage for this arm.

It ran with `compare_timeout` 3000 s and 8 jobs a host, from 20:40:04 to 21:12:23 on av2 and 19:55:04 to 20:27:05 on av3 on 2026-09-23. Each host queued 390 repeats and scored 390, with 0 failed and empty `failures.txt` files. `timings.txt` sums to 3.96 worker-hours on av2 and 3.97 on av3. The longest unit was a `humanoid-742` repeat on each host, 794 s and 798 s.

The joined file holds 291,037 rows over all 780 repeats, 145,873 from av2 and 145,164 from av3. 290,935 rows are scored solutions over 678 repeats, every one dated from `solution-times.csv`. The other 102 rows are one per repeat with blank verdict and time: 72 `no-solutions` repeats, dated by the run-log fall-back because they hold nothing to date, and 30 `no-ideals-dir` repeats of `humanoid-741`, which has no `fixes/` directory. 8,029 solutions imply an ideal.

| verdict against the ideals | rows |
|---|---|
| incomparable | 217,848 |
| strictly weaker | 46,484 |
| timeout | 18,574 |
| strictly stronger | 6,495 |
| equivalent | 1,534 |
| blank | 102 |

Every `timeout` verdict is `humanoid-742`'s, 18,574 of its 18,988 solutions. These are `compare`'s own per-candidate verdicts inside units that finished within 798 s, well short of the 3000 s subprocess timeout. A timed-out implication reads as a non-implication, so `humanoid-742`'s AuRUS ideal count rests on 414 decided verdicts and is a lower bound. The registered table still reads AuRUS at 0.833 there.

## The well-separation pass

The well-separation pass reads the adapted tree `experiments/results-aurus-rerun`, whose candidate filenames `spec<i>.tlsf` match the raw tree's, so the index joins across the two. It ran `check_well_separated.py` with `ltlsynt` from SPOT 2.15.1, a 60 s timeout, pattern `*.tlsf` and the fast path off, which are the 2026-08-17 screen's budgets. av2 ran from 21:15:01 to 21:18:01 and av3 from 20:30:01 to 20:32:53. Each host queued 354 runs and scored 354, with 0 failed, and worker time summed to 0.35 h and 0.33 h.

The joined file holds 296,300 rows, 148,652 from av2 and 147,648 from av3, one per candidate. 174,774 are well-separated and 121,526 are not, and no verdict is undecided. Leaving out `humanoid-741` leaves 290,935 rows, exactly the anytime pass's scored rows, and 116,325 of them are not well-separated. The paper reports 116,325 of 291,007, 40.0%, with 987 of the 8,029 ideal hits among them, against the archive's 113,958 of 287,006.

The `sweep`, `arm`, `scheme`, `spec` and `seed` columns are blank on every row. `campaign.toml` expected `parse_run_id` to fill them from the adapted tree's run names, and it did not. Spec, seed and index are recovered from `path` with `aurus_(.+)_seed(\d+)/.*?/spec(\d+)\.tlsf$`, which matches all 296,300 rows. `humanoid-741`'s 5,365 verdicts join to nothing, the anytime pass grading none of its solutions, so a consumer drops them before the join.

## Collection

Both files were collected on 2026-09-24 with `campaign.py collect --curves` into `<name>/<host>/` and `<name>.csv`. They stay untracked in the `campaign/aurus-rerun-anytime` worktree, beside the curves campaign's files. The paper stages them through `data/rerun-2026-09-uncensored/stage.py`, adding a measured per-run start-up offset of 0.127 s to 1.457 s to every AuRUS time; the rows archived here carry no offset.

`5da7d00` now keeps `compare`'s relation map beside each curve, so a later scoring campaign gets these verdicts as a by-product of the maximality pass. This one paid for the map separately, because the curves pass had already run at `3cd6efb`.
