# Seeded mutant corpus: report

## Summary

The ablation in `2026-11-05-fretish-order` judged seven optional features of the PEREDUR genetic algorithm (GA) on 9 real small subjects from about 7 sources. This experiment repeats the same 16 arms on 110 *seeded mutants* drawn from 6 systems, so that each verdict rests on more subjects with a known *ideal* repair. The *full* configuration repairs a mutant back to an implication of its base in 34.2% of runs against 23.1% for *base*, a gain of 11.1 percentage points (pp) with p = 0.0001 and all 6 clusters up. Climb alone adds 16.7 pp. Gate saves time. Demote now costs time and buys nothing measurable.

| Feature | 9-subject verdict | Seeded verdict |
|---|---|---|
| climb | useful | Confirmed in both directions and all clusters; survives dropping rad; costs about 10 s per run |
| gate | helps strength and wall time | Wall-time saving confirmed strongly (14.5 s on removal); strength gain shows on addition only and depends on rad |
| blame | helps wall time only | Wall-time saving confirmed (3.7 s); no strength benefit; the apparent strength cost in `full` is a rad artefact |
| demote | borderline | Negative: no strength effect, costs 1.8 s per run in all 6 clusters; candidate for removal from the default |
| scale | no significant benefit | No strength effect; small time cost; a marginal found-rate loss when added alone |
| xover | no significant benefit | No effect on any outcome; agrees |
| verdict | no significant benefit | No effect on any outcome; agrees |

## Design

A *base* B is a realisable FRETish specification with a fixed input/output partition, and every live guarantee in B is realisable alone. A *mutant* O is B strengthened by a compound seed of 2 to 4 primitive edits (`scripts/seeded/seed.py` lists the ten edit kinds: deadline tightening, duration lengthening, trigger widening, response conjunction, conflicting outputs and others). Each O is satisfiable and unrealisable, each of its guarantees is realisable alone, and `compare` proves it *strictly stronger* than B. B is written as O's only ideal, so the *ideal rate* is the share of runs whose best repair R satisfies B ⊑ R, which means R implies B.

A seeded corpus invites a shortcut, since one or two random mutations might undo the seed. The *trivial-reversion probe* measures that risk with the GA's own `mutate_specification` operator. It samples 5000 mutation chains of depth 1 and 2 from each O under the `base` arm's configuration and counts chains that end realisable and imply B. A subject is *headline* when both estimated probabilities, p1 and p2, are below 0.01, a threshold fixed before any arm ran. Of 248 mutants, 22 were trivial. The selection kept 120 subjects: 110 headline subjects (fsm 40 of 156, lmcps 36, rad 17, liquid-mixer 11, mode-arbiter 5, valu3s 1) and 10 trivial controls.

The arms are those of the order campaign: `base`, `full`, `no-<f>` (full without feature f) and `only-<f>` (base with feature f). Every run used `nsga2-apportion` with a budget of 1000 individuals, a population of 100 and a 7200 s cap. Five seeds over 16 arms and 120 subjects make 9600 runs, of which 9598 produced rows. All runs used commit `b2d3b91c` from a clean build. Seed 0 took 200 min and seeds 1 to 4 took 801 min, at 8 parallel jobs on 16 cores under a 20 GB cap (`systemd-run --user --scope -q --collect -p MemoryMax=20G -p MemorySwapMax=0 taskset -c 0-15 ...`). No run or compare step timed out, and the two failures are an engine bug described under corpus findings.

The unit of analysis is the subject, with each outcome averaged over its 5 seeds. Contrasts are paired differences over subjects, tested with the exact Wilcoxon signed-rank test. A *Holm correction* runs over the seven features, separately for each direction and outcome. Each contrast also reports a *Hodges–Lehmann (HL) shift* with a 95% interval, the number of clusters that moved up and down, and a 95% *cluster bootstrap* of the mean difference that resamples the 6 systems. Six clusters is few. The bootstrap is coarse, and one system (rad) can move a result on its own.

## Results

### Arm means

| Arm | Ideal rate | Found-repair rate | Maximal repairs per run | Median wall time |
|---|---|---|---|---|
| `base` | 0.231 | 0.933 | 25.4 | 49.3 s |
| `full` | 0.342 | 0.922 | 13.4 | 46.5 s |

