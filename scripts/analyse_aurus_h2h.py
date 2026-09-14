#!/usr/bin/env python3
"""Pre-registered analysis for the 2026-08-14 PEREDUR-against-AuRUS campaign.

Runs the decision rule of `experiments/2026-08-14-aurus-h2h/PLAN.md` §5 as
amended by §10.1 and §10.2:

  primary    per-family repair-quality rate, PEREDUR's implies_ideal over its
             20 seeds against AuRUS's implies_genuine over its 30 repeats,
             two-sided Wilcoxon signed-rank over families at alpha 0.05
  clustered  the same test over the 10 independent problem types of §7.10,
             each contributing the mean of its families' rates -- the result
             the conclusion rests on where the two disagree
  secondary  §6's measures, none of which can move the outcome

§10.1 is what makes the two rates comparable: AuRUS's is over the repairs that
are both realizable under `realize` and well-separated under
`check_well_separated`, because PEREDUR's output gate rejects an ill-separated
survivor unconditionally and its rate is over well-separated repairs by
construction. The unfiltered rate rides along as implies_genuine_all and is
reported beside it, never in place of it.

Written after the campaign to read the merged CSVs, so this is not a vendored
driver copy -- it ran nothing.

    python3 scripts/analyse_aurus_h2h.py [campaign-dir]
"""
import statistics
import sys
from collections import defaultdict
from pathlib import Path

from analysis_lib import (load_rows, med, num_or_nan, num_or_zero, pct,
                          signed_ranks)

DIR = Path(sys.argv[1] if len(sys.argv) > 1
           else "experiments/2026-08-14-aurus-h2h")

# PLAN.md §7.10. Six clusters and four singletons: variants within a cluster
# differ mostly in parameter count and can be expected to succeed or fail
# together, so 25 families are about 10 independent problem types.
CLUSTERS = {
    "arbiter": ["arbiter-aurus", "full-arbiter-aurus", "load-balancer-aurus",
                "prioritized-arbiter-aurus", "round-robin-arbiter-aurus",
                "simple-arbiter-aurus"],
    "lily": ["lily02", "lily11", "lily15", "lily16"],
    "humanoid": ["humanoid-458", "humanoid-503", "humanoid-531",
                 "humanoid-742"],
    "ltl2dba": ["ltl2dba-r-2", "ltl2dba-theta-2", "ltl2dba27"],
    "gyro": ["gyro-var1", "gyro-var2"],
    "rg": ["rg1", "rg2"],
    "detector-aurus": ["detector-aurus"],
    "lift": ["lift"],
    "minepump": ["minepump"],
    "pcar-v2-888": ["pcar-v2-888"],
}


def truth(s):
    return str(s).strip().lower() in ("1", "true", "yes")


# -- Wilcoxon signed-rank ------------------------------------------------------

def wilcoxon_p(diffs):
    """Two-sided exact p for the signed-rank statistic.

    Zero differences are dropped by `signed_ranks`, which is the standard
    Wilcoxon treatment and the one PLAN.md §5's power bound assumes ("the test
    reads only the families whose two rates differ").

    The null distribution is built by enumerating every sign assignment of the
    observed ranks, which is a randomisation test conditional on those ranks.
    That is the textbook exact test when the ranks are distinct, and stays
    valid when averaged ties make them fractional -- where the textbook table,
    built on distinct integer ranks, no longer applies. Rates here are k/20 and
    k/30, so tied |differences| are the common case rather than the exception.
    """
    w, ranks, _ = signed_ranks(diffs)
    n = len(ranks)
    if n == 0:
        return 1.0, 0.0, 0
    # Ranks are integers or half-integers; double them to enumerate over ints.
    doubled = [int(round(r * 2)) for r in ranks]
    dist = {0: 1}
    for r in doubled:
        nxt = defaultdict(int)
        for s, c in dist.items():
            nxt[s] += c
            nxt[s + r] += c
        dist = nxt
    total = 2 ** n
    w2 = int(round(w * 2))
    le = sum(c for s, c in dist.items() if s <= w2)
    ge = sum(c for s, c in dist.items() if s >= w2)
    p = min(1.0, 2 * min(le, ge) / total)
    return p, w, n


# PLAN §5's power bound, reported at every n rather than only where it decides
# the outcome. Below 6 separating units no two-sided p under 0.05 exists, so the
# outcome is 3 by construction rather than by evidence; above it the floor still
# says how much of the alpha a small unit count leaves reachable, and a reader
# restricting the corpus has no other way to see that. A cross-campaign read
# over the 10 families the sweep-O corpus shares with this one lands on 7
# clusters, where the floor is 0.0156 and every cluster has to point one way.
def power_floor(n, unit):
    floor = 2 / 2 ** n if n else 1.0
    # .4f reads 0.0000 past n = 14, which says nothing; a floor is only worth
    # printing where its magnitude is legible.
    shown = f"{floor:.4f}" if floor >= 1e-4 else f"{floor:.1e}"
    line = (f"  n = {n} non-tied {unit}: exact two-sided p floor "
            f"2/2^{n} = {shown}")
    if n < 6:
        return line + "; no p below 0.05 is reachable, outcome 3 by construction"
    return line + ", reachable only with every one pointing the same way"


