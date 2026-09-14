# 2026-09-14-paper-rerun

Declared 2026-09-14, before any row existed.

## 1. Why this campaign exists

The paper in `~/projects/writing/counter-paper` draws three results from `2026-09-04-aurus-rematch`: *research question* 1 (RQ1), per-family yield and `implies_ideal`; RQ3, the 2×2 ablation of selection scheme against status grading; and RQ4 with the *anytime figure*, the maximality and *behavioural-separation* curves. That campaign's binary was `f8fbe26`, and its PEREDUR arm is `nsga2-apportion/mrs`.

Since `f8fbe26`, 21 non-merge commits have landed under `src/`, `include/` and `CMakeLists.txt`. This campaign repeats the search at current `main`, so every PEREDUR number in the paper comes from the engine that ships. AuRUS is not re-run. It has not changed, so its archived 780 runs, 750 over the 25 shared families, are reused as in the rematch.

## 2. What moved since `f8fbe26`

Six commits are recorded as cost-only and output-preserving. `10a7f52` counts guard models in process. `41ff786` and `2c6baba` key the ganak *model-count cache* on the renamed, then the canonical, form; `2c6baba` also fixes soundness, as the old key was `ltlfilt` output. `afb5545` refutes implications from sampled *lasso words* before a solver call. `7d1ae35` streams the maximality filter during the search; merge `afa031e` removed its key, leaving streaming the only route with `accumulate_repairs` and `run_implication` on, as here. It measured 11.7% lower wall time pooled over 6 paired FRETISH runs. `d9fe44d` adds a two-level priority queue to the thread pool.

Two commits change behaviour on the Temporal Logic Synthesis Format (TLSF) search path. `bdbec4c` removes `connective_implies`, always offers `->` in case (2d) of the temporal rewrite, turns every monotone rewrite rule on, and moves `p_monotone` to 0.25 for both paths. These move the TLSF *draw stream*, so no seed reproduces its rematch run. It also deletes the *weakening screen*, which the rematch ran off, and `run_well_separation`, whose gate is now unconditional. `a042e5b` retires `p_burst_continue`, `p_remove_assumption`, `max_concurrent_realizability`, `ganak_timeout_ms` and `run_vacuity`, hard-coding their defaults.

Per-seed rows therefore do not pair with the rematch's. Comparison is per arm and per family, and descriptive.

## 3. Design

Only the binary differs from the rematch. The corpus is the 25 TLSF families of `H2H_TLSF_READY` in `scripts/run_experiments.py`, unchanged since `f8fbe26`; `examples/` has since changed by one comment in an amba ideal. Each family runs 30 seeds in 4 arms, `nsga2-apportion` and `weighted` crossed with `mrs` and `aurus` grading, for 3000 runs.

Settings are `termination = individuals`, `max_individuals = 1000`, population 100, `generations = 500` as a ceiling, `parallel = 1`, weights 0.1/0.2/0.7, the logarithmic metric, a 7200 s harness cap and `compare_timeout` 1800 s. The rematch's `gen_configs.py` line writes the configs to `experiments/configs-paper-rerun` and, at current `main`, emits no retired key.

Profiles `paper-rerun` and `paper-rerun-calib` get their own results directories and CSVs. The *resume key* carries no commit, so `rematch`'s files would skip all 3000 runs as done. av2 runs seeds 0–14 and av3 seeds 15–29, so both arms of a pair share a host.

## 4. Calibration

The `calib` phase repeats the rematch's 48 runs: `humanoid-742`, `humanoid-531`, `pcar-v2-888`, `full-arbiter-aurus`, `minepump` and `rg2`, at 2 seeds across 4 arms, one seed a host. By decision it chains unattended into `main`. No gate applies. The rematch's 786.8 core-hours on the same design is the estimate rather than a bound: the cost-only commits lower it and `bdbec4c`'s operator changes can move it either way.

On collection, read first whether `humanoid-742` still caps on 8 of 8, then realised cost against the rematch.

## 5. Scoring passes, declared later

Two passes run as `kind = "score"` phases. The maximality-with-ideals pass feeds RQ4 and the anytime figure's top panels. The behavioural-separation pass covers all gate-passing candidates at epsilon 0.05, 0.2 and 0.5, with 256 *fingerprint words* and fingerprint seed 0, for the bottom-right panel.

Their tooling exists only on the unmerged `feat/fingerprint-curves`: the `fingerprint` binary, `score_curves.py --epsilon`, the *running-antichain* `maximal --curve` and `aurus_adapt.py`. It is being ported onto `main`, so the scoring phases are enqueued from a later commit than the search, and `PROVENANCE.json` records both. `maximal`, `compare` and `fingerprint` share `src/` with the search, so the port must not change the search path. The check is a diff of the search binary's sources between the two commits.

## 6. The AuRUS curves

AuRUS's curves were scored at `96b47db` for maximality and `d87e200` for separation. The word sampler `sample_words` is unchanged on `main`, so the separation curves stand.

The maximality scorer has moved in `afb5545`, `7707f96` and `2c95d5f`. These preserve every verdict reached within budget, so a count moves only where the old pass hit a budget: a partial curve, or a timed-out implication read as non-implication. Re-scoring AuRUS's 598 adapted run directories with the new scorer is an open decision.

## 7. What the paper regenerates

`data/rematch-2026-09/prepare.py` and `tables.py` in the paper repository read `results-rematch.csv`, `curves-rematch.csv` and `separation-rematch.csv` by name. `tables.py` checks its figures against the rematch's `analysis-output.txt` from `scripts/analyse_matched.py --primary nsga2-apportion/mrs`. A new data directory must point at this campaign's files, with `analyse_matched.py` re-run for a new reference printout.

No decision rule is registered. This campaign re-measures reported figures and tests no hypothesis, so every contrast in the paper stays post-hoc.

## 8. Budget

| Phase | Estimate |
|---|---|
| `calib`, 48 runs | about the rematch's cost of the six families |
| `main`, 3000 runs | about 786.8 core-hours (the rematch at `f8fbe26`), about 25 h wall a host at 16 jobs |
| Maximality pass | ≤ 311.7 worker-hours (per-cut scorer), expected about a sixth, the running-antichain walk having measured 6.2× cheaper |
| Separation pass | minutes, with no solver call |
| AuRUS arm | none |

Whether these figures belong to the shipped engine rests on one diff of the search sources.
