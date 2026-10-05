# 2026-10-05-maoz-coverage

## 1. Registration

This campaign has no registration of its own. Section 11 of `experiments/2026-10-02-maoz-baselines/PLAN.md` is its registration: the admissibility screen (11.1), the frontiers (11.2), the coverage endpoint under both readings of an undecided pair (11.3) and the decision rule (11.5). This file records only how those sections are run.

## 2. What runs

Each phase is one pass of `scripts/maoz_score.py`, declared as a `kind = "maoz-score"` phase in `campaign.toml`. All four run on av2, in order.

- **screen.** Each tool repair in `experiments/results-maoz-baselines` goes through `realize` and through `check_well_separated.check_one`. Output: `experiments/maoz-coverage-screen/screen.csv`.
- **frontier.** Per (tool, subject), `maximal` over the admissible repairs. Output: `experiments/maoz-coverage-frontier/frontier.csv`, one row per frontier file with its class, and `frontier-summary.csv`.
- **coverage.** Every pair of a PEREDUR pool repair and a tool frontier representative of the same subject goes through `compare`. Output: `experiments/maoz-coverage-relations/relations.csv`, one row per pair.
- **report.** The figures of section 11.3 and the verdict of section 11.5. Output: `experiments/maoz-coverage-report/report.csv` and `report.txt`.

Each pass writes `maoz-score-manifest-av2.json` in its out directory, with the binaries' `--version`, the budgets, the start and finish times and the counts.

## 3. Budgets

| Budget | Value | Where it comes from |
|---|---|---|
| `realize` wall time per repair | 600 s, process group killed | Section 11.1 |
| Well-separation `ltlsynt` per repair | 60 s, fast path off | Section 11.1 |
| `maximal` per solver call | 20 s | `maximal`'s default |
| `maximal` wall time per (tool, subject) | 14,400 s | A cap, not a registered budget; a capped pass fails and is not read |
| `compare` unit | 50 pool files against one tool repair, 900 s | |
| `compare` single pair, after a unit overruns | 120 s | A pair still unfinished is undecided |
| Workers | 16, one core each for `compare` | |

`compare` has no timeout flag and prints only when every pair of its call is done, so one hard pair would lose its whole call. The unit size bounds that loss to 50 pairs, and the single-pair re-run recovers the pairs that do finish. A pair `compare` itself reports as `timeout` is undecided too.

## 4. The PEREDUR pool

The PEREDUR side is the final frontier of each of the 30 runs of `sweep_G_mrs_nsga2-apportion_wkoff_log` on the ten subjects, from `2026-09-14-paper-rerun`. Each frontier is the set the run's maximality curve holds at its last cut, read from the `.members.tsv` sidecars of the curves pass. av2 and av3 each hold the sidecars and run directories of half the seeds, so no host can build the pool alone.

The pool is built once, locally, where both halves are:

```sh
python3 scripts/maoz_score.py --pass pool --out experiments/maoz-peredur-pool
rsync -a experiments/maoz-peredur-pool/ av2:projects/counter/experiments/maoz-peredur-pool/
```

It holds 9,178 distinct repairs over 9,281 frontier rows, deduplicated by the MD5 of each file's text with `//` comments and whitespace removed. `pool.tsv` maps each (run, file) to its MD5, and `runs.tsv` lists all 300 runs. The pass is deterministic: two builds gave byte-identical trees. `stage` refuses av2 until the pool is there.

## 5. Readings

`compare` is called with the PEREDUR repair as the repair and the tool repair as the ideal. A PEREDUR repair covers a tool repair when the relation is `equivalent` or `stronger`, and the tool repair covers the PEREDUR repair when it is `equivalent` or `weaker`. `compare` reports one relation per pair. When one direction is decided true and the other is undecided, it prints the decided direction alone, as `stronger` or `weaker`, so the undecided share counts only the pairs where neither direction held.

`maximal` reads an undecided implication as non-implication and reports no count of them. An undecided pair can therefore only keep a dominated repair on a tool's frontier, or split one class in two. A dominated repair is covered whenever its dominator is covered, so neither effect can produce an Outcome 1 that a fully decided frontier would not.

## 6. Cost

A local smoke test on `lift` ran all four passes at two workers, with `realize`, `maximal` and `compare` from `main`'s build (`--allow-stale-binary`). The screen took 3.75 s for 224 repairs: `realize` 0.022 s and the well-separation check 0.010 s per repair, on average. 198 were admissible and 26 not well-separated, with none undecided. `maximal` reduced JVTS-Repair's 195 admissible repairs to 11 classes in 75 s. Coverage classed all 5,603 pairs in 743 s, 0.26 s per pair on one core, with no undecided pair and no unit over its cap.

A probe of one 20-pair `compare` unit per other subject gave 0.011–0.031 s per pair on `rg1`, `rg2`, `gyro-var1`, `gyro-var2` and `humanoid-458`, 0.11 s on `humanoid-531`, 0.24 s on `humanoid-503`, 1.07 s on `pcar-v2-888` and 5.1 s on `humanoid-742`. If every tool repair stayed on its frontier, coverage would cost about 125 core-hours, 103 of them on `pcar-v2-888`, which is about 8 h at 16 workers. The frontiers are smaller than that bound (11 of 195 on `lift`), and screens and frontiers cost minutes.

## Known limit of the converse

`compare` prints `timeout` only when neither direction of a pair is decided. When the PEREDUR repair is shown to imply the tool repair and the reverse direction is undecided, it prints `stronger`. Coverage of the tool's frontier is then correct under both readings, since either answer for the reverse direction leaves the PEREDUR repair covering. The converse needs the reverse direction, and it reads such a pair as not covering under both readings. The converse's uncovered count is therefore an upper bound wherever `compare` hit its 20 s query budget, and the undecided share undercounts those pairs.
