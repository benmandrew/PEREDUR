# 2026-11-18-seeded-corpus-sideways: report

This campaign regenerated the seeded corpus with three *sideways* fault classes (`swap-signal`, `flip-literal` and `reverse-implication`) in the table that the 2 to 4 edits of each mutant draw from, and ran the paper's seeded arms on it. A mutant is kept when it is unrealisable and `compare` proves it strictly stronger than its base B or incomparable with it. The question was whether the per-feature verdicts on the strengthening-only corpus of `2026-11-13-seeded-corpus` hold once some faults are sideways. The design is in [PLAN.md](PLAN.md).

## Execution

The corpus has 130 selected subjects: 120 headline and 10 trivial, from 6 systems. Of the 130, 85 are incomparable with B and 45 strictly stronger, and 95 hold at least one sideways edit. The headline subjects by system are `fsm` 40, `lmcps` 40, `liquid-mixer` 16, `rad` 15, `mode-arbiter` 8 and `valu3s` 1.

The runs took place on av2 (entry 091, seeds 0-2, CPUs 0-15) and av3 (entry 088, seeds 3-4, CPUs 16-31), each under a 24G cap, from 21:40 UTC on 18 November. av3 finished at 02:44 and av2 at 05:00 on 19 November. All 6,500 runs (130 subjects × 10 arms × 5 seeds) are at commit `e117a76` with a clean tree, with no failures, no retries and no timeouts. The mean wall time was 54.1 s per run. `campaign.py collect` merged them into `results-fretish-seeded-sideways.csv` on 19 November at 12:49.

Every arm with the gate on ran in exchange mode (`gate_realizable = "exchange"`), the default since 18 November. The paper's arm names map from the levels as follows: `remove-exchange` is `full`, `lean-exchange` is `no-remove`, `remove-no-climb-exchange` and `remove-no-blame-exchange` are `no-climb` and `no-blame`, and `only-gate-exchange` is `only-gate`. `remove-no-gate`, `only-remove`, `only-climb`, `only-blame` and `base` have no gate and ran plain.

## Probe

