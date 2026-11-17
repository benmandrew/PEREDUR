# 2026-11-17-fretish-order-remove-no-f

Registered 2026-11-17, before launch. The user switched the paper's fourth mechanism from demotion to maximality removal on 17 Nov, after `2026-11-17-fretish-order-removal-followup` found that only-remove makes repairs stronger and more diverse than base, where only-demote does not. The paper's `full` is now climb, gate (`direction_gated` and `bisect_steps`), blame and removal, which is sweep D's `lean-remove`. This campaign runs the three `no-<f>` arms that change with it, and a rerun of `full` under their load.

## Arms

| Arm | Differs from `lean-remove` |
|---|---|
| `remove-no-climb` | `climb_output = false` |
| `remove-no-gate` | `direction_gated = false`, `bisect_steps = false` |
| `remove-no-blame` | `blame_targeted_slots = false` |
| `remove-full` | none: byte-identical to `lean-remove` as it ran on av2 |

Each `remove-no-<f>` also differs from `2026-11-17-fretish-order-full4-no-f`'s `full4-no-<f>` in `maximality_removal = true` alone. Every other sweep D level regenerates byte-identically.

## Reuse

Every other arm of the removal-based ablation exists at 30 seeds on the same 19 subjects, with a config byte-identical to the one the ablation names:

- `base`: sweep O `base` and `cr10-base`. `2026-11-17-fretish-order-full4-no-f`'s sweep D `base` rerun matched `cr10-base` on 285 of 285 cells.
- `full`: `lean-remove`, and `no-remove`: `lean`, both from `2026-11-17-fretish-order-removal`. The five-arm campaign's `full4` rerun matched that campaign's `lean-demote` on 284 of 285 cells, so its binary reproduces.
- `only-climb`, `only-gate`, `only-blame`: sweep O. `only-remove`: `2026-11-17-fretish-order-removal-followup`.

Outcomes come from the reused arms. `remove-full` exists for wall time: each `remove-no-<f>` pairs with it under one load, and any outcome cell where it differs from `lean-remove` is reported.

`2026-11-17-fretish-order-full4-no-f` ran its av2 half (seeds 0–14, 1,425 runs) and was cancelled on av3 at 22:14 on the user's instruction. Its demotion-based arms leave the paper; its av2 half stays as the same-load record of wall time with each mechanism removed from the demotion-based full.

## Runs

19 subjects (the 9 smaller specifications and rad's 10 cores) × 4 arms × 30 seeds = 2,280 runs, confirmed with `run_experiments.py --dry-run`, split by seed:

- **av2**, seeds 0–14, CPUs 0–15, 8 jobs, 32 GB: 1,140 runs.
- **av3**, seeds 15–29, CPUs 16–31, 8 jobs, 32 GB: 1,140 runs.

Each run uses 2 threads (`--parallel 2`).

## Estimate

`2026-11-17-fretish-order-full4-no-f` ran 1,425 runs of the same subjects, budget and split on av2 in about 2 h 55 min. At that rate 1,140 runs take about 2 h 20 min per host. The estimate carries at least ±50%.

## Analysis

`run_measures.py` in `2026-11-17-fretish-order-full4-no-f`, pointed at this campaign's CSV with `remove-full` as `full`: found repair (exact McNemar), repairs per run, gutted final-front members and wall time (Hodges–Lehmann shift over the 18 subjects with repairs, exact signed-rank p, Holm over the three features). Strength and diversity need a pooled pass over `lean-remove` and the three `remove-no-<f>` arms, which is the paper's run (4) and follows this campaign.
