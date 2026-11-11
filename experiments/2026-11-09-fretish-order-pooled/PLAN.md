# 2026-11-09-fretish-order-pooled

The pooled strength-and-diversity pass of 2026-10-08-fretish-rerun-analysis, run on the arms of 2026-11-05-fretish-order and 2026-11-09-fretish-order-cores. `design.toml` declares the subjects, arms and contrasts: each `only-<f>` arm and `full` against `base`, all under nsga2-apportion. valu3s-uc6 and lift-plus-cruise-mini are estimated from 100 draws an arm; every other subject is exhaustive. lift-plus-cruise-full was dropped on 2026-11-11 (see "Audit of 2026-11-11").

## Order of work

The small specs and lift-plus-cruise-full ran split by seed (av2 0-14, av3 15-29), so no host holds all of their runs. Their pool was built locally from both hosts' collected runs and committed as `members/` (with `git add -f`, since `/experiments/*/*` ignores it, as it ignores `design.toml` and the subject lists). The cores are split by core (`2026-11-09-fretish-order-cores/PLAN.md`), so each host holds every seed of its own cores and pools them itself.

1. Done: collect `experiments/results-fretish-order` and `-large`, pool the ten non-core subjects with `python3 scripts/pool_members.py experiments/2026-11-09-fretish-order-pooled/design.toml experiments/2026-11-09-fretish-order-pooled/members --subjects ...`, check `members/index.csv` for `runs_missing` (0) and `runs_empty`, and commit.
2. Enqueue this campaign on each host behind `2026-11-09-fretish-order-cores`. On each host, the `pool` phase writes `experiments/fretish-order-pooled-members` (ignored): it copies the committed pool of each small subject in `subjects-<host>.txt` byte for byte, and pools each of the host's cores from its 30 seeds. A missing or unfinished core run fails the phase. The `fingerprint` and `subsumption` phases follow on the same host.
3. Done on 2026-11-10: `campaign.py collect --outputs fretish-order-pooled --host <host>` for av2 and av3 (av1 never ran the pass), then `campaign.py collect --profile fretish-order-cores --host <host>` for each, which merged 5,400 rows with no duplicate key. `pool_members.py` pooled the ten cores locally, and eight matched their host's `sha256_jsonl` at once. rad-core-17-18 and rad-core-33 did not, because both hosts held 962 of the cores' cells: each had started the other's cores in the cancelled first launch. av3 had left 14 of av2's cells unfinished at seed 20 (nine of rad-core-17-18, five of rad-core-33), and collecting av3 second laid their partial `accumulated/` files over av2's finished runs while av2's `run.json` stayed, so the pool read each as finished. An `rsync -a --delete` from av2 of those 14 directories alone restored them, since av2 ran every seed of its cores, and the re-pooled cores then matched. The hosts' `done-subjects-<host>.csv` files end lines with CRLF, so a shell comparison against them needs `tr -d '\r'`.
4. Done on 2026-11-10: the reduction above, 30 minutes on one core, then the pooled diversity measure with `--pooled-only --pooled-draws 20 --jobs 10` under `taskset -c 8-19`, 20 minutes at 4.4 GB peak. Both write under `reduced/`.

Each host's subject list holds its own cores and about half the planned pairs of the other subjects, from a local plan at 32768 words and seed 3 on 2026-11-09. av2 takes fsm-timing (335,899 pairs), valu3s-uc6 (1,563) and fsm-lmcps (0). av3 takes fsm-combined (116,336), fsm (68,720), takeoff (57,411), mode-arbiter (36,649), liquid-mixer (13,825), lift-plus-cruise-mini and lift-plus-cruise-full, which is 292,941 pairs before the two sampled subjects. The two subjects slowest to fingerprint, valu3s-uc6 (21 min locally) and lift-plus-cruise-mini (over 25 min), are on different hosts. The cores' pair counts are unknown until their hosts pool them.

## Pool sizes

Nodes in the 9 design arms, from `maximal.tsv`: fsm 2837, fsm-combined 5976, fsm-lmcps 8, fsm-timing 7457, lift-plus-cruise-mini 8977, liquid-mixer 12856, mode-arbiter 2312, takeoff 3319, valu3s-uc6 31777, lift-plus-cruise-full 2463 (base and full). On fsm-lmcps, only full (1 member) and only-blame (7) hold any member, and base holds none, so its contrasts carry almost no information. The cores are pooled on their hosts.

