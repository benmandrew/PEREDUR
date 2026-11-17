# 2026-11-17-fretish-order-removal-followup

Registered 2026-11-17, before launch. Two follow-ups of `2026-11-17-fretish-order-removal`, which found that maximality removal on top of `lean` clears every gutted repair from the final front and makes the kept repairs stronger and more diverse. That campaign tested removal on `lean` only. These runs put it on the footing of the paper's evidence: the ablation's `only-<f>` against `base`, and the seeded corpus.

## Questions

1. **Ablation footing.** Does `only-remove` (base with demotion and removal on) make repairs stronger than `base`, as `only-climb` and `only-gate` do in `2026-11-05-fretish-order`?
2. **Seeded corpus.** Does `lean-remove` recover an implication of the ideal more often than `lean` on the corpus of `2026-11-13-seeded-corpus`?

## Reuse

Only the new arms run. Each reference arm is taken from an earlier campaign whose config is byte-identical to the one these profiles generate.

- **`base`** is `2026-11-17-fretish-order-xover-rate`'s `cr10-base`. Its config, `sweep_X_cr10-base.toml`, is byte-identical to sweep O's `base` here. The removal campaign checked that its binary reproduces the xover-rate campaign's: `lean` against `cr10-lean` agreed on repair count, found repair, ideal and best relation on 563 of 570 (subject, seed) cells. The 7 that differ are all on `lift-plus-cruise-mini` (5) and `valu3s-uc6` (2), the two sampled subjects. The xover-rate runs shared CPUs with unpinned fuzzers, and a slower CPU changes what a time-sensitive run on those subjects reaches. So `base` reruns on those two subjects, and the other 17 subjects reuse `cr10-base`.
- **Seeded `lean`** is `fretish-seeded`'s sweep P `lean`, 120 subjects × 5 seeds, run at `1077b43b` and `de2ebcda`. Its config is byte-identical to the one generated here. The engine has changed since: `a365adf1` and `b0a1cfe0` score an `ltl2tgba` timeout or signal kill as unknown, and `5e94f6b5` adds removal, which is off in `lean`. Seed 0 of `lean` reruns, 120 runs, to check that the rest can be reused. If a cell differs, the analysis uses only the rerun seed for that contrast and reports the difference.

## Arms and runs

| Phase | Profile | Arm | Subjects | Seeds | Runs |
|---|---|---|---|---|---|
| `only-remove` | `fretish-order-only-remove` | sweep D `only-remove` | 19 | 0–29 | 570 |
| `base-rerun` | `fretish-order-only-remove` | sweep O `base` | `lift-plus-cruise-mini`, `valu3s-uc6` | 0–29 | 60 |
| `seeded-lean-remove` | `fretish-seeded-removal` | sweep D `lean-remove` | 120 seeded | 0–4 | 600 |
| `seeded-lean-check` | `fretish-seeded-removal` | sweep P `lean` | 120 seeded | 0 | 120 |

`only-remove` is sweep O's `base` with `maximality_demotion = true` and `maximality_removal = true`, and `lean-remove` is `lean` with the same two keys. Those two lines are the only difference from each reference config. Sweep D's existing three arms regenerate byte-identically to the configs that ran on av3. The ablation subjects run at `--parallel 2`, as the xover-rate and removal campaigns did. The seeded subjects run with no `--parallel`, as `fretish-seeded` did. Each run count was confirmed with `run_experiments.py --dry-run` and the phase's sweeps, specs and seeds.

## Inputs

The seeded corpus is untracked. `experiments/2026-11-13-seeded-corpus/selected.txt` and `subjects/` are copied from the main checkout to av3 by `rsync -a` before enqueue.

```
a13727071872e3cb80b52f457376cbd7b204fe7419ba86c7d6ae5703f0c8e7d9  selected.txt
f01bbbc7b05d42fdb5a9c5f5f3fa698ca6b80c81c3f807907034a45612635cf3  sha256 of the sorted sha256sum of every file under subjects/
```

## Measures and analysis

**Question 1.** A pooled pass, as in `2026-11-17-fretish-order-removal-pooled`, over `base` and `only-remove`. It measures strength by accepted-trace share and diversity by net containment at K = 3. The pool takes `base` from `results-fretish-order-xover-rate` for 17 subjects and from this campaign for the two reruns. Run measures follow the removal campaign's PLAN.md: found-repair rate (exact McNemar), repairs per run and gutted final-front members (Hodges–Lehmann shift over the 18 subjects, exact Wilcoxon). The contrast is one test per measure, with no Holm family. It is set beside the paper's `only-<f>` rows and reported as a follow-up. Wall time is not compared, because the reused `base` ran under CPU contention. Removal's wall cost was measured within the removal campaign, where every arm shared one load.

**Question 2.** `scripts/seeded/analyse.py`'s measures for `lean-remove` against `lean`: ideal rate, found rate and repairs per run. The unit is the subject, averaged over seeds, with the cluster bootstrap over systems.

## Hosts

Every phase runs on av3 CPUs 16–31 at 8 jobs, 32 GB. veribee-33 holds av3 CPUs 0–15 and av2 CPUs 16–31 until about 06:00 on 18 Nov, and peredur-extension-7e has av2 CPUs 0–15 for a smoke test.

## Estimate

From the removal campaign's wall times at 8 jobs: about 60 min for `only-remove`, 20 min for `base-rerun`, and 30–70 min for the seeded phases, so about 2–2.5 h in all. The estimate carries at least ±50%.
