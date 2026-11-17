import os, csv, collections
from multiprocessing import Pool
import dirsig as d, sig

def run(args):
    arm, spec, seed = args
    dd = f"{sig.RES}/sweep_O_{arm}_nsga2-apportion_wkoff_log_{spec}_seed{seed:02d}"
    O = sig.load(f"{sig.ROOT}/examples/{spec}/spec.json"); acc = f"{dd}/accumulated"
    pre = [sig.load(f"{acc}/{n}") for n in os.listdir(acc) if n.startswith("gen")] if os.path.isdir(acc) else []
    by = collections.defaultdict(list); slots = set(); nslot = []
    for r in pre:
        ps = d.places(O, r)
        down = frozenset(p[:3] for p in ps if p[3] == "down")
        k = len({p[:2] for p in down}); k = min(k, 3)
        by[k].append(frozenset(ps)); slots |= {p[:2] for p in ps}
        nslot.append(len({p[:2] for p in ps}))
    row = dict(arm=arm, spec=spec, seed=seed, n=len(pre), touched=len(slots),
               mean_changed_slots=sum(nslot) / len(nslot) if nslot else float("nan"))
    for k in range(4):
        row[f"n{k}"] = len(by[k]); row[f"d{k}"] = len(set(by[k]))
    return row

if __name__ == "__main__":
    arms = ["base", "full", "only-blame", "only-gate", "no-blame", "no-gate"]
    with Pool(16) as p:
        rows = p.map(run, [(a, s, k) for a in arms for s in sig.SPECS for k in range(30)], chunksize=4)
    with open("strat.csv", "w") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