Against `base`, `full` raises the ideal rate by 11.1 pp (p = 0.0001, 6 clusters up and 0 down, bootstrap [+5.3, +28.9] pp). Its *found-repair rate* falls by 1.1 pp, which is not significant. It returns 12.0 fewer maximal repairs per run and runs 2.8 s faster at the median. Exact recovery of B, where the best repair is equivalent to B, stays at about 1% in every arm. Nearly every repair that implies B is strictly stronger than it.

### Addition (`only-<f>` against `base`)

| Feature | Ideal Δ | Holm | Clusters up/down | Bootstrap 95% | Other outcomes |
|---|---|---|---|---|---|
| climb | +16.7 pp | <0.001 | 6/0 | [+6.7, +48.1] pp | repairs −9.8; wall HL +10.6 s [+9.0, +12.5], Holm <0.001 |
| gate | +8.4 pp | 0.001 | 6/0 | [+1.9, +27.1] pp | repairs −3.5; wall HL −2.6 s [−3.4, −1.8], Holm <0.001 |
| scale | +2.7 pp | 0.62 | 4/2 | | found −2.0 pp, Holm 0.055 |
| xover | +1.6 pp | 1.0 | 5/1 | | |
| demote | +1.3 pp | 1.0 | 2/2 | | |
| blame | +1.1 pp | 1.0 | 4/1 | | repairs −1.9, Holm 0.001 |
| verdict | −2.0 pp | 1.0 | 1/4 | | |

Climb is the largest single addition and lifts every cluster. Its price is 10.6 s more wall time per run. Gate adds strength at a smaller margin and saves 2.6 s per run. No other feature moves the ideal rate when added alone.

### Removal (`no-<f>` against `full`)

A positive ideal Δ here means the arm does better without the feature.

| Feature | Ideal Δ | Holm | Clusters up/down | Bootstrap 95% | Wall time | Other outcomes |
|---|---|---|---|---|---|---|
| climb | −6.0 pp | <0.001 | 0/6 | [−14.6, −2.4] pp | −3.4 s, Holm <0.001 | repairs +6.1 |
| gate | +0.9 pp | 1.0 | 3/3 | | HL +14.5 s [+11.4, +17.3], Holm <0.001, all 6 clusters slower | repairs +1.7 |
| blame | +6.9 pp | 0.044 | 3/2 | [−1.7, +23.1] pp | HL +3.7 s [+2.7, +5.0], Holm <0.001, all 6 clusters slower | |
| demote | +0.4 pp | 1.0 | 3/3 | | HL −1.8 s [−2.5, −1.2], Holm <0.001, all 6 clusters faster | |
| scale | +0.7 pp | 1.0 | 2/3 | | −0.9 s, Holm 0.025 | |
| xover | +1.1 pp | 1.0 | 3/2 | | n.s. | |
| verdict | +2.0 pp | 1.0 | 3/1 | | n.s. | |

Removing climb costs 6.0 pp of ideal rate in every cluster. Removing gate leaves strength unchanged and adds 14.5 s per run. Blame reads two ways in `full`, since it saves 3.7 s per run but its removal raises the ideal rate by 6.9 pp. That strength effect clears Holm at 0.044, yet only 3 clusters rise, 2 fall, and the bootstrap interval includes 0. The sensitivity analysis below traces it to rad. Demote buys no measurable strength and costs 1.8 s per run in all 6 clusters. Scale costs 0.9 s. Xover and verdict move no outcome.

### Per-cluster ideal effects

| Contrast | fsm | liquid-mixer | lmcps | mode-arbiter | rad | valu3s |
|---|---|---|---|---|---|---|
| `only-climb` − `base` | +5 pp | +20 pp | +6 pp | +24 pp | +58 pp | +100 pp |
| `no-blame` − `full` | +5 pp | 0 pp | −1 pp | −8 pp | +37 pp | 0 pp |

Climb's effect is positive in every cluster, but rad supplies the largest share of its mean. The valu3s cell is a single subject. The `no-blame` gain comes almost entirely from rad.

## Sensitivity without rad

Rad's input/output partition is suspect, because 8 of its 10 unrealisable cores consist of a single guarantee that is unrealisable alone. Rerunning the analysis without rad leaves 93 subjects in 5 clusters.

