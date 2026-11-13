# Seeded mutant corpus: plan

Working plan, kept up to date as the work runs. Deadline: Monday 2026-11-16 11:00 GMT. Branch `feat/seeded-corpus`, worktree `~/projects/peredur/.claude/worktrees/seeded-corpus`. Signed commits, never pushed.

## Question

The ablation of `2026-11-05-fretish-order` ran on 9 small subjects from about 7 independent sources. Do its per-feature verdicts hold on a larger corpus? The verdicts were: climb helps; gate and blame help a little; demote is borderline; xover, scale and verdict show nothing.

## Design

- **Base B.** A realisable FRETish specification with a fixed input/output partition. It is taken from the `fixes/` ideals of the bundled examples, and from realisable guarantee subsets of the unrealisable examples that have no ideals. Every base must pass the single-requirement screen: each live guarantee realisable alone.
- **Mutant O.** B strengthened by a compound seed of 2 to 4 primitive edits, drawn from fault classes (deadline tightening, duration lengthening, response conjunction, trigger widening, condition-type change). O must be satisfiable, unrealisable, and proved strictly stronger than B (`compare`). B is written as O's only ideal, so `implies_ideal` counts repairs R with B ⊑ R.
- **Trivial-reversion probe.** For each O, sample random mutation chains of length 1 and 2 from O with the real `mutate_specification`, under the `base` and `full` configs. Count the chains that end realisable and imply B. A mutant whose estimated depth-2 reversion probability is above the threshold is kept out of the headline set and reported as its own stratum.
- **Clustering.** Mutants of one base, and bases of one system, are not independent. The unit of analysis is the system cluster; tests reduce to per-cluster effects or use a cluster bootstrap.
- **Arms.** Sweep O as in the order campaign: `base`, `full`, `no-<f>`, `only-<f>` under `nsga2-apportion`. Same budget: 1000 individuals, population 100, 7200 s cap.
- **Measures.** found-repair rate, ideal rate (`implies_ideal`), equivalence to B (`best_relation = equivalent`), maximal repairs per run, wall time. Strata by seed size and by reversion probability.

## Machine

20 cores, 30 GB. Experiments use cores 0-15 and at most 20 GB. Every heavy job runs as `systemd-run --user --scope -q --collect -p MemoryMax=20G -p MemorySwapMax=0 taskset -c 0-15 ...`.

## Regenerating the corpus

The corpus files are untracked, as every experiment's data is here. From the worktree root, inside `nix develop`, with the release build at the commit that records this section:

```sh
E=experiments/2026-11-13-seeded-corpus
python3 scripts/gen_configs.py --sweeps O --schemes nsga2-apportion --weakening off --weights 0.1 0.2 0.7 --termination individuals --max-individuals 1000 --generations 500 --population-size 100 --out-dir experiments/configs-fretish-seeded --pin-vintage
python3 scripts/seeded/bases.py --out $E --jobs 14 --slices 12
python3 scripts/seeded/seed.py --out $E --bases-csv $E/bases.csv --per-base 15 --jobs 14
python3 scripts/seeded/seed.py --out $E --bases-csv $E/bases.csv --per-base 15 --jobs 14 --only-clusters lpc,ventilator,valu3s --edits 3,4,5,6 --extra-edits conflict-now --tag x --seed 20261114
tail -n +2 $E/mutants-x.csv >> $E/mutants.csv
python3 scripts/seeded/probe.py --out $E --jobs 6
python3 scripts/seeded/select.py --out $E
```

## Steps

- [x] Worktree, release build, tests pass (68/68).
- [x] `scripts/seeded/bases.py`: collect bases, realise them, single-requirement screen.
- [x] `scripts/seeded/seed.py`: fault classes, filters, write subjects to `subjects/<id>/{spec.json,fixes/base.json}`.
- [x] `sample-mutants` binary and `scripts/seeded/probe.py`: reversion probability with the real mutation operator.
- [x] `fretish-seeded` profile in `scripts/run_experiments.py`, subjects discovered from `subjects/`.
- [ ] Pilot: a few subjects, `base` and `full`, 2 seeds. Check timing and that scoring works.
- [ ] Main run: subjects x 16 arms x seeds sized to fit by Sunday evening.
- [ ] Analysis (`scripts/seeded/analyse.py`, written) and `REPORT.md`; data ready for the paper's `make import`.

## Decisions

- Lineage counters (`via_inverse`) dropped: equivalence to B is computed after the run by `compare`, which needs no schema change.

- Headline threshold, fixed before any arm runs: a subject is headline when p1 and p2 are both below 0.01 (under one expected reversion among the first 100 mutations of O). Clusters capped at 40 subjects, drawn round-robin over bases; 10 trivial subjects run as a control stratum.
- Seed 0 over every selected subject doubles as the pilot; seeds 1-4 follow once its timings are known.
- Analysis unit is the subject (outcomes averaged over seeds), with Holm over the seven features and a cluster bootstrap over systems (`scripts/seeded/analyse.py`).

## Log

- 2026-11-13 20:00. Bases: 91 of 112 candidates realisable and pass the screen. 8 of rad's 10 cores are a single guarantee unrealisable alone, so rad's input/output partition is suspect; rad's drop-one bases have at most one guarantee and are unused, but rad slices that pass the screen are used.
- Two real small subjects are unsatisfiable, not merely unrealisable: `takeoff` (no inputs at all) and `lift-plus-cruise-mini`. Takeoff can give no satisfiable mutant.
- Generation (2-4 edits, 15 candidates per base): 246 of 1140 kept. The lift-plus-cruise and ventilator slices never became unrealisable: their requirements are loosely coupled and mostly `eventually`/`next` under mode scopes, so the controller separates conflicting obligations in time. A second pass on lpc, ventilator and valu3s (3-6 edits, plus `conflict-now`, which pins the edited deadline to `immediately`) kept 2 of 570, both valu3s. Clusters with subjects: fsm, lmcps, liquid-mixer, rad, mode-arbiter, valu3s.
- Probe test, 3 subjects at 20000 draws: p1 from 0 to 0.012. Cut to 5000 draws for the full pass.
- 2026-11-13 21:00. Probe done: 22 of 248 mutants trivial (p1 or p2 at least 0.01). Selected 120: 110 headline (fsm 40 of 156, lmcps 36, rad 17, liquid-mixer 11, mode-arbiter 5, valu3s 1) and 10 trivial controls.
