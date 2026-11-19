# Diversity screen

A cheap screen that scores a new arm against a reference arm (usually `full`) without a pooled pass. The pooled pass (`pool_members.py`, `fingerprint_members.py`, `compare_pairs.py`, `pooled_reduce.py`) draws words aimed at its own pool and asks BLACK about every planned pair, about 25 CPU-hours a pass, so arms pooled separately cannot be compared. The screen fixes one word bank per subject and asks no solver.

| Script | Does |
|---|---|
| `wordbank.py` | Draws a subject's bank once: 32,768 lasso words, 75% aimed at a reference pool and 25% uniform. |
| `screen.py` | Pools an arm's and the reference's runs, fingerprints the nodes on the bank, takes each arm's frontier by rule, and reduces to strength, containment and descriptor counts. One JSON a screen. |
| `descriptors.py` | Per run, the number of distinct (slots changed, kind of change) cells among its repairs. |
| `validate.py` | Checks the screen against a pooled pass: the rule-only frontier against BLACK's, and the pass's own containment for a contrast. |
| `test_diversity.py` | Unit tests; CI runs it with the other script tests. |

It reuses `pool_members.run_members`, `fingerprint_members.materialise` and its helpers, `pooled_reduce.frontier`, `pooled_reduce.pooled_task` and `pooled_reduce.wilcoxon_p`, so its definitions are the pooled pass's. It needs `fpdraw` and `compare` from `build-release` (or `build`): `cmake --build build-release --target compare fpdraw`.

## Word bank

```sh
python3 scripts/diversity/wordbank.py SUBJECT experiments/2026-11-19-diversity-screen/wordbank \
    --aimed-from experiments/fretish-exchange-pooled/av2/fp/SUBJECT/words
```

The aimed words are the first 24,576 of a fingerprint phase's `words` file. That phase drew each word from a member chosen uniformly (black, `draw_black.py`), so a prefix is a uniform subsample. The banks are not in the repository: each user draws them locally. The 2026-11-19 study drew its banks into `experiments/2026-11-19-diversity-screen/wordbank/`, untracked, with the aimed words from `2026-11-19-fretish-exchange-pooled`, whose pool is the union of the seven arms of `2026-11-18-fretish-exchange-rerun`, fetched with `campaign.py collect --outputs fretish-exchange-pooled`. A subject that pass left out (`fsm-lmcps`) is drawn here with `--members POOL.jsonl.gz` over a pool from `pool_members.py`, with black at `$BLACK`. The other 8,192 words are uniform lassos with a stem of 0 to 8 positions and a loop of 1 to 8, the paper's bound, every signal a fair coin. `words.meta.json` records the mix, the line ranges, the seed, the source file and its SHA-256, and the bank's SHA-256. The banks are large, so they stay untracked.

Aimed words can be up to 64 positions long, as black's models are; only the uniform part keeps the paper's bound.

## Screen

```sh
R=../peredur-exchange-rerun/experiments/results-fretish-exchange-rerun
python3 scripts/diversity/screen.py experiments/2026-11-19-diversity-screen/screen-full-vs-base.json \
    --arm "full=$R/sweep_D_remove-exchange_nsga2-apportion_wkoff_log_{subject}_seed*" \
    --ref "base=$R/sweep_D_base_nsga2-apportion_wkoff_log_{subject}_seed*" \
    --subjects fsm mode-arbiter ...
```

`screen.py` reads the banks from `experiments/2026-11-19-diversity-screen/wordbank/` unless `--bank` names another directory, so draw them there with `wordbank.py` first.

Per subject, the runs' `repair_N.json` files are pooled into nodes over both arms and every seed (identical sorted JSON is one node), and every node is evaluated on the bank with `fpdraw eval`. Fingerprints are cached per node and bank under the work directory, so a second screen against the same reference evaluates only the new arm's nodes.

