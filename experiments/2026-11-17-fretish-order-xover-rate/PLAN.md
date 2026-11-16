# 2026-11-17-fretish-order-xover-rate

Registered 2026-11-16, before launch.

## Question

The ablation of `2026-11-05-fretish-order` found no effect of order crossover in either direction, and the pooled pass found none on strength (`only-xover` against `base`, Holm p = 0.33 on the 18 pooled subjects). That arm may have fired too rarely to show one. Order crossover is drawn inside the crossover draw (`include/genetic/pipeline.hpp`, the `probability_check(cfg.crossover_rate, …)` near line 361, then `order_crossover_arm`), so at `crossover_rate = 0.1` and `p_order_crossover = 0.3` it reached at most 3% of bred children. It reached fewer where a parent's verdict was unknown or the pair gave no operator. This campaign asks two questions.

1. **Probability.** Does order crossover have an effect when every crossover that can take it does, at `p_order_crossover = 1.0`?
2. **Rate.** Does that effect grow when crossover itself fires more often, at `crossover_rate` 0.3 and 0.5 as well as the default 0.1?

## Arms

Sweep X in `scripts/gen_configs.py` defines the arms: four arms at each of three crossover rates, 12 in all. A level is named `cr<100 r>-<arm>`, so `cr30-lean-xover` is `lean-xover` at `crossover_rate = 0.3`. Every arm writes `crossover_rate` and all eight order keys, as sweep O does, so no arm inherits a default.

| Arm | Keys |
|---|---|
| `base` | sweep O's `base`: every order feature off |
| `only-xover` | `base` with `p_order_crossover = 1.0` |
| `lean` | sweep P's `lean`: `full` without demote, scale, xover and verdict, so climb, blame and gate on |
| `lean-xover` | `lean` with `p_order_crossover = 1.0` |

The 12 levels are `cr10-base`, `cr10-only-xover`, `cr10-lean`, `cr10-lean-xover`, and the same four under `cr30-` and `cr50-`. `ORDER_FEATURES` and sweeps O and P are unchanged, because archived campaigns regenerate their configs from them at 0.3. `cr10-base` is sweep O's `base` and `cr10-lean` is sweep P's `lean`, apart from `runtime.parallel` (see Budget). At `p_order_crossover = 1.0` the arm fires on at most 10%, 30% and 50% of bred children at the three rates.

## Subjects

The 18 subjects of the paper's pooled analysis, and `fsm-lmcps` as a 19th. The paper counts the 18 as "the small set less `fsm-lmcps`, which has no pairs, and the 10 unrealisable cores of `rad`" (`~/projects/writing/peredur-extension/paper/evaluation.tex`; the `pooled-*` levels of `data/stats.json` carry n = 18). `fsm-lmcps` drops out of the pooled pass because `base` never repairs it, so it has no frontier to compare. It stays here because it costs about 4% of the campaign and is the one subject where `base` finds nothing, which makes it the most sensitive to a found-rate effect.

- Small set (9): `fsm`, `fsm-timing`, `fsm-combined`, `mode-arbiter`, `takeoff`, `liquid-mixer`, `valu3s-uc6`, `fsm-lmcps`, `lift-plus-cruise-mini`.
- rad's cores (10): `rad-core-1-18`, `-10`, `-12-18`, `-17-18`, `-33`, `-45`, `-55`, `-61`, `-65` and `-68`.

19 subjects × 12 arms × 30 seeds gives 6,840 runs, which `run_experiments.py --profile fretish-order-xover-rate --dry-run` confirms.

## Budget and settings

The budget and flags are those of `2026-11-05-fretish-order`: `nsga2-apportion`, `--weakening off`, weights 0.1, 0.2 and 0.7, 1000 bred individuals at population 100, 500 generations so that the count never binds, `--pin-vintage` and a 7200 s cap. The configs go to `experiments/configs-fretish-order-xover-rate`, and the profile is `fretish-order-xover-rate` in `scripts/run_experiments.py`.

The configs line also passes `--parallel 2`. The earlier campaigns ran 15 jobs on 32 cores, and `run_experiments.py` derived 2 threads a run from that. This campaign runs confined to fewer CPUs, at 3 jobs on 6 CPUs and then 8 on 16, and the derived value would move with the phase. A stated value keeps every run at 2 threads, the earlier campaigns' count, whatever CPUs its phase holds.

