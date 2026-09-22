# 2026-09-22-aurus-rerun

Pre-registered 2026-09-22, before any row of this campaign existed.

## 1. Why this campaign exists

Every head-to-head the project has run reused one AuRUS arm, collected on 2026-08-14 and never re-collected. In that arm 173 of 780 runs (22.2%) were killed at the 7200 s cap and lost everything they had found, because upstream AuRUS writes `ga.solutions` in one batch after its search loop and a killed *Java virtual machine* (JVM) takes the list with it. A lost run scores `implies_ideal = 0`, which is the same number a run that searched and found nothing scores.

The loss is not spread evenly. Three families lost all 30 repeats — `humanoid-531`, `humanoid-503` and `humanoid-741` — and three more lost most of them: `prioritized-arbiter-aurus` 29, `full-arbiter-aurus` 28, `pcar-v2-888` 25. Six of 26 families carry 172 of the 173 kills. The paper's AuRUS column on those six families is a statement about output handling.

A second defect is resolution. Upstream dates a solution by reading the run log's per-iteration `#Sol` column, so a discovery time is the first *iteration* whose solution count reaches the index, rounded to the second by the `Elapsed Time` line beside it. The paper's anytime figure carries `AURUS_SHIFT_S = 1.0` and a synthetic 0 s cut to absorb that rounding.

## 2. What changed in AuRUS

`e1cfadf`, on branch `output-on-timeout` of `~/projects/tools/aurus-timeout`, writes each solution as it is found and appends a row to `solution-times.csv` giving its elapsed seconds to the microsecond. The parent commit `3f6f01f` is upstream AuRUS as published and is what the archive ran.

The search is the same under both. The diff touches output alone: where a solution file is written, and a log of when. A run killed at the cap now keeps what it found, and a solution carries the tool's own clock rather than this side's inference.

Two consequences, registered rather than discovered:

- **This is a new measurement of AuRUS, not a more precise reading of the old one.** Un-censoring changes what the arm reports on six families, so no row here pairs against the archive's. The two are reported beside each other and never merged.
- **The gain is bounded above by the censoring.** On the 20 families that lost nothing, the only change is the dating, and a null there is the expected result.

## 3. Design

The AuRUS arm alone runs. The PEREDUR arm reuses `2026-09-14-paper-rerun`, whose 3000 runs were collected at current `main` and closed on 2026-09-18, because nothing in this campaign changes that side.

The arm is the archive's, re-run at `e1cfadf`: 26 families at 30 repeats, 780 runs, `-Max=1000 -Gen=1000 -Pop=100 -k=20 -addA -geneNUM=0 -factors=0.7,0.1,0.2`, with `-onlyInputsA` on the nine families that need it, `-GATO=7200` and a harness kill at 7500 s. AuRUS draws from `Math.random()` with no command-line override, so a repeat is the arm's only replicate dimension and repeats do not reproduce. av2 takes repeats 0–14 and av3 15–29, which is the split the archive used and the split the scoring phases read.

It runs as a `kind = "aurus"` phase of `campaign.toml`, which is new. The archive was launched by hand from an ssh line off `2026-08-14-aurus-h2h/PLAN.md` §8, and the seed split lived in that prose. `aurus_commit = "e1cfadf"` is checked against the host's `COMMIT.txt` at stage time and again by the runner before the first JVM starts, so an arm cannot quietly measure the other vintage.

## 4. Primary endpoint

Per-family `implies_ideal`, PEREDUR's `nsga2-apportion`/`mrs` cell from `2026-09-14-paper-rerun` against this AuRUS arm, over the 25 families of `H2H_TLSF_READY`, read with an exact two-sided *Wilcoxon signed-rank* test at alpha 0.05.

The primary read counts a capped run as a failure on both sides, which is the question the paper asks: what a tool delivers inside a fixed budget. Under `e1cfadf` a capped AuRUS run is no longer empty, so that read now scores what it found rather than scoring zero for having lost it.

Registered secondary, reported always and never substituted for the primary: the same test restricted to the 20 families whose archived repeats were never censored. It isolates the dating change from the un-censoring, and a null there is what section 2 predicts.

