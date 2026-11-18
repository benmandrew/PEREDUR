#!/usr/bin/env python3
"""Reduce the seeded campaign's results to per-feature effects.

Reads ``experiments/results-fretish-seeded.csv`` and the corpus's
``subjects.csv``. The unit of analysis is the subject: each arm's outcome is
averaged over a subject's seeds, and an effect is the paired difference
between two arms over subjects. Two contrasts per feature, as in the order
campaign: addition (``only-f`` less ``base``) and removal (``no-f`` less
``full``).

For each contrast and outcome:

* ``mean`` and the Hodges-Lehmann ``shift`` of the per-subject differences
  with its 95% interval, zeros dropped (as R's wilcox.test);
* ``p``, the exact two-sided Wilcoxon signed-rank p over subjects, and
  ``holm``, Holm-corrected over the seven features of that direction;
* ``cluster``: the mean difference per system cluster, how many clusters
  move each way, and a 95% cluster-bootstrap interval of the subject mean
  (clusters resampled with replacement), since mutants of one system are not
  independent.

Where sweep P ran, ``pruned`` compares its arms (``lean``, ``climb-gate``)\nwith ``full-rerun`` and ``base-rerun``, which ran under the same load, and\neach rerun with its original; uncorrected.\n\nOutcomes: ``ideal`` (implies_ideal), ``found`` (found_repair), ``equal``
(best_relation is equivalent: the run recovered the base itself),
``repairs`` (n_repairs) and ``wall`` (wall_time_s, the per-subject median).

Writes ``<out>/stats.json`` (or ``--stats``) and prints a summary table.
``--exclude-clusters rad --stats stats-no-rad.json`` is the sensitivity
run that leaves out rad, whose input/output partition is suspect.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import sys
from collections import defaultdict
from pathlib import Path
from statistics import mean, median

FEATURES = ["xover", "climb", "demote", "blame", "gate", "scale", "verdict"]
OUTCOMES = ["ideal", "found", "equal", "repairs", "wall"]
ALPHA = 0.05


def signrank_null(ranks2: list[int]) -> list[int]:
    total = sum(ranks2)
    counts = [0] * (total + 1)
    counts[0] = 1
    for r in ranks2:
        for s in range(total, r - 1, -1):
            counts[s] += counts[s - r]
    return counts


def signrank_p(diffs: list[float]) -> float:
    """Exact two-sided signed-rank p, zeros dropped, ties at average rank."""
    d = [x for x in diffs if x != 0]
    if not d:
        return 1.0
    a = [abs(x) for x in d]
    ranks2 = [sum(y < x for y in a) * 2 + sum(y == x for y in a) + 1 for x in a]
    counts = signrank_null(ranks2)
    w = sum(r for r, x in zip(ranks2, d) if x > 0)
    n_all = 2 ** len(d)
    low = sum(counts[: w + 1]) / n_all
    high = sum(counts[w:]) / n_all
    return min(1.0, 2 * min(low, high))


def hodges_lehmann(diffs: list[float]) -> dict:
    """Estimate and 95% interval, untied signed-rank quantile."""
    d = sorted(x for x in diffs if x != 0)
    n = len(d)
    if n == 0:
        return {"estimate": 0.0, "low": 0.0, "high": 0.0, "n": 0}
    walsh = sorted((d[i] + d[j]) / 2 for i in range(n) for j in range(i, n))
    counts = signrank_null([2 * (k + 1) for k in range(n)])
    cdf, acc = [], 0
    for c in counts[::2]:
        acc += c
        cdf.append(acc / 2 ** n)
    qu = max(next(w for w, v in enumerate(cdf) if v > ALPHA / 2), 1)
    ql = n * (n + 1) // 2 - qu
    return {"estimate": median(walsh), "low": walsh[qu - 1],
            "high": walsh[min(ql, len(walsh) - 1)], "n": n}


def holm(ps: dict[str, float]) -> dict[str, float]:
    out, running = {}, 0.0
    items = sorted(ps.items(), key=lambda kv: kv[1])
    for k, (key, p) in enumerate(items):
        running = max(running, min(1.0, (len(items) - k) * p))
        out[key] = running
    return out


def cluster_bootstrap(diffs: dict[str, float], cluster_of: dict[str, str],
                      reps: int = 4000, seed: int = 1) -> tuple[float, float]:
    by = defaultdict(list)
    for s, x in diffs.items():
        by[cluster_of[s]].append(x)
    clusters = sorted(by)
    rng = random.Random(seed)
    means = []
    for _ in range(reps):
        pick = [rng.choice(clusters) for _ in clusters]
        vals = [x for c in pick for x in by[c]]
        means.append(mean(vals))
    means.sort()
    return means[int(reps * ALPHA / 2)], means[int(reps * (1 - ALPHA / 2)) - 1]


def per_subject(rows: list[dict]) -> dict[tuple[str, str], dict[str, float]]:
    """(arm, subject) -> outcome means over seeds."""
    cell = defaultdict(list)
    for r in rows:
        cell[(r["level_name"], r["spec"])].append(r)
    out = {}
    for key, rs in cell.items():
        out[key] = {
            "ideal": mean(float(r["implies_ideal"] == "1" or r["implies_ideal"] == "True")
                          for r in rs),
            "found": mean(float(r["found_repair"] in ("1", "True")) for r in rs),
            "equal": mean(float(r["best_relation"] == "equivalent") for r in rs),
            "repairs": mean(float(r["n_repairs"] or 0) for r in rs),
            "wall": median(float(r["wall_time_s"] or math.nan) for r in rs),
            "n": len(rs),
        }
    return out


def contrast(cells, arm: str, ref: str, subjects: list[str],
             cluster_of: dict[str, str]) -> dict:
    res = {}
    for o in OUTCOMES:
        diffs = {s: cells[(arm, s)][o] - cells[(ref, s)][o] for s in subjects
                 if (arm, s) in cells and (ref, s) in cells}
        if not diffs:
            continue
        by = defaultdict(list)
        for s, x in diffs.items():
            by[cluster_of[s]].append(x)
        cmeans = {c: mean(v) for c, v in sorted(by.items())}
        lo, hi = cluster_bootstrap(diffs, cluster_of)
        res[o] = {"n": len(diffs), "mean": mean(diffs.values()),
                  "shift": hodges_lehmann(list(diffs.values())),
                  "p": signrank_p(list(diffs.values())),
                  "cluster": {"means": cmeans,
                              "up": sum(v > 0 for v in cmeans.values()),
                              "down": sum(v < 0 for v in cmeans.values()),
                              "boot_low": lo, "boot_high": hi}}
    return res


def main() -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    repo = Path(__file__).resolve().parents[3]
    ap.add_argument("--results", type=Path,
                    default=repo / "experiments" / "results-fretish-seeded.csv")
    ap.add_argument("--out", type=Path,
                    default=repo / "experiments" / "2026-11-13-seeded-corpus")
    ap.add_argument("--exclude-clusters", default="",
                    help="comma-separated clusters to leave out (sensitivity)")
    ap.add_argument("--stats", default="stats.json",
                    help="file name for the output, under --out")
    args = ap.parse_args()

    drop = set(filter(None, args.exclude_clusters.split(",")))
    subj = {r["subject"]: r for r in csv.DictReader((args.out / "subjects.csv").open())
            if r["cluster"] not in drop}
    rows = [r for r in csv.DictReader(args.results.open())
            if r["selection"] == "nsga2-apportion" and r["spec"] in subj]
    cells = per_subject(rows)
    cluster_of = {s: r["cluster"] for s, r in subj.items()}
    stats: dict = {"runs": len(rows), "strata": {}}
    for stratum in ("headline", "trivial", "all"):
        subjects = sorted({r["spec"] for r in rows
                           if stratum == "all" or subj[r["spec"]]["stratum"] == stratum})
        if not subjects:
            continue
        st: dict = {"subjects": len(subjects),
                    "clusters": len({cluster_of[s] for s in subjects}),
                    "levels": {}, "addition": {}, "removal": {}}
        for arm in sorted({k[0] for k in cells}):
            vals = [cells[(arm, s)] for s in subjects if (arm, s) in cells]
            st["levels"][arm] = {o: mean(v[o] for v in vals) for o in OUTCOMES} | {
                "subjects": len(vals)}
        st["full_vs_base"] = contrast(cells, "full", "base", subjects, cluster_of)
        # Pruned defaults (sweep P), where they ran.
        # The reruns share the pruned arms' machine load, so wall times compare;
        # rerun against original checks that the ideal rate reproduces.
        pairs = [(a, r) for a in ("lean", "climb-gate")
                 for r in ("full-rerun", "base-rerun")]
        pairs += [("full-rerun", "full"), ("base-rerun", "base")]
        st["pruned"] = {f"{a}-vs-{r}": contrast(cells, a, r, subjects, cluster_of)
                        for a, r in pairs
                        if any((a, s) in cells for s in subjects)}
        for direction, (fmt, ref) in {"addition": ("only-{}", "base"),
                                      "removal": ("no-{}", "full")}.items():
            for f in FEATURES:
                st[direction][f] = contrast(cells, fmt.format(f), ref, subjects,
                                            cluster_of)
            for o in OUTCOMES:
                ps = {f: st[direction][f][o]["p"] for f in FEATURES
                      if o in st[direction][f]}
                for f, h in holm(ps).items():
                    st[direction][f][o]["holm"] = h
        stats["strata"][stratum] = st
    (args.out / args.stats).write_text(json.dumps(stats, indent=1) + "\n")

    for stratum, st in stats["strata"].items():
        print(f"\n== {stratum}: {st['subjects']} subjects, {st['clusters']} clusters")
        print(f"{'':8} {'only-f vs base':>40} | {'no-f vs full':>40}")
        for f in FEATURES:
            cells_txt = []
            for direction in ("addition", "removal"):
                c = st[direction][f]
                if "ideal" not in c:
                    cells_txt.append(" " * 40)
                    continue
                i, w = c["ideal"], c["wall"]
                cells_txt.append(
                    f"ideal {100 * i['mean']:+5.1f}pp h={i['holm']:.3f} "
                    f"c{i['cluster']['up']}/{i['cluster']['down']} "
                    f"wall {w['shift']['estimate']:+6.1f}s")
            print(f"{f:8} {cells_txt[0]:>40} | {cells_txt[1]:>40}")
        for name, c in st["pruned"].items():
            i, w = c["ideal"], c["wall"]
            print(f"{name:18} ideal {100 * i['mean']:+5.1f}pp p={i['p']:.3f} "
                  f"c{i['cluster']['up']}/{i['cluster']['down']} "
                  f"boot[{100 * i['cluster']['boot_low']:+.1f},"
                  f"{100 * i['cluster']['boot_high']:+.1f}] "
                  f"wall {w['shift']['estimate']:+6.1f}s p={w['p']:.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
