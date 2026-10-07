"""Fingerprint precision/recall between the paper's TLSF frontiers, from the
separation-recount sidecars (65536 uniform lasso words per family, one word
set shared by every tool and configuration of a family).

usage: tlsf_prc.py <out.csv> <sidecar-dir>...

A run's frontier is its members.tsv rows at the last cut. Labels are `aurus`
and `<grading>-<selection>` for the rematch configurations. For each
comparison (X, Y) and each family and seed where both runs hold more than K
members, both frontiers are subsampled to the smaller size (seeded), and with
union distance |a^b| / |a|b| (the paper's) each side's region is the union of
balls of radius the K-th nearest same-side distance. The same is done for the
same-arm control: X at seed 2j against X at seed 2j+1, likewise Y.

With MATCH=1, each block of seeds 2j and 2j+1 is compared only where all four
runs (X and Y at both seeds) hold more than K members, and the two cross pairs
and both controls are all subsampled to the smallest of the four. A ball's
radius shrinks as its frontier grows, so unmatched controls and cross pairs
are measured with different rulers.

Two further switches act on the matched mode only. XSEED=1 pairs X at 2j with
Y at 2j+1 and X at 2j+1 with Y at 2j, so no cross pair shares a seed: two
PEREDUR configurations at one seed share an RNG stream, which the controls,
pairing 2j with 2j+1, never do. CUT=equal reads all four frontiers of a block
at the latest cut no later than the earliest of the four runs' last cuts, so
neither side is credited with search time the other did not have; CUT=<s>
reads them at a fixed elapsed time instead.
"""
import collections, csv, glob, os, re, sys
import numpy as np

K = int(os.environ.get("K", "3"))
MATCH = os.environ.get("MATCH") == "1"
XSEED = os.environ.get("XSEED") == "1"
CUT = os.environ.get("CUT")
COMPARISONS = [("mrs-nsga2-apportion", "aurus"),          # RQ3: shipped vs AuRUS
               ("mrs-nsga2-apportion", "aurus-nsga2-apportion"),  # grading
               ("mrs-nsga2-apportion", "mrs-weighted"),    # selection
               ("aurus-nsga2-apportion", "aurus")]         # PEREDUR without MRS vs AuRUS
POP = np.array([bin(i).count("1") for i in range(256)], dtype=np.uint16)
NAME = re.compile(r"^(?:aurus_(.+)|sweep_G_(aurus|mrs)_(nsga2-apportion|weighted)_wkoff_log_(.+))_seed(\d+)\.members\.tsv$")


def frontier(path, t=None):
    rows = [l.rstrip("\n").split("\t") for l in list(open(path))[1:] if "\t" in l]
    if t is not None:
        rows = [(c, f) for c, f in rows if float(c) <= t]
    if not rows:
        return []
    last = max(float(c) for c, _ in rows)
    return [f for c, f in rows if float(c) == last]


def load_prints(path, names):
    want, out = set(names), {}
    with open(path) as fh:
        next(fh, None)
        for line in fh:
            f, _, h = line.rstrip("\n").partition("\t")
            if f in want and h:
                out[f] = np.frombuffer(bytes.fromhex(h), dtype=np.uint8)
    return out


def pop(x):
    return POP[x].sum(-1)


def dist(a, b):
    out = np.empty((len(a), len(b)))
    for i in range(0, len(a), 16):
        x, y = a[i:i + 16, None, :], b[None, :, :]
        u = pop(x | y).astype(float)
        out[i:i + 16] = np.where(u > 0, pop(x ^ y) / np.maximum(u, 1), 0.0)
    return out


