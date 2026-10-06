# Keyword similarity weight

A reviewer asked that FRETISH syntactic similarity rely on syntax alone. The order-based keyword measures move into semantic similarity, which becomes a weighted mean per requirement pair: `w * trace + (1 - w) * keyword`. Here `trace` is the existing trace-count score of the lowered pair. `keyword` is the mean of the timing downset Jaccard, the scope region score and the condition-type order score. Under this mode the syntactic objective scores the keyword fields by token equality. Both are selected by `fitness.keyword_similarity = "semantic"`, and `fitness.semantic_trace_weight` sets `w`. The default stays `"syntactic"`, which is the behaviour every archived run used.

This plan fixes `w`. Stage 1 re-scores archived candidates offline and decides whether `w` changes anything selection sees. Stage 2 runs a short search sweep, and only if stage 1 says `w` matters. Both decision rules are fixed here, before any number is computed.

## Stage 1: offline re-scoring

### Data

The candidates come from `experiments/results-gradsel-fret-m/`, arm `sweep_K_mrs_nsga2-apportion_wkoff_log_<subject>`. That arm uses MRS grading and NSGA-II selection, the shipping configuration. It covers five subjects: `fsm`, `fsm-combined`, `fsm-timing`, `mode-arbiter` and `takeoff`. Each run's `accumulated/` directory holds every candidate that passed the output gate, as FRETISH spec JSON with no scores. Seeds 0–9 of each subject are used, giving 50 runs and 4,694 candidates: 1,035 on `fsm`, 347 on `fsm-combined`, 1,191 on `fsm-timing`, 1,159 on `mode-arbiter` and 962 on `takeoff`. The originals are `examples/<subject>/spec.json`.

Every accumulated candidate passed the gate, so all of them are realisable and score 1 on status. Stage 1 therefore sees selection only between the two similarity objectives. It cannot see how `w` ranks the unrealisable candidates of early generations. No flag dumps whole scored populations today, so that gap is accepted and named in the result.

The archive writes a candidate without its tombstones. A candidate that lost a requirement therefore reads one slot short, and pairing by index would compare every later slot with the slot before its own. This affected 18 of 106 candidates in the one run checked. Removal never reorders a side, so the survivors are a subsequence of the original's slots. The driver places them by the subsequence with the highest total syntactic similarity, using the order-based requirement score, and ties go to the later slot. Each skipped slot becomes a tombstone and scores 0, as it did in the run. The share of candidates realigned this way is reported with the result.

### Scorer

A small driver reads an original and a list of candidates. For every slot pair that differs, it prints the slot, `trace`, the three keyword terms and the two syntactic scores (order-based and token-based). The driver computes `trace` once per pair, and the script forms every `w` from the printed terms. No candidate is scored twice. The driver reuses the fitness library, so its numbers are the ones a run would compute. It joins `BINARIES` in `scripts/coverage_badge.py` and gets an end-to-end test, as every driver does.

### Measures

The weights are `w ∈ {0, 0.25, 0.5, 0.75, 1}`. For each run and each `w`, every candidate gets a (syntactic, semantic) vector under the semantic keyword mode, and the first non-dominated front is taken. The legacy mode's front is computed as well, for reference.

1. **Front agreement.** For each pair of weights, the Jaccard index of their two fronts, per run. The median and the minimum over the 50 runs are reported.
2. **Term agreement.** Over all differing slot pairs, the Spearman correlation between `trace` and `keyword`. This explains measure 1: two terms that rank pairs alike make `w` irrelevant.
3. **Spread.** The share of candidates whose semantic score moves by more than 0.05 between `w = 0` and `w = 1`.

### Decision rule

If the median front Jaccard is at least 0.9 for every pair of weights in `{0.25, 0.5, 0.75}`, `w` does not matter in practice. Then `w = 0.5` is fixed, the paper gives these medians in one sentence, and stage 2 is skipped. Otherwise stage 2 runs on the weights whose fronts differ, plus `w = 1` as the baseline.

### Result

Stage 1 ran on 2026-10-06 over all 50 runs and 4,694 candidates, in 21 s. The driver realigned 617 candidates (13.1%) that had lost a requirement.

The rule is met. Between every pair of the weights 0.25, 0.5 and 0.75, the median front Jaccard is 1.000, and the minimum over runs is 0.29 to 0.43. The endpoints differ from them. Against the other weights, `w = 0` has medians of 0.20 to 0.33, and `w = 1` has medians of about 0.45 to 0.50 against the interior weights. The legacy front and the `w = 1` front have a median Jaccard of 0.75.

The two terms rank pairs almost independently: Spearman's correlation between `trace` and `keyword` is −0.136 over 7,864 differing slot pairs. Between `w = 0` and `w = 1`, 50.3% of candidates move by more than 0.05 on semantic similarity. So the keyword term changes which candidates survive, but its exact weight inside the interior does not.

`w = 0.5` is fixed, which is already the default, and stage 2 is skipped. The gap named above still holds: the result covers selection among realisable candidates only.

## Stage 2: search sweep (conditional)

- **Arms:** the weights stage 1 selects, plus `w = 1`. All arms run under `keyword_similarity = "semantic"`.
- **Subjects:** the FRETISH subjects whose repairs change timing or scope. The list is fixed from stage 1's slot data before launch.
- **Runs:** 10 seeds per arm and subject, on the paper's budget, with the same seeds in every arm. The sweep goes through `scripts/campaign.py` with its own `campaign.toml`.
- **Measures:** found-repair rate, wall time, strength (net subsumption) and diversity (ball-radii), each against `w = 1`.
- **Decision rule:** the highest found-repair rate wins. Ties go to strength, then to wall time. Ten seeds give a Wilcoxon test little power, so the sweep picks a weight and supports no significance claim.

Tuning `w` on the evaluation subjects is a threat to validity. Either some FRETISH subjects are held out of stage 2, or the paper names the threat.
