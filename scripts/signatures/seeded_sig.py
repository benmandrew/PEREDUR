import csv, json, os, sys
from multiprocessing import Pool
import sig
E = os.path.expanduser("~/projects/peredur/experiments")
SUB = f"{E}/2026-11-13-seeded-corpus/subjects"
RES = f"{E}/results-fretish-seeded"
subs = [l.strip() for l in open(f"{E}/2026-11-13-seeded-corpus/selected.txt") if l.strip() and not l.startswith("#")]

def one(args):
    arm, sub, seed = args
    d = f"{RES}/sweep_O_{arm}_nsga2-apportion_wkoff_log_{sub}_seed{seed:02d}"
    if not os.path.isfile(f"{d}/run.json"): return None
    O = sig.load(f"{SUB}/{sub}/spec.json"); B = sig.load(f"{SUB}/{sub}/fixes/base.json")
    eb = sig.signature(O, B)[0]
    acc = f"{d}/accumulated"
    last = sig.load(f"{d}/run.json")["generations_run"]
    pre = [sig.load(f"{acc}/{n}") for n in os.listdir(acc) if n.startswith("gen") and int(n[3:n.index("_")]) <= last] if os.path.isdir(acc) else []
    sp = {sig.signature(O, r)[0] for r in pre}
    return dict(arm=arm, spec=sub, seed=seed, n_edit_locs=len(eb), c_pre=len(sp),
                hit_subset=int(any(s and s <= eb for s in sp)), hit_exact=int(eb in sp),
                hit_touch=int(any(s & eb for s in sp)))

if __name__ == "__main__":
    arms = sys.argv[1].split(",")
    jobs = [(a, s, k) for a in arms for s in subs for k in range(5)]
    with Pool(16) as p:
        rows = [r for r in p.map(one, jobs, chunksize=4) if r]
    w = csv.DictWriter(sys.stdout, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
