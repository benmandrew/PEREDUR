# 2026-11-19-diversity-pooled

Registered 2026-11-19, before launch. `2026-11-19-diversity-screen-blame` ran the paper's full (sweep D's `remove-exchange`, blame on) at `climb_orders` 1 and 4, as sweep K's `full-k1` and `full-k4`, 30 seeds each. This is the pooled pass over those two arms, for strength and diversity. It follows `2026-11-19-fretish-exchange-pooled` and avoids the four faults in the audit of `2026-11-09-fretish-order-pooled`: the design reads `members = "output"`, `compare_pairs.py` uses the build's black wrapper, and the reduction no longer reports `decided` and keeps a point that shares no word outside a ball.

## Arms and contrasts

Both arms come from profile `diversity-climb-blame` at `8ac27a3` (all 1,140 rows `dirty=0`), seeds 0–9 on av2 and 10–29 on av3, collected into `experiments/results-diversity-climb-blame` on 2026-11-19. Every run has `run.json`.

One contrast, `full-k4` against `full-k1`, in family `orders`. Holm over one contrast is the raw p.

More arms join by lines in `design.toml`, whose header says how: one contrast line an arm, and one `sources` line when the arm's runs are in another results tree or at other seeds. This serves `diversity-screen-blame` (the six mechanisms, seeds 0–9, `experiments/results-diversity-screen-blame`) or a later 30-seed profile. `sources` is new in `scripts/pooled_design.py` for this pass. A new arm renumbers every node, so the pool, the fingerprints and the pairs are rebuilt under new output paths.

## Subjects

The small set less `fsm-lmcps`, and rad's 10 cores: 18 subjects. No arm of these holds 3 or fewer members: the smallest is `rad-core-10`, with 10 members (9 nodes) an arm, from the 7 of 30 runs a repair. `valu3s-uc6` and `lift-plus-cruise-mini` are sampled at 100 draws an arm, as in the template. On all 10 rad cores the two arms hold the same node set with the same member counts, so their contrasts there read 0.

Pool sizes (nodes): fsm 277, fsm-combined 430, fsm-timing 893, lift-plus-cruise-mini 1,372, liquid-mixer 1,112, mode-arbiter 242, takeoff 686, valu3s-uc6 1,714, rad-core-1-18 926, rad-core-10 9, rad-core-12-18 929, rad-core-17-18 912, rad-core-33 1,071, rad-core-45 661, rad-core-55 798, rad-core-61 720, rad-core-65 556, rad-core-68 649.

## Measures

The template's first two.

1. Strength: accepted-trace share from `scripts/joint_measures.py --share-only`, over 32,768 words per subject; a lower share is a stronger arm. Net subsumption from `scripts/pooled_reduce.py` as a second reading.
2. Diversity: net containment at K = 3 from `scripts/pooled_reduce.py --pooled-balls`, with each arm's odd-against-even seed-half control.

Each contrast pairs subjects with the exact Wilcoxon signed-rank p and the Hodges–Lehmann shift with its 95% interval.

## Inputs

`members/` was pooled locally with `python3 scripts/pool_members.py experiments/2026-11-19-diversity-pooled/design.toml experiments/2026-11-19-diversity-pooled/members --root .`, in 2 s. All 1,080 runs of the 18 subjects are present; 46 hold no repair, all on `rad-core-10`. The pool, `design.toml` and the subject lists are untracked, like the template's, and copied to each host by `rsync -a` before enqueue. Their SHA-256 sums are below.

## Hosts

av3 on CPUs 0–29 under 64 GB; CPUs 30–31 stay free for the Ollama server. av2 on CPUs 0–13 under 28 GB only, because another job holds CPUs 14–31. The build runs under `taskset -c 0-13` on both. The fingerprint and compare phases are declared once a host, so av2 runs 4 draw workers and 6 compare jobs, and av3 8 and 12. av2 takes `fsm` and five small rad cores; av3 takes the other 12, including the two sampled subjects and every subject whose draws were slow before.

## Estimate

