#!/usr/bin/env python3
"""The pre-registered tests of 2026-09-29-fretish-ablation, from the merged
results CSV and the per-host curve directories.

usage: analyse_ablation.py <experiments-dir>

Reads <dir>/results-fretish-ablation.csv and <dir>/curves-fretish-ablation/
<host>/*.csv. Found at a budget is anytime: a run counts when its curve's
time_to_first_repair is uncensored and at most the budget, so a run killed at
the 7200 s cap still counts when its accumulator held a repair. Standard
library only; the Wilcoxon p is the normal approximation with tie and
continuity corrections, as the paper's tables.py computes it above 300 pairs.
"""
import collections
import csv
import glob
import math
import os
import re
import statistics
import sys

ARMS = ("directed", "uniform")
SEEDS = range(30)
CAP_S = 7200
SIX = ("takeoff", "fsm", "fsm-timing", "fsm-combined", "fsm-lmcps",
       "liquid-mixer")
RUN = re.compile(
    r"^sweep_O_(directed|uniform)_nsga2-apportion_wkoff_log_(.+)_seed(\d+)\.csv$")


def mcnemar_exact(b, c):
    n, k = b + c, min(b, c)
    if n == 0:
        return 1.0
    return min(1.0, 2 * sum(math.comb(n, i) for i in range(k + 1)) / 2 ** n)


def wilcoxon_normal(diffs):
    nonzero = [d for d in diffs if d != 0]
    n = len(nonzero)
    ordered = sorted(abs(d) for d in nonzero)
    ranks, ties, i = {}, 0, 0
    while i < n:
        j = i
        while j + 1 < n and ordered[j + 1] == ordered[i]:
            j += 1
        ranks[ordered[i]] = (i + j + 2) / 2
        t = j - i + 1
        ties += t ** 3 - t
        i = j + 1
    w_plus = sum(ranks[abs(d)] for d in nonzero if d > 0)
    w_minus = sum(ranks[abs(d)] for d in nonzero if d < 0)
    mean = n * (n + 1) / 4
    var = n * (n + 1) * (2 * n + 1) / 24 - ties / 48
    z = (abs(w_plus - mean) - 0.5) / math.sqrt(var)
    return w_plus, w_minus, z, min(1.0, math.erfc(z / math.sqrt(2)))


def main():
    root = sys.argv[1]
    rows = list(csv.DictReader(open(f"{root}/results-fretish-ablation.csv")))
    res = {(r["spec"], int(r["seed"]), r["level_value"]): r for r in rows}
    print(f"rows {len(rows)}; commit/dirty "
          f"{dict(collections.Counter((r['commit'], r['dirty']) for r in rows))}")

    first = {}
    for path in glob.glob(f"{root}/curves-fretish-ablation/*/*.csv"):
        m = RUN.match(os.path.basename(path))
        if not m:
            continue
        key = (m.group(2), int(m.group(3)), m.group(1))
        assert key not in first, key
        for r in csv.DictReader(open(path)):
            if r["metric"] == "time_to_first_repair":
                first[key] = float(r["value"]) if r["censored"] == "0" else None
    print(f"curves {len(first)}")

    subjects = sorted({k[0] for k in res})
    pairs = [(s, seed) for s in subjects for seed in SEEDS]
    assert all(p + (a,) in res and p + (a,) in first
               for p in pairs for a in ARMS)

    def found(key, budget):
        return first[key] is not None and first[key] <= budget

    print("\nFound, anytime, McNemar exact two-sided over "
          f"{len(pairs)} (subject, seed) pairs")
    for label, budget in (("10 s", 10), ("100 s", 100), ("cap", CAP_S)):
        held = {a: sum(found(p + (a,), budget) for p in pairs) for a in ARMS}
        where = collections.Counter()
        for p in pairs:
            d, u = (found(p + (a,), budget) for a in ARMS)
            if d != u:
                where[(p[0], "directed" if d else "uniform")] += 1
        b = sum(n for (s, a), n in where.items() if a == "directed")
        c = sum(n for (s, a), n in where.items() if a == "uniform")
        print(f"  {label:>5}: directed {held['directed']}/600, uniform "
              f"{held['uniform']}/600; directed-only {b}, uniform-only {c}, "
              f"p = {mcnemar_exact(b, c):.3g}; by subject {dict(where)}")

    finished = [p for p in pairs
                if all(res[p + (a,)]["timed_out"] == "0" for a in ARMS)]
    diffs = [float(res[p + ("directed",)]["wall_time_s"])
             - float(res[p + ("uniform",)]["wall_time_s"]) for p in finished]
    w_plus, w_minus, z, p_wall = wilcoxon_normal(diffs)
    print(f"\nWall time, pairs where both finished: {len(finished)}; directed "
          f"faster on {sum(d < 0 for d in diffs)}, uniform on "
          f"{sum(d > 0 for d in diffs)}; median difference (d - u) "
          f"{statistics.median(diffs):.2f} s; W+ {w_plus} W- {w_minus} "
          f"z {z:.3f} p = {p_wall:.3g}")
    for a in ARMS:
        walls = [float(res[p + (a,)]["wall_time_s"]) for p in pairs]
        print(f"  {a}: median {statistics.median(walls):.2f} s, mean "
              f"{statistics.mean(walls):.1f} s over 600 runs; killed "
              f"{sum(res[p + (a,)]['timed_out'] == '1' for p in pairs)}")
    print("  per subject, median paired difference (d - u) and directed-faster count:")
    fin = set(finished)
    for s in subjects:
        ds = [float(res[(s, seed, 'directed')]["wall_time_s"])
              - float(res[(s, seed, 'uniform')]["wall_time_s"])
              for seed in SEEDS if (s, seed) in fin]
        if ds:
            print(f"    {s}: n {len(ds)}, median {statistics.median(ds):.2f} s, "
                  f"directed faster {sum(d < 0 for d in ds)}")

    print("\nimplies_ideal, descriptive, the six subjects with reachable ideals")
    for s in SIX:
        held = {a: sum(res[(s, seed, a)]["implies_ideal"] == "1"
                       for seed in SEEDS) for a in ARMS}
        print(f"  {s}: directed {held['directed']}/30, uniform {held['uniform']}/30")
    total = {a: sum(res[(s, seed, a)]["implies_ideal"] == "1"
                    for s in SIX for seed in SEEDS) for a in ARMS}
    print(f"  total: directed {total['directed']}/180, uniform {total['uniform']}/180")

    print(f"\nwall-clock hours of search, sum over runs: "
          f"{sum(float(r['wall_time_s']) for r in rows) / 3600:.2f}")
    for host, lo, hi in (("av2", 0, 14), ("av3", 15, 29)):
        print(f"  {host}: {sum(float(r['wall_time_s']) for r in rows if lo <= int(r['seed']) <= hi) / 3600:.2f}")


if __name__ == "__main__":
    main()
