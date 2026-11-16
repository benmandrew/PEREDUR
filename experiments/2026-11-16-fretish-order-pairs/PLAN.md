# 2026-11-16-fretish-order-pairs

The paper's one-at-a-time ablation shows that the seven mechanisms' gains overlap: their addition effects sum to +20.0 points of ideal rate, against full's +9.0 over base (p = 0.027). It cannot say which pairs overlap, since every partner of a removed feature stays on in full. This campaign removes three pairs from full together. The code suggests each pair overlaps: climb and gate build their moves from the same step set, climb and demote both act on gutted repairs, and blame and scale act on the same mutation call.

## Design

For each pair (a, b), the arm no-a-b is full with both features off, on the small set's nine specifications at seeds 0-29, as in the first pass. Its configs are the first pass's apart from the pair's keys, in `configs-fretish-order-pairs`. The interaction of a and b, measured from full, is (full - no-a-b) - (full - no-a) - (full - no-b), per (spec, seed) cell, tested with a Wilcoxon signed-rank test over the cells with a reachable ideal, on the ideal rate and the found-repair rate. no-a and no-b come from the first pass. The re-run of 2026-11-11 reproduced the first pass outside the timing-bound specifications, so arms at the current commit compare with it there; the timing-bound specifications are reported apart.

Power is low: the climb's own addition-removal gap, the largest, reached only p = 0.072 at 30 seeds. A null result does not show the pair is independent.

## Hosts

av3 CPUs 8-23 under a 24 GB cap, announced to veribee-33, veribee-0b and gawain-76 before enqueuing. veribee-33 holds av3 CPUs 0-7 and the GPU until Tue 17 Nov 11:00; nothing else holds av3. The user approved the run in session on Mon 16 Nov.

## Estimate

810 runs at 16 jobs: about 3-3.5 h.

## Result

Pending.