The trivial-reversion probe (5,000 draws of depth 1 and 2 under `base`'s configuration) gave a mean p1 of 0.0007 and a mean p2 of 0.0008 over the headline subjects. Rerun under `remove-exchange`'s configuration on the 130 selected subjects, it gave the same counts on all 130, since the chains carry no parent context. Rerun with every draw given an unrealisable parent at status 0.5 (`--unrealizable-parent 0.5`), the mean p1 was 0.0007 and the mean p2 0.0007. 6 headline subjects rose, 2 fell, and none crossed the 0.01 threshold, so the headline set holds under the gate.

## Recovery

The unit is the subject, with each outcome averaged over its 5 seeds. The tests are exact signed-rank tests over the 120 headline subjects, with Holm over the four features in each direction and a 95% bootstrap interval that resamples whole systems. The analysis is `seeded()` in peredur-extension's `scripts/summarise.py`, at commit `e725ae2`.

| Arm | Recovery | Found a repair | Median wall (s) |
|---|---|---|---|
| `base` | 14.0% | 96.2% | 42.2 |
| `full` | 27.2% | 96.7% | 35.7 |
| `no-climb` | 20.7% | 96.7% | 33.2 |
| `no-gate` | 28.7% | 96.2% | 54.9 |
| `no-blame` | 30.8% | 96.3% | 35.4 |
| `no-remove` | 29.2% | 96.7% | 33.1 |
| `only-climb` | 29.5% | 96.2% | 54.2 |
| `only-gate` | 17.7% | 96.3% | 31.1 |
| `only-blame` | 16.7% | 96.2% | 44.8 |
| `only-remove` | 17.3% | 96.2% | 42.5 |

| Contrast | Recovery shift | Holm p | Systems up/down | Bootstrap (pp) | Without `rad` | Wall shift (s) |
|---|---|---|---|---|---|---|
| `full` − `base` | +13.2 pp | p = 2e-5 (uncorrected) | 6/0 | [+3.1, +44.6] | +5.9 pp, p = 0.018 | −1.3 |
| `only-climb` − `base` | +15.5 pp | 7e-12 | 6/0 | [+5.4, +46.1] | +8.2 pp, Holm 1e-7 | +14.3 |
| `only-gate` − `base` | +3.7 pp | 0.029 | 5/1 | [+0.3, +13.2] | +1.7 pp, Holm 0.20 | −13.9 |
| `only-remove` − `base` | +3.3 pp | 0.033 | 6/0 | [+1.9, +5.7] | +3.6 pp, Holm 0.066 | +1.1 |
| `only-blame` − `base` | +2.7 pp | 0.29 | 4/2 | [−2.2, +16.5] | +0.4 pp, Holm 0.87 | +14.3 |
| `no-climb` − `full` | −6.5 pp | 3e-5 | 0/4 | [−22.9, −0.9] | −1.9 pp, Holm 0.031 | −3.6 |
| `no-gate` − `full` | +1.5 pp | 0.49 | 2/3 | [−2.6, +6.2] | +1.7 pp, Holm 0.80 | +36.8 |
| `no-blame` − `full` | +3.7 pp | 0.42 | 3/2 | [−1.3, +10.6] | +3.4 pp, Holm 0.61 | −3.3 |
| `no-remove` − `full` | +2.0 pp | 0.49 | 5/0 | [+0.6, +5.7] | +1.1 pp, Holm 0.80 | −8.6 |

The climb is the only mechanism that is significant in both directions and without `rad`. Two-way mutation and removal each raise `base`'s recovery rate, but neither survives Holm without `rad`, and removing either from `full` changes nothing significant. Two-way mutation costs no recovery when removed and saves 36.8 s per run when kept. Blame has no significant effect in either direction.

## By relation to the base

Recovery differs by the mutant's relation to B. On the 81 incomparable headline subjects, `base` recovers 12.6% and `full` 23.7% (signed-rank p = 0.002). On the 39 strictly stronger ones, `base` recovers 16.9% and `full` 34.4% (p = 0.010). Two-way mutation's gain comes from the stronger mutants alone: `only-gate` recovers 27.2% of them against `base`'s 16.9%, and 13.1% of the incomparable ones against 12.6%. The climb gains on both, with `only-climb` at 27.7% on incomparable and 33.3% on stronger. These splits are uncorrected and were not planned.

## Against the old corpus

The strengthening-only corpus gave `full` 37.1% and `base` 23.1% over its 110 headline subjects, so the sideways corpus is harder for every arm. The verdicts on climb and blame repeat: climb is significant in both directions (old `no-climb` −7.1 pp, new −6.5 pp), and blame is not (old +2.9 pp, new +3.7 pp). Two-way mutation's addition gain fell from +8.4 pp to +3.7 pp, and removal's leave-one-out effect changed sign, from −5.1 pp (Holm 0.011) to +2.0 pp (Holm 0.49). The old corpus's gate-on arms ran without exchange mode, so these changes mix the corpus with the gate's mode and do not isolate either.

## Conclusions

On the sideways corpus, the climb carries `full`'s recovery gain, and the other three mechanisms add little to it. The gain from two-way mutation is confined to mutants that are strictly stronger than their base. Removal's leave-one-out benefit on the old corpus does not carry over.

## Reproduce

**Corpus.** `scripts/seeded/seed.py --sideways` (both passes, with the seeds and options of `2026-11-13-seeded-corpus`), then `scripts/seeded/probe.py` and `scripts/seeded/choose.py` (cap 40, 10 trivial). The probe reruns are `probe.py --config experiments/configs-fretish-seeded-sideways/nsga2-apportion/wkoff/log/sweep_D_remove-exchange.toml --only-selected --jobs 6`, with `--tag=-full`, and with `--tag=-gated --unrealizable-parent 0.5`. The reachability check is `scripts/reach.py`.

**Runs.** `campaign.py enqueue 2026-11-18-seeded-corpus-sideways --host av2`, the same with `--host av3`, then `campaign.py collect --profile fretish-seeded-sideways`.

**Data.** The corpus (`subjects/`, `selected.txt`, `subjects.csv`, `probe*.csv`, `mutants*.csv`, `bases.csv`), `reach.csv` and the results (`experiments/results-fretish-seeded-sideways.csv` and its per-run directories, 3.1 GB) are untracked. They are in the `seeded-corpus-sideways` worktree of `~/projects/peredur` on the analysis machine, and the per-host originals are in `~/projects/counter/experiments` on av2 and av3. peredur-extension's `make import` copies the CSVs into its `data/`.
