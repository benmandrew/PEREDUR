"""Estimate a family's overlap row from sample.py's output, with 95% intervals.

Each sampled unit x carries y = [x in the class] / (1 + equivalents of x in its
pool), so N * mean(y) estimates a class count (each class of size k is hit k
times as often and weighted 1/k). The interval is the normal one with the
finite-population correction. Placement against the other tool follows
analyse.py: equivalent -> shared, strictly implied -> dominated, a timeout with
nothing decided -> unknown, else alone. A query that times out counts as no
implication, as in maximal.

usage: estimate.py <family>...   (reads ~/overlap/out/<family>/)"""
import collections, math, os, sys
H = os.path.expanduser("~/overlap/out")
def place(rs):
    if "equivalent" in rs: return "shared"
    if "dominated" in rs: return "dominated"
    if "timeout" in rs: return "unknown"
    return "alone"
def est(ys, N):
    n = len(ys); m = sum(ys) / n
    v = sum((y - m) ** 2 for y in ys) / (n - 1) if n > 1 else 0.0
    se = math.sqrt((1 - n / N) * v / n)
    return N * m, 1.96 * N * se
def rows(p):
    ls = [l.rstrip("\n").split("\t") for l in open(p)]
    return [dict(zip(ls[0], r)) for r in ls[1:]]
def fmt(v): return f"{v[0]:.0f} ± {v[1]:.0f}"
print("family\tn\tp_frontier\tp_dominated\tp_only\tunknown_p\ta_frontier\ta_dominated\ta_only\tshared\tjoint")
for fam in sys.argv[1:]:
    o = f"{H}/{fam}"
    N = len(os.listdir(os.path.expanduser(f"~/overlap/pool/{fam}/P")))
    S = rows(f"{o}/sample-P.tsv")
    # Each sampled P repair's relations to AuRUS's frontier, P's side of the pair.
    toA = collections.defaultdict(set)
    if os.path.exists(f"{o}/matrix.tsv"):
        src = (l.rstrip("\n").split("\t") for l in open(f"{o}/matrix.tsv"))
    else:
        src = (l.rstrip("\n").split("\t") for l in open(f"{o}/sample-PA.tsv"))
    Arel = collections.defaultdict(set)
    for p, a, r in src:
        p, a = os.path.basename(p), os.path.basename(a)
        toA[p].add({"strictly weaker": "dominated"}.get(r, r))
        Arel[a].add({"strictly stronger": "dominated"}.get(r, r))
    y = collections.defaultdict(list)
    for s in S:
        on = int(s["stronger"]) == 0; w = 1 / (1 + int(s["equivalent"]))
        k = place(toA[s["name"]]) if on else None
        y["front"].append(on * w)
        for c in ("dominated", "alone", "unknown"): y[c].append((k == c) * w)
    pf, pd, po, pu = (est(y[c], N) for c in ("front", "dominated", "alone", "unknown"))
    if os.path.exists(f"{o}/matrix.tsv"):
        # The matrix holds every P repair against every AuRUS class: exact.
        nA = len(Arel); va = collections.Counter(place(r) for r in Arel.values())
        af, ad, ao, sh = (nA, 0), (va["dominated"], 0), (va["alone"], 0), (va["shared"], 0)
    else:
        nA = len(os.listdir(f"{o}/front-A")); SA = rows(f"{o}/sample-A.tsv")
        ka = [place({"dominated"} if int(s["stronger"]) else {"shared"} if int(s["equivalent"])
                    else {"timeout"} if int(s["timeout"]) else set()) for s in SA]
        af = (nA, 0)
        ad, ao, sh = (est([k == c for k in ka], nA) for c in ("dominated", "alone", "shared"))
    joint = (po[0] + ao[0] + sh[0], math.sqrt(po[1] ** 2 + ao[1] ** 2 + sh[1] ** 2))
    print("\t".join([fam, str(len(S))] + [fmt(v) for v in (pf, pd, po, pu, af, ad, ao, sh, joint)]))
