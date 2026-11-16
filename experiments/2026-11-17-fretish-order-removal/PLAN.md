# 2026-11-17-fretish-order-removal

Registered 2026-11-16, before launch.

## Question

Maximality demotion moves a realisable candidate down one front when another realisable candidate is proved strictly stronger. In the ablation of `2026-11-05-fretish-order` it barely reduced the gutted repairs on the final first front (only-demote 521 against base 568, p = 0.17). `genetic.maximality_removal` takes the flagged candidate out of the survivor pool instead. This campaign asks one question: on top of `lean`, does removing proved-weaker realisable candidates make repairs stronger, fewer or faster than demoting them, or does it do none of these?

## What the counters showed

The first explanation for the weak effect was that the distinct survivor pool is usually no larger than the survivor count. In that case `replicate_to_size` keeps every distinct candidate in at least one slot, and demotion only halves its spare copies. The `survivor_selection` counters (run.json schema 67) disprove this. The survivor count is `selection_rate × population_size`, which is 50 at these settings, not 100. Over 27 local runs (`fsm-combined`, `fsm-timing` and `takeoff`, seeds 0–2, base, only-demote and only-demote with removal), the distinct pool held about 63 candidates on average and was truncated in about 91% of selections. So demotion can already cut a candidate. The likely reason a demoted candidate survives is that it moves from front 0 to front 1, and fronts 0 and 1 together usually fit within the 50 slots. Removal took out about 130 candidates per run, and those runs ended with no gutted repairs on the final front. The removal runs averaged 43 repairs, against 33 for base and 36 for only-demote. Three seeds cannot separate that difference from noise.

## Arms

Sweep D in `scripts/gen_configs.py` defines three arms. Each one states `maximality_demotion` and `maximality_removal`, so no arm inherits a default.

| Arm | Keys |
|---|---|
| `lean` | sweep P's `lean` (climb, blame and gate on; demote, scale, xover and verdict off), with `maximality_removal = false` |
| `lean-demote` | `lean` with `maximality_demotion = true` |
| `lean-remove` | `lean-demote` with `maximality_removal = true` |

Every arm runs at the ablation defaults: `crossover_rate = 0.1` and `p_order_crossover = 0`, which is `lean`'s value. The only difference between `lean` here and sweep P's `lean` is the explicit `maximality_removal = false` line. The engine reads that line as its default, so the two arms give the same runs. Sweeps O, P and the default grid regenerate byte-identically: their output before and after sweep D was added was compared file by file.

When removal is on, the parent ordering of the next generation still demotes. The population it orders holds no candidate that was flagged at the last survivor step. A candidate flagged only at the parent ordering would otherwise have to leave a population that must keep its slots.

## Subjects

The subjects are the 19 of `2026-11-17-fretish-order-xover-rate`: the 18 of the paper's pooled analysis, plus `fsm-lmcps`.

- Small set (9): `fsm`, `fsm-timing`, `fsm-combined`, `mode-arbiter`, `takeoff`, `liquid-mixer`, `valu3s-uc6`, `fsm-lmcps`, `lift-plus-cruise-mini`.
- rad's cores (10): `rad-core-1-18`, `-10`, `-12-18`, `-17-18`, `-33`, `-45`, `-55`, `-61`, `-65` and `-68`.

19 subjects × 3 arms × 30 seeds gives 1,710 runs, and `run_experiments.py --profile fretish-order-removal --dry-run` confirms this count.

## Budget and settings

The budget and flags are those of `2026-11-05-fretish-order`:

- `nsga2-apportion` and `--weakening off`;
- weights 0.1, 0.2 and 0.7;
- 1000 bred individuals at population 100, and 500 generations, so that the generation count never binds;
- `--pin-vintage` and a 7200 s cap.

The configs line also passes `--parallel 2`, as in `2026-11-17-fretish-order-xover-rate`, so every run uses 2 threads whatever CPUs its phase holds. The configs go to `experiments/configs-fretish-order-removal`. The profile is `fretish-order-removal` in `scripts/run_experiments.py`.

## Measures

- **Found-repair rate.** The share of runs whose output holds at least one realisable, well-separated repair.
- **Repairs per run.** Maximal repairs, and all repairs before the maximality filter.
- **Wall time.** The median and mean per run, and the share of runs at the cap.
- **Strength and diversity.** The net subsumption share and the net containment share at K = 3, from a pooled pass after this campaign, as in `2026-11-09-fretish-order-pooled`.
- **Final-front gutting.** `final_front.{members, realizable, gutted}` from `run.json`.
- **Survivor selection.** `survivor_selection.{selections, truncated, replicated, pool_size_sum}`. These give the truncated share and the mean distinct pool size per arm.
- **Demotion counters.** `n_maximality_demotions`, `n_maximality_removals` and `n_maximality_removal_fallbacks`.

