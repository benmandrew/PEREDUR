# 2026-11-11-fretish-order-counters

The paper's mechanism table (tab:counters) and three RQ2 bullets need per-run counters that the first pass's run.json lacks. PR #226 (be57f58f) adds them: `climb.wall_s` and `finish_wall_s`, `n_mutations` and `n_slot_draws`, `direction_gate.steps_realizable`, `step_scaling.unscaled`, `unscaled_large` and `unscaled_small`, and `final_front` {members, realizable, gutted}. It changes no search decision and draws nothing new from the random source, so each run reproduces its first-pass repairs byte for byte.

This campaign runs the 4,860 small-set runs again at be57f58f under the new profile `fretish-order-counters`, which shares the first pass's config tree and writes its own results. Every seed runs on av2, at 16 jobs.

Check before use: for every (spec, arm, seed), the repairs and `accumulated/` files match the first pass's. Two specs may differ, valu3s-uc6 and lift-plus-cruise-mini, because BLACK's timeout makes their runs depend on timing. Report how many runs differ on each, and read the counters from all runs that match.

Collect with `campaign.py collect --profile fretish-order-counters --host av2`.

## Run notes

Enqueued on av2 on 2026-11-12 at 04:05, behind the pooled no-climb pass; it started at 05:32. At 06:58 the running runner (`run_experiments.py`, pid 3498499) and its descendants were moved to CPUs 0-15 with `taskset -a -cp 0-15`, at the request of the GAWAIN session, whose 16 nice-19 jobs on CPUs 16-31 were losing about a fifth of their cores to this campaign's unpinned tools. Runs started before 06:58 ran partly across all 32 CPUs, later ones on 16. This changes timing only, so it can move a repair only where a BLACK timeout binds (valu3s-uc6, lift-plus-cruise-mini). At 08:56, 1,050 of 4,860 runs were done, about 320 an hour.
