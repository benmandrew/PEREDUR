# 2026-11-17-fretish-order-removal-pooled

Registered 2026-11-17, before launch. The pooled pass of `2026-11-17-fretish-order-removal`, for its strength and diversity measures.

## Change from the registered measures

The campaign's PLAN.md registered strength as the net subsumption share. On 2026-11-17 the paper replaced it with the accepted-trace share (commit `83d19f4` in `peredur-extension`), and `2026-11-17-fretish-order-xover-pooled` measures strength the same way. This pass follows it. The change was made before any strength or diversity result of this campaign was computed, and before its run measures were read. Joint-front reach is not measured. Net subsumption comes out of the same verdicts and is not reported.

## Measures

1. **Strength (accepted-trace share).** Per subject, 32,768 words are drawn from models of the pooled repairs. A node's share is the fraction of them it accepts. An arm's score is the median share over its frontier classes, from `scripts/joint_measures.py --share-only`. A lower share is a stronger arm. Each contrast takes the reference's share less the arm's, so a positive value means the arm is the stronger.
2. **Diversity (net containment at K = 3).** From `scripts/pooled_reduce.py --pooled-balls`, with each arm's odd and even seeds as two half-pools, as the paper reports it.

The words differ between subjects and between pooled passes. Scores are compared within a subject of this pass only.

## Contrasts

The three contrasts of `design.toml`, as registered in the campaign's PLAN.md.

- `primary` (2, Holm): `lean-remove` against `lean-demote`, and `lean-remove` against `lean`.
- `secondary` (1): `lean-demote` against `lean`.

Each contrast pairs subjects. It reports the exact Wilcoxon signed-rank p, with ties at average rank, and the Hodges–Lehmann shift with its 95% interval, as in `peredur-extension/scripts/summarise.py`. The main result is over the 18 subjects of the paper's pooled analysis. `fsm-lmcps` is reported apart. `valu3s-uc6` and `lift-plus-cruise-mini` are sampled at 100 draws an arm, so their frontier is an estimate.

## Inputs

`design.toml` names the campaign's 3 arms, its 19 subjects and the three contrasts. `members/` was pooled locally on 2026-11-17 with `python3 scripts/pool_members.py experiments/2026-11-17-fretish-order-removal-pooled/design.toml experiments/2026-11-17-fretish-order-removal-pooled/members --root .` from `results-fretish-order-removal`, collected from av2 (1,080 runs) and av3 (630 runs). No run is missing. 156 runs hold no member: 29 of 30 per arm on `fsm-lmcps` and 23 of 30 per arm on `rad-core-10`. The pool holds 2.7 MB. It is untracked, as are `design.toml` and the subject lists, which are those of the xover-pooled pass. All four are copied to the same paths in each host's checkout by `rsync -a` before enqueue. Their SHA-256 sums are below, and the pool phase checks every member file against `members/index.csv`.

The pool holds 22 to 11,789 nodes per subject. `rad-core-33` holds 11,789, against 34,266 in the xover-pooled pass and 11,432 in the joint pass.

## Hosts

veribee-33 holds av2 CPUs 16–31, av3 CPUs 0–15 and both GPUs until about 22:00 on 17 Nov. This pass takes the other halves:

- **av3, CPUs 16–31, 32 GB.** It queues behind entry 074, peredur-extension-12's xover-pooled pass on the same CPUs.
- **av2, CPUs 0–15, 32 GB.** It queues behind entry 078, the av2 half of the same pass.

The split is by subject, as in the xover-pooled pass. av3 takes `rad-core-33` and the two sampled subjects. av2 takes the other 16. `confine` covers every phase, and the build takes a `taskset` chosen by host name.

## Estimate

The joint pass, five arms of about 2,300 nodes each on `rad-core-33`, took 1 h 40 min to fingerprint and 34 CPU-h to compare, 17.6 of them on `rad-core-33`. Pairs grow with the square of an arm's pool. Here three arms of about 3,900 nodes each give about 1.2 times the joint pass's pairs on `rad-core-33`, so about 21 CPU-h there and about 20 elsewhere. At 16 jobs that is about 1.3 h of compare on av3 and 1.3 h on av2, plus the fingerprint phase. The estimate carries at least ±50%.