The frontier is decided by `compare --rule-only --pairs`, within each arm, asking only the directions no bank word refutes (a word one node accepts and the other rejects), as a fingerprint phase plans its pairs. A refuted direction cannot be proved, so the answer is the full grid's. A node is dominated when another node of the arm is proved to imply it and is not proved implied back. The rest, merged by proved equivalence, are the frontier classes. An unproved pair reads as incomparable. Strength is the median, over an arm's frontier classes, of the share of bank words a class accepts; lower is stronger.

Containment is `pooled_reduce.pooled_task` at K = 3, as `pooled_reduce.py --pooled-balls` computes it: each arm's seeds split by parity into half-pools, all four subsampled to the smallest, the arm's odd half against the reference's even half and the reverse, and each arm's own two halves as its control. It reads every node of an arm, not its frontier. `b_in_a` is the share of the reference inside the arm's region and `a_in_b` the converse, so net containment (`b_in_a - a_in_b`) is positive when the arm spreads wider. A subject whose smallest half-pool holds K or fewer nodes gets no containment row.

Over subjects, `summary` gives the Hodges-Lehmann shift with its exact 95% interval and the exact Wilcoxon signed-rank p of: net containment, `b_in_a - ctrl_a`, `a_in_b - ctrl_b`, and the reference's share minus the arm's (positive when the arm is stronger).

Each run also gets its descriptor count and its own containment of the reference: the share of the reference's nodes from seeds of the other parity inside the union of the run's balls at K. A run with K or fewer distinct repairs has none. Each subject reports the Spearman correlation of the two over its runs.

## Descriptors

A repair's cell is the set of (slot, kind) pairs it changed from the original O: `removed`, `assumption` (changed or added), `timing` (timing, scope or condition type), `propositional` (condition or response) or `timing+propositional`. JSON drops a removed requirement, so each side's list is aligned to the original's in order at least cost. Formulas compare by parse tree, so the printer's extra parentheses are not a change.

## What the rule-only frontier misses

`compare --rule-only` is sound and incomplete. On `fsm` and `mode-arbiter` every implication it proved within an arm was also proved by BLACK in the pooled pass, but it proved only 39% of BLACK's on `fsm` and 37% on `mode-arbiter`. So the rule-only frontier is 2 to 3 times larger than BLACK's: for `full`, 164 classes against 57 on `fsm`, and 104 against 42 on `mode-arbiter`. Most misses are not between two requirements in the paper's "at most three fields apart" sense: `trigger true, always r` implies `trigger c, always r'` for r ⇒ r', which the rules do not take, since a trigger's condition moves only to an equivalent one. A missing converse also turns an equivalence into a strict domination, so some nodes on BLACK's frontier are off the rule frontier. Strength from the screen is therefore a different measure from the pooled pass's, and comparable only between screens. The frontier holds weaker repairs that BLACK would dominate, so the screen's strength is a weak check, and a candidate that passes the screen still needs the pooled pass before any claim.

## What the screen's containment shows

Net containment against `base` from the screen follows the pooled pass's. Over the 18 subjects other than `fsm-lmcps`, the Spearman correlation between the two, over the per-subject values, is 0.93 for `remove-exchange` and 0.94 for `remove-no-blame-exchange`, and the signs agree on 18 and 16 of the 18 subjects. But the screen shrinks the shift: the Hodges-Lehmann shift is +0.007 [−0.070, 0.099] from the screen against +0.060 [−0.025, 0.184] from the pooled pass for `remove-exchange`, and +0.008 [−0.059, 0.130] against +0.072 [−0.028, 0.200] for `remove-no-blame-exchange`. Use the screen to read the direction and the order of arms, not the size of a shift. Neither shift is significant in either measure.

## Validation

```sh
P=../peredur-exchange-pooled/experiments/2026-11-19-fretish-exchange-pooled
python3 scripts/diversity/validate.py frontier $P/design.toml $P/members experiments/fretish-exchange-pooled --subjects fsm mode-arbiter
python3 scripts/diversity/validate.py containment $P/design.toml $P/members experiments/fretish-exchange-pooled --contrast full pooled-full.json
python3 scripts/diversity/validate.py compare screen-full-vs-base.json pooled-full.json
```
