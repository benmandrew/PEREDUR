# fretish-rerun

This plan is pre-registered. It fixes the design, corpus, tests and decision rule before any row of the campaign exists. It inherits all of them from `2026-09-29-fretish-ablation` and changes the engine alone.

## Question

The paper's FRETISH research question (RQ4 in counter-paper's `paper/evaluation.tex`) compares *directed mutation* of the three ordered FRETISH fields with a *uniform redraw* of the same fields. Its data come from `2026-09-29-fretish-ablation`, run at `dc3e276`. Four changes to the engine and its defaults have reached `main` since then, and each moves FRETISH scores:

- **5dc3472** renders the operand of `X` in `start_of_mode` as `X(m)`. PEREDUR's parser and `black` read the old `Xm` as one atom, so the implication filters and cache keys were wrong on mode-scoped specs: `mode-arbiter` on every repair, and the ten `rad-core-*` subjects on 15–93% of repairs.
- **#205** screens a requirement's condition and response as two formulae. The old screen rejected realisable requirements whose timing defers the response, and mutants can fall on either side.
- **#206** adds one whole-spec satisfiability query under MRS grading. A candidate whose requirements are each satisfiable but jointly contradictory now scores 0 instead of the walk's fraction. Over 66 short runs (d734fd7) the screen fired on 4.8% of its queries on the FRETISH path.
- **dc13ed2** moves the default of `fitness.keyword_similarity` from `"syntactic"` to `"semantic"`, at `semantic_trace_weight = 0.5`. The timing, scope and condition-type order measures leave the syntactic objective for the semantic one, as a reviewer asked, and the syntactic objective compares those fields as tokens. `2026-10-06-keyword-weight` fixed the weight.

This campaign asks the ablation's question again at a binary that carries all four. Its rows replace the archive's in the paper. It does not pair against the archive, since every subject's score function has moved.

## Design

The design is the ablation's, unchanged. Sweep O has two levels, `directed` and `uniform`, under `[mutation] ordered_fields`. The budget is `nsga2-apportion` selection, `mrs` grading, weights 0.1/0.2/0.7, the log metric, weakening off, 1000 individuals at population 100, `parallel = 1`, and the runner's 7200 s cap with no `max_wall_s`. Every other key is pinned at its default by `--pin-vintage`.

The branch `campaign/fretish-rerun` is `origin/main` at `d734fd7`, plus the default flip `dc13ed2` (branch `feat/keyword-semantic-default`, not yet merged to `main`), with `campaign/fretish-ablation` merged in. That merge brings three things `main` lacks: the uniform arm, the twelve core examples and `compare --timeout`. Its conflicts were the config-key tables, which take both new key sets, and the manifest schema, where `mutation.ordered_fields` becomes version 34 after `mrs_screen` took 33. `--pin-vintage` writes `keyword_similarity` and `semantic_trace_weight` into every config, so the configs state the semantic placement whatever the binary's default. The profiles `fretish-rerun` and `fretish-rerun-calib` write fresh config, results and CSV paths, since av2 and av3 still hold `results-fretish-ablation` and a resume against it would skip every run.

Seeds 0–9 run on av3, 10–19 on av1 and 20–29 on av2, at 16 runs per host. Both arms of a (subject, seed) pair run on the same host, so wall time is comparable pair by pair.

## Corpus

The 20 subjects of the ablation, from `FRETISH_ABLATION_SPECS`: eight whole specs (`takeoff`, `fsm`, `fsm-timing`, `fsm-combined`, `fsm-lmcps`, `liquid-mixer`, `mode-arbiter`, `valu3s-uc6`), the ten `rad` cores, `lpc-mini-core1` and `lpc-full-core1`. That makes 20 × 2 × 30 = 1200 runs.

The cores stay valid, and no subject's realisability verdict can have moved. `realize` lowers a spec and asks `ltlsynt`, which always parsed `Xm` as `X(m)`. Neither status screen nor the similarity objectives take part, and the Lift-Plus-Cruise speed-limit lock (9e8a5d7) was already in the ablation's corpus. The `realize` pass planned for step 0 was dropped on that reasoning.

## Steps

**0. Stage.** Tag the commit as `provenance/fretish-rerun` and push the tag at once. Both declarations are enqueued, and each host's tick stages the branch itself once the entries ahead of it finish. A gcc 11 `maybe-uninitialized` false positive in `src/repair/evolution.cpp` broke a Release build on av3 during #205; if the stage build fails on it, add `-Wno-error=maybe-uninitialized` to that host's build configuration, never to the source.

**1. Calibrate** (`2026-10-06-fretish-rerun-calib`). Seed 0 on av1, both arms, 40 runs, about 2.2 h of wall because `lpc-full-core1` reaches the cap. It answers three things before the main launch:

- *Cost.* The archive's seed 0 is the reference. #206 adds one cached `black` query per candidate that passes the component checks, and its cost on the large cores is unmeasured.
- *Change.* A diff of each run's repairs against the archived seed 0 (av2, `results-fretish-ablation`) names the subjects the three changes moved.
- *Toolchain.* av1 is the only host on gcc 13 and has never run a campaign. Its seed-0 rows must be byte-identical in repairs, accumulated set and generation lines to av3's seed 0 from the main campaign, which runs first in av3's queue.

