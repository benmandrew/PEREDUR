# 2026-11-16-fretish-order-pairs

The paper's one-at-a-time ablation shows that the seven mechanisms' gains overlap: their addition effects sum to +20.0 points of ideal rate, against full's +9.0 over base (p = 0.027). It cannot say which pairs overlap, since every partner of a removed feature stays on in full. This campaign removes three pairs from full together. The code suggests each pair overlaps: climb and gate build their moves from the same step set, climb and demote both act on gutted repairs, and blame and scale act on the same mutation call.

## Design

For each pair (a, b), the arm no-a-b is full with both features off, on the small set's nine specifications at seeds 0-29, as in the first pass. Its configs are the first pass's apart from the pair's keys, in `configs-fretish-order-pairs`. The interaction of a and b, measured from full, is (full - no-a-b) - (full - no-a) - (full - no-b), per (spec, seed) cell, tested with a Wilcoxon signed-rank test over the cells with a reachable ideal, on the ideal rate and the found-repair rate. no-a and no-b come from the first pass. The re-run of 2026-11-11 reproduced the first pass outside the timing-bound specifications, so arms at the current commit compare with it there; the timing-bound specifications are reported apart.

Power is low: the climb's own addition-removal gap, the largest, reached only p = 0.072 at 30 seeds. A null result does not show the pair is independent.

## Hosts

av2 CPUs 18-31 under a 24 GB cap, announced to veribee-33, veribee-0b and gawain-76 before enqueuing. veribee-33 holds av2 CPUs 0-15 and the GPU until Tue 17 Nov 11:00, and hlwide-trap holds 16-17 for under an hour from about 12:10. av3 was the first choice, but on Mon 16 Nov 12:10 UTC it did not answer ssh (no route to host). The user approved the run in session on Mon 16 Nov.

## Estimate

810 runs at 14 jobs: about 3.5-4 h.

## Result

- 2026-11-16 15:24. Entry 075 done: 810 rows at commit `3d15f9b`, clean build, no run or compare step timed out. Collected at 15:27 with `campaign.py collect --profile fretish-order-pairs --host av2`. `interactions.py` in this directory computes the table below from the merged CSV and the first pass.

The interaction of a and b is (full − no-a-b) − (full − no-a) − (full − no-b), so a positive value means removing both loses more than the two single removals together, which is what overlapping features would show. Each value is the mean over (spec, seed) cells in percentage points, with a Wilcoxon signed-rank p over the non-zero cells. Ideal rate uses the 7 specifications with a reachable ideal (210 cells), and found-repair rate uses all 9 (270 cells). The timing-bound rows (lift-plus-cruise-mini, valu3s-uc6, liquid-mixer) compare arms across commits, since the first pass ran at `f7ffb99` and `43b2fce`, so they are reported apart.

| Pair | Ideal, all | Ideal, not timing-bound | Ideal, timing-bound | Found, all |
|---|---|---|---|---|
| climb + gate | +3.8 pp, p = 0.074 | +2.0 pp, p = 0.38 | +8.3 pp, p = 0.20 | 0.0 pp |
| climb + demote | −0.5 pp, p = 1.0 | −0.7 pp, p = 1.0 | 0.0 pp | 0.0 pp |
| blame + scale | −1.9 pp, p = 0.55 | −3.3 pp, p = 0.23 | +1.7 pp, p = 0.85 | −0.7 pp, p = 0.50 |

No interaction is significant on either rate. Climb and gate is the one positive one, in the direction of overlap, and its size (+3.8 pp) matches the climb's own addition-removal gap in the first pass (4.3 pp, p = 0.072). Most of it sits on the timing-bound specifications, where no-climb-gate reaches the ideal in 3.3% of runs against 15.0% for no-climb and 25.0% for no-gate, but those cells cross commits. Outside them it falls to +2.0 pp. Climb and demote show no interaction, so the two do not overlap on ideal rate even though both act on gutted repairs. Blame and scale lean the other way (−1.9 pp), and their found-repair rate on fsm-lmcps is 0 of 30, as for no-blame and no-scale alone. The wall times ran at 14 jobs against the first pass's load, so they are not compared. As the plan warned, power is low, and these nulls do not show the pairs are independent.
