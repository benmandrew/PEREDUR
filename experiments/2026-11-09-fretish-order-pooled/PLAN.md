# 2026-11-09-fretish-order-pooled

The pooled strength-and-diversity pass of 2026-10-08-fretish-rerun-analysis, run on the arms of 2026-11-05-fretish-order and 2026-11-09-fretish-order-cores. `design.toml` declares the subjects, arms and contrasts: each `only-<f>` arm and `full` against `base`, all under nsga2-apportion, with `full` against `base` alone on lift-plus-cruise-full. valu3s-uc6, lift-plus-cruise-mini and lift-plus-cruise-full are estimated from 100 draws an arm; every other subject is exhaustive.

## Order of work

The small specs and lift-plus-cruise-full ran split by seed (av2 0-14, av3 15-29), so no host holds all of their runs. Their pool was built locally from both hosts' collected runs and committed as `members/` (with `git add -f`, since `/experiments/*/*` ignores it, as it ignores `design.toml` and the subject lists). The cores are split by core (`2026-11-09-fretish-order-cores/PLAN.md`), so each host holds every seed of its own cores and pools them itself.

1. Done: collect `experiments/results-fretish-order` and `-large`, pool the ten non-core subjects with `python3 scripts/pool_members.py experiments/2026-11-09-fretish-order-pooled/design.toml experiments/2026-11-09-fretish-order-pooled/members --subjects ...`, check `members/index.csv` for `runs_missing` (0) and `runs_empty`, and commit.
2. Enqueue this campaign on each host behind `2026-11-09-fretish-order-cores`. On each host, the `pool` phase writes `experiments/fretish-order-pooled-members` (ignored): it copies the committed pool of each small subject in `subjects-<host>.txt` byte for byte, and pools each of the host's cores from its 30 seeds. A missing or unfinished core run fails the phase. The `fingerprint` and `subsumption` phases follow on the same host.
3. When both hosts are done, run `campaign.py collect --outputs fretish-order-pooled`. Collect the cores' runs with `campaign.py collect 2026-11-09-fretish-order-cores` too, then pool the cores locally into `members/` and commit them. The gzip output is byte-stable, so each core's `sha256_jsonl` must equal the one in that host's `experiments/fretish-order-pooled-members/done-subjects-<host>.csv`.
4. Reduce: `python3 scripts/pooled_reduce.py experiments/2026-11-09-fretish-order-pooled/design.toml experiments/2026-11-09-fretish-order-pooled/members experiments/fretish-order-pooled experiments/2026-11-09-fretish-order-pooled/reduced`.

The subject lists hold each host's cores. The small subjects keep their first split until the balance between hosts is decided.

## Pool sizes

Nodes in the 9 design arms, from `maximal.tsv`: fsm 2837, fsm-combined 5976, fsm-lmcps 8, fsm-timing 7457, lift-plus-cruise-mini 8977, liquid-mixer 12856, mode-arbiter 2312, takeoff 3319, valu3s-uc6 31777, lift-plus-cruise-full 2463 (base and full). On fsm-lmcps, only full (1 member) and only-blame (7) hold any member, and base holds none, so its contrasts carry almost no information. The cores are pooled on their hosts.

## Cost estimate

A local fingerprint of fsm-timing at 32768 words (seed 3, 253 s at 8 workers) plans 335,899 pairs, or 337,999 directions (2,100 both ways). At the 2026-10-08 pass's 0.51 call-seconds a direction, that is about 48 CPU-h, about 2 h of wall time at 24 jobs. No calibration campaign measures it first; the main pass's first fsm-timing rows give the real figure.
