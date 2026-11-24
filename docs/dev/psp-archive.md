# PSP front end: archive

Work on the PSP front end stopped on 2026-11-24, and it will not be taken forward or merged into `main`. This note records what was built, what was measured and where the pieces are. The tag `archive/psp-front-end` marks the commit that holds this note. `archive/psp-order` and `archive/psp-syntech` mark the commits the two campaigns ran at.

## What was built

PSP (the Dwyer specification patterns, with Konrad and Cheng's real-time extensions) was added as a third input language beside FRETISH and TLSF. The language reference is `docs/dev/psp.md`. It has 9 patterns: Dwyer's five, bounded existence, bounded response, bounded invariance and bounded recurrence. It has 5 scopes: globally, before, after, between and after-until. Fields p, s, q and r are propositional formulas, and counts run from 1 to 64. A `fixed` requirement carries raw linear temporal logic (LTL) and is never weakened.

The four order-directed mechanisms (climb, two-way mutation, maximality removal and novelty) were moved behind a language-generic interface and given PSP's pattern order, scope order and field polarities. FRETISH and TLSF output stayed byte-identical on `fsm` and `mode-arbiter`. The Lean proofs are in `proofs/Psp/` and listed under "PSP order proofs" in `proofs/README.md`: `pattern_exact` and `scope_exact` (both orders exact at every count of 1 or more), `pattern_implies_exact` and `scope_implies_exact` (the C++ tables equal them), `polarity_sound` (sound only), `reach_sound` and `eval_lowerSyn`. The absence of joint pattern-and-scope steps is measured (0 of 534 implications at counts 1 to 4, in `docs/dev/psp-orders.json`) and not proved.

## What was measured

Both campaigns ran on av2 and av3 under `nsga2-apportion` at the final engine's budget, with the arms `base`, `full`, `no-climb`, `no-gate`, `no-remove` and `no-novelty`. Their plans are `experiments/2026-11-20-psp-order/PLAN.md` and `experiments/2026-11-22-psp-syntech/PLAN.md`.

**psp-order** (at `d6a3fc08`, finished 2026-11-23 at about 03:18Z) ran the 5 hand-written subjects at 30 seeds (1,080 rows with one SYNTECH17 import) and the seeded corpus at 5 seeds (1,974 rows). On the hand-written specifications the known-repair rate was 0.25 for `base` and 0.51 for `full`. On the 39 mutants of the hand-written subjects it was 0.36 for `base` and 0.50 for `full`, and between 0.48 and 0.54 for the four ablations. On the 24 mutants of the earlier SYNTECH17 import it was 0.05 for `base` and 0.16 for `full`.

**psp-syntech** (at `d1faef37`, 2026-11-23 from about 11:55Z to 17:27Z) ran the SYNTECH17 import after the lift fix: 5 specifications at 30 seeds (300 rows) and 49 seeded mutants at 5 seeds (588 rows). The arms did not differ. On the specifications every arm found a repair in 0.40 of runs, from `SimpleVehicle_VehicleSpec_31` and `_79` (60 of 60 each). `_78` and `AirportShuttle_177` found nothing, and `_134` timed out in every run. On the seeded mutants every arm found a repair in 0.90 to 1.00 of runs and the known repair in 0.17 to 0.20.

The pooled passes (`experiments/2026-11-20-psp-order-pooled`, `experiments/2026-11-22-psp-syntech-pooled`) are declared and never ran.

## Why it stopped

The construction transfers to a second structured language with proved orders, but the evidence that the mechanisms help on PSP rests on 5 hand-written subjects with 2 to 4 guarantees each. The SYNTECH17 import is the only third-party corpus, and on it the arms tie. The Ultimate requirement files (below) were the remaining route to third-party evidence and were not seeded.

## Where the pieces are

- **Code:** branch `feat/psp-front-end` and the archive tags. The campaign branches are `campaign/psp-order` and `campaign/psp-syntech`.
- **Results:** untracked, as all experiment data in this repository is, in the checkout `~/projects/peredur-psp` on the author's machine. These are `experiments/results-psp-order*`, `experiments/results-psp-syntech*` (1.1 GB, mostly run directories), `experiments/2026-11-20-psp-order/probe.csv`, `experiments/2026-11-22-psp-syntech/probe-syntech-lift.csv` and `experiments/2026-11-20-psp-seeded-corpus/mutants-full.csv`. The run directories are also in `~/projects/counter` on av2 and av3.
- **Plan:** `PLAN.md` at the root of that checkout (untracked, since `.gitignore` ignores a root `PLAN.md`).
- **Paper import:** `psp-archive/paper-import-data.patch` is the one commit (`d5d8da6`) of the paper's `psp/import-data` branch, as `git format-patch` output against the paper's `551cad5`. It adds `import_psp()` to `scripts/import_data.py` and `psp()` to `scripts/summarise.py`. Apply it with `git am` in the paper repository.
- **Research report:** `docs/research/psp-suitability.md` in the paper repository, with its scripts in `docs/research/psp/`.
- **Ultimate converter:** `psp-archive/ultimate-req-to-psp.py SRC DST` converts the boolean Ultimate/Hanfor `.req` files (`trunk/examples/Requirements` in `ultimate-pa/ultimate`) into `.psp` specifications. An atom is an output if it occurs in a consequent field, and an input otherwise. A requirement whose atoms are all inputs becomes an assumption. On the 24 files with 10 or more requirements it kept 959 of 1,038 requirements. Most of the drops have counts above 64, and the rest are duration patterns. Under this split 9 files (8 systems) were unrealisable, and each was satisfiable with every atom taken as an output.

The work shows that the mechanisms can be stated for a language whose scopes are delimited by events. It does not show that they help on specifications that others wrote.
