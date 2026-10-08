"""Each containment direction against the control of the side whose region it
is measured in, over families.

usage: direction_ctrl.py <perm_null out.csv | -> [<fp_prc out.csv>]

Containment of B in A is read against A's control (two seeds of A, both
directions), and A in B against B's. Per family, d is the cross direction
minus that control. Reports the mean of each, the families below the control,
and the exact Wilcoxon p (two-sided and one-sided "below"). A family enters a
direction only when it has both the cross figure and that side's control. The
two sides sample different regions when both directions fall below; when only
one does, the other side's repairs lie inside its spread.

The optional second file is the FRETISH fp_prc.py output, read on its xtriple
units: directed is A, uniform is B.
"""
import collections, csv, statistics as st, sys
from scipy.stats import wilcoxon

NAMES = {"mrs-nsga2-apportion|mrs-weighted": "Sel: Pareto (A) vs scalarised (B)",
         "mrs-nsga2-apportion|aurus-nsga2-apportion": "Grd: MRS (A) vs ladder (B)",
         "mrs-nsga2-apportion|aurus": "Aur: PEREDUR (A) vs AuRUS (B)"}


def line(label, pairs):
    cross, ctrl = [c for c, _ in pairs], [t for _, t in pairs]
    d = [c - t for c, t in pairs if c != t]
    two = wilcoxon(d, mode="exact").pvalue
    less = wilcoxon(d, mode="exact", alternative="less").pvalue
    print(f"  {label:18} n={len(pairs):2d}  cross {st.mean(cross):.3f}  control {st.mean(ctrl):.3f}  "
          f"below {sum(v < 0 for v in d)}/{len(d)}  p {two:.2g}  p_below {less:.2g}")


t = collections.defaultdict(lambda: collections.defaultdict(dict))
# 2026-10-08: "-" for the first file skips the TLSF part, for a FRETISH-only run.
for r in (csv.DictReader(open(sys.argv[1])) if sys.argv[1] != "-" else ()):
    t[r["comparison"]][r["spec"]][r["kind"]] = r
if sys.argv[1] != "-":
    print(f"== {sys.argv[1]}")
for comp, name in (NAMES.items() if sys.argv[1] != "-" else ()):
    fam = t[comp].values()
    print(name)
    line("B in A vs ctrl A", [(float(k["cross"]["prec_y_in_x"]), float(k["ctrl_x"]["sym"]))
                              for k in fam if "cross" in k and "ctrl_x" in k])
    line("A in B vs ctrl B", [(float(k["cross"]["prec_x_in_y"]), float(k["ctrl_y"]["sym"]))
                              for k in fam if "cross" in k and "ctrl_y" in k])

if len(sys.argv) > 2:
    ALL = ["du", "dm", "ud", "um", "md", "mu"]

    def mean(rs, cols):
        v = [float(r[c]) for r in rs for c in cols if r[c] != ""]
        return st.mean(v) if v else None

    x, ctrl = collections.defaultdict(list), collections.defaultdict(list)
    for r in csv.DictReader(open(sys.argv[2])):
        if r["mode"] == "xtriple":
            x[r["subject"]].append(r)
        elif r["mode"] == "null":
            ctrl[r["subject"]].append(r)

    def pairs(col, arm):
        out = []
        for s, rs in sorted(x.items()):
            c, k = mean(rs, [col]), mean(ctrl.get(f"{s}@{arm}", []), [f"prec_{p}" for p in ALL])
            if c is not None and k is not None:
                out.append((c, k))
        return out

    print(f"== {sys.argv[2]} (xtriple)\nFret: directed (A) vs uniform (B)")
    line("B in A vs ctrl A", pairs("prec_du", "directed"))
    line("A in B vs ctrl B", pairs("prec_ud", "uniform"))
