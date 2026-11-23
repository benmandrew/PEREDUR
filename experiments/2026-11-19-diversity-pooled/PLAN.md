# 2026-11-19-diversity-pooled

Registered 2026-11-19, before launch, and revised the same day (v2) before any phase ran. `2026-11-19-diversity-screen-blame` ran the paper's full (sweep D's `remove-exchange`, blame on) at `climb_orders` 1 and 4, as sweep K's `full-k1` and `full-k4`, and full-k1 plus one diversity mechanism an arm. Four mechanisms were then run to 30 seeds. This is the pooled pass over those six arms, for strength, diversity and joint-front reach. It follows `2026-11-19-fretish-exchange-pooled` and avoids the four faults in the audit of `2026-11-09-fretish-order-pooled`: the design reads `members = "output"`, `compare_pairs.py` uses the build's black wrapper, and the reduction no longer reports `decided` and keeps a point that shares no word outside a ball.

## Arms and contrasts

| Arm | Profile | Seeds | Commit |
|---|---|---|---|
| `full-k1`, `full-k4` | `diversity-climb-blame`, `experiments/results-diversity-climb-blame` | 0–29 | `8ac27a3` |
| `novelty`, `slot-coverage`, `flip-tabu`, `flip-sideways` | `diversity-screen-blame`, `experiments/results-diversity-screen-blame` | 0–29 | 0–9 at `8ac27a3`, 10–29 at `45602c3` |

Every row is `dirty=0` and every run has `run.json`. Both trees were collected into this worktree on 2026-11-19. `crowd-spec` and `spread-init` have seeds 0–9 only and are left out.

| Family | Contrasts (a against b) |
|---|---|
| `orders` | full-k4 against full-k1 |
| `screen` | novelty, slot-coverage, flip-tabu and flip-sideways against full-k1 |
| `joint` | the other 10 pairs of the six arms |

Holm runs within each family. The `joint` family is there so that the fingerprint phase plans every cross-arm pair, which the joint front needs; its net shares are not tests.

More arms join by lines in `design.toml`, whose header says how: one contrast line an arm, and one `sources` line when the arm's runs are in another results tree or at other seeds (`sources`, new in `scripts/pooled_design.py` for this pass). A new arm renumbers every node, so the pool, the fingerprints and the pairs are rebuilt under new output paths, as v2 did.

## Subjects

The small set less `fsm-lmcps`, and rad's 10 cores: 18 subjects. No arm of these holds 3 or fewer members. The smallest is `rad-core-10`, with 9 or 10 members (9 nodes) an arm, from the 7 of 30 runs a repair. `valu3s-uc6` and `lift-plus-cruise-mini` are sampled at 100 draws an arm, as in the template. On all 10 rad cores, full-k1 and full-k4 hold the same node set with the same member counts, so the `orders` contrast reads 0 there. On rad-core-61 and rad-core-65, flip-tabu and flip-sideways hold the same member and node counts as full-k1.

Pool sizes (nodes over the six arms): fsm 1,205, fsm-combined 1,495, fsm-timing 3,588, lift-plus-cruise-mini 6,064, liquid-mixer 5,160, mode-arbiter 866, takeoff 2,564, valu3s-uc6 8,249, rad-core-1-18 3,985, rad-core-10 11, rad-core-12-18 4,210, rad-core-17-18 4,128, rad-core-33 4,266, rad-core-45 2,506, rad-core-55 2,966, rad-core-61 1,741, rad-core-65 1,200, rad-core-68 2,396.

## Measures

1. Strength: accepted-trace share from `scripts/joint_measures.py`, over 32,768 words per subject; a lower share is a stronger arm. Net subsumption from `scripts/pooled_reduce.py` as a second reading.
2. Diversity: net containment at K = 3 from `scripts/pooled_reduce.py --pooled-balls`, with each arm's odd-against-even seed-half control.
3. Joint-front reach over all six arms, from `scripts/joint_measures.py` without `--share-only`, with no seed-half control.

Each contrast pairs subjects with the exact Wilcoxon signed-rank p and the Hodges–Lehmann shift with its 95% interval.

## Inputs

`members/` was pooled locally with `python3 scripts/pool_members.py experiments/2026-11-19-diversity-pooled/design.toml experiments/2026-11-19-diversity-pooled/members --root .`. All 3,240 runs of the 18 subjects are present; 138 hold no repair, all on `rad-core-10`. The pool, `design.toml` and the subject lists are untracked, like the template's (the pool is 3.0 MB), and copied to each host by `rsync -a`. Their SHA-256 sums are below.

## Hosts

av2 and av3 on CPUs 0–13 under 28 GB each, the build included: veribee-d3's hunt holds CPUs 14–31 on both until about 22 Nov. Each runs 6 draw workers and 10 compare jobs. The subjects are split to give both hosts about 1 h by the estimate below: av2 takes lift-plus-cruise-mini, rad-core-12-18, rad-core-17-18, mode-arbiter, rad-core-55, rad-core-61 and rad-core-10; av3 the other 11.