**Deviation, recorded before launch.** Both declarations were enqueued together on 2026-10-06 with nobody at the terminal, behind `2026-10-06-pooled-rq2` on every host. On av1 the calibration entry precedes the main one, but av2 and av3 start the main campaign whenever their own queues clear. The calibration gate is therefore unenforced, as in `2026-09-04-aurus-rematch`, and the three checks become post-hoc reads: a subject costing more than twice the archive's per-run cost, or a toolchain mismatch, means dequeuing the main campaign and re-planning.

**2. Search.** 1200 runs. The archive cost 212.5 core-hours, of which `lpc-full-core1` took 120 at the cap. av2 took 7.9 h for 600 runs at 16 jobs, so a host's 400 runs should take about 5.3 h.

**3. Curve phase.** `score_curves.py --maximality` at the archive's budgets: 8 workers of 4 cores, 20 cuts, `maximal_timeout` 900 s, `compare_timeout` 3000 s, `deadline_s` 7500. The archive's passes cost 158.1 worker-hours; av3 scored 600 runs in 5.6 h and av2 280 runs in 5.2 h, so a host's 400 should take 4–7.5 h. It writes `time_to_first_repair`, which the found and first-repair endpoints read, and the `maximal.tsv` sidecars, which steps 4 and 5 read.

**4. Pooled strength.** By hand, after `experiments/2026-10-01-fretish-mixed/diversity/strength-fretish/` on `analysis/repair-diversity`: `scripts/pooled.py`, with `compare` built at this campaign's commit plus `compare-directions.patch`, at compare's fixed 20 s per direction. The exclusions are the archive's: `valu3s-uc6` and `lpc-full-core1` (undecidable at 20 s) and `fsm-lmcps` if uniform again returns no repairs. The archive's pass took 3.3 h on one host at 8 workers, led by `lpc-mini-core1` at 77 min. Split by subject over three hosts it should take about 1.5 h.

**5. Fingerprints and ball radii.** Build `fpdraw` from the sources vendored in `experiments/2026-10-01-fretish-frontier-approx/scripts/` at this campaign's commit, draw 32,768 `black` lasso models per subject over the directed and uniform frontiers, and evaluate every member against them. Then run `DIST=jaccard K=3 MATCH=0 fp_prc.py` over the 15 subjects the archive used. The archive's units held three arms, mixed included, so `fp_prc.py` needs a two-arm unit, and the word set no longer includes words drawn from mixed-arm repairs. That is a change of method, recorded in the paper's provenance note. The archive's hybrid placement, which included the word draw, took 0.60 h of wall over 16 subjects.

**6. Paper.** A new `data/fretish-ablation-2026-10/` in counter-paper, with a `PROVENANCE.txt` naming this campaign and its tag. Run `prepare.py` and `tables.py` there, set the `\QFret*` macros in `evaluation.tex` from steps 4 and 5, and regenerate the repair bundles from `scripts/export_repairs.py` (PR #202). Re-read every sentence of RQ4 that states a result in words, since no macro catches them.

## Endpoints and tests

These are the ablation's, word for word. The primary endpoint is found at the cap, tested by McNemar's exact test on the 600 (subject, seed) pairs, two-sided, at alpha 0.05. Directed wins if p < 0.05 and more discordant pairs favour directed, uniform wins under the symmetric condition, and any other outcome is null.

The secondary endpoints are found at 10 s and at 100 s under the same test, and paired wall time by the Wilcoxon signed-rank test on pairs where both arms finished. `implies_ideal` is descriptive on the six subjects with reachable ideals.

The paper's other RQ4 figures follow from the same rows. Time to first repair is compared per subject by the exact Wilcoxon signed-rank test over subjects. Strength is the pooled net subsumption share and diversity the ball-radii containment at K = 3, each tested by the exact Wilcoxon signed-rank test over subjects, as in counter-paper's `PLAN.md`.

## Cost

| Step | Wall | Core-hours |
|---|---|---|
| 0 Stage and build | 1–2 h | – |
| 1 Calibration | 2.2 h | about 7 |
| 2 Search | 5–6 h | 212 |
| 3 Curve phase | 4–7.5 h | about 630 allocated |
| 4–5 Strength, fingerprints, ball radii | 3–4 h | about 35 |
| 6 Paper | 2 h | – |
| Total | 17–20 h | about 885 |

The wall figures assume idle hosts. On 2026-10-06 av2 and av3 each ran 20–25 cores of untracked jobs, and av1 about 30 cores. On shared hosts the search and the curve phase stretch in proportion.

## Out of scope

#206 also changes MRS grading on the TLSF path, where d734fd7 measured the screen firing on 4.8% of its queries, so the TLSF data behind RQ1–RQ3 and the Maoz coverage pass are stale too. This campaign leaves TLSF out by decision of 2026-10-06. The mixed arm and the paired joint-frontier pass are left out because the paper no longer cites them.
