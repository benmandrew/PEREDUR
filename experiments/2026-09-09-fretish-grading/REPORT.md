# Selection scheme crossed with status grading on FRETISH: null on reach, 1.46x and 1.18x on cost

This campaign asks whether `2026-08-28-selection-grading` replicates on the FRETISH path. That campaign crossed `selection_scheme` (`nsga2-apportion` against `weighted`) with `fitness.status_grading` (`mrs` against `aurus`) over the Temporal Logic Synthesis Format (TLSF) corpus. `nsga2-apportion` ranks by non-dominated sorting genetic algorithm II (NSGA-II) fronts, and `weighted` ranks by one weighted scalar. `mrs` grades status by the *maximum realisable subset* (MRS) walk, and `aurus` reproduces the six-level ladder of AuRUS, the baseline repair tool.

This one runs the same 2x2 over the five FRETISH families (`fsm`, `fsm-combined`, `fsm-timing`, `mode-arbiter`, `takeoff`) at seeds 0-29. That is 600 runs at `generations = 10` and `population_size = 200`, all from one binary at `c365aa5` on av2. Its C++ tree equals `main` at `5c94d66`. Every arm shares a 1800 s `genetic.max_wall_s`, the log metric, fitness weights 0.1/0.2/0.7 and `--pin-vintage`. `accumulate_repairs` and `run_implication` are both on, so the *streaming maximality filter* runs during the search. The configs come from the `configs` command in `campaign.toml`.

No `PLAN.md` was written before launch. `campaign.toml` names `found_repair`, `implies_ideal`, `n_repairs` and `wall_s` as endpoints, read per cell, and registers no primary, alpha or decision rule. Every p-value below is therefore *post-hoc*. It describes this sample and tests no hypothesis. Both factor contrasts are paired on the `(spec, seed)` cell.

## Checks

`stopped_by` reads `generations` on all 600 manifests. The longest run took 102.12 s against the 1800 s deadline and the 4050 s harness cap. Neither bound any run, so no run is *censored*. All 600 `run.json` files name `c365aa5` with `dirty` false. `input_screen` is null on all 600, so every input passed every correctness check. The queue log reads "Done. 600 runs (600 rows) in 20.1 min, 0 errors.", and each arm holds 150 rows over seeds 0-29 with no duplicates.

One run of 600 found no repair. It is `weighted/aurus` on `fsm-combined` seed 19, which took 10.31 s and wrote no `accumulated/` index at all. It is the single `found_repair` discordant pair in both contrasts below.

## The four arms

| arm | `found_repair` | `implies_ideal` | median wall | mean wall | median `n_repairs` | mean accumulated set |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `nsga2-apportion/mrs` | 150/150 | 88/150 | 3.13 s | 3.55 s | 25 | 93.49 |
| `nsga2-apportion/aurus` | 150/150 | 85/150 | 4.90 s | 10.46 s | 25.5 | 88.27 |
| `weighted/mrs` | 150/150 | 84/150 | 3.98 s | 4.77 s | 38.5 | 185.47 |
| `weighted/aurus` | 149/150 | 82/150 | 7.18 s | 10.95 s | 32 | 169.83 |

The *accumulated set* column counts the lines of `accumulated/index.tsv` less its header. That is one line per arrival, and it is what `score_curves.py` counts as solutions. `n_accumulated_repairs` in `run.json` is a different quantity. It is `AccumulatorStats::n_contributed`, the accumulated specifications the final population did not already hold, and it reads a mean of 40.3, 41.2, 143.6 and 135.4 in the arm order above. Every set size in this report comes from `index.tsv`. Arrivals also exceed documents slightly: `nsga2-apportion/mrs` holds 13,776 byte-distinct repair files among 14,024 arrivals, since a tombstoned guarantee is omitted from a repair's JSON.

## Both factors are null on reach

Each count reads pairs where only the first level succeeded against pairs where only the second did. Every p-value is a two-sided *exact McNemar test*.

| contrast | `found_repair`, 300 pairs | `implies_ideal`, 300 pairs | `implies_ideal` within each level of the other factor, 150 pairs |
| --- | --- | --- | --- |
| `nsga2-apportion` against `weighted` | 1 to 0, p = 1.0000 | 15 to 8, p = 0.2100 | `mrs` 7 to 3, p = 0.3438; `aurus` 8 to 5, p = 0.5811 |
| `mrs` against `aurus` | 1 to 0, p = 1.0000 | 12 to 7, p = 0.3593 | `nsga2-apportion` 6 to 3, p = 0.5078; `weighted` 6 to 4, p = 0.7539 |

