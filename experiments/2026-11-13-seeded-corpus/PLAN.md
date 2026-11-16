# Seeded mutant corpus: plan

Working plan, kept up to date as the work runs. Deadline: Monday 2026-11-16 11:00 GMT. Branch `feat/seeded-corpus`, worktree `~/projects/peredur/.claude/worktrees/seeded-corpus`. Signed commits, never pushed.

## Question

The ablation of `2026-11-05-fretish-order` ran on 9 small subjects from about 7 independent sources. Do its per-feature verdicts hold on a larger corpus? The verdicts were: climb helps; gate and blame help a little; demote is borderline; xover, scale and verdict show nothing.

## Design

- **Base B.** A realisable FRETish specification with a fixed input/output partition. It is taken from the `fixes/` ideals of the bundled examples, and from realisable guarantee subsets of the unrealisable examples that have no ideals. Every base must pass the single-requirement screen: each live guarantee realisable alone.
- **Mutant O.** B strengthened by a compound seed of 2 to 4 primitive edits, drawn from fault classes (deadline tightening, duration lengthening, response conjunction, trigger widening, condition-type change). O must be satisfiable, unrealisable, and proved strictly stronger than B (`compare`). B is written as O's only ideal, so `implies_ideal` counts repairs R with B ⊑ R.
- **Trivial-reversion probe.** For each O, sample random mutation chains of length 1 and 2 from O with the real `mutate_specification`, under the `base` arm's config. Count the chains that end realisable and imply B. A mutant whose estimated depth-2 reversion probability is above the threshold is kept out of the headline set and reported as its own stratum.
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
python3 scripts/seeded/choose.py --out $E
```

## Steps

- [x] Worktree, release build, tests pass (68/68).
- [x] `scripts/seeded/bases.py`: collect bases, realise them, single-requirement screen.
- [x] `scripts/seeded/seed.py`: fault classes, filters, write subjects to `subjects/<id>/{spec.json,fixes/base.json}`.
- [x] `sample-mutants` binary and `scripts/seeded/probe.py`: reversion probability with the real mutation operator.
- [x] `fretish-seeded` profile in `scripts/run_experiments.py`, subjects discovered from `subjects/`.
- [x] Pilot: seed 0 over all 120 subjects and 16 arms.
- [x] Main run: 120 subjects x 16 arms x 5 seeds.
- [x] Analysis (`scripts/seeded/analyse.py`), `REPORT.md`, and `data/` in the paper's import format.

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
- 2026-11-14 00:20. Seed 0 done: 1920 runs in 200 min at 8 jobs, median wall 42 s, no run or compare timeouts, all at `b2d3b91c`, clean. Two runs (`full`, `no-verdict` on `fsm-combined__add-request-extend-within__m11`) failed: the engine exits fatally when `ltl2tgba` times out on a long nested-`X` formula, where it could treat the candidate as unknown. Engine left unchanged mid-campaign; the missing cells drop out of their contrasts.
- Seed-0 snapshot, headline (110 subjects): ideal rate `base` 0.21, `full` 0.37; found rate about 0.93 in both. No headline run recovered B exactly (`equal` 0): every repair that implies B is strictly stronger. `only-climb` +14.5 pp (Holm 0.000, up in 5 of 6 clusters); `only-gate` +9.1 pp and `only-scale` +6.4 pp, not significant on one seed. Seeds 1-4 launched 00:25.
- 2026-11-14 13:40. Seeds 1-4 done: 7680 runs in 801 min, no errors or timeouts. 9598 rows in all.
- Analysis: climb holds in both directions and all 6 clusters. Gate's time saving holds; its ideal gain in addition halves and loses significance without rad. Blame's removal raises the ideal rate by 6.9 pp (Holm 0.044), but that comes from rad and falls to 1.5 pp without it. Demote costs 1.8 s per run in all 6 clusters with no strength effect. Details in REPORT.md.
- 2026-11-15 01:00. Extension, under a relayed instruction from the user to use the time before the deadline. Local runs are capped at 2 parallel jobs and nice -j4 builds (the workstation's standing limit, relayed by veribee-ee), so the extension runs at `--jobs 2`. Sweep P adds two pruned defaults: `lean` (full without demote, scale, xover and verdict) and `climb-gate` (base plus climb and gate). At 2 jobs a run takes about 13 s, against about 45 s at 8 jobs: the main campaign's wall times include contention, which affects every arm alike, so its wall contrasts stand but its absolute times do not transfer. Sweep P therefore also reruns `full` and `base` (`full-rerun`, `base-rerun`) so the pruned arms' wall times compare under one load. 2400 runs at `1077b43b`, ETA 05:40. Next: the reversion probe under the `full` config, at 2 jobs.
- 2026-11-15 01:47. Claude Code stopped the sweep-P run at 312 of 2400 because the machine was low on memory (machine-wide, not this run). One failure: `full-rerun` on `fsm-combined__add-request-extend-within__m11` seed 0, the same `ltl2tgba` fatal as in sweep O.
- 2026-11-15 20:57. Restarted on the user's relayed instruction, resuming at run 312 with the runner's default resume, at 2 jobs in a scope capped at 8 GB with no swap. The rows already written are at `1077b43b`; the remaining rows are at `de2ebcda`, which changes only scripts, so the engine is the same. The `full`-config reversion probe (`probe.py --config .../sweep_O_full.toml --tag -full --only-selected --jobs 2`) runs after it in the same scope.
- 2026-11-16 01:38. Sweep P done: 2088 rows, one failure, `full-rerun` on the m11 subject at seed 0, which failed before the stop and again on resume, as `full` did in sweep O: the crash is deterministic too. The reruns reproduce `full` and `base` exactly on every outcome but wall time, which falls by about 30 s per run at 2 jobs. `lean` matches `full` on ideal rate (−2.2 pp, p = 0.17) and is 0.6 s faster; `climb-gate` +3.3 pp (p = 0.23), which depends on rad. The full-config probe first died on start because `scripts/seeded/select.py` shadowed the standard `select` module (renamed to `choose.py`), then on `--tag -full` parsed as an option; rerun with `--tag=-full` at 01:41.