## Estimate

No local plan was run: this machine has no built black outside `~/projects/peredur`, which this pass does not use. The estimate scales `2026-11-19-fretish-exchange-pooled`'s collected rows per subject: its share of unrefuted node pairs applied to this pool's node pairs, and for the sampled subjects its rows scaled by pool size and by 600 drawn nodes against its 700. That gives about 790,000 pairs, of which fsm-timing has 180,000. At that pass's call-seconds a row per subject, the compare costs 6.3 CPU-h; at 2026-11-09's 0.042 s a row, 9.3 CPU-h. The fingerprint phase took 72–785 s a subject at 8 workers in the template; with eval scaled by pool size, it costs at most 6 CPU-h. The total is 12–16 CPU-h, about 1 h of wall time a host, and carries at least ±50%.

## Order of work

1. Push the branch and copy the inputs to both hosts' checkouts (done for v2 on 2026-11-19). Enqueue on both with `--host av2` and `--host av3`.
2. `campaign.py collect --outputs diversity-pooled-v2 --host av2`, then `--host av3`.
3. `pooled_reduce.py --pooled-balls` and `joint_measures.py` on this design, locally, one at a time under a memory cap.

## Input checksums

```
82951d69d302732f83b36854557c6f927e9c229440d11cc8455530a95c873b65  design.toml
47d9df5b71cec9e2591f188182635b3ca027b6a9066a8ee8ccf44f8f06fb54c1  subjects-av2.txt
656fbce5966db6af90c442e470e36fde98953cd674ad32e3b3f6a20905cbc68a  subjects-av3.txt
54a6bf9e2465102258c68786f558fbc4d7fefccf38a48368a24279758f1472f9  members/done.csv
e3740d3d2c515d87c1f1db000e1aea9bc48d59a3fb4bc6581cfd1c621c033c96  members/fsm-combined.jsonl.gz
0091dfaf272d77f9c229ec8c96495ec7f609fadeeff3bdd277a193d41b33a1c4  members/fsm.jsonl.gz
0da0c6beefd2cdc7338a4d64cbb1343d392196f71691929a2a622fba57b92bfd  members/fsm-timing.jsonl.gz
7d9f1b6ba4b1f295b61f1b00c6b7800ceca81d21d139db146a9bd93e3110b34b  members/index.csv
fa2d085381c9d439b5a83c220bf8b388f61399f3a9fa265404421cd26979c431  members/lift-plus-cruise-mini.jsonl.gz
61d1669a7d51e2c05006edbaedfe6cdf6517d2904ac2b1a321319f2434bdddac  members/liquid-mixer.jsonl.gz
1c035ed9db39ab89ad19aaee20eacac6f86f508f31edc2198e356476773a1b88  members/mode-arbiter.jsonl.gz
25ab5e2615b152191f331b53f7c2905e92055d4df2b0a14b38f13d3ada195a59  members/rad-core-10.jsonl.gz
cb56bb3bc2359a29f0a311c9c047cb6c0bac35a5b422c1bf00c9e0e1b116661e  members/rad-core-1-18.jsonl.gz
6b385d6ae901387a69f53f41a43f90e88f6b2f3483748daf694ad18a43636068  members/rad-core-12-18.jsonl.gz
867586dbbc4d9710f6ec5ff91c2e1afab1523a454b06f7624c87af58868fd4f1  members/rad-core-17-18.jsonl.gz
6212a7bdc820e0e3e37aa376c9ca3c26da2abb79050c13002c0573c1b17f3578  members/rad-core-33.jsonl.gz
b97a8328849161226e758aa8b71c866182f4245f788b44d3d6c18086aec0e2da  members/rad-core-45.jsonl.gz
ad9f67b8c199f9ae744f11d3618d527bf3d1f0cdfc77cde2ce716e35e908225f  members/rad-core-55.jsonl.gz
7cfb77eb97d14e8c6391a97941985372492eaf087717060b5df0137be4d1cc9c  members/rad-core-61.jsonl.gz
1ce5fe7a0dbc912994b651546bfbc838bcc6e519bd3e151fa0bb2eba2f63b888  members/rad-core-65.jsonl.gz
8d463270ba977a6eed3789f4f61c44556efdb1900fa8a72c7506b440909cde61  members/rad-core-68.jsonl.gz
4e5bd4ae957f2a294975a35f236fdffbd0403cf5e4a257be8a17a400fa31ca76  members/takeoff.jsonl.gz
2ac78e554700bdf363cd9aeb08152d79ab747d2d24a28beafbe39416e57b9036  members/valu3s-uc6.jsonl.gz
```
