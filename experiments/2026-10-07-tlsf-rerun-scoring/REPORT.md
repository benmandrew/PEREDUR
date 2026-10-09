# TLSF re-run scoring: maximality and separation over 3000 runs in 89.9 worker-hours

This campaign scores the 3000 run directories of `2026-10-07-tlsf-rerun` in two phases. The maximality phase writes the anytime curves, with ideals, and the `.members.tsv` sidecars that list each run's maximal antichain. The recount phase reads those sidecars and counts epsilon-separated solutions at fingerprint seed 0 with 65,536 words. Each host scored the seeds it searched: av2 0-12 and 19-24, av3 13-18, av1 25-29. Every figure below comes from `PROVENANCE.json` and `analysis-output.txt` beside this file.

## Scorer

The scorer is `df219b9`: the paper re-run's scorer `0cfd14a` plus two changes. `032c2dc` tries an implication query with black for 300 ms, accepting only SAT, between a 200 ms SPOT attempt and the full SPOT budget. `1a1e86f` makes the walk's fingerprint prefilter configurable, and this campaign sets 4096 words. Neither can turn a refutation into an implication or the reverse, and `PLAN.md` records byte-identical event logs against `0cfd14a` on 11 archived runs. All three binaries (`maximal`, `compare`, `fingerprint`) are clean builds of `df219b9`. av2's seeds 0-12 were scored by an earlier entry at `48856b7`, which differs from `df219b9` in `PLAN.md` and `campaign.toml` alone. `provenance/tlsf-rerun-scoring` holds `df219b9`.

## Cost

The maximality phase took 63.05 worker-hours at 4 cores a worker, 252.2 core-hours allocated, with every run inside its 7500 s deadline. The longest run took 6474 s. The paper re-run's maximality pass took 183.9 worker-hours for 15 seeds on one host, with 57 runs at the deadline. The recount took 26.84 core-hours, the longest run 460 s against a 900 s deadline, against about 20 core-hours archived for the paper re-run. Neither phase recorded a failure or a non-zero return code. Each phase warned once for each of the 70 run directories killed at the search cap, which carry no `run.json`; the scorer reads those runs from their accumulators.

## Results

At the 7200 s cut, averaged over all 750 runs of an arm with a run lacking the metric counted as zero:

| Metric | nsga2-apportion/mrs | nsga2-apportion/aurus | weighted/mrs | weighted/aurus |
|---|---|---|---|---|
| maximal_solutions | 28.49 | 19.49 | 86.27 | 54.76 |
| maximal_ideal_solutions | 1.87 | 1.75 | 2.07 | 1.68 |
| eps_maximal_solutions_0.05 | 17.25 | 13.10 | 44.25 | 31.46 |
| eps_maximal_solutions_0.2 | 12.76 | 9.66 | 32.35 | 23.33 |
| eps_maximal_solutions_0.5 | 7.84 | 5.97 | 18.85 | 13.74 |

`analysis-output.txt` also gives `solutions`, `ideal_solutions` and the epsilon counts over all solutions.

## Deviations

The scoring entries on all three hosts were cancelled while still queued at about 16:57 on 2026-10-08, with the search entries ahead of them, and re-enqueued later; no scoring work was lost. The search's own restart at that time did leave 63 run directories whose accumulator index holds two attempts, which the scorer counts twice. The search archive measures the effect: at 7200 s the `solutions` mean rises by up to 9.23 a run (`weighted/mrs`) and `ideal_solutions` by up to 0.053. The maximal and epsilon counts read each file once, but 13 of the 63 runs diverged from their killed attempt, whose 1044 orphaned files can still sit in those runs' antichains. Re-scoring the 63 runs with each index cut to its last attempt is owed.
