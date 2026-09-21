# FRETISH MUC repair on the ventilator, RAD and VALU3S UC6

Registered 2026-09-21, before launch.

## Question

How does MUC repair mode, new on the FRETISH path in `feat/fretish-muc` (`5a41203`, `c345203`, `d002997`, and `0cde635`, a gcc build fix with no change in behaviour), perform on three FRETISH specifications imported from FRET exports: `ventilator` (114 guarantees), `rad` (74 guarantees, 3 modes) and `valu3s-uc6` (12 guarantees)? All three are unrealizable under `realize`. Monolithic repair does not finish on the ventilator: on 2026-09-18 two generations of 20 took 1,599 s and returned nothing. The campaign also reads whether the two factors of `2026-09-09-fretish-grading` move anything under MUC mode.

## Design

The design is `2026-09-09-fretish-grading`'s 2x2, `selection_scheme` (`nsga2-apportion`, `weighted`) crossed with `fitness.status_grading` (`mrs`, `aurus`), at its operating point: gen10/pop200, `genetic.max_wall_s = 1800`, log metric, weights 0.1/0.2/0.7, `--pin-vintage`, `accumulate_repairs` and `run_implication` on. The one change is `tlsf.repair_mode = "muc"`, with `muc_max_iterations` (32) and `muc_screen_depth` (3) at the binary's defaults. The run covers 3 specs × 4 cells × 30 seeds = 360 runs, with seeds 0–14 on av2 and 15–29 on av3, at `jobs = 4` on each host. Both factors are crossed within each host, and the runner interleaves arms and specs within each seed.

## Endpoints

Read per run, per cell and per spec:

- **Found**: `found_repair`, meaning at least one repair that a whole-spec `ltlsynt` call confirmed realizable.
- **Provisional**: `n_provisional > 0` in `run.json`. The run wrote at least one reintegrated spec whose whole-spec check was undecided but whose every guarantee subset of up to 3 was decided realizable. It never counts as Found.
- **Exhausted**: 1 − Found.
- **Wall**: `wall_time_s`, mean and median.
- Also recorded: `n_repairs`, `n_gate_undecided` and `n_deadline_unscreened` from `run.json`, and the cores extracted, read from `run.log`.

`implies_ideal` is not an endpoint. None of the three specs has `fixes/`, so compare fails and the column reads 0 by construction.

## Tests

Each factor is a paired contrast on the `(spec, seed)` cell, pooled over the three specs and over the other factor's two levels: 360 runs, 180 pairs per contrast. The primary test is an exact two-sided McNemar test on Found. The secondary test is an exact two-sided sign test on wall time. Alpha is 0.05 for each test, uncorrected, and every other reading is descriptive. A spec at 0 or at 100% Found in every cell contributes no discordant pairs, and the report says so rather than reading its absence as a null.

## Decision rule

None. MUC mode is not the default and this campaign does not propose making it so. The output is the two tables of the paper's Tables 6 and 7 shape: per configuration (Found, Provisional, Exhausted, mean and median wall) and per spec and configuration.

## Known threats, stated before the data

- **The ventilator runs are deadline-bound.** A single-run measurement at `max_wall_s = 600` ended at 601 s with 0 confirmed repairs and 55 candidates left unscreened at the deadline. Ventilator results therefore depend on how many `ltlsynt` calls fit in 1,800 s, and so on host load. The two hosts are identical (32 cores, 125 GB) and idle at launch, and the arms are interleaved, so load falls on every arm alike. Wall-time contrasts on the ventilator measure nothing but the deadline.
- **One-guarantee cores cannot be deleted.** Under MUC mode the rule against removing the last live guarantee applies to the core's own sub-spec, so a one-guarantee core (RAD's `{10}`, the ventilator's `{68}`) can be weakened but never removed. Monolithic mode can remove it. On 2026-09-21 RAD's first core `{10}` could not be repaired at seed 0, and the loop stopped there without trying RAD's other cores.
- **The search stops at the first core it cannot repair**, so a spec's Found reads the first unrepairable core, not the spec as a whole.
- A timed-out implication check reads as non-implication, as in every FRETISH campaign since 2026-09-11.

## Budget

12 hours of wall-clock, set by the user. The measured single runs were: ventilator bounded by the deadline, about 1,800 s plus up to about 2 minutes of in-flight overrun; RAD 75 s on a loaded box; `valu3s-uc6` 17 s. Per host that is 60 ventilator runs × about 1,950 s / 4 slots ≈ 8.1 h, plus about 0.5 h for the other two specs, or about 8.6 h. The harness cap is 4,050 s a run.

## Launch record

The first enqueue at `4dbc77f` failed to build on both hosts: gcc 11 at `-O3` reported `-Werror=maybe-uninitialized` at `src/repair/evolution.cpp:175`, and the freshness gate refused the stale binary. Both entries were dequeued after 2 of 3 attempts, having written no rows. `0cde635` removes the `std::optional` gcc misreads, and the campaign was re-enqueued at the commit that adds this record. Endpoints, tests and design are unchanged. `provenance/fretish-muc` holds `4dbc77f`, and `provenance/fretish-muc-relaunch` holds the relaunch commit.