def pair(a, b, rng, n=None):
    n = min(len(a), len(b)) if n is None else n
    a = a[np.sort(rng.choice(len(a), n, replace=False))]
    b = b[np.sort(rng.choice(len(b), n, replace=False))]
    ra = np.partition(dist(a, a), K, axis=1)[:, K]
    rb = np.partition(dist(b, b), K, axis=1)[:, K]
    ab = dist(a, b)
    in_a = ab <= ra[:, None]           # b's members inside a's balls
    in_b = ab.T <= rb[:, None]         # a's members inside b's balls
    return [n, round(float(in_a.any(0).mean()), 4), round(float(in_b.any(0).mean()), 4),
            round(float(in_a.any(1).mean()), 4), round(float(in_b.any(1).mean()), 4)]


def main():
    out, dirs = sys.argv[1], sys.argv[2:]
    runs = {}
    for d in dirs:
        for p in glob.glob(f"{d}/*.members.tsv"):
            m = NAME.match(os.path.basename(p))
            if not m:
                continue
            label = "aurus" if m.group(1) else f"{m.group(2)}-{m.group(3)}"
            spec = m.group(1) or m.group(4)
            runs[(label, spec, int(m.group(5)))] = p
    w = csv.writer(open(out, "w", newline=""))
    w.writerow(["comparison", "kind", "spec", "seed", "n", "prec_y_in_x", "prec_x_in_y",
                "cov_x_by_y", "cov_y_by_x"])
    cache = {}

    def last_cut(key):
        cuts = [float(l.split("\t", 1)[0]) for l in list(open(runs[key]))[1:] if "\t" in l]
        return max(cuts) if cuts else 0.0

    def prints(key, t=None):
        if (key, t) not in cache:
            p = runs[key]
            names = frontier(p, t)
            fp = load_prints(p.replace(".members.tsv", ".fingerprints.tsv"), names)
            cache[(key, t)] = np.stack([fp[n] for n in names if n in fp]) if fp else np.zeros((0, 1), np.uint8)
        return cache[(key, t)]

    specs = sorted({k[1] for k in runs})
    for spec in specs:
        for x, y in COMPARISONS:
            comp = f"{x}|{y}"
            if MATCH:
                for seed in range(0, 30, 2):
                    rng = np.random.default_rng([seed, len(spec)])
                    keys = [(lab, spec, s) for lab in (x, y) for s in (seed, seed + 1)]
                    if not all(k in runs for k in keys):
                        continue
                    t = None
                    if CUT == "equal":
                        t = min(last_cut(k) for k in keys)
                    elif CUT:
                        t = float(CUT)
                    fx0, fx1, fy0, fy1 = (prints(k, t) for k in keys)
                    if len({f.shape[1] for f in (fx0, fx1, fy0, fy1)}) != 1:
                        continue
                    m = min(len(f) for f in (fx0, fx1, fy0, fy1))
                    if m <= K:
                        continue
                    c0, c1 = (fy1, fy0) if XSEED else (fy0, fy1)
                    w.writerow([comp, "cross", spec, seed] + pair(fx0, c0, rng, m))
                    w.writerow([comp, "cross", spec, seed + 1] + pair(fx1, c1, rng, m))
                    w.writerow([comp, "ctrl_x", spec, seed] + pair(fx0, fx1, rng, m))
                    w.writerow([comp, "ctrl_y", spec, seed] + pair(fy0, fy1, rng, m))
                continue
            for seed in range(30):
                rng = np.random.default_rng([seed, len(spec)])
                if (x, spec, seed) in runs and (y, spec, seed) in runs:
                    a, b = prints((x, spec, seed)), prints((y, spec, seed))
                    if len(a) > K and len(b) > K and a.shape[1] == b.shape[1]:
                        w.writerow([comp, "cross", spec, seed] + pair(a, b, rng))
                if seed % 2 == 0:
                    for lab, kind in ((x, "ctrl_x"), (y, "ctrl_y")):
                        if (lab, spec, seed) in runs and (lab, spec, seed + 1) in runs:
                            a, b = prints((lab, spec, seed)), prints((lab, spec, seed + 1))
                            if len(a) > K and len(b) > K:
                                w.writerow([comp, kind, spec, seed] + pair(a, b, rng))
        cache.clear()
        print(spec, flush=True)


if __name__ == "__main__":
    main()
