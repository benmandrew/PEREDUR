# 2026-11-19-diversity-screen

In progress. Branch `feat/diversity-study`, worktree `~/projects/peredur-diversity`. Hosts are av2 and av3 only, since av1 is not available to this study.

## Question

Four mechanisms direct the search in `full`: the climb, two-way mutation, blame and removal. All four act on the *strength* of repairs, and the climb most of all. The study asks which cheap changes to the search raise the *diversity* of `full`'s repairs without lowering their strength. Diversity is the containment measure of `peredur2027`: each repair's ball reaches its third-nearest neighbour, and a side's region is the union of its balls. Order crossover, cut on 2026-11-17, only recombined parents already in the population, so it is not a candidate here.

## Candidates

| Key | Mechanism | Acts at | Needs new GA runs |
|---|---|---|---|
| `climb-orders` | Climb each member of M under several step orders and keep the maximal elements of the union | output | no: replay over `accumulated/` |
| `crowd-spec` | NSGA-II crowding distance between specifications (slots changed from O) in place of distance between objective vectors | selection | yes |
| `novelty` | An objective that counts the members of M that imply a realisable candidate, lower being better | selection | yes |
| `slot-coverage` | Slot draws weighted towards slots that no member of M changes | mutation | yes |
| `spread-init` | P starts as one-step weakenings spread over the refused guarantees of each core, in place of N copies of O | initialisation | yes |
| `flip-sideways` | After a lineage flips realisability twice on one slot, that slot is tabu for k generations, or takes sideways rewrites only | mutation | yes |

MAP-Elites over (slots changed, kind of change), a core-directed assumption move and restarts with frozen slots wait for the first report.

## Screen

The screen replaces the pooled pass for every candidate. Only the candidates that pass it go through a full campaign and pooled pass.

- **Word bank.** One set of 32,768 lasso words per subject, drawn once and saved. It mixes words from models of every arm's pooled repairs so far with uniform random lassos. A new arm's repairs are scored on the saved words, so every arm shares one sample and needs no satisfiability call.
- **Frontier.** Rule implication only, with no BLACK. On sampled pairs at most three fields apart, the rule misses 6 of 2,625 implications, and an unproved pair reads as incomparable.
- **Descriptor count.** `run.json` records the number of distinct (slots changed from O, kind of change) cells among M's repairs. Its rank correlation with containment is checked once on the existing pooled data. If the correlation is weak, the count is reported and does not decide anything.
- **Subjects and seeds.** The specifications whose `full` maximal set holds more than three repairs, 10 seeds each, at the budget of the specifications. Even and odd seeds form the two halves for the containment control.
- **Pass rule.** A candidate passes when its net containment share against `full` is positive on the screen subjects, beyond `full`'s own odd-against-even control, and its accepted-trace share is no more than 0.02 above `full`'s. A higher share is a weaker arm. Its found-repair rate must not fall on any subject. The thresholds are this study's choice and were fixed before any screen ran.

## Budget

At most 300 CPU-hours across av2 and av3. On the exchange rerun, one arm of 570 runs cost 7.0 to 14.9 CPU-hours, and the large set about 4 per arm. The last pooled pass spent 25.1 CPU-hours in BLACK. A screened arm costs about 2 CPU-hours, and each candidate that passes about 40 more. The study stops and reports before any run would take the total over 300.

## Log