## Cost estimate

A local fingerprint of fsm-timing at 32768 words (seed 3, 253 s at 8 workers) plans 335,899 pairs, or 337,999 directions (2,100 both ways). At the 2026-10-08 pass's 0.51 call-seconds a direction, that is about 48 CPU-h, about 2 h of wall time at 24 jobs. No calibration campaign measures it first; the main pass's first fsm-timing rows give the real figure.

## Audit of 2026-11-11

An audit found four faults that void the Results below, which are kept as the first pass's record. The pass must be re-run from the pool phase.

1. Every arm with the output climb on was pooled from leftovers. With `members = "maximal"`, the pool read `accumulated/maximal.tsv`, which lists only stream members that carry a file name (`src/filter/streaming_maximal.hpp:274`). The climb pushes its results without one (`src/genetic/output_gate.cpp:257`, `:265`), so they reach `repair_N.json` and never the listing. On the cores, only-climb's listings hold 3,529 specs against 11,593 in its output, and full's 7,379 against 13,578. Without the climb, listing and output agree in all 3,000 core runs. `design.toml` now reads `members = "output"`, so the committed `members/` pool is stale and must be rebuilt from both hosts' small-spec runs.
2. 1,424,061 of the 1,953,959 compare rows (73%) are SIGABRT, and no row timed out. `compare_pairs.py` pointed `PEREDUR_BLACK_PATH` at `third_party/black/install/bin/black`, which fails to load `libblack.so` without the build's wrapper; `src/runner/black.cpp:453` then throws uncaught. Every `?` verdict in this pass is such a crash: 99.99% of lift-plus-cruise-full's rows, 98.9% of lift-plus-cruise-mini's, 97.5% of rad-core-33's, 78% of fsm-combined's and 71% of fsm-timing's. `compare_pairs.py` now uses the wrapper and refuses to start when black does not run. A sample of 40 crashed pairs on each of the five worst subjects all decided with the wrapper, at 0.04 s a pair on rad-core-33 and 1.3 s on lift-plus-cruise-full.
3. The `decided` reading equalled `non` on every exhaustive row, since a dropped pair's class stayed in the denominator and a skipped direction reads 0. `pooled_reduce.py` no longer reports it.
4. A member whose K-th nearest other shares no word has radius 1.0, and a ball of radius 1.0 held every point. 4.5% of lift-plus-cruise-mini's base members had one at K = 1. A point sharing no word with a member is now outside its ball.

lift-plus-cruise-full is dropped. Its base pool's 1,604 nodes have 44 distinct fingerprints, with a median pairwise distance of 0, so its pooled diversity reads duplicates, and its compare phase would have cost about 71 CPU-h of the re-run's estimated 90.

The audit also checked, and found sound: the arm configs (each differs from base in exactly its keys, and only-climb's search matches base's in 300 of 300 core runs), the fingerprint plan (SPOT agrees with `fpdraw` on 40 of 40 checks), node numbering across hosts, the strength sign and estimator, the Wilcoxon and Holm code, and the ball-radius arithmetic (fsm/full recomputed to 4 decimals). Minor faults it left in place: Holm corrects within one family and one K only, so the one-contrast `full` family's Holm p is its raw p; a subject with K or fewer members is dropped at every K; and 15 core runs re-run in place hold a doubled `accumulated/index.tsv` (append mode, `src/genetic/accumulator.cpp:72`), which inflates their host-scored curves and leaves the pools alone.

A sensitivity check on the first pass, restricted to the 11 subjects with under 1% crashed rows, left every non-climb contrast where it was: only-gate's median net is +0.188 there, stronger on 10 of 11, against +0.187 over all subjects. Eight of those 11 are rad cores. The climb and full rows have no such check, since their pools are wrong on every subject.

## Results

The tables below are the first pass's, voided by "Audit of 2026-11-11".

### Strength

`reduced/strength/summary.txt` reports each contrast over subjects, with *net* positive when the feature arm is the stronger. Under the `non` reading (an undecided direction counts as no implication), gate, demote and the full set all make the arm stronger than base, and the climb makes it weaker.

