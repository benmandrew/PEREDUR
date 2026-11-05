# FRETISH order-directed search: evaluation plan

This campaign evaluates the seven FRETISH keys added on 2026-10-31 for a new paper. The paper cites the TLSF paper (`~/projects/writing/peredur-paper`) for the algorithm, the measures and the TLSF results, so nothing here re-runs TLSF. Each key was measured on 3 seeds over 8 small examples, and `docs/dev/operators.md` records that "three seeds cannot separate these from noise". Every key is therefore still off by default, and this campaign decides them.

## Research questions

1. **RQ1, effect.** Does the order-directed search (every key on) find more repairs, and better ones, than the same engine with every key off? Is it better than *random selection* on the same budget?
2. **RQ2, ablation.** What does each key contribute? The ablation runs in both directions: each key removed from the full arm, and each key added alone to the baseline.
3. **RQ3, scale.** How do the baseline and the full arm behave on the three largest specifications, where most runs are expected to reach the cap?
4. **RQ4, soundness.** Are the implication rules that the order moves depend on exact? The Lean proofs and the Spot cross-checks answer this without a campaign.

## Subjects

All 12 bundled FRETISH examples. *Req.* counts assumptions plus guarantees in `spec.json`.

| Spec | Req. | Fixes | Set |
|---|---|---|---|
| `fsm` | 3 | 3 | small |
| `fsm-timing` | 3 | 5 | small |
| `takeoff` | 3 | 3 | small |
| `mode-arbiter` | 4 | 1 | small |
| `fsm-combined` | 6 | 8 | small |
| `liquid-mixer` | 12 | 1 | small |
| `valu3s-uc6` | 12 | 0 | small |
| `fsm-lmcps` | 17 | 1 | small |
| `lift-plus-cruise-mini` | 48 | 1 | small |
| `ventilator-controller` | 69 | 0 | large |
| `rad` | 74 | 0 | large |
| `lift-plus-cruise-full` | 76 | 1 | large |

The only fix of `mode-arbiter` is out of reach of the search (see the comment on `FRETISH_SPECS` in `scripts/run_experiments.py`), so its `implies_ideal` is 0 by construction. A spec with no `fixes/` directory gets `best_relation = "no-ideals"` and contributes to found-repair rate, repair count and wall time alone.

## Arms

Sweep O in `scripts/gen_configs.py` defines the arms. Every arm writes all seven keys, so no arm inherits a default.

| Feature | Keys when on |
|---|---|
| `xover` | `p_order_crossover = 0.3` |
| `climb` | `climb_output = true` |
| `demote` | `maximality_demotion = true` |
| `blame` | `blame_targeted_slots = true` |
| `gate` | `direction_gated = true`, `bisect_steps = true` |
| `scale` | `step_scaling = 1.0` |
| `verdict` | `parent_verdict = true` |

The "on" values are the ones the 3-seed measurements in `docs/dev/operators.md` used. `bisect_steps` acts only under `direction_gated`, so the two keys form one feature.

Under `nsga2-apportion` the small set runs 16 arms: `base` (all off), `full` (all on), `no-<f>` for each feature and `only-<f>` for each feature. Under `random` it runs `base` and `only-climb`. Random plus the climb is the control for the climb itself. The climb strengthens every gated repair towards the original after the search, so it could reach the ideals without help from selection. The large set runs `base` and `full` under `nsga2-apportion`, and `base` under `random`.

The `random` selection scheme shuffles the parents before breeding, which makes the bred parents and the elites a uniform draw. It also cuts the deduplicated (μ+λ) pool by a shuffle and reads no objective (see "Algorithm flow" in `docs/dev/algorithm.md`). Scoring still runs, so each individual costs what it costs under the other schemes. The accumulator collects every repair that passes the gate in any generation, so random selection is not limited to its last population.

## Budget and settings

The budget is the TLSF paper's: `termination = "individuals"` with `max_individuals = 1000` at `population_size = 100`, and `generations = 500` so that the generation count never binds. The harness cap is 7200 s per run. Weights are 0.1, 0.2 and 0.7. They decide nothing under NSGA-II or random selection and are written only for parity with the cited paper. `--pin-vintage` writes every key whose default has moved. `runtime.parallel` comes from `run_experiments.py` as cores divided by jobs, which gives 2 threads per run for the small set, 10 for `lift-plus-cruise-full` and 4 for `rad` and `ventilator-controller` on the 32-core hosts. Each phase leaves 2–4 cores free for the Ollama server that shares the hosts. Every arm of a seed runs at the same thread count, so the pairing holds.

`max_wall_s` stays unset, because `docs/dev/algorithm.md` warns that it makes a seed-paired comparison machine-dependent. A run killed at the harness cap therefore writes no `run.json`. Its `accumulated/index.tsv` survives, and the scoring phase reads its curve from that file.

## Measures

The definitions are the cited paper's, so the new paper can refer to them.