Neither factor moves either endpoint, pooled or within either level of the other. `found_repair` sits at 599 of 600, and its within-level reads are 0 to 0 or 1 to 0 at p = 1.0000. Both `implies_ideal` counts lean towards the first level. The smallest of the six p-values is 0.2100.

## Only `fsm` and `fsm-combined` discriminate

Arm cells read `implies_ideal` over `found_repair`. The contrast columns give the discordant `implies_ideal` pairs and the family's median paired wall ratio, second level over first.

| family | `nsga2-apportion/mrs` | `nsga2-apportion/aurus` | `weighted/mrs` | `weighted/aurus` | selection | grading |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| `fsm` | 28/30 | 23/30 | 22/30 | 22/30 | 13 to 6 (1.019) | 10 to 5 (1.220) |
| `fsm-combined` | 0/30 | 2/30 | 2/30 | 0/29 | 2 to 2 (1.124) | 2 to 2 (2.559) |
| `fsm-timing` | 30/30 | 30/30 | 30/30 | 30/30 | 0 to 0 (1.414) | 0 to 0 (1.234) |
| `mode-arbiter` | 0/30 | 0/30 | 0/30 | 0/30 | 0 to 0 (0.969) | 0 to 0 (3.645) |
| `takeoff` | 30/30 | 30/30 | 30/30 | 30/30 | 0 to 0 (2.040) | 0 to 0 (1.258) |

Every discordant `implies_ideal` pair in both contrasts falls on `fsm` or `fsm-combined`. Restricted to those two families, 120 pairs a contrast, the counts and p-values equal the full 300's. `fsm-timing` and `takeoff` read 30/30 on every arm and cannot move. `mode-arbiter` reads 0/30 on every arm, with all 120 of its `best_relation` values incomparable. The *effective sample* is therefore 120 pairs a contrast, holding 23 discordant pairs for selection and 19 for grading.

## The factors separate on cost

Each ratio is the second level's wall over the first's, a median over pairs, with an exact *sign test* on the direction.

| contrast | median wall ratio | first level faster | first level slower | tied | sign test |
| --- | ---: | ---: | ---: | ---: | --- |
| `weighted` over `nsga2-apportion` | 1.177 | 203 | 95 | 2 | p = 3.69e-10 |
| `aurus` over `mrs` | 1.464 | 278 | 21 | 1 | p = 1.97e-58 |

`aurus` grading is slower on 278 of 300 pairs. Its per-family ratio runs from 1.220 on `fsm` to 3.645 on `mode-arbiter`. The accumulated set does not account for it, `nsga2-apportion/aurus` holding 88.27 arrivals a run against `nsga2-apportion/mrs`'s 93.49. The close did not trace where that time goes.

`weighted` selection is slower on 203 of 300 pairs, from 0.969 on `mode-arbiter` to 2.040 on `takeoff`. The weighted arms hold 1.98x `nsga2-apportion`'s arrivals within `mrs` and 1.92x within `aurus`. They also end on a larger *antichain*, at a pooled `n_repairs` median of 35 against 25. The implication filter runs over that larger set, and its solver comparisons read 123,909 and 121,598 under `weighted` against 46,552 and 50,835 under `nsga2-apportion`. Its timeouts read 127 and 158 against 70 and 71.

Both directions sit far outside the noise. The magnitudes are less secure. The campaign ran at `jobs = 4` on a shared host, with the arms not interleaved per case, so each ratio carries host-load drift of unknown size.

## Against the TLSF twin

The twin ran 25 TLSF families at 6 seeds, `generations = 500` under a 400 s `max_wall_s`, with a 3600 s external cap. It lost 245 of 600 manifests to that cap, 214 of them on `weighted` pairs. Its recorded endpoint read 145 to 5 for `nsga2-apportion`. Its 2026-09-03 addendum re-read both factors off the accumulator and found both null, selection 14 to 18 (p = 0.5966) and grading 15 to 7 (p = 0.1338).

This campaign lost no manifest, and its recorded endpoint is null as it stands. The score campaign `2026-09-09-fretish-maximal-curve` read these run directories after the search finished. Its cross-check finds `maximal_ideal_solutions` agreeing with `implies_ideal` on 600 of 600 runs. The recorded and accumulator readings therefore coincide here, and both match the twin's corrected reading: neither factor changes what the search reaches.