## Measures

- **Found-repair rate.** The share of runs whose output holds at least one realisable, well-separated repair.
- **Ideal rate.** `implies_ideal` on the seven small subjects with a reachable ideal. The cores and `valu3s-uc6` have no ideals, and the one fix of `mode-arbiter` is out of reach.
- **Repairs per run.** Maximal repairs, and all repairs before the maximality filter.
- **Wall time.** Median and mean per run, and the share at the cap.
- **Strength and diversity.** The net subsumption share and the net containment share at K = 3, from a pooled pass after this campaign, as in `2026-11-09-fretish-order-pooled`.
- **Order-crossover counters.** `order_crossover.{join,meet,relink}.{attempted,proved,realizable}` from `run.json`, read against the number of bred children. These show how often the arm fired and how often its child was kept, and so check the premise of the campaign directly.

## Analysis

The primary contrasts pair each feature arm with its control at the same crossover rate: `only-xover` against `base` and `lean-xover` against `lean`, at each of the three rates. That is six contrasts per measure, with a Holm correction over the six. All contrasts pair by (subject, seed). Found-repair rate and ideal rate use the exact McNemar test on the paired runs. Repairs per run and wall time take a per-subject difference, summarised by the Hodges–Lehmann shift with the exact Wilcoxon signed-rank interval. Strength and diversity use the pooled pass's exact signed-rank test over subjects, with Holm within each contrast family, as in `scripts/pooled_reduce.py`. Results are reported per subject first, then pooled over the 18 for strength and diversity and over all 19 for the run measures, with `fsm-lmcps` also reported on its own.

The rate contrasts are secondary: `cr30-` and `cr50-` against `cr10-`, within `base` and within `lean`, four contrasts under their own Holm family. They show what raising the crossover rate does without order crossover, which is needed to read the primary contrasts at the higher rates.

The counters are descriptive. If `only-xover` at rate 0.5 still shows no effect while its counters show the arm firing on a large share of children, the null in the ablation was not a matter of rarity.

## Threats

- **The lean features skip crossover offspring.** Blame-targeted slots, the direction gate and step scaling all leave a crossover offspring alone (`include/config.hpp`, lines 236–271): blame keeps the full slot list, the gate acts only "where crossover did not fire", and scaling leaves the offspring unscaled. Raising `crossover_rate` therefore lowers how often blame and the gate act in the `lean` arms, from 90% of children at 0.1 to 50% at 0.5. That is why `lean` has its own control at each rate. A `lean-xover` contrast measures order crossover against a `lean` arm that is itself weaker at the higher rates, and the rate contrasts within `lean` measure that loss.
- **Order crossover replaces the graft.** `fretish_crossover` falls back to the subformula graft only when the order arm yields no child. At p = 1.0, a contrast against its control measures order crossover in place of the graft on those children, not order crossover added to it.
- **Load.** Each host's subjects run in one phase, but av2's phase starts on 6 CPUs and widens to 16 on Tuesday (see Launch). A (subject, seed) cell can straddle the change, with some arms under a different load. Thread count is fixed, so only contention moves. The three timing-bound subjects (`lift-plus-cruise-mini`, `liquid-mixer`, `valu3s-uc6`) run on av3, whose CPU set does not change, because their results move with time as well as seed.
- **Commit.** The campaign runs at a later commit than the ablation, so its arms do not compare with the ablation's rows. `cr10-base` against sweep O's `base` on the small set is a reproduction check, not a contrast. The timing-bound subjects differed across commits before (`2026-11-16-fretish-order-pairs`), and are read apart there.
- **Power.** The largest single-feature effect of the ablation, the climb's addition-removal gap, reached only p = 0.072 at 30 seeds. A null here bounds the effect but does not show there is none.

## Hosts and the queue

The hosts are shared with other agents. `campaign.py queue` and `status` at 19:15 UTC on Mon 16 Nov showed this state.

- **av3.** Entry 071 (`2026-11-16-fretish-order-pooled-joint`) is running, phase 1 of 3, on CPUs 16–31 under a 32 GB cap, and is expected to finish between 21:30 and 22:30. Nothing else is queued. Outside the queue, veribee-33's `hunt.slice` holds CPUs 0–15 until Tue 17 Nov 09:00, free from about 09:30. gawain-76 announced reruns on CPUs 24–31 from about 12:20 for up to 4 h. These are probably over, but check before enqueuing.
- **av2.** Nothing is running or queued. Entry 056 (`2026-10-08-tlsf-rerun-analysis`) has been `failed` since 10 Oct. A failed entry stops moving and does not block a later one. Outside the queue, `hunt.slice` fuzz units hold CPUs 6–31 until Tue 09:00, then everything is free from about 09:30. CPUs 0–5 are free now. `AllowedCPUs` is ignored on these hosts, so those units can drift onto 0–5.

