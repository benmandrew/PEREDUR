# 2026-11-18-fretish-full4-exchange: report

The gate's *exchange mode* (`gate_realizable = "exchange"`) changes how a realisable parent mutates. The parent takes one `steps_towards` step in a slot that differs from O, then weakens one slot still equal to O. This campaign added the mode to the paper's full (`lean-remove`: climb, gate, blame and maximality removal) as `remove-exchange`, and to `remove-no-blame` as `remove-no-blame-exchange`. Each arm's config differs from its control by that one line. The design is in [PLAN.md](PLAN.md).

## Execution

The runs took place from 06:00 to 12:00 UTC on 18 November 2026, on av2 and av3, at commit `3ba397fe` with a clean tree. In total 1245 runs finished, with no failures and no retries. The required block is complete, with 270 small-set runs (9 specifications × 30 seeds) and 550 seeded runs (110 headline subjects × seeds 0-4). The optional block is partial, at 425 of 550 `remove-no-blame-exchange` runs. av3 completed seeds 3-4, and av2 completed seed 0, 95 of 110 runs of seed 1 and none of seed 2. The 12:00 hard stop killed one running item, which is excluded.

The controls come from three earlier commits. On the small set the control is `remove-full` from `2026-11-17-fretish-order-remove-no-f` at `580a6cc`. On the seeded corpus `remove-exchange` pairs with `lean-remove` from `results-fretish-seeded-removal.csv` at `612876c` (in `~/projects/peredur-wt-demote`). `remove-no-blame-exchange` pairs with `remove-no-blame` from peredur-extension-26's profile `fretish-seeded-remove` at `739edd4`, copied read-only from `~/projects/counter/experiments` on av2 and av3 and not committed here. The configs are identical apart from `gate_realizable`, and the `towards` gate is byte-identical across these commits, so outcomes pair by (specification, seed). Wall times do not pair, because host load differed.

## Outcomes

| Contrast | Paired runs | Known-repair or recovery, arm vs control | Test |
|---|---|---|---|
| small, `remove-exchange` vs `remove-full` | 270 | 112 vs 109 (discordant +18/−15) | McNemar p = 0.73 |
| seeded, `remove-exchange` vs `lean-remove` | 550 (110 subjects) | 37.8% vs 37.1% (+0.7 pp) | McNemar p = 0.75, signed-rank p = 0.79 |
| seeded, `remove-no-blame-exchange` vs `remove-no-blame` | 425 | 40.3% vs 40.8% (−0.5 pp) | signed-rank p = 0.50 |

Neither the *known-repair rate* (`implies_ideal`) on the small set nor the *recovery* rate on the seeded corpus changed. The found-repair rate is also unchanged: 241 of 270 in both small-set arms, 93.1% in both seeded arms with blame, and 92.3% in both without. No small-set run timed out.

## Diversity

The diversity measures count distinct *signatures* over the candidates accumulated before the first repair. *Down* counts distinct sets of places that weaken O, *up* and *side* count the other directions, *all* counts distinct direction-tagged signatures, and *candidates* is the number of candidates. The small set pairs by run and the seeded corpus by per-subject means over seeds, with a Wilcoxon signed-rank test and no multiplicity correction.

| Contrast | Down | All | Up | Side | Candidates | Loose hit | Strict hit |
|---|---|---|---|---|---|---|---|
| small, `remove-exchange` vs `remove-full` | 13.3 vs 9.0 (+48%) | 79.2 vs 57.0 (+39%) | 7.5 vs 5.4 | 53.6 vs 39.3 | 111 vs 135 (−18%) | – | – |
| seeded, `remove-exchange` vs `lean-remove` | 9.8 vs 6.8 (+45%) | 34.1 vs 28.4 (+20%) | 2.9 vs 2.6 | 21.8 vs 19.6 | 51 vs 76 (−32%) | 85% vs 79% | 52% vs 52% |
| seeded, `remove-no-blame-exchange` vs `remove-no-blame` | 11.5 vs 8.7 (+31%) | 37.4 vs 43.9 (−15%) | 3.0 vs 3.6 | 23.1 vs 29.1 | 49 vs 80 (−39%) | 82% vs 79% | 46% vs 41% |

