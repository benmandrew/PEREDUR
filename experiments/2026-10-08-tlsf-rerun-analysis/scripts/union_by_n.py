"""Union sizes of same-run frontier pairs at several word counts.

usage: union_by_n.py <sidecar-dir>...   env: NS (comma-separated word counts)
Reads each run's last-cut frontier, truncates its fingerprints to each word
count (the last N/4 hex digits hold the first N words) and prints, per family
and N, the 10th percentile and median of |a or b| over same-run pairs, and the
share of members whose 3rd-nearest-neighbour distance is below 3/|union|.
"""
import collections, glob, os, re, sys
import numpy as np
import tlsf_prc as T
import perm_null_dyn as D

NS = [int(n) for n in os.environ.get("NS", "65536,262144,1048576").split(",")]
stats = collections.defaultdict(lambda: collections.defaultdict(list))
for d in sys.argv[1:]:
    for p in sorted(glob.glob(f"{d}/*.members.tsv")):
        m = T.NAME.match(os.path.basename(p))
        spec = m.group(1) or m.group(4)
        names = T.frontier(p)
        hexes = {}
        with open(p.replace(".members.tsv", ".fingerprints.tsv")) as fh:
            next(fh)
            for line in fh:
                f, _, h = line.rstrip("\n").partition("\t")
                if f in names:
                    hexes[f] = h
        if len(hexes) <= 3:
            continue
        for n in NS:
            a = np.stack([np.frombuffer(bytes.fromhex(h[-n // 4:]), np.uint8) for h in hexes.values()])
            bits = np.unpackbits(a, axis=1).astype(np.float32)
            cnt = bits.sum(1); inter = bits @ bits.T
            u = cnt[:, None] + cnt[None, :] - inter
            dist = np.where(u > 0, (u - inter) / np.maximum(u, 1), 0)
            iu = np.triu_indices(len(a), 1)
            stats[(spec, n)]["u"] += list(u[iu])
            order = np.argsort(dist, axis=1)[:, 3]
            r3 = dist[np.arange(len(a)), order]
            uu = u[np.arange(len(a)), order]
            stats[(spec, n)]["below"] += list(r3 < 3 / np.maximum(uu, 1))
for (spec, n), s in sorted(stats.items()):
    print(f"{spec:20s} N={n:8d}  union p10 {np.percentile(s['u'], 10):8.0f}  median {np.median(s['u']):8.0f}  "
          f"r3 below 3/U: {np.mean(s['below']):.2f}")
