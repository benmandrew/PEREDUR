"""Pool perm_null.py's families into one permutation test per comparison.

usage: perm_report.py <out.csv>   (reads <out.csv> and <out.csv>.npz)

The pooled statistic is the unweighted mean over families, as in the paper's
figures. Its null takes the r-th permutation draw of every family together,
so the R pooled draws keep each family's own null. Also reports how many
families reach p < 0.05 on their own.
"""
import collections, sys
import numpy as np
import pandas as pd

path = sys.argv[1]
rows = pd.read_csv(path)
arr = np.load(path + ".npz")
groups = collections.defaultdict(list)
for k in arr.files:
    comp, kind, spec = k.rsplit("|", 2)
    groups[(comp, kind)].append(arr[k])
for (comp, kind), items in sorted(groups.items()):
    a = np.mean(items, axis=0)            # row 0 observed, rows 1.. permuted
    obs, null = a[0], a[1:]
    gap, ngap = obs[0] - obs[1], null[:, 0] - null[:, 1]
    sym, nsym = obs.mean(), null.mean(1)
    R = len(null)
    p_gap = (1 + (np.abs(ngap) >= abs(gap) - 1e-12).sum()) / (R + 1)
    p_low = (1 + (nsym <= sym + 1e-12).sum()) / (R + 1)
    fam = rows[(rows.comparison == comp) & (rows.kind == kind)]
    print(f"{comp:45s} {kind:6s} fam={len(items):2d}  "
          f"y_in_x {obs[0]:.3f} x_in_y {obs[1]:.3f} gap {gap:+.3f} "
          f"[null 95% {np.quantile(ngap, .025):+.3f}..{np.quantile(ngap, .975):+.3f}] p={p_gap:.4f} "
          f"(fam p<.05: {(fam.p_gap < .05).sum()})  |  "
          f"sym {sym:.3f} null {nsym.mean():.3f} [5% {np.quantile(nsym, .05):.3f}] p_low={p_low:.4f} "
          f"(fam p<.05: {(fam.p_low < .05).sum()})")
