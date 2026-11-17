# 2026-11-17-fretish-order-xover-pooled

Registered 2026-11-17, before launch. The pooled pass of `2026-11-17-fretish-order-xover-rate`, for its strength and diversity measures.

## Change from the registered measures

The campaign's PLAN.md registered strength as the net subsumption share. On 2026-11-17, at 12:46, the paper replaced it with the accepted-trace share (commit `83d19f4` in `peredur-extension`, after `2026-11-16-fretish-order-pooled-joint`). This pass measures strength by accepted-trace share to match. The change was made before any strength or diversity result of this campaign was computed. The run measures (found-repair rate, ideal rate, repairs per run, wall time and the order-crossover counters) had already been read. Order crossover lowered repairs per run in all six primary contrasts, by 1.0 to 7.5 (Hodges–Lehmann). Strength is the open question that result raises: are the fewer repairs weaker, or only fewer? Joint-front reach is not measured. Net subsumption comes out of the same verdicts and is not reported.

## Measures

1. **Strength (accepted-trace share).** Per subject, 32,768 words are drawn from models of the pooled repairs. A node's share is the fraction of them it accepts. An arm's score is the median share over its frontier classes, from `scripts/joint_measures.py --share-only`. A lower share is a stronger arm. Each contrast takes the reference's share less the arm's, so a positive value means the arm is the stronger.
2. **Diversity (net containment at K = 3).** From `scripts/pooled_reduce.py --pooled-balls`, with each arm's odd and even seeds as two half-pools, as the paper reports it.

The words differ between subjects and between pooled passes. Scores are compared within a subject of this pass only, never with the paper's five-arm numbers.

## Contrasts

The ten contrasts of `design.toml`, in two Holm families, as registered in the campaign's PLAN.md.

- `primary` (6): `only-xover` against `base` and `lean-xover` against `lean`, at crossover rates 0.1, 0.3 and 0.5.
- `rate` (4): `cr30` and `cr50` against `cr10`, within `base` and within `lean`.

Each contrast pairs subjects. It reports the exact Wilcoxon signed-rank p, with ties at average rank, Holm within its family, and the Hodges–Lehmann shift with its 95% interval, as in `peredur-extension/scripts/summarise.py`. The main result is over the 18 subjects of the paper's pooled analysis. `fsm-lmcps` is reported apart. `valu3s-uc6` and `lift-plus-cruise-mini` are sampled at 100 draws an arm, so their frontier is an estimate.

## Inputs

`design.toml` names the campaign's 12 arms, its 19 subjects and the ten contrasts. `members/` was pooled locally on 2026-11-17 with `python3 scripts/pool_members.py experiments/2026-11-17-fretish-order-xover-pooled/design.toml experiments/2026-11-17-fretish-order-xover-pooled/members --root .` from the collected `results-fretish-order-xover-rate`, with no missing run. It holds 12 MB, so it is untracked, as are `design.toml` and the subject lists. All four are copied to the same paths in each host's checkout by `rsync -a` before enqueue. Their SHA-256 sums are below, and the pool phase checks every member file against `members/index.csv`.

The pool holds 1.9 to 8.3 times the nodes of the joint pass per subject (`rad-core-33`: 34,266 against 11,432).

## Hosts

Agreed on 17 Nov at about 13:15 UTC with `veribee-33` and `2026-11-xx-fret-collab-a5`:

- **av3, CPUs 16–31, 32 GB.** Free since a5's entry 073 ended at 12:40. veribee-33's AFL units hold CPUs 0–15 for about 8 h.
- **av2, CPUs 0–15, 32 GB.** Free once a5's entry 077 ends, at about 13:40. This entry queues behind 077. veribee-33 holds CPUs 16–31 until about 20:00.

The split is by subject. av3 takes `rad-core-33`, which was half the joint pass's compare time, and the two sampled subjects. av2 takes the other 16. `confine` covers every phase. The build takes a `taskset` chosen by host name.

## Estimate

