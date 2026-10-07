"""Score the pooled RQ3 pass: PEREDUR (A, mrs-nsga2-apportion) against AuRUS
(B), each side's repairs pooled over its 30 runs.

usage: score_pooled.py <plan-dir> <results.csv>... > report.txt
       env: OUT (per-family CSV, default families-pooled.csv)

Classes from plan_pooled.py that compare as equivalent are merged into one
group, and a group belongs to each side that has a member in it. A side's
pooled frontier is its groups that no other group of the same side strictly
implies. sub(A by B) is the share of A's frontier strictly implied by some
group of B, and net = sub(B by A) - sub(A by B), so a positive net means
PEREDUR's repairs are the stronger, as in the paper. A pair absent from the
results reads as incomparable: the prefilter refuted it, or both classes come
from one run, whose frontier is an antichain. A family with no results at
all has not run and is skipped.

An undecided pair between the sides is read both as an implication (imp) and
as a non-implication (non), and a verdict must hold under both. Within a side,
an undecided pair reads as a non-implication, so the frontier keeps both.
The test is the exact Wilcoxon signed-rank over families of the net.
"""
import collections, csv, os, sys
import numpy as np
from scipy import stats

plan, results = sys.argv[1], sys.argv[2:]
classes = {r["cid"]: r for r in csv.DictReader(open(f"{plan}/classes.csv"))}
rel = {}
for f in results:
    for r in csv.DictReader(open(f)):
        rel[r["id"]] = r["relation"]

parent = {c: c for c in classes}


def find(c):
    while parent[c] != c:
        parent[c] = parent[parent[c]]
        c = parent[c]
    return c


for pid, r in rel.items():
    if r == "equivalent":
        a, b = pid.split("|")
        parent[find(a)] = find(b)

groups = collections.defaultdict(lambda: collections.defaultdict(set))
for c, r in classes.items():
    groups[r["spec"]][find(c)].update(r["sides"])
# above[spec][g] = groups strictly stronger than g; undec[spec][g] = groups g has an undecided pair with
above = collections.defaultdict(lambda: collections.defaultdict(set))
undec = collections.defaultdict(lambda: collections.defaultdict(set))
counts = collections.defaultdict(collections.Counter)
for pid, r in rel.items():
    a, b = pid.split("|")
    spec = classes[a]["spec"]
    counts[spec][r] += 1
    ga, gb = find(a), find(b)
    if r == "weaker":
        above[spec][ga].add(gb)
    elif r == "stronger":
        above[spec][gb].add(ga)
    elif r in ("undecided", "error"):
        undec[spec][ga].add(gb); undec[spec][gb].add(ga)

rows = []
for spec in sorted(groups):
    if not counts[spec]:
        print(f"{spec}: no results, skipped")
        continue
    G = groups[spec]
    front = {s: [g for g, sides in G.items() if s in sides and not any(s in G[h] for h in above[spec][g])]
             for s in "PA"}
    if not front["P"] or not front["A"]:
        print(f"{spec}: empty frontier (P {len(front['P'])}, A {len(front['A'])}), skipped")
        continue
    sub = {}
    for k in ("non", "imp"):
        for s, o in (("P", "A"), ("A", "P")):
            hit = [any(o in G[h] for h in above[spec][g])
                   or (k == "imp" and any(o in G[h] and s not in G[h] for h in undec[spec][g]))
                   for g in front[s]]
            sub[(s, k)] = float(np.mean(hit))
    c = counts[spec]
    rows.append([spec, len(front["P"]), len(front["A"]),
                 round(sub[("A", "non")] - sub[("P", "non")], 4), round(sub[("A", "imp")] - sub[("P", "imp")], 4),
                 round(sub[("P", "non")], 4), round(sub[("A", "non")], 4),
                 sum(c.values()), c["undecided"], c["error"]])

print(f"\n== pooled rq3: A = mrs-nsga2-apportion, B = aurus ({len(rows)} families)")
for col, k in ((3, "non"), (4, "imp")):
    net = np.array([r[col] for r in rows])
    nz = net[net != 0]
    p = stats.wilcoxon(nz, method="exact").pvalue if len(nz) > 0 else 1.0
    print(f"  [{k}] A stronger on {(net > 0).sum()}, B on {(net < 0).sum()}, tie {(net == 0).sum()};"
          f" median net {np.median(net):+.4f}, mean {net.mean():+.4f}; exact Wilcoxon p {p:.4g}")
print(f"  mean sub(A by B) {np.mean([r[5] for r in rows]):.4f}, sub(B by A) {np.mean([r[6] for r in rows]):.4f}")
n, u, e = (sum(r[i] for r in rows) for i in (7, 8, 9))
print(f"  pairs run {n}, undecided {u} ({u / max(n, 1):.4%}), errors {e}")
w = csv.writer(open(os.environ.get("OUT", "families-pooled.csv"), "w", newline=""))
w.writerow(["spec", "front_A", "front_B", "net_non", "net_imp", "subA_non", "subB_non", "pairs", "undecided", "errors"])
w.writerows(rows)
