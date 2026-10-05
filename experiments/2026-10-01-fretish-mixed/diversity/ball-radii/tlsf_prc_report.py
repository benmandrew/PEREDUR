"""Report tlsf_prc.py's output: per comparison, the per-family asymmetry of
precision and coverage (paired exact Wilcoxon over families), and the cross
pair against each side's same-arm control.

usage: tlsf_prc_report.py <tlsf_prc.csv>
"""
import collections, csv, os, statistics as st, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stats import wil

rows = list(csv.DictReader(open(sys.argv[1])))
by = collections.defaultdict(lambda: collections.defaultdict(list))
for r in rows:
    by[(r["comparison"], r["kind"])][r["spec"]].append(r)


def fam(comp, kind, col):
    return {s: st.mean(float(r[col]) for r in rs) for s, rs in by[(comp, kind)].items()}


for comp in dict.fromkeys(r["comparison"] for r in rows):
    x, y = comp.split("|")
    cross = by[(comp, "cross")]
    print(f"\n== {x} (X) vs {y} (Y): {sum(len(v) for v in cross.values())} paired runs over {len(cross)} families")
    for a, b, lab in (("prec_y_in_x", "prec_x_in_y", "precision: Y inside X's region vs X inside Y's"),
                      ("cov_x_by_y", "cov_y_by_x", "coverage: X's balls hit by Y vs Y's hit by X")):
        fa, fb = fam(comp, "cross", a), fam(comp, "cross", b)
        ss = sorted(set(fa) & set(fb))
        p = wil([fa[s] for s in ss], [fb[s] for s in ss])
        print(f"  {lab}: {st.mean(fa[s] for s in ss):.3f} vs {st.mean(fb[s] for s in ss):.3f}, "
              f"first larger {p[1]}/{p[2]} p={p[0]:.4f}")
    # Symmetric cross precision against each control (control rows carry the
    # two directions in the same columns; average them).
    sym = lambda kind: {s: st.mean((float(r["prec_y_in_x"]) + float(r["prec_x_in_y"])) / 2 for r in rs)
                        for s, rs in by[(comp, kind)].items()}
    c, cx, cy = sym("cross"), sym("ctrl_x"), sym("ctrl_y")
    for ctrl, lab in ((cx, f"control {x}"), (cy, f"control {y}")):
        ss = sorted(set(c) & set(ctrl))
        if ss:
            p = wil([c[s] for s in ss], [ctrl[s] for s in ss])
            print(f"  symmetric precision cross {st.mean(c[s] for s in ss):.3f} vs {lab} "
                  f"{st.mean(ctrl[s] for s in ss):.3f}: cross larger {p[1]}/{p[2]} p={p[0]:.4f}")
    fa, fb = fam(comp, "cross", "prec_y_in_x"), fam(comp, "cross", "prec_x_in_y")
    print("  per family (Y in X / X in Y):", ", ".join(f"{s} {fa[s]:.2f}/{fb[s]:.2f}" for s in sorted(fa)))
