# FRETISH ablation: uniform redraw wins the registered test on one core

`2026-09-29-fretish-ablation` asks whether *directed mutation* of the three ordered FRETISH fields (timing, condition type and scope) finds repairs more often than a *uniform redraw* of the same fields. FRETISH is the structured-English requirement language of the Formal Requirements Elicitation Tool (FRET). `PLAN.md` registered the test before any row existed: McNemar's exact test on found-at-cap over 600 (subject, seed) pairs, two-sided, at alpha 0.05.

The registered rule fires for uniform. At the 7200 s cap the directed arm holds a repair on 550 of 600 runs and the uniform arm on 570. One discordant pair favours directed and 21 favour uniform, exact McNemar p = 1.1e-05. Uniform wins.

## What ran

Sweep O sets `[mutation] ordered_fields` to `directed` or `uniform` and pins every other key at its shipping default. The budget matches the paper re-run: `nsga2-apportion` selection, `mrs` status grading, weights 0.1/0.2/0.7, the log metric, weakening off, 1000 individuals at population 100, `parallel = 1`, and the runner's 7200 s cap with no `max_wall_s`. The corpus is 20 subjects: 8 whole specifications and 12 *unrealisable cores* of `rad` and both Lift-Plus-Cruise specifications. That makes 1200 runs from one binary at `dc3e276`, held by the annotated tag `provenance/fretish-ablation`.

av3 ran seeds 15-29 from 2026-09-29 21:41 to 2026-09-30 05:21, and av2 (avlab12) seeds 0-14 from 08:42 to 16:30. Both arms of every pair ran on the same host in one launch. All 1200 rows carry commit `dc3e276` with `dirty = 0`. The 1140 runs that wrote `run.json` stopped on the individuals limit at exactly 1000 bred. The other 60 are all `lpc-full-core1` runs, 30 per arm, killed at the 7200 s cap.

The search cost 212.5 core-hours against a registered estimate of 98-177. `lpc-full-core1` accounts for 120 of them, where calibration at seed 0 had put it between 2500 s and the cap.

## The curve pass

Found is *anytime*: a run counts at a budget when its accumulator held a gate-passing repair by then, so a killed run still counts. It is read from `time_to_first_repair` in the score phase's curves, scored by `score_curves.py --maximality` at 20 cuts, `maximal_timeout` 900 s, `compare_timeout` 3000 s and `deadline_s` 7500.

av3 finished its search about 9 h ahead of av2, so av2's half of the curve phase was split across both hosts in a second declaration, `2026-09-29-fretish-ablation-score` (`f3cd190`). av2 scored seeds 0-6 and av3 seeds 7-14, from copies of av2's run directories. av3 had already scored seeds 15-29 under the parent declaration. `f3cd190` differs from `dc3e276` in that declaration alone, so every curve comes from one scorer source.

That declaration is folded into this archive as `campaign-score.toml` rather than closed as an archive of its own. It is this campaign's own curve phase at the same budgets, reading the same results directory and writing the same out directory. Its 600 curves join the parent's 600 into one CSV that the primary reads whole, and a second archive would split one endpoint across two folders.

`campaign.py collect --curves curves-fretish-ablation` joined 1200 curves (av2 280, av3 920) into 187,340 rows with no MISMATCH and no INCOMPLETE. `failures.txt` is empty on both hosts. 147 curves are partial past the 900 s per-cut `maximal` budget (30, 39 and 78 across the three passes), which bounds their `maximal_solutions` from below and leaves `time_to_first_repair` untouched. The three passes cost 158.1 worker-hours of 4 cores each.

One record is lost. `score_campaign.py` names its manifest by hostname alone, and the seeds 7-14 pass on av3 overwrote the seeds 15-29 manifest in the shared out directory. The `maximality_pass` block in `PROVENANCE.json` is built from the two surviving manifests. The third pass's start, finish, counts and binaries (`maximal` and `compare` at `dc3e276`) survive only in av3's queue log, and `PROVENANCE.json` files them under `not_recorded`.

## The registered primary

| Budget | Directed | Uniform | Directed only | Uniform only | Exact McNemar p |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10 s | 516 | 527 | 11 | 22 | 0.0801 |
| 100 s | 536 | 556 | 5 | 25 | 0.0003 |
| 7200 s (primary) | 550 | 570 | 1 | 21 | 1.1e-05 |

One core carries the result. `rad-core-10` supplies all 21 uniform-only pairs at the cap, with directed holding a repair on 9 of 30 seeds and uniform on 30 of 30. The single directed-only pair is `fsm-lmcps`, 1 of 30 against 0 of 30. The other 18 subjects reach 30 of 30 on both arms.