`weighted/mrs` held 10.6x `nsga2-apportion/mrs`'s solutions by 400 s on the twin, and 1.98x at run end here. The twin's weighted arms were censored by an implication filter quadratic in that set, which its deadline did not bound. At gen10/pop200 with the streaming filter, that cost does not arise. The two campaigns share neither a stopping rule nor a search budget, so their figures sit side by side without pairing.

## Cost against the calibration probe

A 20-run probe on av2 on 2026-09-12, against `dbf284b`, crossed both factors over all five families at seed 0, four concurrent. It read a mean of 7.3 s and a worst case of 37.0 s. `campaign.toml` priced the 600 runs at 1.2 core-hours from it. That came to about 18 minutes of wall at `jobs = 4`, and about 23 with the 27% *saturation margin*. The campaign measured a mean of 7.43 s a run over the four arms, 1.24 core-hours summed and 20.1 minutes of wall.

The probe also gave the first FRETISH reading of the sampled-word *implication prefilter*. It refuted 122,945 of 135,578 ordered directions (90.7%), with 3 implication queries timing out across the probe.

## The cancelled first launch

The campaign was first declared at gen40/pop1000 with a 7200 s `max_wall_s`, over av2 seeds 0-14 and av3 seeds 15-29. It was queued as entry 021 on each host at `7eb3a53`. The 2026-09-11 cost audit read the first 94 manifests at a median of 884.4 s a run. Both 021 entries were cancelled while running at 14:25 that day, and the curve campaign's two 022 entries at `30ad56c` were cancelled while still queued. The launch left 45 rows and 49 run directories on av2, and 67 rows and 71 run directories on av3, under `results-gradsel-fret`. They come from another operating point and another binary, and were neither collected nor merged.

The branch was rebased onto `main` on 2026-09-12 and re-declared at gen10/pop200 under the results stem `results-gradsel-fret-m`. av3 had no route to host at the relaunch, and the measured cost fitted all 600 runs on one host, so every seed moved to av2. The rebase left `7eb3a53` unreachable from any branch. The annotated tag `provenance/fretish-grading-prerebase`, cut on 2026-09-12, holds it so the cancelled manifests still resolve. `provenance/fretish-grading` holds `c365aa5`, cut on 2026-09-14 before the close commit, for the case where the branch is rebased or split before it merges. Neither tag may be deleted.

## Caveats

Every figure here is post-hoc, with no endpoint, alpha or decision rule registered. Three of the five families carry no information about either factor. The effective sample for `implies_ideal` is 120 pairs a contrast, holding 23 and 19 discordant pairs.

`n_repairs` and `n_implies` follow the 2026-09-11 whole-specification implication check. They do not compare with FRETISH archives closed before it. The weights 0.1/0.2/0.7 are AuRUS's published triple and the binary default since 2026-09-11, and only the `weighted` scheme reads them. The selection factor is therefore NSGA-II against that one *scalarisation*, as on the twin.

A timed-out implication check reads as non-implication. The search's filter timed out 426 times over 43 runs, none of them on `fsm`. On 10 of those runs, 9 on `fsm-combined` and 1 on `fsm-timing`, with 1 to 46 timeouts each and 139 in all, `n_repairs` exceeds the offline maximal count by 1 or 2. On the other 33 it equals the offline count. `2026-09-09-fretish-maximal-curve` records that comparison under `cross_checks`.

The comments in `campaign.toml` call gen10/pop200 the TLSF twin's operating point and quote a twin median of 30.1 s a run. The twin ran `generations = 500` under a 400 s deadline, so both statements are wrong, and the 30.1 s figure has no traceable source. The declaration stays as the host read it.

No "Config vintage" entry is owed. The configs were generated with `--pin-vintage`, and no C++ default moved between the run and the close.

## Reproducing

Check out `c365aa5` and build the release preset. Generate the configs with this directory's `scripts/gen_configs.py`, using the `configs` command in `campaign.toml`. Then run `scripts/run_experiments.py --profile gradsel-fret` over seeds 0-29 at `jobs = 4`. Every figure above comes from `python3 scripts/analyse_fretish_grading.py experiments`, run from the repository root with this campaign's results and `2026-09-09-fretish-maximal-curve`'s `curves-gradsel-fret-m/` in place. Its output is `analysis-output.txt`.

## What is owed

More seeds on `fsm` and `fsm-combined` are owed, those being the two families where either factor can move `implies_ideal`. At about 7 s a run, 120 further seeds a family cost under an hour of wall on one host.

An interleaved wall-time measurement is owed before the 1.46x cost of `aurus` grading on FRETISH is quoted as a ratio. Until then the sign test's direction is the result.