# -- load ----------------------------------------------------------------------

peredur_rows = load_rows(DIR / "results-aurus-h2h.csv")

aurus_rows = []
for host in ("av2", "av3"):
    path = DIR / f"validation-{host}.csv"
    if not path.exists():
        sys.exit(f"missing {path} -- run aurus_validate.py first")
    for r in load_rows(path):
        r["host"] = host
        aurus_rows.append(r)

print("=" * 78)
print("INTEGRITY")
print("=" * 78)
commits = sorted({r["commit"] for r in peredur_rows})
print(f"PEREDUR rows      {len(peredur_rows)}   commits {commits}   "
      f"dirty {sorted({r['dirty'] for r in peredur_rows})}")
print(f"AuRUS repeat rows {len(aurus_rows)}")

c_fams = {r["spec"] for r in peredur_rows}
a_fams = {r["spec"] for r in aurus_rows}
both = sorted(c_fams & a_fams)
print(f"families: PEREDUR {len(c_fams)}, AuRUS {len(a_fams)}, "
      f"scored by both {len(both)}")
if c_fams - a_fams:
    print(f"  PEREDUR only: {sorted(c_fams - a_fams)}")
if a_fams - c_fams:
    print(f"  AuRUS only:   {sorted(a_fams - c_fams)}")

flat = [f for fs in CLUSTERS.values() for f in fs]
assert len(flat) == len(set(flat)), "a family is in two clusters"
if set(flat) != set(both):
    print(f"  WARNING: cluster map covers {sorted(set(flat) ^ set(both))} "
          f"differently from the scored set")

# -- rates ---------------------------------------------------------------------

c_hit = defaultdict(int)
c_n = defaultdict(int)
for r in peredur_rows:
    c_n[r["spec"]] += 1
    c_hit[r["spec"]] += truth(r["implies_ideal"])

a_hit = defaultdict(int)
a_hit_all = defaultdict(int)
a_n = defaultdict(int)
for r in aurus_rows:
    a_n[r["spec"]] += 1
    a_hit[r["spec"]] += truth(r["implies_genuine"])
    a_hit_all[r["spec"]] += truth(r["implies_genuine_all"])

print()
print("=" * 78)
print("PRIMARY -- per-family repair-quality rate (PLAN §5, §10.1)")
print("=" * 78)
print(f"{'family':28s} {'PEREDUR':>14s} {'AuRUS':>14s} {'diff':>8s}  "
      f"{'AuRUS unfilt':>13s}")

c_rate, a_rate, a_rate_all = {}, {}, {}
for f in both:
    c_rate[f] = c_hit[f] / c_n[f]
    a_rate[f] = a_hit[f] / a_n[f]
    a_rate_all[f] = a_hit_all[f] / a_n[f]
    d = c_rate[f] - a_rate[f]
    mark = "  <- PEREDUR" if d > 0 else ("  <- AuRUS" if d < 0 else "")
    print(f"{f:28s} {c_hit[f]:3d}/{c_n[f]:<3d} {c_rate[f]:6.3f} "
          f"{a_hit[f]:3d}/{a_n[f]:<3d} {a_rate[f]:6.3f} "
          f"{d:+8.3f}  {a_rate_all[f]:13.3f}{mark}")

diffs = [c_rate[f] - a_rate[f] for f in both]
wins = sum(1 for d in diffs if d > 0)
losses = sum(1 for d in diffs if d < 0)
ties = sum(1 for d in diffs if d == 0)
p, w, n = wilcoxon_p(diffs)

print()
print(f"  PEREDUR mean rate {statistics.mean(c_rate.values()):.3f}   "
      f"AuRUS mean rate {statistics.mean(a_rate.values()):.3f}   "
      f"(unfiltered {statistics.mean(a_rate_all.values()):.3f})")
print(f"  families: PEREDUR higher {wins}, AuRUS higher {losses}, tied {ties}")
print(f"  Wilcoxon signed-rank over {n} non-tied families: W+ = {w:g}, "
      f"p = {p:.4f}")

print(power_floor(n, "families"))

print()
print("=" * 78)
print("CLUSTERED -- the same test over 10 problem types (PLAN §7.10)")
print("=" * 78)
print("Where this disagrees with the per-family test, this is the one the")
print("conclusion rests on.")
print()
print(f"{'cluster':16s} {'n':>2s} {'PEREDUR':>8s} {'AuRUS':>8s} {'diff':>8s}")

