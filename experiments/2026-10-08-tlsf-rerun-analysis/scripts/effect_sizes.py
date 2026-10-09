"""Effect sizes for the ball-radii comparisons, over families.

usage: effect_sizes.py <perm_null out.csv>...

Per family, d is either the asymmetry gap (prec_y_in_x - prec_x_in_y of the
cross pairs) or the symmetric cross figure minus one same-side control. For
each, reports the mean with a 95% percentile bootstrap interval over families
(10,000 resamples), the Hodges-Lehmann estimate (median of Walsh averages)
with its exact 95% interval from the signed-rank distribution, the
matched-pairs rank-biserial correlation, and the exact Wilcoxon p.
"""
import sys
import numpy as np
import pandas as pd
# Vendored: the archived path, data/rematch-2026-09, no longer exists in the
# paper repo; its successor holds the same helper.
sys.path.insert(0, "/home/y19056ba/projects/writing/counter-paper/data/rerun-2026-09-uncensored")
from tables import wilcoxon_exact_p


def hl(d):
    n = len(d)
    walsh = np.sort([(d[i] + d[j]) / 2 for i in range(n) for j in range(i, n)])
    counts = np.zeros(n * (n + 1) // 2 + 1)
    counts[0] = 1
    for r in range(1, n + 1):
        counts[r:] = counts[r:] + counts[:-r].copy() if r else counts
    cdf = np.cumsum(counts) / 2 ** n
    c = int(np.searchsorted(cdf, 0.025, side="right"))   # P(W <= c-1) <= .025
    return np.median(walsh), walsh[c], walsh[len(walsh) - 1 - c]


def rank_biserial(d):
    d = d[d != 0]
    r = pd.Series(np.abs(d)).rank().to_numpy()
    return (r[d > 0].sum() - r[d < 0].sum()) / r.sum()


def report(label, d):
    d = np.asarray(d)
    rng = np.random.default_rng(0)
    boot = d[rng.integers(0, len(d), (10000, len(d)))].mean(1)
    h, lo, hi = hl(d)
    print(f"  {label:28s} n={len(d):2d}  mean {d.mean():+.3f} [{np.quantile(boot, .025):+.3f}, "
          f"{np.quantile(boot, .975):+.3f}]  HL {h:+.3f} [{lo:+.3f}, {hi:+.3f}]  "
          f"r_rb {rank_biserial(d):+.2f}  p {wilcoxon_exact_p(list(d)):.2g}")


for path in sys.argv[1:]:
    t = pd.read_csv(path)
    print(f"== {path}")
    for comp, g in t.groupby("comparison"):
        x, y = comp.split("|")
        w = g.pivot(index="spec", columns="kind", values=["prec_y_in_x", "prec_x_in_y", "sym"])
        print(f"{x} (X) vs {y} (Y)")
        c = w.dropna(subset=[("prec_y_in_x", "cross")])
        report("gap: Y in X - X in Y", c["prec_y_in_x"]["cross"] - c["prec_x_in_y"]["cross"])
        for k, name in (("ctrl_x", x), ("ctrl_y", y)):
            s = w["sym"][["cross", k]].dropna()
            report(f"cross - control {name[:12]}", s["cross"] - s[k])
