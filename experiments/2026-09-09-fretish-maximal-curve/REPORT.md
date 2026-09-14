# FRETISH maximality curves: `weighted` holds 1.98x the raw set and 1.54x the antichain

`2026-09-09-fretish-grading` crossed `selection_scheme` (`nsga2-apportion`, `weighted`) with `fitness.status_grading` (`mrs`, `aurus`) over five FRETISH families at 30 seeds, 600 runs at gen10/pop200. That campaign's report carries the paired contrasts on `found_repair`, `implies_ideal` and wall time, and they are not repeated here.

This pass read the same 600 run directories after the search and scored each run's accumulated set into *anytime curves*. Each curve holds four counts: every gate-passing candidate (`solutions`), the implication *antichain* over them (`maximal_solutions`), the candidates implying a known ideal (`ideal_solutions`), and the antichain members implying one (`maximal_ideal_solutions`). It also asks whether the offline readings agree with what each run reported at its end. Nothing was pre-registered and no `PLAN.md` was written, so every comparison below is description and tests no hypothesis.

## What ran

One `kind = "score"` phase through `campaign.py`, on av2 (avlab12), the host that ran the search, so the pass read the run directories in place. The declaration sits at `336838a`, and both `maximal` and `compare` report that commit with `dirty` false. The C++ tree there equals that of the search binary `c365aa5`, so the scorer and the search ran the same implication check. The pass started at 03:10:04 and finished at 03:25:34 on 2026-09-12, 15.5 min of wall at 8 workers of 4 cores each. All 600 runs scored, 0 failed and 0 left a partial curve; `failures.txt` is empty and `warnings.log` carries no WARN line.

`score_campaign.py` drove one invocation per run:

```sh
python3 scripts/score_curves.py --maximality --cuts 20 --jobs 4 --deadline-s 4500 --maximal-timeout 900 --compare-timeout 600 --out <out>/<run>.csv.part <run-dir>
```

`timings.txt` sums to 1.906 worker-hours over 600 lines, all exiting 0, at a mean of 11.44 s a run, a median of 5.0 s and a maximum of 311 s. No run approached the 900 s per-cut budget, the 4500 s deadline or the 5400 s wall cap. `fsm-timing` was the dearest family at 12.4 s to 29.8 s a run per arm, and `fsm` the cheapest at 1.3 s to 2.8 s. `campaign.py collect --curves` merged 600 per-run CSVs into 98,809 rows with no duplicates. The annotated tag `provenance/fretish-maximal-curve` holds `336838a`, cut before the close commit.

## How the curves count

`score_curves.py` draws 20 *log-spaced cuts* over each run's arrival times. At each cut one `maximal` process receives the previous cut's survivors plus the candidates that arrived since. A cut whose prefix equals the previous cut's is skipped and writes no row. The *running-antichain walk*, one pass over the arrival order, is not on this branch.

A gen10 run accumulates in a few bursts, so most of its cuts add nothing. 465 of 600 runs keep 9 to 11 of their 20 cuts, 225 of them keeping 10, while 3 keep all 20 and 5 keep 4 or fewer. The run with none is `weighted/aurus` `fsm-combined` seed 19, which accumulated nothing and is the one run of the campaign without a repair.

## Run-end means

Means a run at each run's end, 150 runs an arm, with the antichain's share of the raw set in the last column.

| arm | solutions | maximal | ideal | maximal ideal | antichain share |
| --- | ---: | ---: | ---: | ---: | ---: |
| `nsga2-apportion/mrs` | 93.49 | 26.78 | 4.46 | 1.98 | 28.6% |
| `nsga2-apportion/aurus` | 88.27 | 25.94 | 4.93 | 2.03 | 29.4% |
| `weighted/mrs` | 185.47 | 41.31 | 6.84 | 2.31 | 22.3% |
| `weighted/aurus` | 169.83 | 35.61 | 7.08 | 2.37 | 21.0% |

`weighted` holds 1.98x `nsga2-apportion`'s raw set under `mrs` and 1.92x under `aurus`. On the antichain those ratios fall to 1.54x and 1.37x, since a larger share of the weighted arms' candidates is subsumed by another candidate in the same set. The ideal-implying antichain moves least, reading 1.98 to 2.37 across the four arms.

## At fixed times

Curve means carried forward to each time, given as solutions / maximal / maximal ideal.

