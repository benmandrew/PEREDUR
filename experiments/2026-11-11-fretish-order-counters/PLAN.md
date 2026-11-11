# 2026-11-11-fretish-order-counters

The paper's mechanism table (tab:counters) and three RQ2 bullets need per-run counters that the first pass's run.json lacks. PR #226 (be57f58f) adds them: `climb.wall_s` and `finish_wall_s`, `n_mutations` and `n_slot_draws`, `direction_gate.steps_realizable`, `step_scaling.unscaled`, `unscaled_large` and `unscaled_small`, and `final_front` {members, realizable, gutted}. It changes no search decision and draws nothing new from the random source, so each run reproduces its first-pass repairs byte for byte.

This campaign runs the 4,860 small-set runs again at be57f58f under the new profile `fretish-order-counters`, which shares the first pass's config tree and writes its own results. Every seed runs on av2, at 16 jobs.

Check before use: for every (spec, arm, seed), the repairs and `accumulated/` files match the first pass's. Two specs may differ, valu3s-uc6 and lift-plus-cruise-mini, because BLACK's timeout makes their runs depend on timing. Report how many runs differ on each, and read the counters from all runs that match.

Collect with `campaign.py collect --profile fretish-order-counters --host av2`.
