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

## Steps

- [x] Worktree, release build, tests pass (68/68).
- [ ] `scripts/seeded/bases.py`: collect bases, realise them, single-requirement screen.
- [ ] `scripts/seeded/seed.py`: fault classes, filters, write subjects to `subjects/<id>/{spec.json,fixes/base.json}`.
- [ ] `probe-reversion` binary: reversion probability with the real mutation operator.
- [ ] `fretish-seeded` profile in `scripts/run_experiments.py`, subjects discovered from `subjects/`.
- [ ] Pilot: a few subjects, `base` and `full`, 2 seeds. Check timing and that scoring works.
- [ ] Main run: subjects x 16 arms x seeds sized to fit by Sunday evening.
- [ ] Analysis script and `RESULTS.md`; data ready for the paper's `make import`.

## Decisions

- Lineage counters (`via_inverse`) dropped: equivalence to B is computed after the run by `compare`, which needs no schema change.

## Log
