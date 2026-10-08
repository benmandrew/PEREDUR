"""Report fp_prc.py's output. Per subject, each measure is averaged over its
units; tests are Wilcoxon signed-rank over subjects (scipy, exact for small n).

usage: fp_prc_report.py <fp_prc.csv>
"""
import collections, csv, statistics as st, sys
from scipy.stats import wilcoxon

rows = list(csv.DictReader(open(sys.argv[1])))
ALL = ["du", "dm", "ud", "um", "md", "mu"]
subj = lambda r: r["subject"].split("@")[0]


def mean(rs, cols):
    v = [float(r[c]) for r in rs for c in cols if r[c] != ""]
    return st.mean(v) if v else None


by = collections.defaultdict(lambda: collections.defaultdict(list))
for r in rows:
    by[r["mode"]][subj(r)].append(r)


def test(x, y):
    ss = [s for s in x if x[s] is not None and y.get(s) is not None]
    a, b = [x[s] for s in ss], [y[s] for s in ss]
    d = [p - q for p, q in zip(a, b) if p != q]
    p = wilcoxon(d).pvalue if len(d) > 1 else 1.0
    return st.mean(a), st.mean(b), sum(v > 0 for v in d), len(d), p


for m in ("prec", "cov"):
    ctrl = {s: mean(rs, [f"{m}_{p}" for p in ALL]) for s, rs in by["null"].items()}
    print(f"\n== {m}: cross against same-arm control (symmetric, both directions)")
    for mode in ("triple", "xtriple"):
        for lab, ps in (("d<->u", ["du", "ud"]),):
            x = {s: mean(rs, [f"{m}_{p}" for p in ps]) for s, rs in by[mode].items()}
            a, b, k, n, p = test(x, ctrl)
            print(f"  {mode:7} {lab}: cross {a:.3f} vs control {b:.3f}, cross larger {k}/{n} p={p:.4f}")
print("\n== asymmetry: precision of B inside A's region")
for mode in ("triple", "xtriple"):
    for a_, b_ in (("d", "u"),):
        x = {s: mean(rs, [f"prec_{a_}{b_}"]) for s, rs in by[mode].items()}
        y = {s: mean(rs, [f"prec_{b_}{a_}"]) for s, rs in by[mode].items()}
        a, b, k, n, p = test(x, y)
        print(f"  {mode:7} {b_}-in-{a_} {a:.3f} vs {a_}-in-{b_} {b:.3f}: first larger {k}/{n} p={p:.4f}")
print("\n== same-arm control by arm")
for arm in ("directed", "uniform"):
    v = [mean([r for r in rs if r["subject"].endswith("@" + arm)], [f"prec_{p}" for p in ALL]) for rs in by["null"].values()]
    v = [x for x in v if x is not None]
    print(f"  {arm:8} {st.mean(v):.3f} over {len(v)} subjects")
print("\n== per subject: xtriple u-in-d / d-in-u | control")
ctrl = {s: mean(rs, [f"prec_{p}" for p in ALL]) for s, rs in by["null"].items()}
for s, rs in sorted(by["xtriple"].items()):
    a, b = mean(rs, ["prec_du"]), mean(rs, ["prec_ud"])
    f = lambda v: "-" if v is None else f"{v:.3f}"
    print(f"  {s:15} {f(a)} / {f(b)} | {f(ctrl.get(s))}")
