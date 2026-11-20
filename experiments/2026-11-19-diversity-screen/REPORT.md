# 2026-11-19-diversity-screen: findings

The BLACK pooled pass `2026-11-19-diversity-pooled` ran on 2026-11-20, over `full-k1`, `full-k4`, novelty, slot-coverage, flip-tabu and flip-sideways at 30 seeds on 18 subjects. Its verdicts are final. The screen results below it are kept as the record of how each arm was chosen.

## Pooled pass (BLACK)

Each row is against `full-k1`, and a positive value favours the arm. Containment is at K = 3 over 17 subjects, because rad-core-10's half-pools hold 4 nodes. It gives the median difference over subjects, with Holm-corrected exact signed-rank p. Net subsumption is the median over 18 subjects. Accepted-trace share and joint-front reach give Hodges–Lehmann shifts with 95% intervals, Holm over the 15 arm pairs.

| Arm | Net containment | full-k1 in arm − control | arm in full-k1 − control | Net subsumption | Accepted-trace share | Joint-front reach |
|---|---|---|---|---|---|---|
| novelty | +0.047, Holm 0.001 | +0.026, Holm 0.002 | −0.042, Holm 0.010 | +0.185, 17 up / 0 down, Holm 6×10⁻⁵ | +0.0225 [+0.0038, +0.0592], Holm 0.05 | +0.137 [+0.056, +0.198], Holm 0.039 |
| full-k4 | +0.001, Holm 0.36 | +0.001, Holm 0.034 | −0.001, Holm 0.005 | 0.000, 5 up / 13 tied, p 0.063 | 0.0000 [−0.0003, +0.0015], Holm 1 | +0.002 [0.000, +0.012], Holm 0.031 |
| flip-sideways | +0.006, Holm 0.36 | +0.004, Holm 0.48 | −0.000, Holm 1 | 0.000, Holm 0.69 | +0.0046 [+0.0008, +0.0159], Holm 0.15 | +0.001 [−0.013, +0.010], Holm 0.98 |
| flip-tabu | +0.000, Holm 1 | +0.003, Holm 1 | −0.001, Holm 1 | −0.003, Holm 0.69 | −0.0009 [−0.0050, +0.0029], Holm 1 | −0.007 [−0.024, +0.002], Holm 0.52 |
| slot-coverage | −0.011, Holm 1 | +0.001, Holm 1 | −0.010, Holm 1 | −0.019, Holm 0.69 | +0.0003 [−0.0154, +0.0105], Holm 1 | −0.042 [−0.066, −0.021], Holm 0.017 |

The reduced tables are in `experiments/diversity-pooled-v2-reduced/` of `~/projects/peredur-div-climb-campaign` (untracked): `ball-radii-pooled/report-k3.txt`, `balls.log` (net subsumption) and `joint.log` (share and reach).

## The answer

Novelty against the *accumulator* M (`genetic.novelty`) raises diversity and strength together, and the pooled pass confirms it on every measure. Its region covers `full-k1`'s nodes beyond the seed-half control, and its own nodes fall outside `full-k1`'s, for a net containment shift of +0.047 (16 of 17 subjects). Its frontier classes subsume `full-k1`'s on 17 of 18 subjects, a median net of +0.185. Its accepted-trace share is 0.0225 lower, and it reaches 13.7 points more of the joint front of the six arms. No subject loses a found repair. The screen's 30-seed shift (+0.056) was close to the pooled one, so the 9× shrinkage seen when validating against `base` did not recur.

The multi-order climb (`climb_orders = 4`, arm `full-k4`) does not hold on diversity. Both directions beat their controls by 0.001, but the net shift is not significant (Holm 0.36), no strength measure moves, and reach gains 0.2 points (Holm 0.031). For 13% more wall time, that is too small to adopt. flip-tabu and flip-sideways are indistinguishable from `full-k1` after Holm. slot-coverage loses 4.2 points of reach (Holm 0.017). crowd-spec trades diversity for strength, and spread-init harms both. Under each of those two, rad-core-10 loses found repairs, from 4 of 10 seeds to 3 and to 1. Neither went to the pooled pass.

## Screen results

Net is net containment against `full-k1`. Strength is `full-k1`'s *accepted-trace share* less the arm's, so a positive value means the arm is stronger. Cells give the shift, its interval and p, then subjects positive / negative where recorded.

| Arm | Net, 10 seeds | Strength, 10 seeds | Net, 30 seeds | Strength, 30 seeds | Verdict |
|---|---|---|---|---|---|
| full-k4 | | | +0.0035 [+0.0003, +0.0077], 0.031, 12/6 | +0.0000 [−0.0004, +0.0003], 0.81 | pass |
| novelty | +0.0708 [+0.0347, +0.0965], 0.001, 14/3 | +0.0308 [+0.0087, +0.0569], <0.001, 15/2 | +0.0564 [+0.0303, +0.0779], <0.001, 16/2 | +0.0288 [+0.0017, +0.0487], 0.006, 14/3 | pass |
| flip-tabu | +0.0161 [−0.0045, +0.0328], 0.12, 13/4 | −0.0008 [−0.0126, +0.0010], 0.45 | +0.0063 [−0.0098, +0.0176], 0.44, 10/8 | −0.0013 [−0.0148, +0.0004], 0.12 | noise |
| slot-coverage | +0.0136 [−0.0096, +0.0329], 0.23, 9/8 | −0.0013 [−0.0106, +0.0064], 0.61 | +0.0039 [−0.0142, +0.0222], 0.68, 9/9 | −0.0010 [−0.0125, +0.0136], 0.52 | noise |
| flip-sideways | +0.0094 [−0.0127, +0.0444], 0.49, 10/7 | +0.0017 [−0.0006, +0.0206], 0.17 | +0.0059 [−0.0027, +0.0221], 0.18, 12/6 | +0.0019 [−0.0008, +0.0133], 0.35 | noise |
| crowd-spec | −0.0057 [−0.0322, +0.0201], 0.60, 6/10 | +0.0081 [+0.0006, +0.0291], 0.027, 15/3 | | | fail |
| spread-init | −0.1797 [−0.2630, −0.1132], <0.001, 1/16 | −0.0562 [−0.1244, −0.0065], 0.008, 4/14 | | | fail |