cl_diffs, cl_c, cl_a = [], {}, {}
for name, fams in CLUSTERS.items():
    present = [f for f in fams if f in c_rate]
    if not present:
        continue
    cl_c[name] = statistics.mean(c_rate[f] for f in present)
    cl_a[name] = statistics.mean(a_rate[f] for f in present)
    d = cl_c[name] - cl_a[name]
    cl_diffs.append(d)
    mark = "  <- PEREDUR" if d > 0 else ("  <- AuRUS" if d < 0 else "")
    print(f"{name:16s} {len(present):2d} {cl_c[name]:8.3f} "
          f"{cl_a[name]:8.3f} {d:+8.3f}{mark}")

cp, cw, cn = wilcoxon_p(cl_diffs)
cwins = sum(1 for d in cl_diffs if d > 0)
closses = sum(1 for d in cl_diffs if d < 0)
print()
print(f"  clusters: PEREDUR higher {cwins}, AuRUS higher {closses}, "
      f"tied {len(cl_diffs) - cwins - closses}")
print(f"  Wilcoxon signed-rank over {cn} non-tied clusters: W+ = {cw:g}, "
      f"p = {cp:.4f}")
print(power_floor(cn, "clusters"))

print()
print("=" * 78)
print("OUTCOME (PLAN §5)")
print("=" * 78)


def verdict(p_val, n_val, higher, lower, unit):
    if n_val < 6:
        return (f"outcome 3 -- no separation. Only {n_val} {unit} differ, "
                f"below the 6 the design needs for any p < 0.05 to exist.")
    if p_val >= 0.05:
        return (f"outcome 3 -- no separation (p = {p_val:.4f}). The two tools "
                f"are not distinguished by this design on this corpus.")
    if higher > lower:
        return (f"outcome 1 -- PEREDUR higher (p = {p_val:.4f}), on this "
                f"corpus at this operating point, qualified by every threat "
                f"in §7.")
    return (f"outcome 2 -- AuRUS higher (p = {p_val:.4f}), reported as "
            f"measured, with no re-run and no post-hoc arm.")


print(f"  per-family: {verdict(p, n, wins, losses, 'families')}")
print(f"  clustered:  {verdict(cp, cn, cwins, closses, 'clusters')}")

# -- secondary (PLAN §6) -------------------------------------------------------

print()
print("=" * 78)
print("SECONDARY (PLAN §6) -- reported, not gating")
print("=" * 78)

c_yield = sum(truth(r["found_repair"]) for r in peredur_rows)
print(f"PEREDUR yield              {c_yield}/{len(peredur_rows)} "
      f"({pct(c_yield, len(peredur_rows))})")

c_reps = [num_or_nan(r["n_repairs"]) for r in peredur_rows if truth(r["found_repair"])]
a_claimed = [num_or_nan(r["n_claimed"]) for r in aurus_rows]
a_scored = [num_or_nan(r["n_scored"]) for r in aurus_rows]
print(f"solutions per run          PEREDUR median "
      f"{med(c_reps)} maximal repairs, "
      f"AuRUS median {med(a_claimed)} claimed "
      f"({med(a_scored)} after the §10.1 filter)")

c_to = sum(truth(r["timed_out"]) for r in peredur_rows)
print(f"PEREDUR timeout rate       {c_to}/{len(peredur_rows)} "
      f"({pct(c_to, len(peredur_rows))}) at the 7200 s cap")

tot_claimed = sum(int(num_or_zero(r["n_claimed"])) for r in aurus_rows)
tot_ok = sum(int(num_or_zero(r["n_realize_ok"])) for r in aurus_rows)
tot_dis = sum(int(num_or_zero(r["n_disagree"])) for r in aurus_rows)
tot_ill = sum(int(num_or_zero(r["n_ill_separated"])) for r in aurus_rows)
tot_und = sum(int(num_or_zero(r["n_sep_undecided"])) for r in aurus_rows)
print(f"AuRUS re-validation        {tot_ok}/{tot_claimed} claimed repairs "
      f"REALIZABLE under ltlsynt, {tot_dis} disagreements")
print(f"AuRUS ill-separated        {tot_ill}/{tot_claimed} "
      f"({pct(tot_ill, tot_claimed, 2)}), {tot_und} undecided")

# The filter's effect on the conclusion, not just on the counts: how many
# families the §10.1 amendment actually moves.
moved = [f for f in both if a_rate[f] != a_rate_all[f]]
print(f"§10.1 filter moved         {len(moved)}/{len(both)} family rates")
if moved:
    for f in moved:
        print(f"    {f:28s} {a_rate_all[f]:.3f} -> {a_rate[f]:.3f}")
    d_all = [c_rate[f] - a_rate_all[f] for f in both]
    pa, _, na = wilcoxon_p(d_all)
    print(f"  unfiltered per-family test (the §5 original): "
          f"n = {na}, p = {pa:.4f}")

print()
print("Zero-output families (assumption-free, PLAN §6):")
for f in both:
    if a_hit[f] == 0 or c_hit[f] == 0:
        print(f"    {f:28s} PEREDUR {c_hit[f]:2d}/{c_n[f]:<3d}  "
              f"AuRUS {a_hit[f]:2d}/{a_n[f]:<3d}")