No local plan was run: this machine has no built black outside `~/projects/peredur`, which this pass does not use. The estimate scales `2026-11-19-fretish-exchange-pooled`'s collected rows per subject: its share of unrefuted node pairs (0.01% to 5.4%) applied to this pool's node pairs, and for the sampled subjects its rows scaled by pool size. That gives about 54,000 pairs, 6,000 on av2 and 48,000 on av3, of which `fsm-timing` has 11,000 and `lift-plus-cruise-mini` 6,000 (an upper figure, since this design draws 200 nodes there against the template's 700). At that pass's per-subject call-seconds a row, the compare costs about 0.9 CPU-h; at 2026-11-09's 25.1 CPU-h over 2,132,728 rows (0.042 s a row), 0.6 CPU-h. Either way it is minutes of wall time. The fingerprint phase, at 32,768 black draws a subject, dominates: about 1–2 h on av3, most of it `valu3s-uc6` and `lift-plus-cruise-mini`. The estimates carry at least ±50%.

## Order of work

1. Push the branch, copy the inputs to both hosts' checkouts, enqueue on both with `--host av2` and `--host av3`.
2. `campaign.py collect --outputs diversity-pooled --host av2`, then `--host av3`.
3. `pooled_reduce.py --pooled-balls` and `joint_measures.py --share-only` on this design, locally, one at a time under a memory cap.

## Input checksums

```
69b635db434380202e9fc844e32be3023eaefc5047c6b882a65dde02f637de13  design.toml
51b281c470e110fe6fb3ddea938588d868358e657bf2031f179875df880a44f7  subjects-av2.txt
91c8ddcc574afa849777d9bbec968fe95aaf6e8e1b17bdec62b16f8f9a51d3ad  subjects-av3.txt
010c718462631ce8379e3eed5fcae728b34153bb0323d5e05ec98a9ee147d781  members/done.csv
7728bc8129185d4e6c2d327dfa7b7757b023c646ccd139eb2d7508fd1e62a5e0  members/fsm-combined.jsonl.gz
88f08519256900e4f345cd59ef15d3fb3fb9005c38ca524aa9467c2d4b87196f  members/fsm.jsonl.gz
dc4d57caa8cbdef64e5912a6461fe238b057fcfffcda4ca5f430235e905e4d47  members/fsm-timing.jsonl.gz
5b72d74f6026ae9b9bf35f12989b409c777a1a74bd9e8853bb1750bf8235819a  members/index.csv
c0b984bc8803450a0e7fb97f1aa61620d6e9a2a8221837ce7b126e6060ec10f5  members/lift-plus-cruise-mini.jsonl.gz
1307be01732a1074bf3b848231dc5dbc55b29422a14ca17e25e99932c56cc5e7  members/liquid-mixer.jsonl.gz
75510301fd21ed0a31cb9bc95137d97c1d660b5ea61cbc1b1bacb1599813db65  members/mode-arbiter.jsonl.gz
1cc77ca152a89f69f8014fdab9568aa27475a4cabbf7cc1c60d3b78efce081f4  members/rad-core-10.jsonl.gz
b09b750a830bf2cb1b87d3c91df649438913083b521a00d4fed7ddc3f0cab6bc  members/rad-core-1-18.jsonl.gz
ac310b98b908a6db193423e9376afcd11e80460e1f50b59ff354e2565051ded6  members/rad-core-12-18.jsonl.gz
c71eee042d70dc9817ac266b4db706ac4fad31428d0612df3fb4752cc7e2650b  members/rad-core-17-18.jsonl.gz
d583f7d15f94aac4ed01ca0019f6f97135986a55a54785d968f3144fdc3a9cc6  members/rad-core-33.jsonl.gz
826bfae980100ac2556450e1fba7846eecb74a969fd22536b4858c5fa0fe90b0  members/rad-core-45.jsonl.gz
e5efa9d40993778f3bf0d77cc9a529b1136f4d441e648d6c7942ae8f2c2800db  members/rad-core-55.jsonl.gz
cce0cf347a4acee0e560136dfdd17b55a01f9367757d05e4f5380df2b950e9a3  members/rad-core-61.jsonl.gz
bfbca6628470848c8274fce51ce8441bb5e06a6eaa53fb6889dc68a057b1db16  members/rad-core-65.jsonl.gz
74a7aa719050efb48511e9d138488ed5b79a16dee27c2134c29640d538349088  members/rad-core-68.jsonl.gz
639cfe5e9d4c02317fea942ba30f50c73e2271b3f6eb542f2b1afe7d113e5400  members/takeoff.jsonl.gz
d9611931419b34d422586f2af56b413eb1ec08c7795f33e23358b4c9edfbdd52  members/valu3s-uc6.jsonl.gz
```
