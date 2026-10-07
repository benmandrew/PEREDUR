"""Plan the net-subsumption pass: list, per host, the frontier pairs that
survive the fingerprint prefilter.

usage: plan.py <outdir>     env: EXCLUDE (comma-separated families)

Frontiers are each run's last-cut members: rematch from the
separation-recount-rematch-s0 sidecars, AuRUS from the well-separation
screened sidecars in ../ball-radii/out/ws/aurus/. The design follows PLAN.md:
for each seed block (2j, 2j+1), the cross pairs A@2j-B@2j+1 and A@2j+1-B@2j
and the controls A@2j-A@2j+1 and B@2j-B@2j+1, for the RQ3, grading and
selection comparisons. A pair of runs shared by two comparisons (a control)
is listed once.

A pair (a, b) of repairs survives when one fingerprint is a subset of the
other, since a implies b only if b accepts every sampled word a accepts. A
refuted pair is incomparable and is not listed: the scorer reads its absence
as incomparable.

Blocks with both seeds on av2 (0-14) run there, both on av3 (15-29) there;
the straddling block (14, 15) runs on av2, and xfer-av2.txt lists the av3
files it needs copied to ~/subsum/xfer/ on av2.
"""
import csv, glob, os, sys
import numpy as np
BR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../ball-radii")
sys.path.insert(0, BR)
import tlsf_prc as T

EXP = os.path.expanduser("~/projects/counter-wt-fingerprint/experiments/separation-recount-rematch-s0")
DIRS = [f"{EXP}/av2", f"{EXP}/av3", f"{BR}/out/ws/aurus/av2", f"{BR}/out/ws/aurus/av3"]
COMPS = {"rq3": ("mrs-nsga2-apportion", "aurus"),
         "grading": ("mrs-nsga2-apportion", "aurus-nsga2-apportion"),
         "selection": ("mrs-nsga2-apportion", "mrs-weighted")}
EXCLUDE = set(filter(None, os.environ.get("EXCLUDE", "").split(",")))
HOME = "/home/benandrew"


def host_path(label, spec, seed, f, xfer=False):
    if label == "aurus":
        rel = f"aurus-h2h-out/{spec}/repeat-{seed:02d}/{f}"
    else:
        g, s = label.split("-", 1)
        rel = f"projects/counter/experiments/results-rematch/sweep_G_{g}_{s}_wkoff_log_{spec}_seed{seed:02d}/accumulated/{f}"
    return (f"{HOME}/subsum/xfer/{rel}" if xfer else f"{HOME}/{rel}"), rel


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    runs = {}
    for d in DIRS:
        for p in glob.glob(f"{d}/*.members.tsv"):
            m = T.NAME.match(os.path.basename(p))
            if m:
                runs[("aurus" if m.group(1) else f"{m.group(2)}-{m.group(3)}", m.group(1) or m.group(4), int(m.group(5)))] = p
    cache = {}

    def front(k):
        if k not in cache:
            p = runs[k]
            names = T.frontier(p)
            fp = T.load_prints(p.replace(".members.tsv", ".fingerprints.tsv"), names)
            names = [n for n in names if n in fp]
            cache[k] = (names, np.stack([fp[n] for n in names]).view(np.uint64) if names else np.zeros((0, 1024), np.uint64))
        return cache[k]

    pairs = {h: csv.writer(open(f"{out}/pairs-{h}.csv", "w", newline="")) for h in ("av2", "av3")}
    for w in pairs.values():
        w.writerow(["id", "a_path", "b_path"])
    runsets = csv.writer(open(f"{out}/runsets.csv", "w", newline=""))
    runsets.writerow(["comp", "kind", "spec", "block", "a_label", "a_seed", "b_label", "b_seed", "n_a", "n_b", "pairs", "survivors", "host"])
    members = csv.writer(open(f"{out}/members.csv", "w", newline=""))
    members.writerow(["label", "spec", "seed", "file"])
    xfer, seen, n = set(), set(), {"av2": 0, "av3": 0}
    for spec in sorted({k[1] for k in runs} - EXCLUDE):
        for comp, (x, y) in COMPS.items():
            for j in range(0, 30, 2):
                keys = [(x, spec, j), (x, spec, j + 1), (y, spec, j), (y, spec, j + 1)]
                if not all(k in runs for k in keys):
                    continue
                host = "av3" if j >= 16 else "av2"
                for kind, (u, v) in (("cross", (0, 3)), ("cross", (1, 2)), ("ctrl_x", (0, 1)), ("ctrl_y", (2, 3))):
                    ka, kb = keys[u], keys[v]
                    (na, fa), (nb, fb) = front(ka), front(kb)
                    pk = (ka, kb)
                    if pk in seen:
                        runsets.writerow([comp, kind, spec, j, ka[0], ka[2], kb[0], kb[2], len(na), len(nb), len(na) * len(nb), "dup", host])
                        continue
                    seen.add(pk)
                    for k, names in ((ka, na), (kb, nb)):
                        if (k, "m") not in seen:
                            seen.add((k, "m"))
                            for f in names:
                                members.writerow([k[0], k[1], k[2], f])
                    surv = 0
                    for i in range(len(na)):
                        xa = fa[i][None, :]
                        ok = ~((xa & ~fb).any(1)) | ~((fb & ~xa).any(1))
                        for t in np.nonzero(ok)[0]:
                            paths = []
                            for k, f in ((ka, na[i]), (kb, nb[t])):
                                remote = host == "av2" and k[2] >= 15
                                hp, rel = host_path(k[0], k[1], k[2], f, xfer=remote)
                                if remote:
                                    xfer.add(rel)
                                paths.append(hp)
                            n[host] += 1
                            pairs[host].writerow([f"{spec}|{ka[0]}@{ka[2]}|{na[i]}|{kb[0]}@{kb[2]}|{nb[t]}", *paths])
                            surv += 1
                    runsets.writerow([comp, kind, spec, j, ka[0], ka[2], kb[0], kb[2], len(na), len(nb), len(na) * len(nb), surv, host])
        print(spec, n, flush=True, file=sys.stderr)
    open(f"{out}/xfer-av2.txt", "w").write("".join(f"{r}\n" for r in sorted(xfer)))


if __name__ == "__main__":
    main()
