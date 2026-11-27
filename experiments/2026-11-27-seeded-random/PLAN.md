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