The screens are under `screens/<arm>-vs-full-k1.json` (10 seeds) and `screens/<arm>-vs-full-k1-30.json` (30 seeds), untracked, as the repository ignores experiment outputs. The `full-k4` screen is `screens/full-k4-vs-full-k1.json`.

## What the screen can and cannot say

The screen reads direction and rank, and it shrinks shifts. It was validated against the BLACK pooled pass on net containment against `base`. For `remove-exchange` the signs agreed on 18 of 18 subjects with Spearman 0.93, and for `remove-no-blame-exchange` on 16 of 18 with Spearman 0.94. The screen's shifts were +0.007 and +0.008 against the pooled pass's +0.060 and +0.072, so it shrank them by about 9×. Novelty's +0.0564 was the largest screen shift in the study, and the pooled pass gave +0.047, so on this contrast the screen did not shrink the shift. That validation used the exchange rerun's `base`, which has blame off, so it tested the tooling and judged no candidate.

Its strength measure is weak. The *rule-only frontier* proves only 39% of BLACK's implications on fsm and 37% on mode-arbiter, and BLACK confirmed every implication the rules did prove. On fsm, `full`'s frontier has 164 classes against BLACK's 57, so the accepted-trace share is taken over a frontier inflated with weaker repairs. The descriptor count correlated with containment at a median Spearman ρ of −0.23, and it decides nothing.

The wall times of the mechanism arms are confounded by host and width. `full-k1`'s seeds 0 to 9 all ran on av2, while seeds 4 to 9 of the arms ran on av3. Over seeds 10 to 29, `full-k1` ran at 14 jobs on av3 and the arms at 7 jobs per host. Novelty's ratio was ×1.45 at 10 seeds and ×1.14 over seeds 10 to 29, and neither is a clean cost. `full-k4`'s +13% ran in the same campaign phases as `full-k1`, so it is the cleanest of the wall-time figures.

The promising arms regressed. Between 10 and 30 seeds, flip-tabu fell from +0.0161 to +0.0063, slot-coverage from +0.0136 to +0.0039 and flip-sideways from +0.0094 to +0.0059, and all three are now indistinguishable from `full-k1`. Novelty also shrank, from +0.0708 to +0.0564, and its interval stayed clear of zero.

## Branches

Every new key is off by default, and the goldens are unchanged on every branch.

| Branch | Holds | Merge readiness |
|---|---|---|
| `feat/diversity-climb` | `climb_orders` reinstated from `04d00eb7^`, and the offline `climb-replay` driver (`66884b9e`, `a6de1c04`) | Debug ctest passes 68 of 68. Already merged into `feat/diversity-mech`. |
| `feat/diversity-mech` | The five mechanisms (`2d38245a` crowding, `160d6c41` novelty, `bb393cfd` slot coverage, `e66b9eaf` spread-init, `32bb5b6b` flip-tabu and flip-sideways), with the climb merged at `02dbd64d`. `climb_orders` takes schema 70 and the blocks 71 to 75. | Debug ctest passes 70 of 70. Five deviations from the plan are listed in the Log. |
| `merge/maximality-removal` (`113e923d`) | `genetic.maximality_removal` at schema 70, which main lacks (on main, `full` demotes and does not remove) | Merge first. |
| `merge/diversity-screen` (`14edc986`) | The screen tooling: `compare --rule-only`, `--pairs FILE` and `scripts/diversity/` | Merge after `merge/maximality-removal`. |
| `campaign/diversity-climb` | Removal ported onto `02dbd64d` at schema 76 (`48884d5b`), the campaign declarations and the pooled pass at `e5fff7e3` | Campaign branch only. Release ctest passes 70 of 70. |

## Paper

Two `\bennote`s are in the draft (`peredur-extension`, `paper/repair/directed.tex`, local and not pushed), updated with the pooled numbers: one says the multi-order climb does not hold, and one gives novelty as a candidate fourth mechanism. The evaluation bullets are unchanged.

## Budget

About 145 CPU-hours are spent or committed out of 300, including the 15 wasted when the no-blame entries were dequeued. The pooled pass took 12 to 16 by estimate. Its reductions took 56 CPU-minutes on av3. The total is about 160 CPU-hours.

## Next steps

**Separate the cause.** The user accepted novelty as the fourth objective on 2026-11-20, so neither control (a random fourth objective, or novelty as the tie-breaker within a front) was run. The tie-break arm was implemented (`02fa8c65`) and dequeued before finishing.

**Time it.** Measure novelty's wall time against `full-k1` on one host at one width, since every ratio so far is confounded by host.

**Decide on the deferred ideas.** MAP-Elites over (slots changed, kind of change), the core-directed assumption move and restarts with frozen slots wait for your decision.

Of the six mechanisms, novelty alone reads what M already holds when it selects, and it alone moved the measure. That points the next round of ideas towards selection pressure against M.
