# FRETISH joint-frontier rows by hybrid placement

## Question

The paper's FRETISH research question compares the directed and uniform arms on a paired *joint frontier*: per seed, which repairs each arm keeps once the other arm's repairs can dominate them. The exact pass (`data/exact.csv`, byte-identical to `data/fretish-ablation-2026-09/overlap.csv` in counter-paper at e3d7480) skipped four subjects as too costly: `liquid-mixer`, `lpc-mini-core1`, `lpc-full-core1` and `valu3s-uc6`. This pass asks whether those four rows can be filled without changing what the table means. It is an offline analysis of frontiers the FRETISH ablation campaign already wrote, run by hand and outside `scripts/campaign.py`.

## Method

*Hybrid placement* (`scripts/hybrid.py`) evaluates each frontier member on sampled *lasso words* before any solver call. A word satisfying x and violating y is a *witness* that x does not imply y. A pair refuted in both directions is therefore proven incomparable. Every other pair, within and across arms, goes to `compare --timeout 20`, the exact pass's per-query budget. A timeout reads as non-implication, as it does in `maximal`. The errors are one-sided: a word can refute an implication, never confirm one. Unrefuted pairs still meet the solver, so the words decide cost and never verdicts.

The shipped prefilter in `maximal` and the output gate (see "Implication prefilter" in `docs/dev/performance.md`) draws 256 uniform words. Uniform words failed on the Lift-Plus-Cruise (LPC) cores. On `lpc-mini-core1` no uniform word satisfies any repair, and on `lpc-full-core1` every word is *vacuous*, falsifying the assumptions and so satisfying every repair alike. Neither case refutes anything.

This pass drew words from three sources instead:

- **Targeted models.** `scripts/draw_black.py` asks `black` for 32,768 lasso models per subject, each of a random frontier repair's assumptions and guarantees under 1–8 random literal hints.
- **Boundary words.** On `lpc-full-core1` only, 32,768 further words satisfy a repair's assumptions and violate exactly one guarantee, sitting just outside it.
- **Per-pair witnesses.** `scripts/pairwit.py` asks `black` for a model of x ∧ ¬y for each direction the other words leave standing.

`fpdraw eval` re-evaluates every word against every member, so refutation stays sound whatever the solver produced. With `LTL` set, a `compare` timeout falls back to `black` in text mode on each direction: UNSAT proves the implication and SAT refutes it. Placement reproduces `paired.py`'s convention by running `maximal` on each equivalence class holding more than one spelling. The trace drawing used here is not in the codebase; `fpdraw.cpp` and `fpcal.cpp` were built against 78cae62 in a worktree and never committed.

## Results

Values are medians over seeds, in percent.

| Subject | Seeds | Directed-only | Uniform-only | Shared | Directed dominated | Uniform dominated | Undecided pairs |
|---|---|---|---|---|---|---|---|
| `liquid-mixer` | 30 | 54.6 | 45.0 | 0.0 | 30.6 | 21.9 | 0.006 |
| `lpc-mini-core1` | 30 | 47.3 | 50.9 | 2.0 | 22.7 | 13.2 | 0.225 |
| `valu3s-uc6` | 30 | 51.7 | 48.1 | 0.0 | 0.0 | 0.0 | 0.221 |
| `lpc-full-core1` | 13 of 30 (owed) | 49.6 | 47.2 | 3.6 | 2.9 | 4.5 | 1.689 |

The `lpc-full-core1` row is preliminary. Seeds 13–29 were running on av3 at archive time, at about 2,000 s per seed. The `owed` key in `PROVENANCE.json` says where to collect them.

Pairs reaching `compare` were 0.53% on `liquid-mixer`, 0.81% on `lpc-mini-core1` and 21.66% on `lpc-full-core1`. On that last subject, targeted models gave 241 distinct fingerprints over 4,137 repairs, and boundary words raised it to 1,141. Per-pair witnesses cut seed 0's `compare` queries from 7,040 to 1,348 and its timeouts from 6,996 to 93.

## Validation

The hybrid was run on the 16 subjects the exact pass covers, every seed, and checked against `data/exact.csv` with `scripts/check.py`. 420 of 429 (subject, seed) rows are identical. One difference is a `compare` timeout on `fsm-timing` seed 9. The other 8 come from the `start_of_mode` bug below. A `maximal` with the bug fixed reproduces the hybrid on 8 of 8 (`data/redo-maximal-unglued.csv`). The shipped `maximal` reproduces `exact.csv` on 8 of 8 (`data/redo-maximal.csv`). The exact pass erred there.

Over the 16 subjects the hybrid sent 2.89% of pairs to `compare`. It took 0.60 h of wall time against 2.13 h for the exact pass.

## Caveats

Timeouts read as non-implication, so dominated shares are lower bounds and move with host load. Two `lpc-mini-core1` runs differ on 21 of 30 seeds, with uniform dominated at 16.1% in the earlier run (`data/hyb/lpc-mini-core1.csv`) and 13.2% in the final one.

The `valu3s-uc6` zeros mean no domination is provable. The 0.22% of pairs the words leave also time out in `compare` at 300 s (24 of 24 sampled) and in `black`.

`black -m` models with `loop == size` are dropped. 1,077 such words reached early RAD and `takeoff` runs, whose outputs were redone; `data/hyb/fixbad` and the `v1` files hold the superseded outputs.

`shared` counts structurally identical members only, following `paired.py`. The `sem_*` columns give the semantic reading. Over the 16 validated subjects it raises the median shared share from 0% to 0.52%.

## Findings about the engine

**Glued X.** Since 0ef4d47, `start_of_mode` rendered `(!m) & Xm` unparenthesised, for example `Xiap_task_execution`. SPOT reads `X(iap_task_execution)`, as intended. PEREDUR's own parser and `black` read one atom that no sampled word sets. The fingerprint prefilter in `maximal`, the output gate's implication filter and the streaming run-frontier filter could then refute a true implication and keep a dominated repair. The token appears in every `mode-arbiter` repair and in 15–93% of repairs on all ten `rad-core-*` subjects. In the paper, 8 `overlap.csv` rows gain 1–2 dominated members. Regenerating `tables.py` on a scratch copy moves one macro, `\FretOverlapUniformSubsumedShare`, from 1.94% to 2.01%. The fix is `"X(" + mode + ")"`, on main in 5dc3472. It changes the mode-scope linear temporal logic (LTL) bytes, and with them cache keys and filter outcomes on the affected subjects.

**black JSON timeouts.** `black -o json` (25.09.0) prints `{"result": "UNSAT"}` at its `-t` limit, where text output prints `UNKNOWN`. `hybrid.py` trusts only text output. The UNSAT tallies `pairwit.py` prints include timeouts, though its SAT witness words are unaffected.

## Files and reproduction

The `files` and `reproduce` keys in `PROVENANCE.json` list the data layout and the commands. Only `REPORT.md`, `PROVENANCE.json` and `scripts/` are tracked in git; `data/` is untracked. The vendored scripts hard-code the original scratch paths, so they need editing before they run elsewhere. `PROVENANCE.json` also records each script's blob.

The four rows rest on solver verdicts the exact pass would have reached, with sampled words only deciding which questions to ask. Where the solver could not answer, as on `valu3s-uc6`, the table reports that silence as zero domination.