| Contrast | Subjects | Median net | Arm / base stronger | Holm p |
|---|---|---|---|---|
| only-gate | 18 | +0.187 | 17 / 1 | 0.0001 |
| full | 19 | +0.039 | 14 / 4 | 0.0056 |
| only-demote | 18 | +0.009 | 14 / 4 | 0.014 |
| only-climb | 17 | −0.032 | 0 / 14 | 0.0007 |
| only-xover | 18 | +0.020 | 13 / 5 | 0.15 |
| only-scale | 18 | +0.008 | 10 / 6 | 0.48 |
| only-blame | 18 | +0.001 | 9 / 6 | 0.98 |
| only-verdict | 18 | 0.000 | 6 / 8 | 0.98 |

The `decided` and `imp` readings agree on every sign. Gate, climb and full stay significant under all three readings. Demote's Holm p is 0.0066 under `decided` and 0.083 under `imp`, so demote holds only while an undecided direction does not count as an implication.

### Per-run diversity is unusable for climb and full

The per-run ball radii in `reduced/ball-radii/` read one run's members as a group, and a group of K or fewer members has no radius. Climb runs are small: over the 18 subjects with members, only-climb's median run holds 4 members and 61% of its runs hold 5 or fewer, against a median of 54 for base. Full's median is 13, with 19% at 5 or fewer. At K = 3, only-climb's "b in a" line rests on 9 subjects, the survivors being its unusually large runs. Every other arm has a median of 45 to 58 and at most 0.2% of runs at 5 or fewer, so the per-run figures stand for those six contrasts, and none of them passes Holm. fsm-lmcps (58 of 60 runs empty) and rad-core-10 (166 of 240) carry almost no information under either measure.

### Pooled diversity

`--pooled-only` writes `reduced/ball-radii-pooled/`. Each arm's distinct nodes over its odd seeds form one half-pool and over its even seeds another, and all four half-pools of a contrast are subsampled to the smallest one's size. A contrast pairs a's odd half with b's even half and the reverse, and an arm's control pairs its own two halves, so every comparison is between disjoint seeds at one size.

Seed-disjoint halves are required because an arm whose feature never fires repeats base run for run. At the same seed, 33% of only-blame's runs (175 of 523) and 28% of only-verdict's (146 of 522) have exactly base's member set, against at most 2% for every other arm. A first version pooled all 30 seeds per arm and compared them with 15-seed half-pool controls. It showed only-verdict's pool inside base's region at 0.82 against a base control of 0.64, which was those shared runs and the unequal sizes.

With disjoint halves, only the climb and the full set differ from their controls, and in one direction. Base's nodes fall inside the feature arm's region less often than the feature arm's own other half does, while the feature arm's nodes fall inside base's region about as often as base's do. The climb and the full set therefore reach a narrower region than base, and most of it lies within base's.

| Contrast | K | Base in arm vs arm's control | Holm p | Arm in base vs base's control | Holm p |
|---|---|---|---|---|---|
| only-climb | 1 | 0.425 vs 0.549 | 0.0081 | 0.502 vs 0.470 | 1 |
| only-climb | 3 | 0.604 vs 0.760 | 0.011 | 0.719 vs 0.677 | 1 |
| only-climb | 5 | 0.679 vs 0.830 | 0.021 | 0.806 vs 0.768 | 1 |
| full | 1 | 0.425 vs 0.566 | 0.0056 | 0.479 vs 0.469 | 0.24 |
| full | 3 | 0.595 vs 0.741 | 0.0079 | 0.693 vs 0.645 | 0.23 |
| full | 5 | 0.671 vs 0.805 | 0.016 | 0.786 vs 0.720 | 0.17 |

Gate, demote, scale, crossover, verdict and blame show no difference from their controls at any K (the smallest Holm p is 0.12, only-blame at K = 1). Gate makes repairs stronger without changing their spread.

Whether the full set narrows because it contains the climb is untested here. Sweep O ran a `no-climb` arm on every subject, but `design.toml` pools only the `only-<f>` arms and full, so answering it means adding `no-climb` to the design and running the fingerprint and compare phases again.