The split is by subject, as in `2026-11-09-fretish-order-cores`, so every arm of a (subject, seed) cell runs on one host and each host holds all 30 seeds of its subjects. av3 runs the three timing-bound subjects and four cores (2,520 runs), confined to CPUs 16–31 and 32 GB at 8 jobs. av2 runs the other six small subjects and six cores (4,320 runs), confined to CPUs 0–5 and 12 GB at 3 jobs, then widened to CPUs 0–15 and 32 GB at 8 jobs after 09:30 on Tuesday. 8 jobs at 2 threads fills the 16-core, 32 GB per-agent share, and stays under the 14–15 jobs the other campaigns on these hosts ran.

The pooled pass after this campaign runs fingerprint, compare and pool phases. On `main`, `confine` applies to run phases only. Its extension to those phases is on branch `campaign/fretish-order-pooled-joint` (commit `1663f21a`) alone. If the pooled pass runs while the hosts are still shared, it needs that commit merged first, or its phases will spread over CPUs others hold.

## Cost

The estimate is built from the mean wall time per run under the same budget at 2 threads a run: the small set's per-subject means in `results-fretish-order.csv` (32.5 s for `fsm` and `mode-arbiter` up to 505 s for `lift-plus-cruise-mini`), and about 40 s a run for the cores in `results-fretish-order-cores.csv`. It adds 30% for order crossover firing on up to half the children, which calls the prover where the graft does not. That gives about 670,000 run-seconds in all, about 345,000 of them on av3 for `lift-plus-cruise-mini`, `liquid-mixer` and `valu3s-uc6`. The figures carry at least ±30%.

| Option | Start | Finish (estimate) |
|---|---|---|
| A, chosen: av2 on CPUs 0–5 now, av3 on 16–31 after entry 071, av2 widened to 0–15 at 09:30 Tue | 19:30 Mon (av2), about 22:30 Mon (av3) | about 13:00 Tue, range 10:00–16:30 |
| A without av3 overnight (071 overruns, or gawain-76 still holds 24–31) | 19:30 Mon (av2), 09:30 Tue (av3) | about 21:30 Tue, set by the three timing-bound subjects on av3 |
| B: both hosts at 8 jobs from 09:30 Tue | 09:30 Tue | about 21:00 Tue |

Option A finishes about 8 h before option B, because av3's half runs overnight on CPUs that 071 frees. It needs one hand step on av2 at 09:30, since the queue has no start time and a phase cannot change its own CPU set. If nobody takes that step, the av2 phase still finishes on CPUs 0–5, at about 18:30 Tue. No score phase is declared, since the measures above need no anytime curves. The pooled pass is a separate campaign and is not costed here.

## Launch

Commit `scripts/gen_configs.py`, `scripts/run_experiments.py` and this directory on `campaign/fretish-order-xover-rate`, then:

```sh
python scripts/campaign.py queue                 # 071 still running on av3? anything new on av2?
python scripts/campaign.py stage 2026-11-17-fretish-order-xover-rate --host av2 --dry-run
python scripts/campaign.py enqueue 2026-11-17-fretish-order-xover-rate --host av2
# av3: check gawain-76 is off CPUs 24-31 first. Do not stage av3 while 071
# runs: the entry queues behind 071 and its tick stages av3 when 071 is done.
python scripts/campaign.py enqueue 2026-11-17-fretish-order-xover-rate --host av3
```

At 09:30 on Tue 17 Nov, once veribee-33 confirms av2 is free, widen the av2 phase in `campaign.toml` to `confine = { av2 = { cpus = "0-15", memory_max = "32G" } }` and `jobs = 8`, commit, and re-enqueue. Resume keeps the finished rows.

```sh
python scripts/campaign.py dequeue --host av2 <entry>.toml
python scripts/campaign.py enqueue 2026-11-17-fretish-order-xover-rate --host av2
python scripts/campaign.py status
```