- 2026-11-19: branch cut from `origin/main` at `5a7a2fc8`.
- 2026-11-19: `filters.climb_orders` existed until #258 (`04d00eb7`, 18 November), which retired it because no config set it. No campaign measured it, so `climb-orders` reinstates the hash-priority permutations from `04d00eb7^`.
- 2026-11-19: work split over three branches off this one, each in its own worktree. `feat/diversity-climb` (`~/projects/peredur-div-climb`) holds `climb_orders` and the offline `climb-replay` driver. `feat/diversity-screen` (`~/projects/peredur-div-screen`) holds the word bank, the rule-only frontier, the reducer and the descriptor count. `feat/diversity-mech` (`~/projects/peredur-div-mech`) holds the five search mechanisms, each a key that is off by default. They merge back into `feat/diversity-study` before any campaign.
- 2026-11-19: `feat/diversity-climb` done (`66884b9e`, `a6de1c04`); debug ctest passes 68 of 68 and the goldens are untouched. At one order, `climb-replay` matches the run's own output on 44 of 54 remove-exchange runs (18 subjects, seeds 00 to 02). Of the other 10, 8 match once #263 (`07015ddc`) is reverted, since main proves more climb steps, and 2 differ because the repair JSON omits removed guarantees and the replay reconstructs them. Both arms of the climb screen are therefore replayed on one binary: one order against four, never the run's own output against a replay. On fsm-combined seed 00, one order took 6.8 s for 13 repairs and four orders took 8.1 s for 15. On takeoff seed 00 the figures were 17.2 s for 40 and 22.5 s for 42.
- 2026-11-19: replay of all 570 remove-exchange runs at one and four orders started locally (`scripts/replay.sh`, 4 jobs, MemoryMax 10G). Outputs go to `replay/k1` and `replay/k4`, untracked.
- 2026-11-19: the paper drops blame from `full`, so `full` is now `remove-no-blame-exchange` (climb, twoway and removal) and the old `full` (`remove-exchange`) is `plus-blame`. Every screen is against `remove-no-blame-exchange` from here on. Its climb replay (`scripts/replay-arm.sh`, output under `replay/remove-no-blame-exchange/`) follows the `remove-exchange` replay, which is kept as a second arm.
- 2026-11-19: both local replays stopped. They had finished 138 and 134 of 570 runs in about 35 minutes at a load of 24 on 20 cores, so they would have taken about 11 hours together. The replacement is campaign `2026-11-19-diversity-climb` on av3 and av2: `remove-no-blame-exchange` rerun at `climb_orders` 1 and 4, 19 subjects × 30 seeds each, about 20 CPU-hours. The search draws the same numbers at both settings, and the climb runs only after it, so the two arms share every search and differ in the climb alone. The replays' partial outputs under `replay/` are kept but not used.
- 2026-11-19: `feat/diversity-mech` done: `2d38245a` `genetic.crowding`, `160d6c41` `genetic.novelty`, `bb393cfd` `mutation.slot_coverage`, `e66b9eaf` `genetic.spread_init` and `32bb5b6b` `mutation.flip_tabu` / `mutation.flip_sideways`. Debug ctest passes 69 of 69, with the goldens untouched. Deviations from this plan:
  - novelty counts a candidate already in M as implying itself;
  - slot coverage leaves the remove-guarantee draw unweighted;
  - each spread-init seed is one directed `mutate_requirement` proved weaker, which can change more than one field of a slot;
  - `flip_sideways` acts only when `flip_tabu` is above 0, with the same span;
  - lineage records are stored per specification and skip crossed offspring.

  No mechanism has yet made a real repair run.
