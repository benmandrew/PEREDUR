import csv, statistics as st, collections, sys
from scipy.stats import wilcoxon
import os
S = os.environ["F4X_WORK"]
def rows(f): return list(csv.DictReader(open(f"{S}/{f}")))
def cmp(rs, x, c, metrics, unit):
    d = {(r["arm"], r["spec"], int(r["seed"])): r for r in rs}
    keys = sorted({k[1:] for k in d if k[0] == x} & {k[1:] for k in d if k[0] == c})
    print(f"\n== {x} vs {c}: {len(keys)} paired runs, {len({k[0] for k in keys})} {unit}")
    for m in metrics:
        if unit == "subjects":  # per-subject means over seeds, paired by subject
            subs = sorted({k[0] for k in keys})
            xs = [st.mean(float(d[(x, *k)][m]) for k in keys if k[0] == s) for s in subs]
            cs = [st.mean(float(d[(c, *k)][m]) for k in keys if k[0] == s) for s in subs]
        else:
            xs = [float(d[(x, *k)][m]) for k in keys]; cs = [float(d[(c, *k)][m]) for k in keys]
        try: p = wilcoxon(xs, cs).pvalue
        except ValueError: p = float("nan")
        print(f"   {m:10} {st.mean(xs):7.2f} vs {st.mean(cs):7.2f}  ({100*(st.mean(xs)/st.mean(cs)-1):+.0f}%)  signed-rank p={p:.2g}")
M = ["down", "all_dir", "up", "side", "n", "up_pre", "up_out"]
cmp(rows("f4x_small.csv"), "remove-exchange", "remove-full", M, "runs")
R = rows("f4x_seeded.csv")
cmp(R, "remove-exchange", "lean-remove", M + ["hit_loose", "hit_strict"], "subjects")
cmp(R, "remove-no-blame-exchange", "remove-no-blame", M + ["hit_loose", "hit_strict"], "subjects")
cmp(R, "remove-no-blame", "lean-remove", M + ["hit_loose", "hit_strict"], "subjects")
