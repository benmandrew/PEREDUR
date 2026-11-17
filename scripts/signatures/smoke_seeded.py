import sys, csv
from multiprocessing import Pool
import sig, dirsig, allsig
NEW = sys.argv[1]; OLD = dirsig.SRES
def job(a):
    arm, sub, seed, res = a
    dirsig.SRES = res
    r = dirsig.seeded_run((arm, sub, seed))
    if r is None: return None
    q = allsig.seeded((arm, sub, seed))
    if q: r.update({k: v for k, v in q.items() if k not in r})
    r["src"] = "new" if res == NEW else "old"
    return r
if __name__ == "__main__":
    subs = [l.strip() for l in open(f"{dirsig.E}/2026-11-13-seeded-corpus/selected.txt") if l.strip() and not l.startswith("#")]
    jobs = [(a, s, k, NEW) for a in ["blame-unreal", "gate-free", "gate-exchange"] for s in subs for k in range(2)]
    jobs += [(a, s, k, OLD) for a in ["full", "no-blame", "no-scale", "base"] for s in subs for k in range(2)]
    with Pool(4) as p:
        rows = [r for r in p.map(job, jobs, chunksize=2) if r]
    keys = sorted({k for r in rows for k in r})
    w = csv.DictWriter(open("smoke_seeded.csv", "w"), fieldnames=keys); w.writeheader(); w.writerows(rows)
