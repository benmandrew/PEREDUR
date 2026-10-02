# The AuRUS baseline re-run at a fork that keeps its output: parity again at p = 0.7214, and 118 of 175 capped runs now keep what they found

Every head-to-head before this campaign reused one AuRUS arm, `2026-08-14-aurus-h2h`. Upstream AuRUS writes its solutions in one batch after its search loop, so a *Java virtual machine* (JVM) killed at the 7200 s cap took them all with it, and 173 of that arm's 780 runs scored zero for that reason alone. This campaign re-ran the arm at `e1cfadf`, branch `output-on-timeout` of `benmandrew/AuRUS`, which writes each solution as it is found and logs its elapsed time to the microsecond in `solution-times.csv`. Its parent `3f6f01f` is upstream as published. The search is the same under both commits, and `PLAN.md` section 2 registers the change as output alone.

The arm is the archive's design: 26 *Temporal Logic Synthesis Format* (TLSF) families at 30 repeats, 780 runs, `-Max=1000 -Gen=1000 -Pop=100 -k=20 -addA -geneNUM=0 -factors=0.7,0.1,0.2`, `-onlyInputsA` on nine families, `-GATO=7200` and a harness kill at 7500 s. It ran as a `kind = "aurus"` phase at concurrency 14 from `feat/aurus-rerun` at `5d2f796`, through queue entries av2 033 and av3 031, with av2 taking repeats 0 to 14 and av3 15 to 29. An earlier attempt at `21feb84` (av2 031, av3 029) is recorded as cancelled and wrote no row. av2 ran from 2026-09-22T19:51:10 to 2026-09-23T12:00:21 over 16.15 h, and av3 from 19:50:02 to 11:51:44 over 16.03 h, against the plan's estimate of 16.4 h a host. av3's clock runs fast with network time off, so its stamps are the looser pair.

No PEREDUR search ran here. The PEREDUR side of every contrast is the `nsga2-apportion/mrs` cell of `2026-09-14-paper-rerun`, run at `57fcefb` eight days earlier. Scoring is two further campaigns with their own archives: `2026-09-22-aurus-rerun-curves` for separation and maximality, and `2026-09-23-aurus-rerun-anytime` for per-solution verdicts and the well-separation screen. The decision was computed in the paper repository, `~/projects/writing/counter-paper/data/rerun-2026-09-uncensored`, and its printout is archived here as `analysis-output.txt`.

## What the fork recovered

175 of 780 runs were killed at the cap, 88 on av2 and 87 on av3, against the archive's 173. The kills fall on the same six families: `full-arbiter-aurus`, `humanoid-531` and `pcar-v2-888` on 30 each, `prioritized-arbiter-aurus` and `humanoid-503` on 29, and `humanoid-741` on 27. The archive's one kill outside them, a `lily02` repeat, has no counterpart here. 118 of the 175 killed runs kept their output, 5,080 solutions in all. The other 57 hit the cap before writing a first solution, which is a search result, and the adapter skips them as `no-solutions`.

708 runs materialised as scorable run directories holding 296,300 candidate files, 148,652 on av2 and 147,648 on av3. Every one of the 708 was dated from the fork's own `solution-times.csv`, and none fell back to the run-log inference that rounded to the second. The killed rows all record `exit_code` -9 and a wall time of 7500.0 s (171 rows) or 7500.01 s (4 rows), the 7200 s budget plus the harness's 300 s grace. A consumer clamps them to 7200 s and reads cap status from the `killed` column. `n_solutions` is blank on exactly those 175 rows, being scraped from a summary AuRUS prints only when its loop returns.

| family | killed, archive | killed, now | runs materialised | candidates |
|---|---|---|---|---|
| `full-arbiter-aurus` | 28 | 30 | 24 | 69 |
| `humanoid-503` | 30 | 29 | 29 | 97 |
| `humanoid-531` | 30 | 30 | 5 | 86 |
| `humanoid-741` | 30 | 27 | 30 | 5,365 |
| `pcar-v2-888` | 25 | 30 | 4 | 6 |
| `prioritized-arbiter-aurus` | 29 | 29 | 29 | 285 |
| `lily02` | 1 | 0 | 29 | 10,812 |

`humanoid-741` has no `examples/humanoid-741/fixes/` directory, so it carries no ideal and sits outside the 25-family endpoint. Its 30 runs appear in every pass as `no-ideals-dir`, and the paper filters it out of the curve inputs.

## The registered endpoint

`PLAN.md` section 4 registers per-family `implies_ideal` over the 25 families of `H2H_TLSF_READY`, PEREDUR's `nsga2-apportion/mrs` cell against this arm, read with an exact two-sided *Wilcoxon signed-rank* test at alpha 0.05, a capped run counting as a failure on both sides. `analysis-output.txt` reads PEREDUR higher on 12 families, lower on 8 and tied on 5, mean difference +0.012, p = 0.7214. Against the archived arm, `2026-09-14-paper-rerun` read the same 12, 8 and 5 at +0.003 and p = 0.9345.