The rise in down signatures is significant in all three contrasts (p = 3e-25 small, 1e-15 seeded with blame, 7e-11 seeded without blame). With blame on, the loose *basin hit* rate rose from 79% to 85% (p = 0.002), and the strict hit rate stayed at 52% (p = 0.77). Without blame, neither moved significantly (loose p = 0.13, strict p = 0.074). Exchange without blame shifts diversity from up and side places into down places, so the total of distinct signatures falls by 15%. For scale, removing blame alone (`remove-no-blame` vs `lean-remove`, across commits) gave 8.5 down signatures against 6.7 (+27%), with recovery at 45.0% vs 42.2% (p = 0.24).

These results repeat the 17 November smoke test (`2026-11-17-fretish-gate-smoke`, against scale-off full controls). There, exchange raised distinct down signatures (small 14.1 vs 9.3, seeded 11.6 vs 7.9) and loose hits (+5.0 pp), and recovery rose by a non-significant 2.7 pp (p = 0.29).

## Wall time

The mean time per finished item was 83 s on av2 and 77 s on av3. Medians against the controls are not comparable, since host and load differed. In the smoke test, arm and control shared host and commit, and exchange ran faster than its control (for example, `lift-plus-cruise-mini` at a median of 489 s against 986 s).

## Conclusions

Exchange reproduces at full seed counts and on the removal-based full. It widens the spread of weakening (down) signatures by 31-48% while producing 18-39% fewer candidates. With blame on, it raises loose basin hits by 6 pp. It does not change the known-repair or recovery rates on either set. Whether to add exchange to the paper's full is the author's decision, since the wider weakening spread has not produced a recovery gain in two campaigns. A same-host wall-time comparison is still needed.

## Reproduce

**Fetch.** Set `F4X_WORK` to a work directory. `scripts/sync.sh` and `scripts/fetch.sh` copy the run directories and CSVs from the hosts.

**Signatures.** `scripts/f4x_sig.py` builds `data/f4x_small.csv` and `data/f4x_seeded.csv`. Run it under `systemd-run --user --scope -p MemoryMax=8G`. `sig.py`, `dirsig.py` and `allsig.py` are copies of `scripts/signatures` from commit `2b023e2d` on `campaign/fretish-gate-smoke`.

**Compare.** `scripts/cmp_small.py <remove-no-f CSV>`, `scripts/cmp_seeded.py` and `scripts/f4x_cmp_sig.py` print the comparisons, saved untracked as `analysis/output.txt`. The merged arm results are in `data/results-fretish-*.csv`. Like other campaigns' data, `data/` is untracked. It is in the `full4-exchange` worktree of `~/projects/peredur` on the analysis machine, and the per-host originals are in `~/projects/peredur-full4-exchange/experiments` on av2 and av3.

## Local diversity smoke, paper's measure

On 18 November, from 13:11 to 13:30 UTC, the paper's diversity measure (net containment at K = 3, the pooled pass of `2026-11-17-fretish-order-remove-no-f-pooled`) was run locally on four small subjects: `remove-exchange` (a) against `remove-full` (b), 30 seeds each, 32,768 words, compare at 20 s. The pool, the fingerprints and the compare ran in a scratch worktree at `3ba397fe`, and every pair was decided. Design, per-subject table and report are in `analysis/divsmoke/`, untracked. Net is b-in-a minus a-in-b, positive when exchange spreads wider.

| Subject | Nodes, exchange / full | Full in exchange (exchange halves) | Exchange in full (full halves) | Net |
|---|---|---|---|---|
| `fsm` | 314 / 402 | 0.831 (0.845) | 0.882 (0.887) | −0.051 |
| `takeoff` | 631 / 733 | 0.899 (0.918) | 0.905 (0.915) | −0.006 |
| `fsm-timing` | 834 / 936 | 0.732 (0.804) | 0.848 (0.806) | −0.116 |
| `mode-arbiter` | 238 / 207 | 0.915 (0.913) | 0.918 (0.939) | −0.003 |

Net is negative on all four subjects, so on the paper's measure exchange's repairs spread no wider than full's, and on `fsm-timing` narrower. Four subjects cannot give a significant signed-rank result (the smallest exact p is 0.125). The syntactic signature counts above point the other way: exchange varies more places, but its repairs cover less of the semantic space.
