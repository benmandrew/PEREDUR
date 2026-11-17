# 2026-11-17-fretish-order-full4-no-f

Registered 2026-11-17, before launch. The three arms the paper's new ablation lacks, and reruns of its `full` and `base` under the same load, after the paper cut step scaling (`603322e` in `peredur-extension`), order crossover (`9792fb6`) and every mention of `parent_verdict`.

## Arms

The paper's `full` is now climb, gate (`direction_gated` and `bisect_steps`), blame and demote. That is sweep D's `lean-demote`. The archived sweep O `full` differs from it in only five lines:

- `p_order_crossover`, `step_scaling` and `parent_verdict` are on in `full` and off in `lean-demote`;
- `lean-demote` also states `maximality_removal = false`, the engine default, and `runtime.parallel = 2`.

Every other key matches, as do the budget and `--pin-vintage`. This campaign runs `lean-demote` without each of its other three features:

| Arm | Differs from `lean-demote` |
|---|---|
| `full4-no-climb` | `climb_output = false` |
| `full4-no-gate` | `direction_gated = false`, `bisect_steps = false` |
| `full4-no-blame` | `blame_targeted_slots = false` |

Two reruns go with them, so that `full` and `base` pair with the `no-<f>` arms and with each other under one load:

| Arm | Config |
|---|---|
| `full4` | byte-identical to `lean-demote` as it ran on av3 |
| `base` (sweep D) | byte-identical to sweep O's `base`, and so to `cr10-base` |

The `full4-` prefix keeps the level names apart from sweep O's `no-<f>`, which have the cut features on. Sweep D's `base` writes to `sweep_D_base_…` directories, apart from sweep O's.

## Reuse

Every other arm of the new ablation already exists at 30 seeds on the same 19 subjects:

- `base`: sweep O `base` (`2026-11-05-fretish-order`, `2026-11-09-fretish-order-cores`) and `cr10-base` (`2026-11-17-fretish-order-xover-rate`).
- `full`: `lean-demote`, and `no-demote`: `lean`, both from `2026-11-17-fretish-order-removal`.
- `only-climb`, `only-gate`, `only-blame`, `only-demote`: sweep O. `base` has the cut features off, so the cut leaves these arms unchanged.

Outcomes reproduce across the engine versions and the `parallel = 2` line. Sweep O `base` at `f7ffb99`, `8d88c5f` and `798bf22` agrees with `cr10-base` at `3c42822` on repair count, found repair, ideal and best relation in 569 of 570 (subject, seed) cells, and the one that differs is on `valu3s-uc6`. Wall times come from different loads and are not compared across campaigns. The `full4` and `base` reruns exist for that reason: the removal direction's wall-time shifts, and `full` against `base`, pair within this campaign. Their outcomes are expected to match the reused arms', and any cell that differs is reported.

## Runs

19 subjects (the 9 smaller specifications and rad's 10 cores) × 5 arms × 30 seeds = 2,850 runs, confirmed with `run_experiments.py --dry-run`. The runs are split by seed, as the ablation's `base` was:

- **av2**, seeds 0–14, CPUs 0–15, 8 jobs, 32 GB: 1,425 runs.
- **av3**, seeds 15–29, CPUs 16–31, 8 jobs, 32 GB: 1,425 runs.

Each run uses 2 threads (`--parallel 2`). peredur-extension-d5 allocated these CPUs on 17 Nov. av2 goes after entry 080. av3 goes after `2026-11-17-fretish-order-removal-followup` and its pooled pass.

## Estimate

The removal campaign ran 1,710 runs of the same subjects and budget in about 1 h 15 min on av2 and 1 h 40 min on av3, split by subject. This split is by seed, so each host takes half of every subject for five arms, 1,425 runs, about 2.5–3.5 h each. The estimate carries at least ±50%.

## Analysis

The paper's own: `scripts/summarise.py` in `peredur-extension` reads the merged CSV for the removal direction (each `no-<f>` against `full`), with `lean-demote` as `full`. The strength, reach and diversity of these arms need a pooled pass over `full` and every `no-<f>`. That pass is the paper's run (4) and is not part of this campaign.
