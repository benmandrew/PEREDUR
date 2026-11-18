# Signature diversity and basin hits for the full4-exchange follow-up against its controls.
import os, sys, csv, glob
from multiprocessing import Pool
import sig, dirsig, allsig
S = os.environ["F4X_WORK"]
WT = os.path.expanduser("~/projects/peredur-wt-demote/experiments")
SRC = {  # (set, arm) -> source dir holding sweep_D_<arm>_* runs
    ("small", "remove-exchange"): f"{S}/f4xdirs/small",
    ("small", "remove-full"): f"{WT}/results-fretish-order-remove-no-f",
    ("seeded", "remove-exchange"): f"{S}/f4xdirs/seeded",
    ("seeded", "lean-remove"): f"{WT}/results-fretish-seeded-removal",
    ("seeded", "remove-no-blame-exchange"): f"{S}/f4xdirs/seeded",
    ("seeded", "remove-no-blame"): f"{S}/ctl/nb",
}
L = f"{S}/f4xlinks"
def link():
    for (st, arm), src in SRC.items():
        os.makedirs(f"{L}/{st}", exist_ok=True)
        for d in glob.glob(f"{src}/sweep_D_{arm}_nsga2-apportion_*"):
            if not os.path.isfile(f"{d}/run.json"): continue
            dst = f"{L}/{st}/" + os.path.basename(d).replace("sweep_D_", "sweep_O_", 1)
            if not os.path.lexists(dst): os.symlink(d, dst)
def small(a):
    sig.RES = f"{L}/small"
    r = dirsig.small_run(a)
    if r: r.update({k: v for k, v in allsig.small(a).items() if k not in r})
    return r
def seeded(a):
    dirsig.SRES = f"{L}/seeded"
    r = dirsig.seeded_run(a)
    if r:
        q = allsig.seeded(a)
        if q: r.update({k: v for k, v in q.items() if k not in r})
    return r
if __name__ == "__main__":
    link()
    subs = [l.strip() for l in open(f"{dirsig.E}/2026-11-13-seeded-corpus/selected.txt") if l.strip() and not l.startswith("#")]
    js = [(a, s, k) for a in ("remove-exchange", "remove-full") for s in sig.SPECS for k in range(30)]
    jq = [(a, s, k) for a in ("remove-exchange", "lean-remove", "remove-no-blame-exchange", "remove-no-blame") for s in subs for k in range(5)]
    with Pool(4) as p:
        a = [r for r in p.map(small, js, chunksize=4) if r]
        b = [r for r in p.map(seeded, jq, chunksize=4) if r]
    for name, rows in (("f4x_small.csv", a), ("f4x_seeded.csv", b)):
        keys = sorted({k for r in rows for k in r})
        with open(f"{S}/{name}", "w") as fh:
            w = csv.DictWriter(fh, fieldnames=keys); w.writeheader(); w.writerows(rows)
    print(len(a), len(b))
