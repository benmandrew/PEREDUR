"""Hodges-Lehmann shifts for the ball-radii comparisons, over families.

usage: hl_ctrl.py <perm_null out.csv> [<fp_prc out.csv>]

The effect sizes behind the paper's tab:quality diversity figures. For each
comparison: the net containment, BinA - AinB, and each direction against the
control of the side whose region it is measured in, as direction_ctrl.py
pairs them. Each is the Hodges-Lehmann shift with its exact 95% interval, the
signed-rank test inverted, from the paper's own helper: zero differences
drop, and the interval excludes zero exactly when the exact Wilcoxon p is
below 0.05 (the helper asserts it).

The optional second file is the FRETISH fp_prc.py output, read on its xtriple
units: directed is A, uniform is B.
"""
import collections, csv, importlib.util, statistics as st, sys

_spec = importlib.util.spec_from_file_location(
    "tlsf_tables", "/home/y19056ba/projects/writing/counter-paper/data/"
    "rerun-2026-09-uncensored/tables.py")
tlsf = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tlsf)

NAMES = {"mrs-nsga2-apportion|mrs-weighted": "Sel: Pareto (A) vs scalarised (B)",
         "mrs-nsga2-apportion|aurus-nsga2-apportion": "Grd: MRS (A) vs ladder (B)",
         "mrs-nsga2-apportion|aurus": "Aur: PEREDUR (A) vs AuRUS (B)"}


def line(label, d):
    est, lo, hi = tlsf.hodges_lehmann(d)
    print(f"  {label:16} n={len(d):2d}  zeros {sum(x == 0 for x in d)}  "
          f"mean {st.mean(d):+.3f}  HL {est:+.3f} [{lo:+.3f}, {hi:+.3f}]  "
          f"p {tlsf.wilcoxon_exact_p(d):.2g}")


t = collections.defaultdict(lambda: collections.defaultdict(dict))
for r in csv.DictReader(open(sys.argv[1])):
    t[r["comparison"]][r["spec"]][r["kind"]] = r
print(f"== {sys.argv[1]}")
for comp, name in NAMES.items():
    fam = t[comp].values()
    print(name)
    line("Net BinA - AinB", [float(k["cross"]["prec_y_in_x"]) - float(k["cross"]["prec_x_in_y"])
                             for k in fam if "cross" in k])
    line("BinA - ctrl A", [float(k["cross"]["prec_y_in_x"]) - float(k["ctrl_x"]["sym"])
                           for k in fam if "cross" in k and "ctrl_x" in k])
    line("AinB - ctrl B", [float(k["cross"]["prec_x_in_y"]) - float(k["ctrl_y"]["sym"])
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

    def against(col, arm):
        out = []
        for s, rs in sorted(x.items()):
            c, k = mean(rs, [col]), mean(ctrl.get(f"{s}@{arm}", []), [f"prec_{p}" for p in ALL])
            if c is not None and k is not None:
                out.append(c - k)
        return out

    print(f"== {sys.argv[2]} (xtriple)\nFret: directed (A) vs uniform (B)")
    line("Net BinA - AinB", [mean(rs, ["prec_du"]) - mean(rs, ["prec_ud"])
                             for rs in x.values()
                             if mean(rs, ["prec_du"]) is not None
                             and mean(rs, ["prec_ud"]) is not None])
    line("BinA - ctrl A", against("prec_du", "directed"))
    line("AinB - ctrl B", against("prec_ud", "uniform"))