The rule as written reads the pooled discordant counts and nothing else. It fires for uniform, and the decision stands as registered. The effect it measures is a corpus-level one that sits on a single core. Directed mutation misses `rad-core-10`'s repair on 21 seeds, and the reason is unexamined.

The directed arm found a repair on `rad-core-10` at 2 of 15 seeds on av2 and 7 of 15 on av3. Each pair ran both arms on one host, so this cannot bias the paired test.

## Secondaries

At 10 s the arms do not separate, 11 against 22, p = 0.0801. The discordant pairs there spread wider: directed alone holds a repair on `liquid-mixer` (5), `fsm-combined` (3) and `valu3s-uc6` (3), and uniform alone on `rad-core-10` (21) and `fsm-combined` (1). At 100 s uniform leads 25 against 5, p = 0.0003, and the 21 `rad-core-10` pairs are again most of it.

Wall time on the 570 pairs where both arms finished favours directed. Directed is faster on 377 pairs and uniform on 193, a median paired difference of -2.51 s, Wilcoxon signed-rank p = 1.8e-05 under the normal approximation with tie and continuity corrections. Medians over all 600 runs are 40.1 s and 38.9 s, and means 628.6 s and 646.3 s. Part of the directed lead is `rad-core-10`, where directed is faster on 30 of 30 pairs, mostly by finishing without a repair. Per subject the sign varies: `valu3s-uc6` reads -357 s and `lpc-mini-core1` -194 s, while `liquid-mixer` reads +148 s and `fsm-timing` +14 s.

`implies_ideal` is descriptive on the 6 subjects with reachable ideals and reads 91 of 180 for directed against 88 for uniform. `fsm` goes 25 against 29, `fsm-timing` 30 against 27 and `fsm-combined` 5 against 1. `takeoff` reads 30 on both arms, `liquid-mixer` 1 on both and `fsm-lmcps` 0 on both. Mean `n_repairs` is 64.6 against 62.9, and mean `maximal_solutions` at the last cut 55.7 against 53.8.

`scripts/analyse_ablation.py` computes every figure in this section and the last, and `analysis-output.txt` holds its output.

## The paired frontier pass

The paper's RQ5 also reports how the two arms' *frontiers* overlap pair by pair. This was not pre-registered. `prepare.py` in the paper's `data/fretish-ablation-2026-09/` reads it through `--paired`. Each pair's frontier comes from one `maximal` call over the union of both runs' `accumulated/maximal.tsv`. The drivers never reached `scripts/` on any branch and are vendored here as `scripts/frontier_*`.

The pass is complete for 16 subjects and absent for 4. Seeds 15-29 ran locally at `dc3e276`, and seeds 0-6 and 7-14 on av2 and av3 at `78cae62`, which changes `src/compare.cpp` alone. Together they place 480 of 600 pairs, 451 with a frontier and 29 empty. `liquid-mixer`, `lpc-mini-core1`, `lpc-full-core1` and `valu3s-uc6` are unplaced. `lpc-full-core1` hit the 1800 s cap on all 8 pairs tried locally, and the local run was killed there. A sampled placement of 200 frontier members each covers `liquid-mixer` and `lpc-mini-core1`, and `prepare.py` does not read it.

Over the 451 placed pairs the median pair shares none of its joint frontier. Directed alone holds 46.9% and uniform alone 52.2%, and each run holds repairs the other lacks on 428 of the 429 pairs where both found one.

## Paper side

The paper's RQ5 (`8deef6e`) was built from 336 scored pairs and 240 placed. Re-run on this archive, `prepare.py` writes 1200 runs, all scored, and 480 overlap pairs. `tables.py` refuses the final mode on the 120 unplaced pairs, so it ran with `--interim`. Every found and outlier number is now filled where it printed `\tbd`, and the frontier numbers still print `\tbd`. Those changes are left uncommitted in the paper repository.

## Config vintage

No entry is owed. Between `dc3e276` and the branch tip `78cae62` the only C++ change adds a `--timeout` flag to `compare`, whose default stays 20 s. `ordered_fields` is new on this branch and defaults to `directed`, the behaviour every earlier archive ran under.

## Owed

The frontier pass for the four unplaced subjects is owed, or a decision that RQ5 reports overlap over 16 subjects. So are a look at why directed mutation misses `rad-core-10`, and a `score_campaign.py` that refuses to overwrite another pass's manifest.

The registered test has an answer, and it is narrower than its p-value suggests. Of 600 pairs, 22 disagree at the cap, and 21 of those sit on one core.
