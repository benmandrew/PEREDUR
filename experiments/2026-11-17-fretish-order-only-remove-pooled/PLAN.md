# 2026-11-17-fretish-order-only-remove-pooled

Registered 2026-11-17, before launch. The pooled pass of question 1 of `2026-11-17-fretish-order-removal-followup`: `only-remove` against `base`, for strength and diversity.

## Measures

The same as `2026-11-17-fretish-order-removal-pooled`. Strength is the accepted-trace share from `scripts/joint_measures.py --share-only`, over 32,768 words per subject; a lower share is a stronger arm. Diversity is net containment at K = 3 from `scripts/pooled_reduce.py --pooled-balls`. The contrast pairs subjects over the 18 subjects of the paper's pooled analysis, with the exact Wilcoxon signed-rank p and the Hodges–Lehmann shift with its 95% interval. It is one test, with no Holm family, and is reported beside the paper's `only-<f>` rows as a follow-up. `valu3s-uc6` and `lift-plus-cruise-mini` are sampled at 100 draws an arm.

## Inputs

`base` comes from two campaigns, so `results-fretish-order-only-remove-pooled-runs/` was built locally of symlinks under one pattern, `sweep_D_{level}_…`:

- `only-remove`, all 19 subjects: `results-fretish-order-only-remove`, sweep D, at `612876c` (`2026-11-17-fretish-order-removal-followup`).
- `base`, `lift-plus-cruise-mini` and `valu3s-uc6`: the same campaign's sweep O `base` rerun.
- `base`, the other 17 subjects: `results-fretish-order-xover-rate`'s `cr10-base`, at `3c42822`. The follow-up's PLAN.md records why these reproduce.

`members/` was pooled locally on 2026-11-17 with `python3 scripts/pool_members.py experiments/2026-11-17-fretish-order-only-remove-pooled/design.toml experiments/2026-11-17-fretish-order-only-remove-pooled/members --root .`. All 1,140 runs are present, and 96 hold no member. `fsm-lmcps` has no repair in either arm, so it pools to no nodes and is left out of `subjects-av3.txt`. The other 18 subjects hold 73 to 10,026 nodes. The pool, `design.toml` and the subject list are untracked and copied to av3 by `rsync -a` before enqueue. Their SHA-256 sums are below.

## Host

All on av3, CPUs 16–31, 32 GB, as allocated by peredur-extension-d5 and approved by the user on 17 Nov. It queues after `2026-11-17-fretish-order-removal-followup` (076) and before the av3 half of `2026-11-17-fretish-order-full4-no-f`.

## Estimate

`2026-11-17-fretish-order-removal-pooled` took 40 min on av2 and 36 min on av3 for three arms. Two arms here give about two thirds of its within-arm pairs and one contrast of three. All 18 subjects on one host gives about 45–75 min. The estimate carries at least ±50%.

## Input checksums

```
c180688b7cdf67b677dbdaf1d44d98be5ac26d8c3cb81e93f544abb679dd1e88  design.toml
db855cdf52887c0db425b8af090df8a3430e0f1aa9d170eccc115d7ecac2af0a  subjects-av3.txt
3f039d40c8f64d163005d16d7ffa69d6bf0940340ef1ba1a15497cbdccb28068  members/done.csv
cbce3b832d1cd3da989663f5e9af014fc23f9a4c374da1fc5f2e863013a17f02  members/fsm-combined.jsonl.gz
39dc6845e5163d59a572bf110272f17b7625628346fbfd3c410252bc221f7319  members/fsm.jsonl.gz
9ceffb7310338057cfe71a4ae1e2c98d2c485d81cdef906532a801f457a38d64  members/fsm-lmcps.jsonl.gz
b9e3aeab5d0930458e3f7d6b437eaa0c2a3b4c72aeb6b74c0bb028c697503f42  members/fsm-timing.jsonl.gz
8ab1a86e27bc6197ea116f3caacf27ab8071107c14c200ce383928185128d1b1  members/index.csv
3c9eabbc55a6af7a7ee91bd4253e3b32315d946a1332b64be020aa050e91e9bd  members/lift-plus-cruise-mini.jsonl.gz
1fcddb0a85b48f6af14d63687f2dfb835e333dcaed65fbd2f6471fb154eefb1b  members/liquid-mixer.jsonl.gz
01bfbef39e4c328089624623151b8f7978a80827a3a65a05781128e1a657c02c  members/mode-arbiter.jsonl.gz
f25284e9a6de0e0bb61cba0ed24340324032d7f56b492628ecd46638714569d7  members/rad-core-10.jsonl.gz
dca3463ec32989aca0d84f7be44455707a39707bf1aae233914977f38e9f784e  members/rad-core-1-18.jsonl.gz
b9b9b2625b4e5319e96a5d26649b95fbfd9923db1ac6045930c556203ee5606d  members/rad-core-12-18.jsonl.gz
85b5d26bbabe454db3dd0f972213c894697c3855113694e758a0d3b28d7c8bea  members/rad-core-17-18.jsonl.gz
9d5f04be239e4d5bfab88a578e889de5d502b7a85ebdd81bbcd1503e035a39bc  members/rad-core-33.jsonl.gz
48b7b3f3a56673badae9b2d9191c4ffa866208c6f006022dd74dec5373fbb543  members/rad-core-45.jsonl.gz
8acd9ca2b8e945df2c1ffd246ec36aff93e535cad5faef0f5356aca24bfc29c7  members/rad-core-55.jsonl.gz
42731dc12d973c96a72c3aa71ac97eeacf160e8fdc7de2ed1ca58a2d253dc87d  members/rad-core-61.jsonl.gz
88c6aed0233449bc8b9a53ae077b83d3ac50159374f633115abf8c91c2828e1e  members/rad-core-65.jsonl.gz
8d9855e93149f3ba84057456c147c0b947f8e6a640cf966847d8c94903ae881d  members/rad-core-68.jsonl.gz
6db6ee1e52194d6882d7e47e44adcaade94e4e64bb983ab90c05189e9bed20d5  members/takeoff.jsonl.gz
0007459ddf132814255d1ee75275a1bd6675cd262cdf6685311abfd4b9d174d0  members/valu3s-uc6.jsonl.gz
```
