# 2026-11-26-one-way-wall-cores

The paper is to read rad's 10 cores as a per-run subject, beside the whole of rad (user, 2026-11-26). Every arm already has 30 seeds on the cores: `base`, `full` (sweep F's `no-gate`), `random`, `random+climb` and the `only-<f>` arms from sweep F, and the `no-<f>` arms from sweep T. Found-repair rates and repair counts pair as they are. Leave-one-out wall time does not: sweeps F and T ran at different host speeds, which is why `2026-11-26-one-way-wall` reran `full` beside the `no-<f>` arms on the 9 smaller specifications and rad, and that campaign left the cores out. This campaign gives the cores the same rerun, so every per-run measure on them pairs as it does on the other specifications.

## Design

Profile `one-way-wall-cores` runs sweep T's `full`, `no-climb`, `no-remove` and `no-novelty` on the 10 cores at seeds 0-29, with `one-way-wall`'s configs (`configs-one-way`, 2 threads a run). Runs are seed-major, so the four arms of one (core, seed) share one load.

| Phase | Subjects | Seeds av2 / av3 | Jobs × threads, CPUs | Runs a host | Estimate |
|---|---|---|---|---|---|
| cores | 10 cores | 0–14 / 15–29 | 7 × 2 on 0–13 | 600 | about 1.2 h a host |

The seed split, jobs and CPU window are those of sweep F's small phases, which held the cores, and of `one-way-wall`. The estimate is the summed run time of the same four arms in sweeps F and T (about 16 h over both hosts), at 7 jobs. It is enqueued behind `2026-11-26-extra-seeds`. The contrast reads within this campaign, so the load need only be steady while it runs.

## After the runs

1. Collect `one-way-wall-cores` (1,200 rows; every (arm, core, seed) once).
2. Repairs: `scripts/check_repairs.py` (this campaign's copy) compares each run with its twin, `full` with sweep F's `no-gate` and each `no-<f>` with sweep T's, byte for byte in `repair_*.json` and the accumulated set. If any pair differs, report the cores that differ before the paper is changed: the pooled pass reads the twins.
3. Speed: compare the new `full` wall times with sweep F's, per host.
4. Catalogue: mark the set collected in `experiments/catalogue.toml` and render `CATALOGUE.md`.
5. Paper (`peredur-extension`): in `scripts/import_data.py`, take the cores' `full-wall` and `no-<f>` arms from this campaign, as `one-way-wall` gives them on the other specifications; their counters come from each run's `run.json`. In `scripts/summarise.py`, read the cores as one subject (the user's choice of unit and of whether whole rad stays is pending). Then `make import` and `make`.

## Log

- 2026-11-26: prepared.
