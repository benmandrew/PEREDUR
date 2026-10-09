# FRETISH re-run: uniform redraw wins again on the same core

`2026-10-06-fretish-rerun` repeats `2026-09-29-fretish-ablation` at a later engine. It asks again whether *directed mutation* of the three ordered FRETISH fields (timing, condition type and scope) finds repairs more often than a *uniform redraw* of the same fields. FRETISH is the structured-English requirement language of the Formal Requirements Elicitation Tool (FRET). `PLAN.md` inherits the ablation's design, corpus, tests and decision rule word for word, and changes the engine alone.

The registered rule fires for uniform. At the 7200 s cap the directed arm holds a repair on 552 of 600 runs and the uniform arm on 572. No discordant pair favours directed and 20 favour uniform, exact McNemar p = 1.91e-06. Uniform wins.

## Why it ran

The ablation ran at `dc3e276`. Four changes to the engine and its defaults reached the campaign branch after it, and each moves FRETISH scores. `5dc3472` renders the operand of `X` in `start_of_mode` as `X(m)`, where PEREDUR's parser and `black` had read the old `Xm` as one atom. Pull request (PR) #205 screens a requirement's condition and response as two formulae. PR #206 adds one whole-spec satisfiability query under `mrs` grading, so a candidate whose requirements are jointly contradictory now scores 0. `dc13ed2` moves the default of `fitness.keyword_similarity` from `"syntactic"` to `"semantic"` at `semantic_trace_weight = 0.5`.

The rows replace the ablation's in the paper's FRETISH research question. They do not pair against the archive, since every subject's score function has moved.

## What ran

Sweep O sets `[mutation] ordered_fields` to `directed` or `uniform`. The budget is `nsga2-apportion` selection, `mrs` status grading, weights 0.1/0.2/0.7, the log metric, weakening off, 1000 individuals at population 100, `parallel = 1`, and the runner's 7200 s cap with no `max_wall_s`. `gen_configs.py --pin-vintage` pins every other key, and writes `keyword_similarity = semantic` and `semantic_trace_weight = 0.5` into every config. The corpus is the ablation's 20 subjects: 8 whole specifications and 12 *unrealisable cores* (ten of `rad`, plus `lpc-mini-core1` and `lpc-full-core1`). That makes 20 × 2 × 30 = 1200 runs.

Every row comes from one binary at `f828ff3` on branch `campaign/fretish-rerun`, held by the annotated tag `provenance/fretish-rerun`. All 1200 rows carry commit `f828ff3` with `dirty = 0`, and all three run manifests record `peredur` and `compare` at the same commit. The branch is pushed but not reachable from `main`.

Three hosts ran 16 jobs each on 2026-10-07. av3 ran seeds 0-9 from 07:32 to 12:42, av2 (avlab12) seeds 20-29 from 07:56 to 13:36, and av1 seeds 10-19 from 10:30 to 16:00. The start stamps come from the run manifests and the finish stamps from each host's last CSV write. av3's clock runs about 17 minutes fast with the Network Time Protocol (NTP) off. Both arms of every (subject, seed) pair ran on the same host in one launch, so wall time compares pair by pair.

`campaign.py collect --profile fretish-rerun` merged 1200 rows on 2026-10-08, 400 per host and 600 per arm, with 1200 distinct keys and no duplicates. The 1140 runs that wrote `run.json` stopped on the individuals limit at exactly 1000 bred. The other 60 are all `lpc-full-core1`, 30 per arm, killed at the 7200 s cap. No row timed out in `compare`.

## Calibration

The calibration declaration `2026-10-06-fretish-rerun-calib` ran seed 0 of all 20 subjects on av1, both arms, 40 runs. It started at 08:26:55 on 2026-10-07 and its queue entry finished at 10:26:55. It is folded in here as `campaign-calib.toml`.

Its gate went unenforced. Both declarations were enqueued together on 2026-10-06, and av2 and av3 started the main search before the calibration finished. `PLAN.md` recorded this deviation before launch. The three registered checks therefore became post-hoc reads at the close.

**Cost.** The check passes. Per-subject search cost against the ablation archive is 0.36-1.05x, and no subject crosses the 2x re-plan threshold. `rad-core-33` (0.36x) and `rad-core-61` (0.38x) fell most.

**Toolchain.** av1 is the one host built with gcc 13, and it had never run a campaign. Its 40 runs were compared with av3's seed-0 runs of the same cells, ignoring wall-clock fields. 33 of 40 are identical in repairs, accumulated set and generation lines. The 7 that differ are `lpc-full-core1` (both arms, stopped by the cap), `liquid-mixer` and `lpc-mini-core1` (both arms) and `valu3s-uc6` uniform. Every one of them had tool calls time out, mostly `ltlfilt` implication queries at the 10 s budget. `liquid-mixer` directed, for example, logged 781 such timeouts on av1 against 875 on av3. Every run without a timeout on either host is byte-identical. The divergence follows the wall clock, and the check is recorded as a qualified pass.

**Change.** No seed-0 diff against the archive was run. The registered analysis below gives the change at corpus level instead, and the decision and its locus are unchanged.

## The curve pass

Found is *anytime*: a run counts at a budget when its accumulator held a gate-passing repair by then, so a killed run still counts. It is read from `time_to_first_repair` in the score phase's curves. Each host scored its own 400 runs with `score_curves.py --maximality --cuts 20 --jobs 4 --deadline-s 7500 --maximal-timeout 900 --compare-timeout 3000`, at 8 pinned workers of 4 cores each and an 8400 s wall cap per run.

