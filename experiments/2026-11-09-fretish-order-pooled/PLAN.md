# 2026-11-09-fretish-order-pooled

The pooled strength-and-diversity pass of 2026-10-08-fretish-rerun-analysis, run on the arms of 2026-11-05-fretish-order and 2026-11-09-fretish-order-cores. `design.toml` declares the subjects, arms and contrasts: each `only-<f>` arm and `full` against `base`, all under nsga2-apportion, with `full` against `base` alone on lift-plus-cruise-full. valu3s-uc6, lift-plus-cruise-mini and lift-plus-cruise-full are estimated from 100 draws an arm; every other subject is exhaustive.

## Order of work

Each host holds only its own seeds of every results directory (av2 0-14, av3 15-29), so the pool is built locally and committed before anything is enqueued.

1. Collect the run phases of both source campaigns, so that `experiments/results-fretish-order`, `-large` and `-cores` hold all 30 seeds locally.
2. Pool, then commit `members/` with `git add -f`, since `/experiments/*/*` ignores it (as it ignores `design.toml` and the subject lists, which are force-added too, as 2026-10-08 did): `python3 scripts/pool_members.py experiments/2026-11-09-fretish-order-pooled/design.toml experiments/2026-11-09-fretish-order-pooled/members`. Check `members/index.csv` for `runs_missing` (must be 0) and `runs_empty`.
3. Enqueue `2026-11-09-fretish-order-pooled-calib` (fsm-timing on av2) and read its directions, undecided share and call-seconds before the main pass. Rebalance `subjects-av2.txt` and `subjects-av3.txt` on what it shows.
4. Enqueue this campaign. When it is done, run `campaign.py collect --outputs fretish-order-pooled`.
5. Reduce: `python3 scripts/pooled_reduce.py experiments/2026-11-09-fretish-order-pooled/design.toml experiments/2026-11-09-fretish-order-pooled/members experiments/fretish-order-pooled experiments/2026-11-09-fretish-order-pooled/reduced`.

## Pool sizes

Nodes in the 9 design arms, from `maximal.tsv`: fsm 2837, fsm-combined 5976, fsm-lmcps 8, fsm-timing 7457, lift-plus-cruise-mini 8977, liquid-mixer 12856, mode-arbiter 2312, takeoff 3319, valu3s-uc6 31777, lift-plus-cruise-full 2463 (base and full). On fsm-lmcps, only full (1 member) and only-blame (7) hold any member, and base holds none, so its contrasts carry almost no information. The cores are not yet collected.

## Calibration estimate

A local fingerprint of fsm-timing at 32768 words (seed 3, 253 s at 8 workers) plans 335,899 pairs, or 337,999 directions (2,100 both ways). At the 2026-10-08 pass's 0.51 call-seconds a direction, that is about 48 CPU-h, about 2 h of wall time at 24 jobs. The calibration campaign measures the real figure.
