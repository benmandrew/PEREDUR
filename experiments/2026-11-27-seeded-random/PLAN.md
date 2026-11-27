# 2026-11-27-seeded-random

The paper's table of `full` against `base` has a `random` column, as a check that `base` is a fair baseline. `random` ran on the specifications only, so its seeded cells are empty (user, 2026-11-27: run it). This campaign runs sweep F's `random/base` on the sideways seeded corpus at the seeds every other seeded arm has. It reruns `nsga2-apportion/base` beside it, because the paper pairs wall time only within one campaign.

## Design

Profile `final-engine-seeded-random`: sweep F, levels `base` only, schemes `nsga2-apportion` and `random`, the 130 subjects of `2026-11-18-seeded-corpus-sideways/selected.txt` (untracked on the hosts), seeds 0-4, `configs-final-engine` at 2 threads a run.

| Phase | Seeds | Jobs × threads, CPUs | Runs | Estimate |
|---|---|---|---|---|
| av3 | 0–2 | 15 × 2 on 0–29, 48G | 780 | about 1.4 h |
| av2 | 3–4 | 15 × 2 on 0–29, 48G | 520 | about 0.9 h |

The split, jobs and CPU window are those of sweep F's seeded phases. The estimate takes sweep F's `base` (53,218 s summed over 650 runs) and `random` at 1.38 times `base`'s per-run time, as on the specifications.

## After the runs

1. Collect `final-engine-seeded-random` (1,300 rows).
2. Repairs: compare each rerun `base` run with sweep F's, byte for byte in `repair_*.json` and the accumulated set. A difference means a tool-call timeout; report the subjects.
3. Pooled pass: a two-arm pass, `random` against `base`, on the seeded subjects that the ten-arm seeded pass kept (126 after its `--drop` list, plus the extra-seeds mutants). Copy `2026-11-25-one-way-pooled-random` and run its `make_design.py random` with the seeded subjects. Pool locally under `MemoryMax=20G`, one pass at a time, then rsync and enqueue. Needs the user's approval.
4. Catalogue: mark the set collected in `experiments/catalogue.toml` and render `CATALOGUE.md`.
5. Paper (`peredur-extension`): in `scripts/import_data.py`, add `random` on the seeded corpus (`ARMS`, `CAMPAIGNS`), and the seeded random pooled pass (`POOLED_EXTRA`, `POOLED_CONTRASTS`). In `scripts/summarise.py`, give `seeded-random` its levels, with wall time paired against this campaign's `base`. Fill the seeded cells of `random` in `tab:rq1`. Then `make import` and `make`.

## Log

- 2026-11-27: prepared and enqueued.
- 2026-11-27 19:32Z: both phases done (av2 18:56Z, av3 19:30Z; the last run, random on fsm-timing add-not-supported m00 seed 2, took about 3,200 s). Collected: 1,300 rows, every key once. `scripts/check_repairs.py`: 650 base pairs against sweep F, 5 differ, on fsm-combined add-request-extend-within m12 (seed 3) and fsm-timing add-not-supported m00 (seeds 0-3). No found-repair outcome changes; n_repairs moves by 1-19 (m00 seed 2: 44 in sweep F, 25 here). As in 2026-11-26-one-way-wall, the cause is load-dependent tool-call timeouts. The pooled pass reads sweep F's base, as every other seeded pass does; wall time pairs against this campaign's base.
- 2026-11-27 19:34Z: pooled pass `2026-11-27-seeded-random-pooled` (user approved steps 1-3), written by `2026-11-25-one-way-pooled/scripts/make_design.py seeded-random --drop sw-fsm-lmcps__paper-v2__m09`, on the 126 subjects and host split of 2026-11-25-one-way-seeded-pooled less m09, where random holds 1 member (join rule: no arm at 3 or fewer). 125 subjects, 47,281 nodes before the drop (largest 1,149); members base 17,268, random 31,256. The three extra-seeds fsm-lmcps mutants stay out: random ran only at seeds 0-4, where no arm repairs them. Words copied on each host from one-way-seeded-pooled/fp.
- 2026-11-27 19:58Z: pooled pass done (av3 19:50Z, av2's reduce 19:55Z; 16 min a host to compare). Reduced outputs copied to `experiments/seeded-random-pooled-reduced/av2/` (untracked). random (a) against base (b), 125 subjects: strength net_imp median -0.029 (a>b 49, b>a 74), Wilcoxon p = 0.30; accepted-trace share HL -0.016 [-0.029, -0.003], p = 0.0008; pooled balls at K = 3 (n = 122), base in random 0.841 against random's own control 0.751, random in base 0.600 against base's control 0.768. Joint-front reach over two arms is not the paper's measure. Hosts handed to the GAWAIN session (av3 19:53Z, av2 19:58Z). Steps 4-5 not done.
- 2026-11-27: steps 4-5 (user). Catalogued as `final-engine-seeded-random` and `seeded-random-pooled`. Paper: import_data.py reads this campaign as sweep R (random/base, and its base as `base-wall`) and the pooled pass as `random-seeded-final`; summarise.py pairs seeded random's wall time with base-wall. Seeded random against base: recovery 10.7% against 16.7% (-6.0 pp, p = 0.0011), found 96.0% against 96.2%, median repairs per run 36 against 21, wall 1.35x [1.29, 1.41] base-wall. In the pooled pass, random's repairs spread wider than base's (net containment at K = 3, shift +0.234, p = 5e-26), where the specifications showed no difference; flagged in the paper for the author.