The decision is Outcome 3, null, reported as parity. PEREDUR leads by 0.667 on `full-arbiter-aurus` and `prioritized-arbiter-aurus`, where AuRUS implies an ideal on 0 of 30 runs, and by 0.133 on `humanoid-531`, `lily02` and `load-balancer-aurus`. It trails by 0.500 on `gyro-var2`, 0.433 on `lily11` and 0.333 on `lift`. The analyser prints AuRUS's rates unscreened for well-separation; the paper applies the screen downstream.

The registered secondary restricts the test to the families the archive never censored. `analyse_matched.py` does not print it, so it was computed at close from the per-family table in `analysis-output.txt` with the analyser's own `wilcoxon_exact_p`. The plan counts 20 such families, removing the five censored families inside the 25. That reads PEREDUR higher on 7, lower on 8 and tied on 5, mean -0.063, p = 0.3369. Strictly the archive also killed one `lily02` repeat, which leaves 19 families: higher on 6, lower on 8, tied on 5, mean -0.074, p = 0.2235. Both are null, as section 2 predicted for families where only the dating changed. The analyser's printed `scorable runs only` read is the rematch's secondary, and gives 13, 7 and 5 at p = 0.6280.

In every outcome `PLAN.md` section 5 requires a supersession row naming `2026-08-14-aurus-h2h` and every campaign that reused it. That row is now in `experiments/README.md`. It names `2026-08-21-aurus-h2h-ship`, `2026-08-29-aurus-matched`, `2026-09-04-aurus-rematch`, `2026-09-14-paper-rerun` and `2026-09-14-paper-rerun-curves`.

## The arm against the archive

The paper's AuRUS row in `paper/generated/ablation-aurus.csv` moved as follows between `1fc2134^` and `1fc2134`, over the 750 runs of the 25 endpoint families.

| measure | archive | this arm |
|---|---|---|
| found a repair | 0.777 | 0.824 |
| implies an ideal | 0.544 | 0.536 |
| exhausted the budget with nothing | 0.205 | 0.096 |
| median wall time | 298.2 s | 179.85 s |
| runs flagged capped | 154 | 163 |
| curve denominator | 678 | 750 |
| runs lost at the cap | 72 | 0 |

The curve denominator was 596 scored runs plus 82 padded in the archive. It is now 678 scored plus the 72 `no-solutions` runs entered as flat zeros. `implies_ideal` barely moved, because the recovered runs sit on families where AuRUS rarely reaches an ideal. Summed wall time fell from 457.9 h to 426.3 h, with the killed runs costing 360.4 h then and 364.6 h now. Finished runs alone fell from a median of 164.55 s to 75.04 s. The fork changes output alone, and that drop was not investigated.

## Defects found at close

Re-reading the rows at close turned up four defects that no earlier record of this campaign carried.

**Quick exits.** 15 runs that were not killed produced nothing. Each exited 0 with `aurus_time_s` 0 after 0.5 s to 40.2 s, and each `run.log` on the hosts reads `Initial specification is: unknown` and then `The specification is inconsistent`. The one read in full, `detector-aurus` repeat 0, first prints SAT-solver warnings of a DIMACS header mismatch. 9 of the 15 are repeat 0 or repeat 15, the first of a host's split. The cause was not investigated. Every read scores them as failures.

**The capped flag in the paper.** The paper's `prepare.py` enters every `no-solutions` run at `AURUS_CAP_S` with `wall_capped = 1`. Its 163 capped runs are therefore the 148 killed runs inside the 25 families plus these 15, and `TimeoutsPrioritised` reads 30 where the family has 29 killed runs. The paper's `PROVENANCE.txt` line "capped runs 154 to 72" matches neither file. No registered figure changes, the endpoint scoring a quick exit and a kill alike.

**Memory.** `peak_rss_mb` peaks at 3.5 MB over all 780 rows against an 8 GB heap reservation, and reads 0.0 on 14 of the 15 quick exits. The sampler read some process other than the JVM, and this arm has no usable memory figure.

**Float ties in the test.** `wilcoxon_exact_p` receives differences of float rates and `signed_ranks` groups ties with `==`, so 0.0333…326 and 0.0333…333 rank apart. On exact differences the registered p is 0.7348, the 20-family secondary 0.3369 rather than 0.3370 and the 19-family one 0.2235 rather than 0.2224. No outcome moves. The registered figure stays the analyser's printed 0.7214, and the defect in `analyse_matched.py` at `57fcefb` is left for a separate fix.

## Provenance

`provenance/aurus-rerun` is an annotated tag at `5d2f796`, cut and pushed on 2026-10-01 before the close commit. The AuRUS commit `e1cfadf` is held by the pushed branch `output-on-timeout`, and `COMMIT.txt` on both hosts names it. The six data files beside this report were checked by `md5sum` against the hosts on 2026-10-01 and are identical. `scripts/` vendors the five drivers that ran, the three the close procedure requires, the analyser pair at `57fcefb`, and `stage.py` and `run_analysis.py` from the paper repository, each with its blob in `PROVENANCE.json`.

The re-run moved the arm's output and left the registered verdict where it was. What it changed is which claims about AuRUS's capped families rest on measurement rather than on lost files.