The joint pass, five arms on the same subjects, took 1 h 40 min to fingerprint and 2 h 15 min to compare at 16 jobs (34 CPU-h of compare, 17.6 of them on `rad-core-33`). Pairs grow with the square of an arm's pool. Here there are 12 within-arm sets and 10 cross-arm contrasts, against 5 and 10 in the joint pass. Scaling by both gives about 65 CPU-h of compare, about 40 of them on `rad-core-33`, which is about 3 h on av3 and 2 h on av2 at 16 jobs, plus the fingerprint phase. The estimate carries at least ±50%.

## Order of work

1. Push the branch, copy the inputs to both hosts, enqueue on both.
2. `campaign.py collect --outputs fretish-order-xover-pooled`.
3. `joint_measures.py --share-only` and `pooled_reduce.py --pooled-balls` on this design, then the contrasts above.

## Input checksums

```
fe9c308b32c7635ce887e141f096b29aa484668df79afe8e9290c2f85c2c6c0f  design.toml
20d88c80e1eb6995513a22c7416e0569bc830caf63ee45fe7b09807bdfee7528  subjects-av2.txt
855f78e4c637cbe7837c5da9cc7c40be51c4afd02754f6d6d7f9ea8567b4abf8  subjects-av3.txt
2d3aa2075b260a20d405e42a368f9846c50066916f990094ec3cd795f053dd4e  members/done.csv
3286e0a6840ded8586d632caabd1a7a2b0527626f9d47323e98eacbd11ce8ea0  members/fsm-combined.jsonl.gz
7efa86e78f5466dace86ad01ca46071f7be36a78fedfa447a8d10e6d1af51520  members/fsm.jsonl.gz
938a696bb05b9ba3dc62257d5e83d5bfb0e4b548f574afb285c149a8cd10fa90  members/fsm-lmcps.jsonl.gz
3ea6f7027aa85434f2f2ff32aafe88cca6995d8be8d8fec47a582cfe905c8778  members/fsm-timing.jsonl.gz
1ac724b20e574d1af8260445c6987bb55c83c274acfedf4c7f7de675038d7d43  members/index.csv
a3c504d0ef999d8a7a15fcf53457642d627a8bcd87e94bb0b1d0fab87efb1c03  members/lift-plus-cruise-mini.jsonl.gz
fa9f796f3703d6e7844bc03fc0526929ba1c8d529eb0b14d4b5b17d95fe947f7  members/liquid-mixer.jsonl.gz
9d4ea50392608418f5228341de4d7ecab384e4197367b195ce9b3367be24f89a  members/mode-arbiter.jsonl.gz
3e2238683a49d279aaa01133c0c719d33b59d7c89bcd972d4994e16362f99dc7  members/rad-core-10.jsonl.gz
2225f072a7e50bb76ac9d5df66323e3dfdcd025b79a799cb67e9f0d585bcbc24  members/rad-core-1-18.jsonl.gz
8e60264e7d909fc67cee1d96b93180f2afd383a512aa1faae825ecc8714de9fa  members/rad-core-12-18.jsonl.gz
b3157cc1983376144adf8520108f9e9430b21414c6633b668b705d2f33b8ba10  members/rad-core-17-18.jsonl.gz
69a88e90c67bc38540aab0c4091bce169176048a034a692d57f469c24f3cb397  members/rad-core-33.jsonl.gz
f20072de89f8c84c4412b7304cc8d49ee3e336b2169af3a504ecf4fb488eed6c  members/rad-core-45.jsonl.gz
ee45f9e743ccbbe53a05ae23f900a3bdea511896e8cf92dd25bc59044b66ca21  members/rad-core-55.jsonl.gz
894883a9abb27b9cfeaf1df833e5920e10a9cb3220569e80d47b60fc5bd8feac  members/rad-core-61.jsonl.gz
b0bd3940aaaf7344c8cfe6fbeb83909a72a211e4497a8041fdacffe61994dc14  members/rad-core-65.jsonl.gz
1ee20535bd0567a4d6920f957966c9dea401cb2a975c3439dcf860e04fcf7aa9  members/rad-core-68.jsonl.gz
d57fd484f28b73a0dad4aeecfe51d7aeecc8cf50f4a3f57329c98357218f628b  members/takeoff.jsonl.gz
0af21da752374dd70eacbc9fab0b2dfe1fc922ce8154d14f78176bcc373ee447  members/valu3s-uc6.jsonl.gz
```
