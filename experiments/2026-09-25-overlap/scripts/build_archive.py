"""Build the sparse archive from the collected per-family outputs.

members.csv   family, tool, name, own_frontier: every PEREDUR repair compared
              (tool P) and every AuRUS frontier class (tool A); own_frontier
              is 1 when the repair survives its own tool's pooled maximal
              pass, empty when that pass did not finish.
relations.csv family, peredur, aurus, relation: every pair whose relation of
              the PEREDUR repair to the AuRUS class is not "incomparable".
              A pair of members absent here is incomparable."""
import csv, os, re, sys
root, out = sys.argv[1], sys.argv[2]
SURV = re.compile(r"^class\s+\d+\s+(.+?)(?:\s+\(\+\d+ identical\))?$")
base = os.path.basename
def front(path):
    if not os.path.exists(path): return None
    s = {base(m.group(1)) for m in map(SURV.match, (l.strip() for l in open(path))) if m}
    return s or None
with open(f"{out}/members.csv", "w", newline="") as fm, open(f"{out}/relations.csv", "w", newline="") as fr:
    wm, wr = csv.writer(fm), csv.writer(fr)
    wm.writerow(["family", "tool", "name", "own_frontier"]); wr.writerow(["family", "peredur", "aurus", "relation"])
    for fam in sorted(os.listdir(root)):
        d = f"{root}/{fam}"
        names = {s: open(f"{d}/names-{s}.txt").read().split() if os.path.exists(f"{d}/names-{s}.txt") else [] for s in "PA"}
        if not names["P"]: continue
        pf = front(f"{d}/maximal-P.txt")
        for n in sorted(names["P"]): wm.writerow([fam, "P", n, "" if pf is None else int(n in pf)])
        for n in sorted(names["A"]): wm.writerow([fam, "A", n, 1])
        if os.path.exists(f"{d}/matrix.tsv"):
            for line in open(f"{d}/matrix.tsv"):
                p, a, r = line.rstrip("\n").split("\t")
                if r != "incomparable": wr.writerow([fam, base(p), base(a), r])
