# 2026-11-17-fretish-order-remove-no-f-pooled

Registered 2026-11-18, before launch. The paper's run (4) once removal replaced demotion as the fourth mechanism: the pooled pass of the removal direction, for strength and diversity.

## Arms and contrasts

All four arms come from `2026-11-17-fretish-order-remove-no-f` at `580a6cc`, 19 subjects × 30 seeds, so the pool reads one run tree. `remove-full` stands for the paper's full: it is a byte-identical rerun of `lean-remove`, and it matched `2026-11-17-fretish-order-removal`'s `lean-remove` on 569 of 570 cells.

| Contrast | a | b |
|---|---|---|
| `no-climb` | `remove-no-climb` | `remove-full` |
| `no-gate` | `remove-no-gate` | `remove-full` |
| `no-blame` | `remove-no-blame` | `remove-full` |
| `exchange` | `remove-exchange` | `remove-full` |

The first three form one family, `removal`, with Holm over it. The fourth row of the direction, no-remove (`lean`) against full, is already measured: `2026-11-17-fretish-order-removal-pooled`'s `remove-over-lean` pools `lean-remove` and `lean` from one campaign.

`exchange` was added on 18 Nov, before launch, at the user's request through peredur-extension-b7. `remove-exchange` is the `2026-11-18-fretish-full4-exchange` arm at `3ba397fe` (clean): `lean-remove` with `gate_realizable = "exchange"`, its config differing from `remove-full` by that one line. It is not an ablation, so it sits in its own family, `exchange`, and is not corrected with the other three. It ran on the 9 small specifications only, at seeds 0–29, so the design splits into two groups: the small set carries all four contrasts, and rad's 10 cores carry the first three. A local smoke test of the same pipeline on `fsm`, `takeoff`, `fsm-timing` and `mode-arbiter` gave net containment at K = 3 of −0.051, −0.006, −0.116 and −0.003 for exchange, too few subjects for a test; this pass gives one without new engine runs.

## Measures

The same as `2026-11-17-fretish-order-removal-pooled`. Strength is the accepted-trace share from `scripts/joint_measures.py --share-only`, over 32,768 words per subject; a lower share is a stronger arm. Diversity is net containment at K = 3 from `scripts/pooled_reduce.py --pooled-balls`. Each contrast pairs subjects with the exact Wilcoxon signed-rank p and the Hodges–Lehmann shift with its 95% interval. `valu3s-uc6` and `lift-plus-cruise-mini` are sampled at 100 draws an arm.

## Inputs

`members/` was pooled locally on 2026-11-18 with `python3 scripts/pool_members.py experiments/2026-11-17-fretish-order-remove-no-f-pooled/design.toml experiments/2026-11-17-fretish-order-remove-no-f-pooled/members --root .`, with `experiments/results-fretish-order-remove-no-f-exchange` a directory of symlinks to both campaigns' collected runs: the 2,280 of `2026-11-17-fretish-order-remove-no-f` and the 270 of `remove-exchange`, copied from both hosts' `experiments/results-fretish-remove-exchange` (135 each, every one with `run.json`). All 2,550 runs are present, and 233 hold no member, 29 of them `remove-exchange`. Every subject pools to at least one node, from 18 (`fsm-lmcps`) to 10,383 (`valu3s-uc6`).

The pool was first built without `remove-exchange`. Adding the arm leaves rad's 10 pools byte for byte the same, and on each small subject keeps every earlier node and its number, appending nodes held by `remove-exchange` alone. Over `remove-exchange` and `remove-full` the pool gives `fsm` 695 nodes, `takeoff` 1,225, `fsm-timing` 1,697 and `mode-arbiter` 399, matching peredur-extension-b7's smoke test. The subject split is `2026-11-17-fretish-order-xover-pooled`'s: 16 subjects on av2, and `rad-core-33`, `valu3s-uc6` and `lift-plus-cruise-mini` on av3. The pool, `design.toml` and the subject lists are untracked and copied to each host by `rsync -a` before enqueue. Their SHA-256 sums are below.

## Hosts

av2 CPUs 0–15 and av3 CPUs 16–27, 32 GB each, approved by the user on 18 Nov. It queues behind peredur-extension-26's `2026-11-18-fretish-extension-lpc` (av2 088, av3 085), which holds CPUs 0–29 on both hosts until about 16:30. av3 leaves out CPUs 28–31, which peredur-extension-55 held until 12:00 on 18 Nov; the plan kept that split. Compare runs 12 jobs on both hosts.

## Estimate

`2026-11-17-fretish-order-removal-pooled` took 40 min on av2 and 36 min on av3 for three arms on the same split. Four arms give about 1.5–2 times its pairs, and compare runs 12 jobs, not 16, so about 60–100 min per host. The fifth arm adds about a quarter to the fingerprints and pairs of the small set, which sits on both hosts (`valu3s-uc6` and `lift-plus-cruise-mini` on av3), so add about 15–25 min to each. The estimate carries at least ±50%.

## Input checksums

```
ce7e1e67f474eaa1f73298d655d89f981b3d7916f034df22e0b24273c4743ea8  design.toml
20d88c80e1eb6995513a22c7416e0569bc830caf63ee45fe7b09807bdfee7528  subjects-av2.txt
855f78e4c637cbe7837c5da9cc7c40be51c4afd02754f6d6d7f9ea8567b4abf8  subjects-av3.txt
b85c19ee3e0fb4b13b1074794966531922e36fc9fb5368d3fe925ee0bd7611fa  members/done.csv
fb888d97a470a098545d94782066c4ee54097f6e809af20c2b16f0fe8fff0a9a  members/fsm-combined.jsonl.gz
0304e3323e6e1c3d9dc8fa36acec552ef5da546a8b09d3432f805b67e2781856  members/fsm.jsonl.gz
32f395be8b0162ba1a979c3cd69ec00071838a91ca80dbd82433454fa3ed40d3  members/fsm-lmcps.jsonl.gz
88536f430f34f85e80cacf6cf21e4aef1b566ea52e69e3ed7f6207755b5659f8  members/fsm-timing.jsonl.gz
53e9c471c420d34c7d9a5f14742111723de4c058fa20c28ca47f3018cb9cd3bc  members/index.csv
da0b9532e84e50c3ae10163e4994c8b902a6997029787ff9a0ae5f58f8f7b880  members/lift-plus-cruise-mini.jsonl.gz
ead4943466e928ee20c29e17e4a4ece080d18d9f1b210c9b104049ab30fbef89  members/liquid-mixer.jsonl.gz
852f653816c8a06ad8650ff8d43793e3ab32d14acd66f8da06277c9c0403369c  members/mode-arbiter.jsonl.gz
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
8a9a5d2db346c64b9a2e589a16935cb49eaaa244a6017203838c4562e6125c1d  members/takeoff.jsonl.gz
968eb37dca191ccf0959998ceceabb1fa87d7c5c5ae19c779cba722779e550e4  members/valu3s-uc6.jsonl.gz
```