## Order of work

1. Push the branch, copy the inputs to both hosts, enqueue on both.
2. `campaign.py collect --outputs fretish-order-removal-pooled`.
3. `joint_measures.py --share-only` and `pooled_reduce.py --pooled-balls` on this design, then the contrasts above.

## Input checksums

```
c4d40bdb219b11535a961c5c25ab7318f66926306e40788a400869818d9365f9  design.toml
20d88c80e1eb6995513a22c7416e0569bc830caf63ee45fe7b09807bdfee7528  subjects-av2.txt
855f78e4c637cbe7837c5da9cc7c40be51c4afd02754f6d6d7f9ea8567b4abf8  subjects-av3.txt
1246266788a2d9d6678217ccd3a89dae4f14db0e7854697821bee375c4d69c62  members/done.csv
ad11f3521d6e074d2d1e47d4a08f5a690cadf307d997f5c88576e14054cb32ab  members/fsm-combined.jsonl.gz
13ced9908ebe334f6c2deed0e5a87531526c6df28476139b2d49470fe972e476  members/fsm.jsonl.gz
79f69312869d0d40b0827c051138627aee571db35c6b53faa2565f81a2e699b4  members/fsm-lmcps.jsonl.gz
806fd811c5cd368913aa6dddd7d6a2848697205a1a84c39a4a97face91b4ddd4  members/fsm-timing.jsonl.gz
5ddd4803a622bd61487c2c9fa86d631ddb8e34f7ad2508400de7e4714cf78825  members/index.csv
34f31cfba31e0a1039341309996baf3d9a488ac22fe36be0d100657188634e05  members/lift-plus-cruise-mini.jsonl.gz
5ca8ae687c45575b911b4146e7f27db3f2a1f0b83aabc8ea29813b837d7a8483  members/liquid-mixer.jsonl.gz
6dc2f4cb264334bef5b9dfe38b04afd7fdb7c01ec6dd3a9308b9df7537eb1f40  members/mode-arbiter.jsonl.gz
79e2d000e42885d81e865f7fce60eb6e7366ce985ed16c18341e1272db14fa61  members/rad-core-10.jsonl.gz
ffdfdfe726553c072b4aaeda7c5b83808eeecf83ba4f10c1cc3a05e3fd9121cd  members/rad-core-1-18.jsonl.gz
4180a80eb738c0ce22f9ce9840127c1acb6d8f2570e20f0da4212296cc4afb5c  members/rad-core-12-18.jsonl.gz
8440dc9ff63ef5318f9479eb185eecaa8db28fc5cdea5548f6258555c35b3684  members/rad-core-17-18.jsonl.gz
265cb92a13ee2cd7f25eb8f04d6bd102957f5a16d52a17770f859276c43a0d06  members/rad-core-33.jsonl.gz
a2cc002746f96ac545f9ee9d4e12dd0465032a04f18d5b06fbf91b75ddcb3ce4  members/rad-core-45.jsonl.gz
6f3c6325d2ed11e4de310ae1144876002fe5dc0c529a5f50329be17bf539210e  members/rad-core-55.jsonl.gz
e5d198e83a26228103cb4526a85ef936f5f0365949ccf163a38706fc954594f3  members/rad-core-61.jsonl.gz
4c157e6b241eaf570cb060f1bec78038098ec65c4ffb84eeb1af3beb43c65689  members/rad-core-65.jsonl.gz
1cfa84733e59e47c7001195cad0bc2451fcf48d6f8cb2d4528382c7566834eae  members/rad-core-68.jsonl.gz
a2e438d31ef5b78883c019214a5736ae7ff13c6e722a1a206a2a9fd7b034cd24  members/takeoff.jsonl.gz
14811907c7264f683a8d5072c0dfe7ef8cc07d5439194dfcb3bf131608a0f362  members/valu3s-uc6.jsonl.gz
```