- 2026-11-19: `02dbd64d` merges `feat/diversity-climb` into `feat/diversity-mech`. `climb_orders` keeps schema 70 and the five blocks take 71 to 75. Debug ctest passes 70 of 70. Each new key is off by default, so the one campaign runs from this commit. It holds `full-k1` and `full-k4` (19 subjects × 30 seeds), plus six screen arms, each `full-k1` with one key on (19 subjects × seeds 0 to 9, which pair with `full-k1`'s seeds 0 to 9): `crowd-spec`, `novelty`, `slot-coverage`, `spread-init`, `flip-tabu` (5 generations) and `flip-sideways`.
- 2026-11-19: `genetic.maximality_removal` was never merged to main. It lives on `feat/demotion-removal` (`5e94f6b5`) and the campaign branches cut from it, and main ignores it with a warning, so on main `full` would demote and not remove. `campaign/diversity-climb` (`48884d5b`) ports `5e94f6b5` onto `02dbd64d` with schema 76. `1c8e34f9` declares campaign `2026-11-19-diversity-screen-runs` with sweep K. `full-k1`'s config differs from all 570 of the rerun's `remove-no-blame-exchange` configs by the line `climb_orders = 1` only, and each screen arm differs from `full-k1` by its own keys only. Release ctest passes 70 of 70. Smoke runs on fsm seed 0: every arm removed 61 to 107 candidates with no fallback, the climb tried 87 steps at one order and 345 at four, and every mechanism's counter block was non-zero.
- 2026-11-19: enqueued at `1c8e34f` as av2 entry 094 and av3 entry 092, with 2,280 runs (1,140 per host) at 14 jobs × 2 threads on CPUs 0 to 29 under 64G. The estimate is about 75 CPU-hours, at the rerun's mean of 58 s a run over 2 threads. The budget had 0 CPU-hours spent before this.
- 2026-11-19: `feat/diversity-screen` done: `64f086ef` (`compare --rule-only`), `2667e13a` (`--pairs FILE`), `ba8e2730` (`scripts/diversity/`) and `6649c219`. The word banks are under `wordbank/`, untracked. Each subject has 24,576 aimed words taken from the 2026-11-19-fretish-exchange-pooled draws, plus 8,192 uniform words (stem 0 to 8, loop 1 to 8, seed 3). fsm-lmcps was not in that pass, so its aimed words were drawn fresh. One screen of one arm takes 7.3 minutes over 19 subjects once the reference is fingerprinted, and 21 minutes with nothing cached. Validation:
  - Rule-only frontier against BLACK's. BLACK confirmed every implication the rules proved, but the rules prove only 39% of BLACK's on fsm and 37% on mode-arbiter. `full`'s frontier has 164 classes against BLACK's 57 on fsm, and 104 against 42 on mode-arbiter. The main miss is a trigger `true` with response `always r` implying a narrower trigger with a weaker response. The screen's accepted-trace share is therefore taken over a frontier inflated with weaker repairs, and its strength half of the pass rule is a weak check. A passing candidate still goes through the BLACK pooled pass before any claim.
  - Net containment against `base`, screen against pooled pass. For `remove-exchange`: signs agree on 18 of 18 subjects, Spearman 0.93, Hodges–Lehmann +0.007 [−0.070, 0.099] against +0.060 [−0.025, 0.184]. For `remove-no-blame-exchange`: 16 of 18, Spearman 0.94, +0.008 [−0.059, 0.130] against +0.072 [−0.028, 0.200]. The screen ranks arms as the pooled pass does, but it shrinks the shift, so it reads direction and order only. Neither shift is significant in either measure.
  - Descriptor count against per-run containment: median Spearman ρ −0.23 (range −0.75 to 0.46). It decides nothing.
  - Screen subjects: the 18 subjects other than fsm-lmcps, where `remove-no-blame-exchange` returns no repair.
- 2026-11-19: the user reversed the blame decision. Blame stays on as an engine optimisation and the paper does not mention it, so `full` is `remove-exchange` again. On whole rad, the arms with blame have median wall times of 140 to 190 s, against 557 to 787 s without it (peredur-extension-02). On this study's 19 subjects the two `full` arms cost the same, 57 and 58 s a run. Entries 094 (av2) and 092 (av3) were dequeued at about 25% of their first phase, which wasted about 15 CPU-hours. Their replacement is `2026-11-19-diversity-screen-blame`: sweep K with blame on, and profiles `diversity-climb-blame` and `diversity-screen-blame` with their own directories. veribee-d3's bug hunt holds av2 CPUs 14 to 29, so av2 runs 7 jobs on CPUs 0 to 13 with a third of the seeds. Every screen below is against this blame-on `full-k1`, and the no-blame entries above are not used. Their branch and merge work (`48884d5b`, `merge/maximality-removal` at `113e923d` with schema 70, `merge/diversity-screen` at `14edc986`) is unaffected.
- 2026-11-19: the paper's final decision is in the paper repository (`peredur-extension`) at `afd698e`: blame is on in every arm of the final engine, `base` included. Every arm of this study is `full-k1` with blame on, or `full-k1` plus one key, so nothing here changes. The screen's validation against `base` (above) used the exchange rerun's `base`, which has blame off. It tested the screen tooling only, and no candidate is judged against it.
- 2026-11-19: the climb phases of `2026-11-19-diversity-screen-blame` finished on both hosts (`full-k1` and `full-k4`, 1,140 runs). Collected with `campaign.py collect --profile diversity-climb-blame --host av2`, then `--host av3`: 1,140 rows, each source row present once. A dry run without `--host` also probed av1 with a read-only rsync, which failed; every collect now names its host. Screen `full-k4` against `full-k1` started on the 18 screen subjects, writing to `screens/full-k4-vs-full-k1.json`.
- 2026-11-19: screen `full-k4` against `full-k1` (`screens/full-k4-vs-full-k1.json`, 308 s). Over 18 subjects:
  - Net containment: Hodges–Lehmann +0.0035 [+0.0003, +0.0077], p = 0.031, positive on 12 and negative on 6.
  - `full-k4` inside `full-k1`'s region, less `full-k1`'s control: −0.0010 [−0.0030, −0.0001], p = 0.029.
  - `full-k1` inside `full-k4`'s region, less `full-k4`'s control: +0.0008 [−0.0002, +0.0028], p = 0.11.
  - Accepted-trace share: 0.0000 [−0.0004, +0.0003].
  - No subject loses found repairs.
  - Wall time: mean 61 s a run against 54 s, so +13%. Total 9.7 against 8.6 run-hours.
  - Median repairs per run rise on 6 of the 9 smaller subjects (lift-plus-cruise-mini 40.5 to 43.5, for example) and on none of rad's cores.

  So `full-k1` lies inside `full-k4`'s wider spread, and strength does not move. **`climb-orders` passes the screen.** The shift is small, but the screen shrank `full`'s shift against `base` by about 9× (0.008 against 0.072), so a pooled pass is needed to size it. `full-k4` already has 30 seeds, so it needs only the BLACK pooled pass. That pass waits for the mechanism screens, so that every candidate shares one pool.
- 2026-11-19: pooled pass `2026-11-19-diversity-pooled` drafted on `campaign/diversity-climb` at `3454ce2`, not yet pushed or enqueued. It holds pool, fingerprint and compare phases: av3 on CPUs 0 to 29 under 64G with 12 subjects, and av2 on CPUs 0 to 13 under 28G with 6. The estimate for `full-k1` and `full-k4` is about 54,000 pairs, or 0.6 to 0.9 CPU-hours of compare, plus 1 to 2 hours of fingerprinting on av3. A new arm adds one contrast and one `[groups.sources]` line (`b272235` adds the per-arm `sources` key), then re-pools. On all 10 rad cores, `full-k1` and `full-k4` hold identical pools. Open before enqueue: design.toml and the pool are untracked and must reach both hosts by the template's route, and none of the study's sets is in `catalogue.toml` yet.
- 2026-11-19: entries 095 and 093 finished. `diversity-screen-blame` was collected with `--host av2`, then `--host av3`: 1,140 rows, each present once, 190 runs per arm. Each arm is screened against `full-k1`'s seeds 0 to 9 (`seed0?`), one screen at a time, writing to `screens/<arm>-vs-full-k1.json`. Run-level checks against `full-k1` on seeds 0 to 9:

  | Arm | Found repairs | Wall time | Median repairs (subjects up / down) |
  |---|---|---|---|
  | crowd-spec | rad-core-10: 4 → 3 of 10 | ×1.13 | 9 / 8 |
  | novelty | no drop | ×1.45 | 15 / 1 |
  | slot-coverage | no drop | ×1.06 | 3 / 14 |
  | spread-init | rad-core-10: 4 → 1 of 10 | ×0.92 | 6 / 8 |
  | flip-tabu | no drop | ×1.13 | 7 / 6 |
  | flip-sideways | no drop | ×1.13 | 6 / 9 |

  The wall times are rough. `full-k1`'s seeds 0 to 9 all ran on av2, while seeds 4 to 9 of the arms ran on av3. veribee-d3 has av3 CPUs 14 to 29 from now. My later work confines to CPUs 0 to 13 on both hosts, under 28G.
- 2026-11-19: the six mechanism screens, each against `full-k1`'s seeds 0 to 9 on 18 subjects (`screens/<arm>-vs-full-k1.json`). Net is net containment. Strength is the reference's accepted-trace share less the arm's, so a positive value means the arm is stronger. Each cell gives the Hodges–Lehmann shift, its 95% interval, the signed-rank p, and the subjects positive / negative.

  | Arm | Net | Strength | Verdict |
  |---|---|---|---|
  | full-k4 (30 seeds) | +0.0035 [+0.0003, +0.0077], 0.031, 12/6 | +0.0000 [−0.0004, +0.0003], 0.81 | pass |
  | novelty | +0.0708 [+0.0347, +0.0965], 0.001, 14/3 | +0.0308 [+0.0087, +0.0569], <0.001, 15/2 | pass |
  | flip-tabu | +0.0161 [−0.0045, +0.0328], 0.12, 13/4 | −0.0008 [−0.0126, +0.0010], 0.45 | promising |
  | slot-coverage | +0.0136 [−0.0096, +0.0329], 0.23, 9/8 | −0.0013 [−0.0106, +0.0064], 0.61 | promising |
  | flip-sideways | +0.0094 [−0.0127, +0.0444], 0.49, 10/7 | +0.0017 [−0.0006, +0.0206], 0.17 | promising |
  | crowd-spec | −0.0057 [−0.0322, +0.0201], 0.60, 6/10 | +0.0081 [+0.0006, +0.0291], 0.027, 15/3 | fail: net not positive; rad-core-10 loses a found repair |
  | spread-init | −0.1797 [−0.2630, −0.1132], <0.001, 1/16 | −0.0562 [−0.1244, −0.0065], 0.008, 4/14 | fail: less diverse and weaker; rad-core-10 drops from 4 to 1 found repair |

  Novelty's net shift is about 20× full-k4's, and about the size of `full`'s screen shift against `base` scaled up nine times. It is also the only mechanism that is more diverse and stronger at once. Its 1.45× wall time is partly the host split. Crowding in specification space trades diversity for strength. Spread initialisation hurts both. These runs do not show why.
- 2026-11-19: follow-up campaign `2026-11-19-diversity-followup` declared: profile `diversity-followup-blame`, seeds 10 to 29 of novelty, slot-coverage, flip-tabu and flip-sideways, written into `results-diversity-screen-blame`. av2 takes seeds 10 to 19 and av3 seeds 20 to 29, each with 7 jobs on CPUs 0 to 13 under 28G: 1,520 runs, about 59 CPU-hours. Budget after it: about 145 CPU-hours spent or committed, out of 300. The pooled pass of all six arms follows, rebuilt to add the four (see `3454ce2`'s design.toml).
- 2026-11-19: power. The 95% intervals above are about ±0.1 wide at 30 seeds a side, and a screen arm has 10. Before seeing any screen result, the pass rule's containment half is read as follows. A candidate passes if its shift against `full-k1` is positive and its two directions beat the controls, as the rule states. A positive shift with a non-significant interval counts as *promising*, and those candidates go on to a 30-seed campaign and the pooled pass, budget permitting.
- 2026-11-19: `2026-11-19-diversity-followup` finished on both hosts. Collected with `--host av2`, then `--host av3`: 1,520 new rows, each source row present once. novelty, slot-coverage, flip-tabu and flip-sideways now have 570 runs each (30 seeds × 19 subjects) in `results-diversity-screen-blame`. veribee-d3's hunt holds CPUs 14 to 31 on av3 as well as av2, until about 22 Nov, so the pooled pass is being rebuilt as v2: six arms on CPUs 0 to 13 of each host. (Log entries from here on go at the end. The entries above "power" were inserted before it, although it was written first.)
- 2026-11-19: pooled pass v2 committed on `campaign/diversity-climb` at `e5fff7e3`, not pushed and not enqueued. It has six arms and 15 contrasts in the families orders, screen and joint. All 18 subjects stay (the fewest members is 9, on rad-core-10). Both hosts run on CPUs 0 to 13 under 28G, with 6 draw workers and 10 compare jobs. av2 takes lift-plus-cruise-mini, rad-core-12-18, rad-core-17-18, mode-arbiter, rad-core-55, rad-core-61 and rad-core-10, and av3 takes the other 11. The estimate is about 790,000 pairs: 6.3 to 9.3 CPU-hours of compare and at most 6 of fingerprinting, so 12 to 16 in all, scaled from the exchange pooled pass. **Blocked on the user:** the permission system refused the drafting agent's `git push` and its `rsync` of the untracked design.toml, subject lists and `members/` to `/home/benandrew/projects/counter/` on av2 and av3. I did not run either in its place. Once both are done, enqueue with `python3 scripts/campaign.py enqueue 2026-11-19-diversity-pooled`. Meanwhile the four follow-up arms are screened again at 30 seeds against `full-k1`'s 30 (`screens/<arm>-vs-full-k1-30.json`).
- 2026-11-19: 30-seed screens against `full-k1`'s 30 seeds (`screens/<arm>-vs-full-k1-30.json`), on 18 subjects. Each cell gives the Hodges–Lehmann shift, its 95% interval, p, and the subjects positive / negative. "full in arm" is full-k1's nodes inside the arm's region less the arm's seed-half control. "arm in full" is the reverse.

  | Arm | Net | full in arm | arm in full | Strength (positive: arm stronger) |
  |---|---|---|---|---|
  | novelty | +0.0564 [+0.0303, +0.0779], <0.001, 16/2 | +0.0249 [+0.0127, +0.0376], <0.001, 15/3 | −0.0311 [−0.0451, −0.0163], 0.001, 3/15 | +0.0288 [+0.0017, +0.0487], 0.006, 14/3 |
  | flip-tabu | +0.0063 [−0.0098, +0.0176], 0.44, 10/8 | +0.0002 [−0.0069, +0.0080], 0.90 | +0.0037 [−0.0018, +0.0090], 0.22 | −0.0013 [−0.0148, +0.0004], 0.12 |
  | slot-coverage | +0.0039 [−0.0142, +0.0222], 0.68, 9/9 | +0.0003 [−0.0083, +0.0086], 1.00 | +0.0020 [−0.0087, +0.0141], 0.87 | −0.0010 [−0.0125, +0.0136], 0.52 |
  | flip-sideways | +0.0059 [−0.0027, +0.0221], 0.18, 12/6 | +0.0031 [−0.0037, +0.0136], 0.27 | −0.0030 [−0.0141, +0.0040], 0.47 | +0.0019 [−0.0008, +0.0133], 0.35 |

  No arm changes the number of subjects with a found repair at 30 seeds. Novelty passes on every part of the rule. Its region covers full-k1's nodes beyond control, its own nodes fall outside full-k1's, and it is stronger. Its wall time is ×1.14 over seeds 10 to 29, and that ratio is still confounded: full-k1's seeds 10 to 29 ran at 14 jobs on av3, the arms' at 7 jobs per host. The three flip and coverage arms shrink towards zero from their 10-seed shifts (+0.016, +0.014 and +0.009) and are now indistinguishable from full-k1. The pooled pass (blocked, see above) decides whether novelty's shift holds under BLACK. The screen shrank full's shift against base by about 9×, so novelty's +0.056 is unlikely to vanish there.