## Analysis

There are two primary contrasts: `lean-remove` against `lean-demote`, and `lean-remove` against `lean`. A Holm correction runs over the two contrasts for each measure. `lean-demote` against `lean` is reported as a secondary contrast, because the ablation already measured demotion alone.

All contrasts pair runs by (subject, seed).

- Found-repair rate uses the exact McNemar test.
- Repairs per run and wall time use the per-subject difference, summarised by the Hodges–Lehmann shift with the exact Wilcoxon signed-rank interval.
- Strength and diversity use the pooled pass's exact signed-rank test over subjects, with Holm within each contrast family, as in `scripts/pooled_reduce.py`.

Results are reported per subject first. Strength and diversity are then pooled over the 18 subjects, and the run measures over all 19, with `fsm-lmcps` also reported on its own. The counters are descriptive.

## Threats

- **Gutting is near zero by construction.** Under removal, the final population holds no candidate that was flagged at its last survivor step, so `final_front.gutted` is close to 0 for that reason alone. It is a check that removal ran, not evidence that repairs are stronger. Strength must come from the pooled pass.
- **Fallbacks.** The rule proves implication in one direction at a time. A cycle among equivalent candidates that it never proves equivalent can therefore flag every candidate in the pool. Such a selection demotes instead and counts in `n_maximality_removal_fallbacks`. A non-zero count means `lean-remove` partly ran as `lean-demote`, so the count is reported per subject.
- **The pool shrinks.** Removal lowered the mean distinct pool from about 63 to 59 candidates and slightly raised the share of replicated selections. Part of any effect on diversity may come from the smaller pool rather than from the removed candidates.
- **Load.** Each host's subjects run in one phase. If the av2 phase is widened part-way through, a (subject, seed) cell can straddle the change. The thread count is fixed, so only contention changes. The three timing-bound subjects run on av3, where the CPU set does not change.
- **Commit.** This campaign runs at a later commit than the ablation, so its rows do not compare with the ablation's rows.
- **Power.** At 30 seeds, the ablation's largest single-feature effect reached only p = 0.072. A null result here limits the size of an effect but does not show that there is none.

## Cost

The estimate scales the figure of `2026-11-17-fretish-order-xover-rate`, about 670,000 run-seconds for 12 arms, to 3 arms. That figure includes 30% extra for order crossover, which no arm here has. Against that, all three arms here run `lean`, whose climb costs more than `base`. These two corrections roughly cancel. Demotion and removal add the rule's proof pass, which costs 1–5% of a run (`docs/dev/algorithm.md`). The estimate is about 150,000 run-seconds, roughly half of it on av3 for `lift-plus-cruise-mini`, `liquid-mixer` and `valu3s-uc6`, with at least ±30% error. At 8 jobs, av3's phase takes about 3 h. At 3 jobs on CPUs 0–5, av2's phase takes about 7 h, or about 2.5 h if it is widened to 8 jobs on CPUs 0–15. No score phase is declared. The pooled pass is a separate campaign and is not costed here.

## Hosts and the queue

The host split and the CPU confinement are those of `2026-11-17-fretish-order-xover-rate`: av3 on CPUs 16–31 under 32 GB at 8 jobs, and av2 on CPUs 0–5 under 12 GB at 3 jobs. This campaign queues behind the xover-rate entries, 072 on av3 and 076 on av2, so it starts on each host when that entry ends. If av2's CPUs 0–15 are free by then, widen the av2 phase to `confine = { av2 = { cpus = "0-15", memory_max = "32G" } }` and `jobs = 8` before you enqueue it.

## Launch

Create `campaign/fretish-order-removal` from `feat/demotion-removal`. Commit the engine change, `scripts/gen_configs.py`, `scripts/run_experiments.py` and this directory on it, then run:

```sh
python scripts/campaign.py queue                 # 072 on av3 and 076 on av2 still ahead?
python scripts/campaign.py stage 2026-11-17-fretish-order-removal --host av2 --dry-run
python scripts/campaign.py enqueue 2026-11-17-fretish-order-removal --host av3
python scripts/campaign.py enqueue 2026-11-17-fretish-order-removal --host av2
python scripts/campaign.py status
```

Sweep X lives on `campaign/fretish-order-xover-rate` and sweep D lives here. Both branches add an entry to `SWEEPS` and a profile in the same places, so merging them will conflict on those lines.
