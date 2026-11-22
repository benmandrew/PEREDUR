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
- 2026-11-20. The corpus moved to `examples/psp-seeded/`, because the `.gitignore` rule for `experiments/` keeps no data there. A rerun gave the same 121 kept mutants and the same selection.
- 2026-11-20. The 51 realisable SYNTECH17 imports joined `bases.tsv`, the five with event-delimited slots first, each with `max_candidates = 40`. The script skips a base whose file is absent, and these files reach the branch only when `feat/psp-front-end` (0c1c7147) is merged. That merge has not been done yet. Screened from that commit, the five event-delimited bases are realisable in at most 26 s. AirportShuttle 163, 169 and 174 have no weakenable guarantee and give no candidate. Elevator 55 and 94 give 2,089 and 968 candidates.
- 2026-11-22. The SYNTECH17 imports were redone with state constraints lifted to patterns and valid requirements dropped (`scripts/import_spectra.py`). The earlier SYNTECH-based subjects kept tautologies: the vacuity filter discarded every offspring of SimpleVehicle 24, and 0 of 96 runs on its mutants found a repair. The SYNTECH lines of `bases.tsv` were replaced by the 26 lifted realisable imports that `realize` decides in at most 1 s and that have a weakenable guarantee, with ids `syntech-lift-<import>`, so no new subject id repeats an old one (the old rows stay in `results-psp-order-seeded.csv` of `campaign/psp-order`). The slower imports are left out because each run then costs too much (Elevator 94, at 25 s a `realize`, reached the 7,200 s cap on 27 of 28 runs). To keep the 39 hand-written subjects, their files and their `mutants.csv` rows byte-identical while `campaign/psp-order` runs them, `seed.py --bases <the 26 lines> --out <scratch> --bin build-release --jobs 4 --cap 4` ran on the SYNTECH lines alone, and its rows, selection and subjects replaced the SYNTECH ones here. Of 1,040 candidates (40 from each base), 341 failed the relation check, 394 were realisable, 80 failed the singles check and 26 the satisfiability check. 199 were kept (134 incomparable with B, 65 strictly stronger), from 15 bases, and 55 are selected (at most 4 a base). Of the 55, 39 are incomparable, 16 strictly stronger, and 25 have a guarantee under an event-delimited scope. Spot finds no requirement valid in any spec or base of the 320 subjects. The SYNTECH17 requirements are long, so their `before` and `after` text would take `mutants.csv` to 2.9 MB, over the 500 KiB blob limit. The tracked file leaves those two columns empty on the `syntech-lift-` rows (269 KiB), each kept subject's `seed.json` holds its own, and seed.py's full output is untracked at `mutants-full.csv` here.