| time | `nsga2-apportion/mrs` | `nsga2-apportion/aurus` | `weighted/mrs` | `weighted/aurus` |
| --- | --- | --- | --- | --- |
| 0.5 s | 13.71 / 5.38 / 0.89 | 8.79 / 3.87 / 0.76 | 11.29 / 5.05 / 0.82 | 8.11 / 3.65 / 0.81 |
| 1 s | 37.20 / 12.83 / 1.24 | 25.18 / 9.66 / 1.19 | 40.80 / 13.01 / 1.25 | 23.63 / 8.71 / 1.13 |
| 2 s | 71.57 / 21.34 / 1.74 | 48.54 / 15.77 / 1.60 | 114.52 / 26.76 / 1.77 | 64.18 / 16.96 / 1.53 |
| 5 s | 92.86 / 26.67 / 1.98 | 75.15 / 22.53 / 2.00 | 183.96 / 41.15 / 2.31 | 140.56 / 30.13 / 2.27 |
| 10 s | 93.28 / 26.75 / 1.98 | 83.06 / 24.65 / 2.01 | 184.48 / 41.22 / 2.31 | 157.11 / 32.95 / 2.29 |

The two arms sharing a grading level track each other until about 1 s, at 37.20 against 40.80 raw under `mrs` and 25.18 against 23.63 under `aurus`. After that `weighted` pulls ahead on raw and maximal counts. The `mrs` arms have almost finished by 5 s, at 92.86 and 183.96 against run-end means of 93.49 and 185.47, while the `aurus` arms are still accumulating at 10 s. `maximal_ideal_solutions` spans at most 0.33 across the four arms at any time shown.

Median time to first repair is 0.26 s, 0.34 s, 0.27 s and 0.38 s in table order, reached on 150, 150, 150 and 149 runs, with the no-repair run the one censored row. Median time to first ideal-implying repair is 0.25 s, 0.30 s, 0.27 s and 0.30 s, reached on 88, 85, 84 and 82 runs, and 261 rows are *right-censored*. Each median covers only the runs that arrived, so the first-ideal median can sit below the first-repair one.

## Cross-checks against the search

**Ideal reach.** `maximal_ideal_solutions > 0` at run end agrees with the search's `implies_ideal` on 600 of 600 runs.

**Antichain size.** `maximal_solutions` at run end equals the search's `n_repairs` on 590 of 600 runs, 147, 149, 146 and 148 per arm in table order. On the other 10 the offline count is lower by 1 or 2, and every one of them logged implication timeouts inside the search, 1 to 46 a run and 139 in all. Nine are `fsm-combined`: `nsga2-apportion/mrs` seeds 5, 7 and 24, `nsga2-apportion/aurus` seed 9, `weighted/mrs` seeds 12, 16 and 20, and `weighted/aurus` seeds 17 and 21. The tenth is `weighted/mrs` `fsm-timing` seed 20, at 104 against 105. A timed-out check keeps both sides in the search, and the offline pass, with 900 s a cut, decided the pair. The offline antichain never exceeds the reported one and falls short only where the search could not decide a pair, which is the direction a correct pass must take.

## Cost against the declaration

`campaign.toml` priced a run at a mean of 3.5 s and the pass at about 35 core-minutes, off a probe of 20 runs at seed 0 scored at jobs 4 on the same host. The pass cost 11.44 s a run and 1.906 worker-hours, 3.3x the declaration. Set size does not account for it, the probe's accumulated sets averaging 137 and reaching 273 against 134.3 and 272 here, and the cause was not determined at close.

## Caveats

The analysis is post-hoc throughout. No curve comparison here tests a hypothesis, and the search's own paired contrasts belong to `2026-09-09-fretish-grading`.

`ideal_solutions` counts candidates implying one of the ideals under `examples/*/fixes` at `336838a`. No candidate on any arm implies `mode-arbiter`'s single ideal, and `fsm-timing` and `takeoff` reach an ideal on every run, so the ideal curves discriminate on `fsm` and `fsm-combined` alone.

The pass invokes `maximal` once per non-empty cut, so its 1.906 worker-hours price that mechanism and nothing here measures the running-antichain walk. Wall and worker-hour figures come from one pass on a shared host at 8 workers, not interleaved with anything.

## What is owed

A cost figure for the running-antichain walk against this pass, over these same 600 run directories, once that walk is on `main`. The directories are small enough that the comparison costs under two worker-hours.