- **Found-repair rate.** The share of runs whose output holds at least one realisable, well-separated repair.
- **Ideal implication.** `implies_ideal` and the count of repairs equivalent to or stronger than an ideal, from `compare`. The TLSF paper had no ideals, so this measure is new.
- **Output size.** Maximal repairs per run, and all repairs before the maximality filter.
- **Wall time.** Median and mean per run, and the share of runs at the cap.
- **Anytime curves.** Found-repair rate and output size against elapsed time, from the scoring phases.
- **Strength.** The net subsumption share between the maximal sets of two arms.
- **Diversity.** The net containment share, read against a second-seed control.
- **Mechanism counters.** Read from `run.json`: `order_crossover.{join,meet,relink}.{attempted,proved,realizable}`, the `climb` block, `n_maximality_demotions` and the parent-verdict drops.

## Analysis

All contrasts pair by (spec, seed), because every arm of a seed runs on one host. Found-repair rate and `implies_ideal` use the exact McNemar test on the paired runs. Continuous measures take a per-spec difference, summarised by the Hodges–Lehmann shift with the exact Wilcoxon signed-rank interval, as in the cited paper. The statistics module is `data/stats.py` in the paper repository.

- **RQ1:** `full` against `base`, and `full` and `base` each against random `base`. Random `only-climb` against random `base` and against `full` separates the climb from the search.
- **RQ2:** each `no-<f>` against `full`, and each `only-<f>` against `base`, with a Holm correction over the 7 features in each direction.
- **RQ3:** `full` against `base` on the large set, found-repair rate and wall time only (`rad` and `ventilator-controller` have no ideals).
- **RQ4:** pass counts of `scripts/field_orders.py`, `scripts/check_lean_timing_order.py`, `scripts/check_lean_scope_order.py` and `scripts/check_lean_lowering.py`, run locally at the campaign commit.

## Campaigns

Two declarations share the profiles `fretish-order` (small set) and `fretish-order-large` (large set) and their results CSVs.

- `2026-11-05-fretish-order-pilot` runs seed 0 on av2 and seed 15 on av3 over every arm and spec. Its purpose is to replace the estimates below with measured times. Its rows resume into the main campaign, because the resume key holds the spec, the arm and the seed.
- `2026-11-05-fretish-order` runs seeds 0–14 on av2 and 15–29 on av3. Its phases are `small` (4,860 runs, 15 jobs), `lift-full` (90 runs, 3 jobs), `rad-vent` (60 runs over seeds 0–4 and 15–19, 7 jobs), then a score phase for each profile.

## Cost

The table gives estimates for both hosts together. The basis for each is in the last column, and the pilot replaces them.

| Phase | Runs | Estimate | Basis |
|---|---|---|---|
| small | 4,860 | 1–3 h | 230 s over 24 default runs on 8 small specs; climb up to 2.2×; `lift-plus-cruise-mini` 49 s median at 10 generations |
| lift-full | 90 | about 3 h | 176 s median at 3 generations and parallel 8 |
| rad-vent | 60 | about 8 h | `ventilator-controller` took 1,600 s for 2 generations of 20 in September, before the controller store and the decomposition re-ask existed |
| scoring | all | a few hours | not measured; FRETISH runs hold about 29 repairs each, against about 100 on TLSF |
| pilot | 324 small, 6 + 12 large | about 2.5 h | dominated by the large specs at the cap |

## Launch

```sh
python scripts/campaign.py stage 2026-11-05-fretish-order-pilot --dry-run
python scripts/campaign.py stage 2026-11-05-fretish-order-pilot
python scripts/campaign.py enqueue 2026-11-05-fretish-order-pilot
# after the pilot: update the Cost table from its wall times, commit, then
python scripts/campaign.py stage 2026-11-05-fretish-order
python scripts/campaign.py enqueue 2026-11-05-fretish-order
python scripts/campaign.py status
```

## Out of scope

- **Flattened TLSF and AuRUS.** A comparison with PEREDUR's TLSF mode and with AuRUS needs a FRETISH-to-TLSF converter, which does not exist (`src/tlsf/writer.cpp` writes TLSF specifications only). It also needs the AuRUS launcher that `1b12cb7e` removed. Their repairs are not FRETISH, so they would support the semantic measures alone.
- **Random sampling.** Mutating the original k times per candidate with no generations would test search against no search. It needs its own driver path. Random selection keeps the operators and the generations, and tests selection pressure.

## Open questions

1. Are 0.3 for `p_order_crossover` and 1.0 for `step_scaling` the right "on" values, or does either need a level sweep first?
2. What rule moves a default to on? The proposed rule is a significant gain in found-repair rate or `implies_ideal` in at least one ablation direction, with no significant loss in the other direction and no more than a 2× rise in median wall time.
3. Should `rad` and `ventilator-controller` stay in? If the pilot shows every run at the cap, they show only that the engine does not scale to them.
4. Does the paper need the flattened TLSF and AuRUS comparison, given its cost (the converter, the launcher, and about half a day on both hosts)?
5. Should the campaign run from this feature branch, or from `main` once the `random` scheme is merged? Every row records its commit, so the choice fixes the commit that the provenance tag names.
