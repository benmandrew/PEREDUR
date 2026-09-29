# lpc-smoke

## Question

Can monolithic `peredur` repair the two Lift-Plus-Cruise (LPC) specifications directly? The alternative is the per-core route the ventilator and RAD needed: screen out each unrealisable core and repair its sub-spec on its own.

## Design

One cell at the shipping configuration: `nsga2-apportion` selection, `mrs` status grading, gen10/pop200, log metric, `max_wall_s = 1800`. It runs over `lift-plus-cruise-mini` (48 guarantees, no assumptions) and `lift-plus-cruise-full` (59 guarantees, 17 assumptions) at seeds 0–9, so 20 runs on av3 at `jobs = 4`. Both specs carry the paper's own fix as their ideal, `fixes/paper-reach-hover-11.json` and `fixes/paper-wind-20.json`, so `implies_ideal` is readable.

## Decision rule

Read per spec, on `found_repair` over its 10 seeds:

- **Directly repairable.** At least one seed finds a repair. `implies_ideal` and `n_repairs` are then reported as descriptive figures.
- **Needs per-core repair.** No seed finds one, and the runs finish inside the deadline. The follow-up is a `mucs` screen of that spec on av3, then monolithic repair of each core.
- **Inconclusive.** No seed finds one, and the runs hit `max_wall_s`. The budget is then the question, and the spec is re-run at a larger one before any core screen.

`wall_time_s` is reported alongside, since no run cost for either spec has been measured.