## 5. Decision rule

- **Outcome 1, PEREDUR higher at p < 0.05.** Reported as PEREDUR ahead, against an AuRUS arm that no longer loses its output at the cap.
- **Outcome 2, AuRUS higher at p < 0.05.** Reported as measured, and the paper's RQ1 claim is rewritten to match.
- **Outcome 3, null.** Reported as parity, with the discordant counts.

In every outcome the paper reports the censoring of the archived arm and states that this campaign replaces it. A supersession row goes in `experiments/README.md`, naming `2026-08-14-aurus-h2h` and every campaign that reused it. No outcome licenses reporting the archived AuRUS numbers alongside these as though the two were one sample.

Per-family numbers and the contrast against the archive are secondary and carry no rule. They describe this sample.

## 6. Registered hazards

- **The PEREDUR arm is four days older than this one.** Its binary is `2026-09-14-paper-rerun`'s and is not rebuilt here. Commits landing on `main` between the two do not enter the comparison, and the arms therefore differ in date as well as tool. `PROVENANCE.json` records both commits.
- **The six censored families dominate whatever moves.** 172 of 173 recovered runs sit there, so a swing on the primary is those families and must be reported per family rather than pooled.
- **AuRUS's repeats do not pair with the archive's.** `Math.random()` is unseeded, so a family's 30 repeats are a fresh sample and a per-repeat diff against 2026-08-14 means nothing.
- **Concurrency is a memory bound and is being held at the archive's 10.** A JVM reserves 8 GB of heap and holds about one core, measured at 1.0–1.4 cores a run with Strix single-threaded, so 32 cores are not the constraint. Raising it is a separate change and is not made here. `aurus_results.csv` now records `peak_rss_mb` per run, which is what a later decision would read.

## 7. Budget

The archive spent 457.9 wall-hours over 780 runs, median 329.2 s, split across two hosts at concurrency 10. At the same concurrency this arm costs about 23 h a host, and the fork's extra writing is one file and one CSV row per solution.

The un-censoring moves that estimate one way only: a run killed at 7500 s cost 7500 s in the archive too, so the six censored families are already priced at the cap. Total is about 46 wall-hours.

## 8. Scoring passes

Four `kind = "score"` phases, the budgets copied unchanged from `2026-09-14-paper-rerun-curves` so both arms reduce through one scorer.

The separation pass runs first, over all gate-passing candidates at epsilon 0.05, 0.2 and 0.5 with 256 *fingerprint words* and fingerprint seed 0; it makes no solver call. The maximality-with-ideals pass follows at `maximal_timeout` 900 and `compare_timeout` 3000. The PEREDUR side is re-scored rather than reused, because the archived PEREDUR curves were written by the scorer at that campaign's commit and a comparison wants one scorer on both sides.

`scripts/aurus_adapt.py` materialises the arm as run directories named `aurus_<spec>_seed<NN>`, which the scorer's seed split and resume already key on. It runs inside the AuRUS phase, since the tree is scorable only once every repeat of a host's split has written.

## 9. What the paper regenerates

`data/rerun-2026-09/` in `~/projects/writing/counter-paper` reads `results-paper-rerun.csv`, `curves-*.csv` and `separation-*.csv` by name through `prepare.py`, then `tables.py`. A new data directory points at this campaign's AuRUS files and keeps the PEREDUR ones.

Three constants in `tables.py` exist to absorb second-resolution AuRUS dating and are wrong against `e1cfadf`: `AURUS_SHIFT_S = 1.0`, the synthetic 0 s entry in `AURUS_CUTS`, and the `cut_shift` x-axis it feeds. `pad_aurus_zero_runs` exists to fill in the runs that lost their output and has nothing left to pad. Each is removed against the new data, and `analysis-output.txt` is regenerated from `scripts/analyse_matched.py --primary nsga2-apportion/mrs` for a fresh reference printout.

The well-separation screen is re-run over the new solutions. It joins one-to-one on `(spec, repeat, index)`, and the archive's indices do not survive a fresh sample.
