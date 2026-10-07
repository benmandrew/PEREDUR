"""Plan the pooled RQ3 pass from members.csv and the hosts' body hashes.

usage: plan_pooled.py <outdir>

Members whose bodies match once comments and whitespace are removed form one
class, which may hold members of both sides. Every pair of classes in a
family is listed when it survives the fingerprint prefilter, except pairs
whose classes both come from one run alone: a run's frontier is an antichain.
Each class is compared through one representative file. Pairs are dealt
round-robin to av1, av2 and av3 in a seeded shuffle, so each host gets a
third of every family. Paths are ~/subsum/data/<origin>/<rel> on every host.

Writes classes.csv (class id, spec, hash, representative, sides, runs, size),
pairs-<host>.csv in run.py's format, and need.txt (the representatives'
<origin>/<rel> paths, for staging).
"""
import csv, glob, os, random, sys
from collections import defaultdict
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "../../ball-radii"))
import plan
import tlsf_prc as T

HOSTS = ("av1", "av2", "av3")


def main():
    out = sys.argv[1]
    digest = {}
    for h in ("av2", "av3"):
        for rel, md5 in csv.reader(open(f"{HERE}/hash-{h}.csv")):
            digest[(h, rel)] = md5
    prints = {}
    for d in plan.DIRS:
        for p in glob.glob(f"{d}/*.members.tsv"):
            m = T.NAME.match(os.path.basename(p))
            if m:
                prints[(m.group(1) or m.group(4), int(m.group(5)), "A" if m.group(1) else f"{m.group(2)}-{m.group(3)}")] = p
    fams = defaultdict(dict)
    for r in csv.DictReader(open(f"{HERE}/members.csv")):
        md5 = digest[(r["origin"], r["rel"])]
        c = fams[r["spec"]].setdefault(md5, {"rep": r, "sides": set(), "runs": set(), "n": 0})
        c["sides"].add(r["side"]); c["runs"].add((r["side"], int(r["seed"]))); c["n"] += 1
    cache = {}

    def fp(r):
        k = (r["spec"], int(r["seed"]), "A" if r["side"] == "A" else "mrs-nsga2-apportion")
        if k not in cache:
            p = prints[k]
            cache[k] = T.load_prints(p.replace(".members.tsv", ".fingerprints.tsv"), T.frontier(p))
        return cache[k][r["file"]]

    os.makedirs(out, exist_ok=True)
    cw = csv.writer(open(f"{out}/classes.csv", "w", newline=""))
    cw.writerow(["cid", "spec", "md5", "rep", "sides", "runs", "size"])
    pairs, need = [], set()
    for spec in sorted(fams):
        cls = sorted(fams[spec].items(), key=lambda kv: kv[0])
        ids, X = [], []
        for i, (md5, c) in enumerate(cls):
            r = c["rep"]
            cid = f"{spec}#{i}"
            ids.append(cid)
            path = f"{r['origin']}/{r['rel']}"
            c["path"] = path
            need.add(path)
            cw.writerow([cid, spec, md5, path, "".join(sorted(c["sides"])),
                         " ".join(f"{s}@{n}" for s, n in sorted(c["runs"])), c["n"]])
            X.append(fp(r))
        X = np.stack(X).view(np.uint64)
        surv = 0
        for i in range(len(cls)):
            xi, Y = X[i][None, :], X[i + 1:]
            ok = ~((xi & ~Y).any(1)) | ~((Y & ~xi).any(1))
            ri = cls[i][1]["runs"]
            for t in np.nonzero(ok)[0]:
                j = i + 1 + t
                rj = cls[j][1]["runs"]
                if len(ri) == 1 and ri == rj:
                    continue
                pairs.append((f"{ids[i]}|{ids[j]}", cls[i][1]["path"], cls[j][1]["path"]))
                surv += 1
        print(spec, "classes", len(cls), "pairs", surv, flush=True)
    random.Random(0).shuffle(pairs)
    ws = {h: csv.writer(open(f"{out}/pairs-{h}.csv", "w", newline="")) for h in HOSTS}
    for w in ws.values():
        w.writerow(["id", "a_path", "b_path"])
    for k, (pid, a, b) in enumerate(pairs):
        ws[HOSTS[k % 3]].writerow([pid, f"/home/benandrew/subsum/data/{a}", f"/home/benandrew/subsum/data/{b}"])
    open(f"{out}/need.txt", "w").write("".join(f"{p}\n" for p in sorted(need)))
    print("total pairs", len(pairs), "files", len(need))


if __name__ == "__main__":
    main()
