"""Fingerprint precision/recall between run frontiers: Kynkaanniemi et al.
(2019) improved precision and recall, with Naeem et al. (2020) coverage, on
the fpdraw fingerprints hybrid3.py reads.

usage: fp_prc.py <out.csv> <fp.tsv>...   (one tsv per subject and host)

A member's fingerprint is one bit per sampled word, set when the member
accepts it; the Hamming distance between two fingerprints counts the sampled
words on which their languages disagree. Units are hybrid3.py's: a triple is
the directed, uniform and mixed frontiers at one seed, and a control unit is
one arm's frontiers at seeds lo+3k..lo+3k+2 (lo = 0 or 15). For each ordered
pair of a unit's groups (A, B), A's region is the union of balls around its
members of radius the distance to the K-th nearest other member of A, and

  precision(B in A) = share of B's members inside A's region
  coverage(A by B)  = share of A's balls holding at least one B member

Recall of B against A is precision(A in B). A pair is skipped (blank) when
either group has K members or fewer.

Two PEREDUR arms at one seed share an RNG prefix, so a same-seed triple can
overlap more than a control unit for that reason alone. An `xtriple` unit
takes the arms from three different seeds of one control block, d at
lo+3k+r, u at lo+3k+(r+1)%3 and m at lo+3k+(r+2)%3 for r = 0, 1, 2, so it
spans the same seeds as the block's control units and no pair shares a seed.

DIST=jaccard measures |a^b| / |a|b|, the union distance the paper uses for
epsilon-separation; the default is the Hamming count.
"""
import collections, csv, os, re, sys
import numpy as np

K = int(os.environ.get("K", "3"))
# MATCH=<seed> subsamples every group of a unit to the unit's smallest group,
# so ball radii do not shrink with group size.
MATCH = os.environ.get("MATCH")
JACCARD = os.environ.get("DIST") == "jaccard"
POP = np.array([bin(i).count("1") for i in range(256)], dtype=np.uint16)
ARMS = ("directed", "uniform", "mixed")
G = ("d", "u", "m")
PAIRS = [a + b for a in G for b in G if a != b]
RUN = re.compile(r"/sweep_O_(directed|uniform|mixed)_nsga2-apportion_wkoff_log_(.+)_seed(\d+)/")


def load(path):
    keys, rows = [], []
    for line in open(path):
        p, _, digits = line.rstrip("\n").partition("\t")
        m = RUN.search(p)
        keys.append((m.group(1), m.group(2), int(m.group(3))))
        rows.append(bytes.fromhex(digits))
    width = len(rows[0])
    pad = (-width) % 8
    x = np.frombuffer(b"".join(r + b"\0" * pad for r in rows), dtype=np.uint64)
    return keys, x.reshape(len(rows), -1)


def popcount(v):
    return POP[v.view(np.uint8)].sum(-1)


def distances(x):
    out = np.empty((len(x), len(x)), dtype=float if JACCARD else np.int32)
    for i in range(0, len(x), 64):
        a, b = x[i:i + 64, None, :], x[None, :, :]
        diff = popcount(a ^ b)
        if JACCARD:
            union = popcount(a | b).astype(float)
            out[i:i + 64] = np.where(union > 0, diff / np.maximum(union, 1), 0.0)
        else:
            out[i:i + 64] = diff
    return out


def unit_row(groups, x):
    idx = {g: np.array(v, dtype=int) for g, v in groups.items()}
    if MATCH is not None:
        rng = np.random.default_rng([int(MATCH)] + [int(i) for g in G for i in idx[g][:1]])
        n = min(len(v) for v in idx.values())
        idx = {g: np.sort(rng.choice(v, n, replace=False)) for g, v in idx.items()}
    order = np.concatenate([idx[g] for g in G])
    d = distances(x[order])
    span, at = {}, 0
    for g in G:
        span[g] = slice(at, at + len(idx[g])); at += len(idx[g])
    radius = {}
    for g in G:
        n = len(idx[g])
        if n > K:
            within = d[span[g], span[g]]
            radius[g] = np.partition(within, K, axis=1)[:, K]
    prec, cov = [], []
    for p in PAIRS:
        a, b = p
        if a not in radius or len(idx[b]) <= K:
            prec.append(""); cov.append(""); continue
        inside = d[span[a], span[b]] <= radius[a][:, None]
        prec.append(round(float(inside.any(axis=0).mean()), 4))
        cov.append(round(float(inside.any(axis=1).mean()), 4))
    return [len(idx[g]) for g in G] + prec + cov


def main():
    out_path, paths = sys.argv[1], sys.argv[2:]
    w = csv.writer(open(out_path, "w", newline=""))
    w.writerow(["mode", "subject", "seed"] + [f"{g}_n" for g in G]
               + [f"prec_{p}" for p in PAIRS] + [f"cov_{p}" for p in PAIRS])
    for path in (p for p in paths if os.path.getsize(p)):
        keys, x = load(path)
        runs = collections.defaultdict(list)
        for i, (arm, subject, seed) in enumerate(keys):
            runs[(arm, seed)].append(i)
        subject = keys[0][1]
        seeds = sorted({s for _, s in runs})
        lo = 0 if seeds[0] < 15 else 15
        for s in range(lo, lo + 15):
            if all((a, s) in runs for a in ARMS):
                groups = {g: runs[(a, s)] for g, a in zip(G, ARMS)}
                w.writerow(["triple", subject, s] + unit_row(groups, x))
        for k in range(5):
            for r in range(3):
                ss = [lo + 3 * k + (r + j) % 3 for j in range(3)]
                if all((a, s) in runs for a, s in zip(ARMS, ss)):
                    groups = {g: runs[(a, s)] for g, a, s in zip(G, ARMS, ss)}
                    w.writerow(["xtriple", subject, 3 * k + r] + unit_row(groups, x))
        for a in ARMS:
            for k in range(5):
                ss = [lo + 3 * k + i for i in range(3)]
                if all((a, s) in runs for s in ss):
                    groups = {g: runs[(a, s)] for g, s in zip(G, ss)}
                    w.writerow(["null", f"{subject}@{a}", k] + unit_row(groups, x))
        print(path, len(keys), flush=True)


if __name__ == "__main__":
    main()