av3 scored from 12:45 to 19:27, av2 from 13:40 to 20:32 and av1 from 16:05 to 23:27, all on 2026-10-07. Each pass queued 400 runs and scored 400, with `maximal` and `compare` at `f828ff3` and `failures.txt` empty. `campaign.py collect --curves curves-fretish-rerun` joined 1200 curves into 167,475 rows with no MISMATCH and no INCOMPLETE.

146 curves are partial past the 900 s per-cut `maximal` budget: 47 on av3, 47 on av1 and 52 on av2. That bounds their `maximal_solutions` from below and leaves `time_to_first_repair` untouched. The passes cost 157.64 worker-hours by `timings.txt`, against 158.1 for the archive's. All three score manifests survive, copied verbatim into `score-manifests/`.

## The registered primary

| Budget | Directed | Uniform | Directed only | Uniform only | Exact McNemar p |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10 s | 514 | 525 | 12 | 23 | 0.0895 |
| 100 s | 537 | 556 | 4 | 23 | 0.000311 |
| 7200 s (primary) | 552 | 572 | 0 | 20 | 1.91e-06 |

One core still carries the result. `rad-core-10` supplies 18 of the 20 uniform-only pairs at the cap, and `fsm-lmcps` the other 2. The remaining 18 subjects tie.

The rule reads the pooled discordant counts and nothing else, and it fires for uniform as it did at `dc3e276`. Four engine changes moved every subject's score function and left the decision and its locus in place.

## Secondaries

The secondaries carry no decision. At 10 s the arms do not separate, 12 against 23, p = 0.0895. The discordant pairs there spread across four subjects. Directed alone holds a repair on `liquid-mixer` (5), `valu3s-uc6` (4) and `fsm-combined` (3). Uniform alone holds one on `rad-core-10` (18), `fsm-combined` (2), `valu3s-uc6` (2) and `liquid-mixer` (1). At 100 s uniform leads 23 against 4, p = 0.000311. Its share is `rad-core-10` (18), `lpc-full-core1` (3) and `fsm-lmcps` (2), and directed's 4 are all `lpc-full-core1`.

Wall time on the 570 pairs where both arms finished favours directed. Directed is faster on 426 pairs and uniform on 143, a median paired difference of -2.94 s. The Wilcoxon signed-rank test gives z = 8.644 and p = 5.43e-18, under the normal approximation with tie and continuity corrections that the paper's `tables.py` uses above 300 pairs. Medians over all 600 runs are 33.98 s for directed and 35.06 s for uniform, and means 619.9 s and 628.8 s.

Per subject the median paired difference favours directed on 15 of the 19 subjects with finished pairs; `lpc-full-core1` has none. `lpc-mini-core1` reads -292.01 s and `valu3s-uc6` -163.49 s. The four that favour uniform are `liquid-mixer` (+408.36 s), `fsm-combined` (+3.55 s), `takeoff` (+2.95 s) and `fsm-timing` (+1.01 s). On `rad-core-10` directed is faster on 30 of 30 pairs, partly by ending without a repair.

`implies_ideal` is descriptive on the 6 subjects with reachable ideals and reads 92 of 180 for directed against 88 for uniform. `fsm` goes 28 against 27, `fsm-timing` 30 against 29 and `fsm-combined` 3 against 1. `takeoff` reads 30 on both arms, `liquid-mixer` 1 on both and `fsm-lmcps` 0 on both. Mean `n_repairs` is 55.2 against 52.27, and mean `maximal_solutions` at the last cut 46.6 against 44.98.

`scripts/analyse_rerun.py` computes every figure in this section and the last, and `analysis-output.txt` holds its output. It is the ablation's `analyse_ablation.py` with the profile name and the per-host seed split changed.

## Cost

The search cost 208.11 core-hours against the archive's 212.47 and a registered estimate of 212. At `parallel = 1` the sum of run wall times equals the core-hours to a close approximation. av3 took 66.93, av1 69.59 and av2 71.60. `lpc-full-core1` accounts for 120 of the total, all 60 runs killed at 7200 s, as in the archive.

## Provenance

The six scripts in `scripts/` other than `analyse_rerun.py` are vendored from `f828ff3`. `git hash-object` on each copy equals `git rev-parse f828ff3:scripts/<file>`, and `PROVENANCE.json` lists the six hashes.

`dc13ed2` moves the default of `fitness.keyword_similarity` from syntactic to semantic. `--pin-vintage` writes the key into every config here, so these configs read the same whichever default a later binary carries.

## Follow-up passes

Steps 4 (pooled strength) and 5 (fingerprints and ball radii) of `PLAN.md` ran on these rows as their own campaign, `2026-10-08-fretish-rerun-analysis`, archived at `308e01a` on `campaign/fretish-rerun-analysis` under the tag `provenance/fretish-rerun-analysis`. This archive holds none of their outputs. Uniform's pooled maximal set subsumes more of directed's than the reverse on 17 of 17 subjects (exact Wilcoxon p = 1.5e-05). Under ball-radii containment at K = 3, uniform's repairs fall in directed's region less often than directed's own control, a shift of -0.031 [-0.062, -0.003], while directed's repairs lie inside uniform's spread at +0.007 [-0.028, +0.043]. Directed's region nests inside uniform's.

Step 6, the paper's new `data/fretish-ablation-2026-10/`, belongs to the paper repository. The paper reads wall time per subject over all 20, where directed's median is lower on 15, a shift of -3.25 s [-6.98, -0.817]; at the ablation the interval held zero. Directed reaches a first repair sooner on 18 of 19 subjects, a shift of -0.160 s [-0.611, -0.0703].

The re-run moved every score function in the corpus and returned the same verdict from the same core. Of 600 pairs, 20 disagree at the cap, and 18 of those are `rad-core-10`.
