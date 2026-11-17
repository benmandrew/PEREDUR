import sys, csv
from multiprocessing import Pool
import sig, dirsig, allsig, strat
NEW = sys.argv[1]; OLD = sig.RES
def job(a):
    arm, spec, seed, res = a
    sig.RES = res
    r = dirsig.small_run((arm, spec, seed))
    if r is None: return None
    r.update({k: v for k, v in allsig.small((arm, spec, seed)).items() if k not in r})
    r.update({k: v for k, v in strat.run((arm, spec, seed)).items() if k not in r})
    r["src"] = "new" if res == NEW else "old"
    return r
if __name__ == "__main__":
    jobs = [(a, s, k, NEW) for a in ["blame-unreal", "gate-free", "gate-exchange"] for s in sig.SPECS for k in range(10)]
    jobs += [(a, s, k, OLD) for a in ["full", "no-blame", "no-scale", "base"] for s in sig.SPECS for k in range(10)]
    with Pool(4) as p:
        rows = [r for r in p.map(job, jobs, chunksize=2) if r]
    keys = sorted({k for r in rows for k in r})
    w = csv.DictWriter(open("smoke_small.csv", "w"), fieldnames=keys); w.writeheader(); w.writerows(rows)
