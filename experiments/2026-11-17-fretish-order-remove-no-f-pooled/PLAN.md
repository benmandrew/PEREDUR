# 2026-11-17-fretish-order-remove-no-f-pooled

Registered 2026-11-18, before launch. The paper's run (4) once removal replaced demotion as the fourth mechanism: the pooled pass of the removal direction, for strength and diversity.

## Arms and contrasts

All four arms come from `2026-11-17-fretish-order-remove-no-f` at `580a6cc`, 19 subjects × 30 seeds, so the pool reads one run tree. `remove-full` stands for the paper's full: it is a byte-identical rerun of `lean-remove`, and it matched `2026-11-17-fretish-order-removal`'s `lean-remove` on 569 of 570 cells.

| Contrast | a | b |
|---|---|---|
| `no-climb` | `remove-no-climb` | `remove-full` |
| `no-gate` | `remove-no-gate` | `remove-full` |
| `no-blame` | `remove-no-blame` | `remove-full` |

The three form one family, `removal`, with Holm over it. The fourth row of the direction, no-remove (`lean`) against full, is already measured: `2026-11-17-fretish-order-removal-pooled`'s `remove-over-lean` pools `lean-remove` and `lean` from one campaign.

## Measures

The same as `2026-11-17-fretish-order-removal-pooled`. Strength is the accepted-trace share from `scripts/joint_measures.py --share-only`, over 32,768 words per subject; a lower share is a stronger arm. Diversity is net containment at K = 3 from `scripts/pooled_reduce.py --pooled-balls`. Each contrast pairs subjects with the exact Wilcoxon signed-rank p and the Hodges–Lehmann shift with its 95% interval. `valu3s-uc6` and `lift-plus-cruise-mini` are sampled at 100 draws an arm.

## Inputs

`members/` was pooled locally on 2026-11-18 with `python3 scripts/pool_members.py experiments/2026-11-17-fretish-order-remove-no-f-pooled/design.toml experiments/2026-11-17-fretish-order-remove-no-f-pooled/members --root .`, with `experiments/results-fretish-order-remove-no-f` a symlink to that campaign's collected run tree. All 2,280 runs are present, and 204 hold no member. Every subject pools to at least one node, from 17 (`fsm-lmcps`) to 8,901 (`valu3s-uc6`). The subject split is `2026-11-17-fretish-order-xover-pooled`'s: 16 subjects on av2, and `rad-core-33`, `valu3s-uc6` and `lift-plus-cruise-mini` on av3. The pool, `design.toml` and the subject lists are untracked and copied to each host by `rsync -a` before enqueue. Their SHA-256 sums are below.

## Hosts

av2 CPUs 0–15 and av3 CPUs 16–27, 32 GB each, approved by the user on 18 Nov. It queues behind peredur-extension-26's `2026-11-18-fretish-extension-lpc` (av2 088, av3 085), which holds CPUs 0–29 on both hosts until about 16:30. av3 leaves out CPUs 28–31, which peredur-extension-55 held until 12:00 on 18 Nov; the plan kept that split. Compare runs 12 jobs on both hosts.

## Estimate

`2026-11-17-fretish-order-removal-pooled` took 40 min on av2 and 36 min on av3 for three arms on the same split. Four arms give about 1.5–2 times its pairs, and compare runs 12 jobs, not 16, so about 60–100 min per host. The estimate carries at least ±50%.

## Input checksums

```
1cac0381663c2f33628d12b8414680ba831c674f47595ca49e9935e9a367058e  design.toml
20d88c80e1eb6995513a22c7416e0569bc830caf63ee45fe7b09807bdfee7528  subjects-av2.txt
855f78e4c637cbe7837c5da9cc7c40be51c4afd02754f6d6d7f9ea8567b4abf8  subjects-av3.txt
f65cf5f65d164915bf4290878b3ba91fbbf57acd27017e6a3b1fce1bc68eb55f  members/done.csv
513ed485dfe729faec21fb788250327b08d2a4fc662bbab32e246e892bba2d5d  members/fsm-combined.jsonl.gz
e051ce8edd8d3928e5d1baf3432752596465addf763de61cf92c97d064aa9906  members/fsm.jsonl.gz
76782da28e219d7c827f77f708bab3f56b5b10f6bfa0f3619d9362ba93f3a75d  members/fsm-lmcps.jsonl.gz
53f892869c6c08f5af8f537f19848964337eadfa55c25728eefceeb70905fcf7  members/fsm-timing.jsonl.gz
ed2e7b4a07d19d9bd530e0e4917eaca8b5284642d51ba8ce36f2042bda29f697  members/index.csv
6a43f178320c8f2320abdb9ea4ae1c0db770cebf3e647344c9530e3bccf530f4  members/lift-plus-cruise-mini.jsonl.gz
56edfd1290a6db059028ecd28c852ea70ee3cb5904c373a3b6aabb0b9fe4e1da  members/liquid-mixer.jsonl.gz
ee29f72defc6214091dd7456af76b21eca06a87f21b9b8344284a49c8abb1364  members/mode-arbiter.jsonl.gz
419e33b180ee8437d0966afb48821db6e541e4459b9089a5c2f8a3bf5623189c  members/rad-core-10.jsonl.gz
952a410427e92601e4b34c5cb48d83f967bd48e3439bdbbf52bcbc791d79ff1a  members/rad-core-1-18.jsonl.gz
9f97c0e3e455ea3d3d6ae110a0427215c2c531b0b06dc67a1c5846cabf11bda7  members/rad-core-12-18.jsonl.gz
dbd7b2ce35312fbd94573dbc0c7c74701bd7edb523bf76f65e874e85b39230ba  members/rad-core-17-18.jsonl.gz
a90ddc5dc47142fb4894d4504cebf9f19dec6986118e1fd400ffe7f9a2bcc1cf  members/rad-core-33.jsonl.gz
41b06ca4fd53dc6a1b8aac93fc6165d2b3f284b85159e2e259d477522846519d  members/rad-core-45.jsonl.gz
2d752fb5b5771b20b0489262b5ddc88e75069b853b759a28c70f5ab8bf8c7a3b  members/rad-core-55.jsonl.gz
e1170a12a531d794455ce91f55060c197c285abb4313ca5e1510f1ad989ecc9c  members/rad-core-61.jsonl.gz
b2d91c1a4470e28530cdce2c9572df7e21972fb574b24185d312975d3508f027  members/rad-core-65.jsonl.gz
b99d5c62398692f5cc8230ac4b2932e07b721a36ee3ed157b4eb2ee19102010f  members/rad-core-68.jsonl.gz
6e11e9214264d6fd317b53694c3a9c16c7e93fb3947b24d5d660d3463494b012  members/takeoff.jsonl.gz
6776773abe8ae0de885d0d73bc8f72e4daa29d7d93f526b278e3a9b47a9f7ba0  members/valu3s-uc6.jsonl.gz
```
