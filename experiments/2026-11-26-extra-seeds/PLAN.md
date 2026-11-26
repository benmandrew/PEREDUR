# 2026-11-26-extra-seeds

The specification pooled pass leaves out `fsm-lmcps`, since every Pareto arm repairs it in 1 of seeds 0-29 (seed 15 in every arm), and pooled over the 30 seeds the arms hold 2-9 repairs. The seeded pooled pass leaves out 4 fsm-lmcps mutants (`sw-fsm-lmcps__paper-v2__m01`, `m02`, `m06`, `m07`), where no arm repairs any of 40 runs (8 arms x 5 seeds). The user approved more seeds for both on 2026-11-26. The new runs feed the pooled passes only; the paper's per-run aggregates keep seeds 0-29 (specifications) and 0-4 (seeded corpus).

## Design

Profiles `extra-seeds` (fsm-lmcps) and `extra-seeds-seeded` (the 4 mutants). Arms: sweep F's `base`, `only-climb`, `only-remove`, `only-novelty` and `no-gate` (the paper's `full`), sweep T's `no-climb`, `no-remove` and `no-novelty`, and on fsm-lmcps sweep F's `random` (`random/sweep_F_base`), so the random-against-base pass can take the subject too. The generated configs are byte-identical to `configs-final-engine` and `configs-one-way` (checked 2026-11-26), and run directories keep sweeps F and T's names.

| Phase | Subject | Seeds av2 / av3 | Runs a host | Jobs x threads, CPUs | Estimate |
|---|---|---|---|---|---|
| lmcps | fsm-lmcps | 30-179 / 180-329 | 1,350 | 15 x 2 on 0-29 | about 1.4 h |
| seeded | 4 mutants | 5-17 / 18-29 | 416 / 384 | 15 x 2 on 0-29 | about 40 min |

Load does not matter here: the pooled passes read repairs, not wall time, and fsm-lmcps's repairs matched byte for byte between sweeps F and W at different loads.

## After the runs

1. Collect both profiles. Count, per arm, the repairing runs and the members pooled over all seeds.
2. fsm-lmcps joins the specification pooled pass if every arm holds at least 4 members from at least 2 seeds; a seeded mutant joins the seeded pass if every arm holds at least 4 members. Report the counts to the user before running any pooled pass.
3. Pooled passes on the new subjects only, on the 8 arms (and random against base on fsm-lmcps), with the groups of `2026-11-25-one-way-pooled` extended by these runs. Whether a single-subject pass can be merged into the existing reduced outputs, or the whole pass must rerun, is checked before it is designed.

## Log

- 2026-11-26: prepared.
