# 2026-10-08-fretish-rerun-analysis

Steps 4 (pooled strength) and 5 (fingerprints and ball radii) of `experiments/2026-10-06-fretish-rerun/PLAN.md`, run through the queue as one declaration instead of by hand. The inputs are the rerun's `accumulated/maximal.tsv` members, pooled once locally and committed here, so every host reads the same nodes.

## Inputs

`members/<subject>.jsonl.gz` holds one line per node: members with identical sorted JSON over 30 seeds and both arms are one node, and each node lists every `(arm, seed, file)` holding it. `scripts/pool_members.py` built them from `counter-wt/fretish-rerun/experiments/results-fretish-rerun`; `members/index.csv` gives the counts and the SHA-256 of each file. The 17 subjects are the archive's strength set: `valu3s-uc6` and `lpc-full-core1` are excluded as undecidable at 20 s, and `fsm-lmcps` because its directed arm has no maximal set (58 of 60 runs lack one). `rad-core-10` lacks `maximal.tsv` in 18 of 60 runs, as in the archive, where it also fills few directed units.

## Phases

One entry per host, two phases, both unseeded. The build is `cmake --build build-release` plus the `EXCLUDE_FROM_ALL` target `fpdraw`, at the branch commit, so `compare` carries the directions change as committed source and reports `dirty=0`.

1. `fingerprint` (`kind = "fingerprint"`, `scripts/fingerprint_members.py`). For each subject in `subjects-<host>.txt`: write the node files, draw 32,768 black lasso words over the members (each node once per member holding it, seed 3, 30 s a draw, 8 at a time) with `fpdraw ltl` and the archive's `draw_black.py`, evaluate every node with `fpdraw eval` (`FPDRAW_UNGLUE=1` on `rad-core-*` and `mode-arbiter`), and plan every node pair with a direction no word refutes, with a `dirs` column. A draw with any black `ERROR`, or no word, fails the subject. Output: `experiments/analysis-fretish-rerun/fp/<subject>/` (words, `.meta`, `fp.tsv`) and the joined `experiments/analysis-fretish-rerun-work/pairs-<host>.csv`.
2. `subsumption` (`kind = "compare"`). Every planned pair through `compare_pairs.py`, 24 jobs, `compare --timeout 20` per direction (the archive's fixed budget), 180 s outer cap, 8 GiB `ulimit -v`. A word-refuted direction is skipped through `COMPARE_DIRECTIONS` and reads 0. Output: `experiments/analysis-fretish-rerun/subsumption-<host>.csv` with `a_implies_b,b_implies_a`.

## Split and cost

Subjects, not seeds, are split, since the pooled design needs a subject's whole pool on one host. The basis is a local pass on 2026-10-08 at 2,048 words (so an upper bound on the pairs 32,768 words leave) and a 200-pair systematic sample of each heavy subject's plan through `compare_pairs.py` at 4 jobs:

| Subject | Pairs at 2,048 words | Mean s/pair | CPU-h (upper) |
|---|---|---|---|
| `lpc-mini-core1` | 193,321 | 1.28 | 69 |
| `fsm-timing` | 140,860 | 0.33 | 13 |
| `liquid-mixer` | 11,658 | 1.48 | 4.8 |
| `fsm`, `takeoff` | 14,373, 24,362 | 0.02 | under 0.2 |
| `rad-core-10` | 1,942 | 0.02 | under 0.1 |

The other `rad-core-*` subjects come from the archive's strength pass (`subjects.csv`, same method family): `rad-core-33` asked 991,014 directions with 356 undecided, the rest at most 10,153 each. At about 0.03 s a rad pair plus 40 s per undecided pair, `rad-core-33` is about 9 CPU-h and the other eight under 1 CPU-h together. `fsm-combined` and `mode-arbiter` asked 13,696 and 10,559 directions there.

| Host | Subjects | Compare | Draws | Wall |
|---|---|---|---|---|
| av1 | `lpc-mini-core1`, `rad-core-10` | ≤ 2.9 h | ≈ 0.3 h | ≤ 3.2 h |
| av2 | `fsm-timing`, `liquid-mixer`, `fsm-combined`, `mode-arbiter`, `takeoff`, `fsm` | ≈ 0.8 h | ≈ 0.4 h | ≈ 1.2 h |
| av3 | `rad-core-33`, `-61`, `-65`, `-17-18`, `-12-18`, `-55`, `-1-18`, `-45`, `-68` | ≈ 0.5 h | ≈ 0.6 h | ≈ 1.1 h |

Draw time is the local 2,048-word time scaled by 16 for the word count and halved for 8 workers (`lpc-mini-core1` 59 s, `fsm-timing` 15 s, `rad-core-10` 9 s); planning and evaluation add minutes. `lpc-mini-core1` is the only subject that cannot share a host, so av1 sets the campaign's length. The 1.28 s mean there is driven by 20 s timeouts (3 of 199), so its figure carries most of the uncertainty.

## After the queue

```sh
python3 scripts/campaign.py collect --outputs analysis-fretish-rerun
sh experiments/2026-10-08-fretish-rerun-analysis/scripts/post_collect.sh
```

`post_collect.sh` runs `scripts/reduce.py`, which checks each subject's planned set against its fingerprints, then writes `out/strength/subjects.csv`, `relations.csv` and `summary.txt` in the archive's format, and one per-member fingerprint file a subject for the ball radii. It then runs `fp_prc.py` at K = 1, 3 and 5 (`DIST=jaccard MATCH=0`), `fp_prc_report.py` on each, and `direction_ctrl.py - out/ball-radii/j3m.csv` (FRETISH only; the `-` skips the TLSF file). Strength covers the 17 subjects above, and ball radii the archive's 15, which omit `liquid-mixer` and `lpc-mini-core1`.

## Deviations from the archive

- **Non-adaptive plan.** The archive queried within-arm pairs first, built frontiers, then queried only cross pairs between frontier classes, and used antichain insertion for `fsm-timing` and `lpc-mini-core1`. Here every word-unrefuted pair is planned upfront, as in `2026-10-06-pooled-rq2`, and the frontier is built all-pairs locally. It asks more queries but holds every verdict the archive's method would read, so the frontier rule and the summary are unchanged; `method_d`/`method_u` read `all-pairs` everywhere.
- **Word set.** The archive's words came from the `fmix-front` draw over three arms. These are drawn over directed and uniform members only, since this campaign has no mixed arm. Words decide cost, never a strength verdict; for ball radii they are the measure itself, so the fingerprints are not comparable with the archive's.
- **Two-arm ball-radii units**, as adapted in `analysis/repair-diversity`'s `2026-10-06-fretish-rerun/diversity/ball-radii/` and copied into `scripts/`.
- **Exclusions.** `fsm-lmcps` is excluded because directed, not uniform as in the archive, has no repairs.
- **Cost columns.** `call_s_sum`, the summed per-pair seconds, replaces `query_wall_s` and `wall_s`; `queries` counts every planned direction.
