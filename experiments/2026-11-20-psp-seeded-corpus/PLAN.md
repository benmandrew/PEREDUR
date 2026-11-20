# 2026-11-20-psp-seeded-corpus

The PSP seeded corpus of PLAN.md P7 (on `feat/psp-front-end`). Each subject is a realisable base B with one guarantee edited by a fault from `psp::seeded_edits` (`include/psp/seeding.hpp`). The base is then a known repair of the subject. The corpus is tracked under `examples/psp-seeded/`, because `experiments/` tracks no data, and this directory holds only the plan and `scripts/seed.py`. No campaign runs here. P8 declares the PSP campaign, and `scripts/run_experiments.py` registers the subjects in `selected.txt` as `PSP_SEEDED_SPECS`.

## Design

- **Bases.** The lines of `examples/psp-seeded/bases.tsv`: id, path, source and an optional `max_candidates`. A realisable SYNTECH17 import joins the corpus as one more line. A base with no weakenable guarantee gives no candidate.
- **Faults.** One guarantee is edited, by one of these faults. A step up the measured orders: `pattern-step`, `scope-step`, `count-step`, or `narrow-field` (a literal conjoined to a monotone field or disjoined to an antitone one). A sideways move, as the FRETISH sideways corpus has: `pattern-swap`, `scope-swap`, `swap-delimiters`, `flip-literal`, or `swap-signal`. `sample-mutants --psp-seeded` lists every such edit that the rule does not prove weaker than the original.
- **Admission.** As in `2026-11-18-seeded-corpus-sideways`. `compare` finds O strictly stronger than B or incomparable with it. O is unrealisable. Every live guarantee of O is realisable alone. The conjunction of O's requirements is satisfiable.
- **Selection.** `selected.txt` holds at most `--cap` (8) kept mutants per base, drawn with `--seed`. `examples/psp-seeded/subjects/` holds every kept mutant.
- **Not done here.** The FRETISH corpus has a trivial-reversion probe (`probe.py`), and this corpus has none. Each subject differs from its base in one edit, so one mutation can revert it. P8 should measure that before it trusts the seeded results.

## Running it

```
python3 experiments/2026-11-20-psp-seeded-corpus/scripts/seed.py --bin build --jobs 4
```

Run it inside the memory cap of `PLAN.md` D10. Its scratch directory, `work/` here, is untracked.

## Log

- 2026-11-20. The script was run on the five hand-written bases (`psp-arbiter` and the four of P7). It gave 332 candidates and kept 121. 23 candidates failed the relation check, 135 were realisable, and 53 failed the singles check. Of the kept mutants, 69 are incomparable with B and 52 strictly stronger. 39 are selected. Every kept mutant was `realize`d as unrealisable in under a second.
