"""Label-permutation null for the matched, cross-seed ball-radii figures.

usage: perm_null.py <out.csv> <sidecar-dir>...
env: K (3), R (1000 permutations), CUT (as tlsf_prc.py), JOBS (8),
     METRIC (knn, or pprec for Park & Kim's P-precision)

Every pair is built as `MATCH=1 XSEED=1 tlsf_prc.py` builds it: the four
frontiers of a seed block (X and Y at seeds 2j and 2j+1) are subsampled to the
smallest of the four, with the same seeded draws, so the observed figures
reproduce that script's rows. Each pair's 2m repairs are then pooled and split
at random into two halves of m, R times, and both precisions are recomputed on
each split. The split ignores which side a repair came from, so it needs no
assumption about the distance beyond the one the metric already makes, and the
null is exchangeability of the two sides on that family.

Each row is one (comparison, kind, family): the observed figure averaged over
the family's pairs, the same average under each permutation, and from those
two permutation p-values. `p_gap` is two-sided for prec_y_in_x - prec_x_in_y;
`p_low` is one-sided for the symmetric precision falling below exchangeable
(the two sides sampling different regions). Kind is `cross` for X against Y,
`ctrl_x` and `ctrl_y` for the two seeds of one side. The null draws are kept
in <out.csv>.npz so families can be pooled afterwards.

METRIC=pprec swaps the figure for P-precision (Park & Kim, ICCV 2023, Eq.
13-15): one radius R per set, a = 1.2 times the mean distance to each
member's k-th nearest neighbour in the set (k = 4), and each point of the
other set scored 1 - prod(1 - max(0, 1 - d/R)) over the set's members.
Blocks whose matched size is 4 or less are skipped under it.
"""
import csv, glob, os, sys
from concurrent.futures import ProcessPoolExecutor
import numpy as np
import tlsf_prc as T

R = int(os.environ.get("R", "1000"))
JOBS = int(os.environ.get("JOBS", "8"))
K = T.K
METRIC = os.environ.get("METRIC", "knn")
PK, PA = 4, 1.2


def sub(a, n, rng):
    return a[np.sort(rng.choice(len(a), n, replace=False))]


def pprec(d, ia, ib):
    """P-precision of ib in ia's set, and of ia in ib's."""
    out = []
    for own, other in ((ia, ib), (ib, ia)):
        r = PA * np.partition(d[np.ix_(own, own)], PK, axis=1)[:, PK].mean()
        if r <= 0:
            out.append(float((d[np.ix_(own, other)] == 0).any(0).mean()))
            continue
        p = np.clip(1 - d[np.ix_(own, other)] / r, 0, 1)
        out.append(float((1 - np.prod(1 - p, axis=0)).mean()))
    return out[0], out[1]


def figures(d, ia, ib):
    """Share of ib inside ia's region, and of ia inside ib's."""
    if METRIC == "pprec":
        return pprec(d, ia, ib)
    ra = np.partition(d[np.ix_(ia, ia)], K, axis=1)[:, K]
    rb = np.partition(d[np.ix_(ib, ib)], K, axis=1)[:, K]
    ab = d[np.ix_(ia, ib)]
    return (ab <= ra[:, None]).any(0).mean(), (ab.T <= rb[:, None]).any(0).mean()


def null(a, b, rng):
    """Observed (y_in_x, x_in_y) and R permuted draws of the same."""
    m = len(a)
    pool = np.concatenate([a, b])
    d = T.dist(pool, pool)
    obs = figures(d, np.arange(m), np.arange(m, 2 * m))
    draws = np.empty((R, 2))
    for r in range(R):
        p = rng.permutation(2 * m)
        draws[r] = figures(d, p[:m], p[m:])
    return np.array(obs), draws


def family(job):
    spec, comp, keys_by_block, cut = job
    x, y = comp
    out = {}
    for seed, paths in keys_by_block:
        rng = np.random.default_rng([seed, len(spec)])
        prints = []
        for p in paths:
            t = None
            if cut == "equal":
                t = min(T_last(q) for q in paths)
            elif cut:
                t = float(cut)
            names = T.frontier(p, t)
            fp = T.load_prints(p.replace(".members.tsv", ".fingerprints.tsv"), names)
            prints.append(np.stack([fp[n] for n in names if n in fp]) if fp else np.zeros((0, 1), np.uint8))
        fx0, fx1, fy0, fy1 = prints
        if len({f.shape[1] for f in prints}) != 1:
            continue
        m = min(len(f) for f in prints)
        if m <= (max(K, PK) if METRIC == "pprec" else K):
            continue
        # tlsf_prc.pair draws a then b for each pair, in this order: both
        # cross pairs, then ctrl_x, then ctrl_y. Repeat the draws to match.
        prng = np.random.default_rng([seed, len(spec), 1])
        for kind, a, b in (("cross", fx0, fy1), ("cross", fx1, fy0),
                           ("ctrl_x", fx0, fx1), ("ctrl_y", fy0, fy1)):
            a, b = sub(a, m, rng), sub(b, m, rng)
            obs, draws = null(a, b, prng)
            out.setdefault(kind, []).append((obs, draws))
    rows, keep = [], {}
    for kind, items in out.items():
        obs = np.mean([o for o, _ in items], axis=0)
        draws = np.mean([dr for _, dr in items], axis=0)
        gap, ngap = obs[0] - obs[1], draws[:, 0] - draws[:, 1]
        sym, nsym = obs.mean(), draws.mean(1)
        rows.append([f"{x}|{y}", kind, spec, len(items), round(obs[0], 4), round(obs[1], 4),
                     round(sym, 4), round(nsym.mean(), 4),
                     round((1 + (np.abs(ngap) >= abs(gap) - 1e-12).sum()) / (R + 1), 4),
                     round((1 + (nsym <= sym + 1e-12).sum()) / (R + 1), 4)])
        keep[f"{x}|{y}|{kind}|{spec}"] = np.concatenate([[obs], draws])
    print(spec, comp, flush=True)
    return rows, keep


def T_last(path):
    cuts = [float(l.split("\t", 1)[0]) for l in list(open(path))[1:] if "\t" in l]
    return max(cuts) if cuts else 0.0


def main():
    out, dirs = sys.argv[1], sys.argv[2:]
    runs = {}
    for d in dirs:
        for p in glob.glob(f"{d}/*.members.tsv"):
            mt = T.NAME.match(os.path.basename(p))
            if mt:
                label = "aurus" if mt.group(1) else f"{mt.group(2)}-{mt.group(3)}"
                runs[(label, mt.group(1) or mt.group(4), int(mt.group(5)))] = p
    jobs = []
    for spec in sorted({k[1] for k in runs}):
        for x, y in T.COMPARISONS:
            blocks = []
            for seed in range(0, 30, 2):
                keys = [(lab, spec, s) for lab in (x, y) for s in (seed, seed + 1)]
                if all(k in runs for k in keys):
                    blocks.append((seed, [runs[k] for k in keys]))
            if blocks:
                jobs.append((spec, (x, y), blocks, os.environ.get("CUT")))
    w = csv.writer(open(out, "w", newline=""))
    w.writerow(["comparison", "kind", "spec", "pairs", "prec_y_in_x", "prec_x_in_y",
                "sym", "sym_null_mean", "p_gap", "p_low"])
    keep = {}
    with ProcessPoolExecutor(JOBS) as ex:
        for rows, k in ex.map(family, jobs):
            w.writerows(rows)
            keep.update(k)
    np.savez_compressed(out + ".npz", **keep)


if __name__ == "__main__":
    main()
