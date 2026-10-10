# fretish-adaptive

This plan is pre-registered. It fixes the design, corpus, tests and decision rule before any row of the campaign exists.

## Question

Directed mutation of the ordered FRETISH fields weakens a guarantee and strengthens an assumption. That rule suits a candidate that is not yet realisable. `2026-10-06-fretish-rerun` found uniform's repairs stronger than directed's, and one explanation is that directed keeps weakening a candidate after it has become a repair. `ordered_fields = "adaptive"` (c7d7cd5) reverses the direction when the parent scored realisable. This campaign asks whether adaptive's repairs are stronger than directed's, and whether they are as strong as uniform's.

`2026-10-10-adaptive-smoke` ran first: 16 subjects, seeds 0-9, three arms, av3. On a sample of at most 10 final repairs a run, adaptive read stronger than directed on 12 subjects and weaker on 2 (sign p = 0.0129), and stronger than uniform on 9 against 3 (p = 0.146). Found was 145/160 for adaptive and directed, and 150/160 for uniform. The smoke was not pre-registered, and its strength measure is not this plan's.

## Design

The design is the rerun's with one arm. Sweep O runs at the level `adaptive` alone, under `nsga2-apportion` selection, `mrs` grading, weights 0.1/0.2/0.7, the log metric, weakening off, 1000 individuals at population 100, `parallel = 1`, and the runner's 7200 s cap. Every other key is pinned at its default by `--pin-vintage`.

The directed and uniform arms are the rerun's archived rows at `f828ff3`. The branch `campaign/adaptive-smoke` is `campaign/fretish-rerun-analysis` plus the adaptive arm, and the commits between `f828ff3` and that base change `compare` and the scripts alone. The reversal spends no draw, so the directed and uniform streams are unchanged. The smoke checked this: its 160 directed runs on av3 against the rerun's same runs on av3 agree on the final repairs in 159. The one that differs, `fsm-combined` seed 2, holds the same 110 accumulated candidates and differs in the final implication filter, which timed out on 16 queries against 1.

All 30 seeds run on av3 at 16 jobs. av1 was off limits at declaration. av2 held a running entry on its third attempt and two queued entries, and av3 held one running entry. The rerun's arms ran on three hosts, so wall time pairs with adaptive on a common host for seeds 0-9 alone.

## Corpus

The 20 subjects of `FRETISH_ABLATION_SPECS`, 30 seeds, one arm: 600 runs.

## Steps

**0. Stage.** Tag the enqueued commit `provenance/fretish-adaptive` and push the tag at once. The tick stages the branch.

**1. Search.** 600 runs. One arm of the rerun cost about 104 core-hours, 60 of them `lpc-full-core1` at the cap, and av2 ran 600 runs in 7.9 h at 16 jobs.

**2. Curve phase.** `score_curves.py --maximality` at the rerun's budgets. av3 scored 600 runs in 5.6 h. It writes `time_to_first_repair` and the `maximal.tsv` sidecars.

**3. Pooled strength.** After collect, a second declaration in the form of `2026-10-08-fretish-rerun-analysis`: pool each subject's `maximal.tsv` members over the three arms with `pool_members.py`, draw 32,768 words, plan every unrefuted pair and decide it with `compare` at 20 s a direction. The rerun's verdicts are not reused, since the word set is redrawn over three arms. `reduce.py` then writes each subject's net, `sub_a - sub_b`, for each pair of arms.

## Endpoints and tests

The strength set is the rerun analysis's 17 subjects: `valu3s-uc6` and `lpc-full-core1` are excluded as undecidable at 20 s, and `fsm-lmcps` because directed has no maximal set there. A subject where adaptive has no maximal set is dropped and named.

- **Primary.** Pooled net strength per subject, adaptive against directed and adaptive against uniform, each by a two-sided Wilcoxon signed-rank test over subjects, Holm-corrected over the two at 0.05. An undecided direction is read both ways, and a result must hold under both readings.
- **Secondary.** Found at the cap, by exact McNemar over the 600 (subject, seed) pairs against each arm.
- **Descriptive.** `implies_ideal` on the six subjects with reachable ideals, time to first repair, and paired wall time over seeds 0-9. The directed-against-uniform net over the redrawn words is reported beside the rerun analysis's figure as a check on the method.

## Decision rule

Adaptive replaces directed as the paper's directed arm, and becomes the recommended setting, if its net against directed is positive at Holm 0.05 and its found-at-cap is not lower than directed's at McNemar 0.05. If the first holds and the second fails, both arms are reported and the default stays. If the first fails, the hypothesis is recorded as unsupported and nothing moves. The adaptive-against-uniform test decides only how the paper words the comparison with uniform.

## Known limits

- The reversal reads the parent's verdict. A crossover ahead of the mutation is not scored, so some strengthened offspring are not realisable.
- `p_add_assumption` and `p_remove_guarantee` are not reversed and still weaken a realisable parent.
- `rad-core-10` is a subject directed rarely repairs (5/10 in the smoke, for adaptive too). Adaptive cannot help where no parent becomes realisable.
