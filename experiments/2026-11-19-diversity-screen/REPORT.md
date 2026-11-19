# 2026-11-19-diversity-screen: findings

*Provisional.* The BLACK pooled pass `2026-11-19-diversity-pooled` has not run, and it is the decisive measure. Every verdict below rests on the rule-only screen, which the pooled pass can still overturn.

The pooled pass is committed at `e5fff7e3` on `campaign/diversity-climb`. It is blocked because the permission system refused the drafting agent's `git push` and its copy of the pass's untracked files to the hosts, and neither refusal was worked around. Its estimate is 12 to 16 CPU-hours. Three steps unblock it, all from `~/projects/peredur-div-climb-campaign`.

**Push.** `git push origin campaign/diversity-climb`.

**Copy.** Send design.toml, both subject lists and `members/` to each host:

```
rsync -aR experiments/2026-11-19-diversity-pooled/design.toml experiments/2026-11-19-diversity-pooled/subjects-av2.txt experiments/2026-11-19-diversity-pooled/subjects-av3.txt experiments/2026-11-19-diversity-pooled/members/ av2:/home/benandrew/projects/counter/
rsync -aR experiments/2026-11-19-diversity-pooled/design.toml experiments/2026-11-19-diversity-pooled/subjects-av2.txt experiments/2026-11-19-diversity-pooled/subjects-av3.txt experiments/2026-11-19-diversity-pooled/members/ av3:/home/benandrew/projects/counter/
```

**Enqueue.** `python3 scripts/campaign.py enqueue 2026-11-19-diversity-pooled`.

## The answer

Novelty against the *accumulator* M (`genetic.novelty`) is the only mechanism that raises diversity, and it raises strength as well. At 30 seeds against `full-k1`'s 30 on 18 subjects, its *net containment* shift is +0.0564 [+0.0303, +0.0779] (p < 0.001, positive on 16 subjects and negative on 2), and its strength shift is +0.0288 [+0.0017, +0.0487] (p = 0.006). Both directions beat the *seed-half controls*: `full-k1`'s nodes inside novelty's region gain +0.0249 [+0.0127, +0.0376], and novelty's nodes inside `full-k1`'s region fall by 0.0311 [−0.0451, −0.0163]. No subject loses a found repair. Each figure here is a *Hodges–Lehmann shift* with its 95% Wilcoxon signed-rank interval.

The multi-order climb (`climb_orders = 4`, arm `full-k4`) gives a small but significant gain of +0.0035 [+0.0003, +0.0077] (p = 0.031, 12 subjects up and 6 down). Strength does not move (+0.0000 [−0.0004, +0.0003]), and wall time rises 13%, from a mean of 54 s a run to 61 s. The other mechanisms give nothing or do harm. flip-tabu, slot-coverage and flip-sideways looked promising at 10 seeds and fell to noise at 30. crowd-spec trades diversity for strength, and spread-init harms both. Under each of those two, rad-core-10 loses found repairs, from 4 of 10 seeds to 3 and to 1.

## Results

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

The screen reads direction and rank, and it shrinks shifts. It was validated against the BLACK pooled pass on net containment against `base`. For `remove-exchange` the signs agreed on 18 of 18 subjects with Spearman 0.93, and for `remove-no-blame-exchange` on 16 of 18 with Spearman 0.94. The screen's shifts were +0.007 and +0.008 against the pooled pass's +0.060 and +0.072, so it shrank them by about 9×. Novelty's +0.0564 is the largest screen shift in the study, and only the pooled pass can size it. That validation used the exchange rerun's `base`, which has blame off, so it tested the tooling and judged no candidate.

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

Two `\bennote`s are in the draft (`peredur-extension`, `paper/repair/directed.tex`, commit `927e7a4`, local and not pushed): one on the multi-order climb and one on novelty as a candidate fourth mechanism. The evaluation bullets are unchanged.

## Budget

About 145 CPU-hours are spent or committed out of 300, including the 15 wasted when the no-blame entries were dequeued. The pending pooled pass adds 12 to 16.

## Next steps

**Run the pooled pass.** It decides whether novelty's shift holds under BLACK and sizes `full-k4`'s gain.

**Combine and time.** If novelty holds, test it with `climb_orders = 4` in one arm, and measure novelty's wall time on one host at one width, since every ratio so far is confounded.

**Decide on the deferred ideas.** MAP-Elites over (slots changed, kind of change), the core-directed assumption move and restarts with frozen slots wait for your decision.

Of the six mechanisms, novelty alone reads what M already holds when it selects, and it alone moved the measure. That points the next round of ideas towards selection pressure against M.
