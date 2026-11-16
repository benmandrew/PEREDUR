# 2026-11-16-fretish-order-pooled-joint

The net subsumption share of 2026-11-09-fretish-order-pooled compares each arm with base alone. It is a binary measure, so it does not rank the arms: only-climb's +0.90 and full's +0.37 over base say nothing about full against only-climb. It also favours an arm whose search is base's, since only-climb's maximal set is base's own outputs climbed. This pass scores five arms against one shared reference instead. The arms are base, the three features on the collaboration slides (only-climb, only-gate, only-blame) and full, all under nsga2-apportion, on the same 18 subjects and 30 seeds.

## Measure

For each subject, take the frontier classes of the five arms, merge classes that are equivalent across arms, and keep the classes that no class of another arm strictly implies. That is the *joint front*. An arm's *reach* is the share of the joint front for which the arm holds an equivalent or stronger class. Reach is one number per arm and subject on a common scale, so arms can be ranked.

Reported, per arm: the median and mean reach over subjects. Per pair of arms: the paired difference in reach, with the exact Wilcoxon signed-rank p over subjects and a Holm correction over the ten pairs. The reduction also reports the net subsumption share of the six new contrasts (`family = "joint"` in `design.toml`), as `pooled_reduce.py` does for the four against base. valu3s-uc6 and lift-plus-cruise-mini are sampled (100 draws an arm), so their reach is estimated from the drawn nodes and is reported apart. fsm-lmcps has no base member and is reported but carries almost no information.

The question for the slides is whether full's repairs reach less of the joint front than only-climb's. The pass decides that by the full-against-only-climb reach difference and its Holm p. A null result is reported as such.

## Inputs

`design.toml` is 2026-11-09-fretish-order-pooled's design with the four other only-arms dropped and the six contrasts between non-base arms added. `members/` was pooled locally on 2026-11-16 with `python3 scripts/pool_members.py experiments/2026-11-16-fretish-order-pooled-joint/design.toml experiments/2026-11-16-fretish-order-pooled-joint/members --root .` from the collected results directories, with no missing run. At 4.8 MB it exceeds the blob limit, so it is untracked, and it is copied with `design.toml` and `subjects-av3.txt` to the same paths in av3's checkout by `rsync -a` before enqueue. Their SHA-256 sums are listed under "Input checksums" below, and the pool phase checks every member file against `members/index.csv`.

## Host

av3 alone, confined to CPUs 16-31 under a 32 GB cap, the build included. veribee-33 holds av3 CPUs 0-15 (hunt.slice) until Tue 17 Nov 09:00, and both veribee-33 and gawain-ad confirmed on 16 Nov at 18:45 that they held nothing on 16-31. Nothing runs on av2. This pass is the first to use `confine` on unseeded phases (commit 1663f21a).

## Estimate

Scaled from the 2026-11-11 re-run's call seconds per query and per subject: about 29 CPU-h of compare work, of which rad-core-33 is 17. At 16 jobs that is about 2 h, plus the fingerprint phase. The 2026-11-11 no-climb pass, three arms on the same subjects, took 84 min for all three phases at 16 jobs.

## Order of work

1. Push the branch, copy the inputs to av3, enqueue on av3.
2. `campaign.py collect --outputs fretish-order-pooled-joint --host av3`.
3. `pooled_reduce.py` on this design for the net shares, then the joint-front reach from the collected verdicts.

## Input checksums

```
d34c53aa99b3947ee238dd9fb62b39ed398de084a09de8cbbd44eab5fa0424e2  design.toml
46bf022e9aa54363ea842026358bd4c1ef32047e54f45a19b8ebb1eed1b087dc  subjects-av3.txt
4d44834b04efb10a6f6a808d9161a5333dad4085ac181bc5e3c64688d89a4e13  members/fsm-combined.jsonl.gz
7791d86485a389a2a2c314fee58e58b5c342684bca72521339127afdb51b5ae6  members/fsm.jsonl.gz
e3aa998dfbacae7a6c6d15403672c9f13b8eac978e14957307cf4feb07c6a17a  members/fsm-lmcps.jsonl.gz
6006175967863e761dfad20b9d8851788d588fa3d3ff4aacee38cee98d1ba4bb  members/fsm-timing.jsonl.gz
b37878fa7144379a3af56b9395d2c8ad96b5f530d75b1f90bcd5de27f52612d7  members/lift-plus-cruise-mini.jsonl.gz
ae43ebfc45b169dc2f60c2d8df7aaf9f04cad4aa21daeb0a4cab22fc1faec0f6  members/liquid-mixer.jsonl.gz
cab3f9004b6b17e100dc939d7453ac252b30c91cef808cf3e6c7dd4f486e89c7  members/mode-arbiter.jsonl.gz
71c8ea4969131b6c39188be972ffa3d8de4b737ad814b9b4bf22d9c5aa559cea  members/rad-core-10.jsonl.gz
48b5555394e5932086ccf02f4e98d4652798a7f6a0bb4815ff1e9a04d2e00f75  members/rad-core-1-18.jsonl.gz
e256d499928074e67786fb829a7aaa50f45928321356724420dba7c1e16743ee  members/rad-core-12-18.jsonl.gz
c55c0afd39b0a2046bf46f3823c42d4cd0918275cdd392e78fb97b992a390739  members/rad-core-17-18.jsonl.gz
6d7ae3f9c1152906e6ea5708ec26a088f3664aa7828eb0ff35d9a1e98f71369f  members/rad-core-33.jsonl.gz
c5069969a4ee990a3754f379cc699edd09fdc9dc4d10f69e2aaeda726be7043b  members/rad-core-45.jsonl.gz
3834a953dadc531c706667e54201c9b0889d6e248086938068d7d72015687bb2  members/rad-core-55.jsonl.gz
ebab45bfd54c5d1d1c9d7d623cf0c3bcd4e7e668b7c36a6f705446a8988e3624  members/rad-core-61.jsonl.gz
c74db30a44953db925366deb35b4c3e04b95705534791cc2426059d15dd732b2  members/rad-core-65.jsonl.gz
051866c5defac3b38c9788603a0d4d9b975212d338ba6c51d3b7d4ce69507f9d  members/rad-core-68.jsonl.gz
8b57b7642ac50ede731acaff9c4ca3d66146c594955ff6614066709410d0d543  members/takeoff.jsonl.gz
9a430e8c319aca940411f2011b282f4f6ebffab0f9e6d546904ce3732105be4c  members/valu3s-uc6.jsonl.gz
b979773983c810507eeb282af96273d198375ee1a1aff9747ac2ec928a355bf1  members/index.csv
```
