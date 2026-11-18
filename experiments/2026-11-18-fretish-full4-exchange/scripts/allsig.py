import os, sys, csv
from multiprocessing import Pool
import dirsig as d, sig

def counts(O, reps):
    P = [d.places(O, r) for r in reps]
    part = lambda ds: len({frozenset(p[:3] for p in ps if p[3] in ds) for ps in P})
    return dict(all_dir=len(set(P)), all_nodir=len({frozenset(p[:3] for p in ps) for ps in P}),
                down=part({"down"}), side=part({"side"}), up=part({"up"}), n=len(P))

def small(args):
    arm, spec, seed = args
    dd = f"{sig.RES}/sweep_O_{arm}_nsga2-apportion_wkoff_log_{spec}_seed{seed:02d}"
    O = sig.load(f"{sig.ROOT}/examples/{spec}/spec.json"); acc = f"{dd}/accumulated"
    pre = [sig.load(f"{acc}/{n}") for n in os.listdir(acc) if n.startswith("gen")] if os.path.isdir(acc) else []
    return dict(arm=arm, spec=spec, seed=seed, **counts(O, pre))

def seeded(args):
    arm, sub, seed = args
    dd = f"{d.SRES}/sweep_O_{arm}_nsga2-apportion_wkoff_log_{sub}_seed{seed:02d}"
    if not os.path.isfile(f"{dd}/run.json"): return None
    O = sig.load(f"{d.SUB}/{sub}/spec.json"); last = sig.load(f"{dd}/run.json")["generations_run"]; acc = f"{dd}/accumulated"
    pre = [sig.load(f"{acc}/{n}") for n in os.listdir(acc) if n.startswith("gen") and int(n[3:n.index("_")]) <= last] if os.path.isdir(acc) else []
    return dict(arm=arm, spec=sub, seed=seed, **counts(O, pre))

if __name__ == "__main__":
    arms = "base,full,only-blame,only-gate,no-blame,no-gate,only-demote,only-scale,only-xover".split(",")
    subs = [l.strip() for l in open(f"{d.E}/2026-11-13-seeded-corpus/selected.txt") if l.strip() and not l.startswith("#")]
    with Pool(16) as p:
        a = p.map(small, [(x, s, k) for x in arms for s in sig.SPECS for k in range(30)], chunksize=4)
        b = [r for r in p.map(seeded, [(x, s, k) for x in arms if x in ("base","full","only-blame","only-gate","no-blame","no-gate") for s in subs for k in range(5)], chunksize=4) if r]
    for name, rows in (("all_small.csv", a), ("all_seeded.csv", b)):
        with open(name, "w") as fh:
            w = csv.DictWriter(fh, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
