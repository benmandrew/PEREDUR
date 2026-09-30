"""Per-family overlap from the sparse archive (members.csv, relations.csv).

Each repair is placed by its relations to the other tool's set: shared when
equivalent to one of it, dominated when strictly weaker than one, unknown
when undecided only by timeouts, alone otherwise. The joint frontier is each
tool's own frontier less its dominated classes; a shared class is counted
once, from AuRUS's side. Writes overlap.csv (one row per family) to stdout."""
import collections, csv, sys
d = sys.argv[1]
INV = {"strictly stronger": "strictly weaker", "strictly weaker": "strictly stronger"}
mem = collections.defaultdict(lambda: {"P": {}, "A": {}})
for r in csv.DictReader(open(f"{d}/members.csv")):
    mem[r["family"]][r["tool"]][r["name"]] = r["own_frontier"]
rel = collections.defaultdict(lambda: {"P": collections.defaultdict(set), "A": collections.defaultdict(set)})
for r in csv.DictReader(open(f"{d}/relations.csv")):
    rel[r["family"]]["P"][r["peredur"]].add(r["relation"])
    rel[r["family"]]["A"][r["aurus"]].add(INV.get(r["relation"], r["relation"]))
def place(rs):
    if "equivalent" in rs: return "shared"
    if "strictly weaker" in rs: return "dominated"
    if "timeout" in rs: return "unknown"
    return "alone"
cols = ["family", "p_repairs", "p_frontier", "a_frontier", "joint", "p_only", "a_only", "shared",
        "p_dominated", "a_dominated", "unknown"]
w = csv.writer(sys.stdout); w.writerow(cols)
for fam in sorted(mem):
    P, A = mem[fam]["P"], mem[fam]["A"]
    pf = [n for n, f in P.items() if f == "1"]
    if not pf and any(f == "" for f in P.values()):
        w.writerow([fam, len(P)] + [""] * (len(cols) - 2)); continue
    vp = collections.Counter(place(rel[fam]["P"][n]) if A else "alone" for n in pf)
    va = collections.Counter(place(rel[fam]["A"][n]) for n in A)
    row = [fam, len(P), len(pf), len(A), vp["alone"] + va["alone"] + va["shared"], vp["alone"], va["alone"],
           va["shared"], vp["dominated"], va["dominated"], vp["unknown"] + va["unknown"]]
    w.writerow(row)