| Contrast | With rad (110 subjects) | Without rad (93 subjects) |
|---|---|---|
| `only-climb` ideal | +16.7 pp, Holm <0.001, 6/0 | +9.2 pp, Holm <0.001, 5/0 |
| `no-climb` ideal | −6.0 pp, Holm <0.001, 0/6 | −3.9 pp, Holm 0.001, 0/5 |
| `only-gate` ideal | +8.4 pp, Holm 0.001, 6/0 | +3.4 pp, Holm 0.28, 5/0 |
| `no-gate` wall | +14.5 s | +13.3 s |
| `no-blame` ideal | +6.9 pp, Holm 0.044 | +1.5 pp, n.s. |
| `no-blame` wall | +3.7 s | +3.7 s |

Climb holds in both directions and in every remaining cluster. Gate's strength gain shrinks below significance while it still rises in all 5 clusters, and its time saving holds. Blame's strength cost disappears without rad, and its 3.7 s saving is unchanged. Every wall-time verdict survives.

## Seed size

| Edits in seed | Subjects | `base` ideal | `full` ideal | `only-climb` ideal |
|---|---|---|---|---|
| 2 | 39 | 0.29 | 0.37 | 0.47 |
| 3 | 34 | 0.22 | 0.36 | 0.41 |
| 4 | 36 | 0.18 | 0.27 | 0.29 |

Each extra edit lowers the ideal rate in all three arms. Climb's margin over `full` narrows from 10 pp at 2 edits to 2 pp at 4 edits. One headline subject has a 5-edit seed and is left out of this table.

The 10 trivial controls (2 clusters) reach an ideal rate of 0.92 under `base` and 1.00 under `full`, and no contrast is significant. They confirm the probe. Low-depth reversions make the task easy for every arm, which is why these subjects sit outside the headline set.

## Corpus findings

These findings concern the subjects and the engine, not the features.

**Unsatisfiable real subjects.** Two of the nine real small subjects are unsatisfiable, not merely unrealisable. `takeoff` has no inputs at all, so it yields no satisfiable mutant, and `lift-plus-cruise-mini` is the other.

**Suspect partition in rad.** Of rad's 10 cores, 8 are a single guarantee that is unrealisable alone. Its input/output partition is therefore suspect, which motivated the sensitivity analysis. Rad's drop-one bases have at most one guarantee and went unused, but rad slices that passed the single-requirement screen were used.

**Systems the fault model cannot break.** The lift-plus-cruise and ventilator slices never became unrealisable under the fault model. Their requirements are loosely coupled and mostly `eventually` or `next` under mode scopes, so a controller separates conflicting obligations in time. The first generation pass kept 246 of 1140 candidates. A second pass on lpc, ventilator and valu3s used 3 to 6 edits plus a `conflict-now` edit that pins the edited deadline to `immediately`, and it kept 2 of 570, both from valu3s. The corpus therefore covers 6 systems and is dominated by fsm and lmcps.

**Engine exit on an `ltl2tgba` timeout.** On `fsm-combined__add-request-extend-within__m11` at seed 0, arms `full` and `no-verdict` exited fatally when `ltl2tgba` timed out on a long nested-`X` formula, an `X`-chain of about 14 steps. The engine could treat such a candidate as unknown and continue. The bug was left unfixed mid-campaign so that every run shares one build, and the two missing cells drop out of their contrasts.

## Limits

The corpus spans 6 systems with unbalanced sizes, from 40 headline subjects in fsm to 1 in valu3s. Seeded mutants are strengthenings of realisable specifications, which is a narrower fault model than the faults in real requirements. Each subject has one ideal B, so a repair weaker than B but still useful counts as a miss. The probe estimates use the `base` arm's configuration only, and arms with other mutation settings can revert a mutant at a different rate.

## Files

The `data/` directory next to this report holds the results. Like all experiment data in this repository it is untracked; `PLAN.md` gives the commands that regenerate the corpus.

- `results-fretish-seeded.csv` and `counters-fretish-seeded.csv`, in the same format the paper's `make import` produces.
- `subjects.csv`, `probe.csv`, `bases.csv` and `mutants.csv`, which describe the corpus and the probe.
- `stats.json` for the headline analysis and `stats-no-rad.json` for the sensitivity analysis.

The paper's `scripts/import_data.py` hard-codes `CAMPAIGN = "fretish-order"` and also copies a `-large` CSV, so importing this campaign needs a small change there. The paper repository was not touched. From the worktree root, `python3 scripts/seeded/analyse.py` reproduces `stats.json`, and `python3 scripts/seeded/analyse.py --exclude-clusters rad --stats stats-no-rad.json` reproduces the sensitivity file.
