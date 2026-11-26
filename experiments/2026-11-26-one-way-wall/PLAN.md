# 2026-11-26-one-way-wall

The paper's leave-one-out wall times on the specifications compare sweep T's `no-<f>` arms with sweep F's `full` (its `no-gate`), and the two sweeps ran at different host speeds. On av3, sweep T ran 15 jobs against sweep F's 7, and every tool's per-call time was 29-39% higher. On av2, both ran 7 jobs, but sweep T was still 7-10% faster per call. The paper therefore prints no leave-one-out wall time on the specifications, and drops sweep T's load-affected seeds from its per-run data. The user approved this rerun on 2026-11-26.

## Design

Profiles `one-way-wall` (the 9 smaller specifications) and `one-way-wall-large` (rad) run sweep T's `full`, `no-climb`, `no-remove` and `no-novelty` in one campaign. Sweep T's `full` config equals sweep F's `no-gate` apart from its name and comments (checked 2026-11-26). Runs are seed-major, and the four arms of one (spec, seed) run next to each other, so they share one load. rad's cores are left out, since only the pooled pass reads them.

| Phase | Subjects | Seeds av2 / av3 | Jobs × threads, CPUs | Slot-h (both hosts) |
|---|---|---|---|---|
| small | 9 specs | 0–14 / 15–29 | 7 × 2 on 0–13 | about 44 |
| rad | rad | 0–4 / 15–19 | 7 × 4 on 0–29 | about 2 |

These match sweep F's host split, jobs and CPU windows. Estimates come from sweep F's `full` and sweep T's av2 seeds, scaled to 30 seeds. Both hosts were otherwise idle at launch, whereas veribee held CPUs 14–29 during sweep F, so the new `full` need not match sweep F's wall times exactly. The contrast reads within this campaign.

## Checks after the runs

1. Repairs: every run's `repair_*.json` and accumulated set should be byte-identical to sweep T's run of the same (arm, spec, seed), and the new `full` to sweep F's `no-gate`. Runs end after a fixed number of individuals, so only a time-dependent call (BLACK's 30 s timeout, the controller store gate) could change a repair. If all are identical, the pooled passes stand. If not, report the subjects that differ before anything else is run.
2. Speed: compare the new `full` wall times with sweep F's, per host.

## Log

- 2026-11-26: prepared at this commit.
