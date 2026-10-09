#!/usr/bin/env python3
"""AuRUS's pooled frontier per family, read from the archived RQ3 verdicts.

    python3 aurus_frontier.py POOLED_DIR AURUS_LINKS OUT.csv

POOLED_DIR is the archived pooled RQ3 pass
(experiments/2026-10-01-fretish-mixed/diversity/subsumption/pooled/ on
analysis/repair-diversity, tag provenance/pooled-rq3): plan/classes.csv and
results/results-pooled20-av{1,2,3}.csv. Its score_pooled.py merges classes
that compare as equivalent into groups and keeps, per side, the groups that
no other group of that side strictly implies. This script applies the same
rule to the AuRUS-against-AuRUS pairs alone, so no verdict is re-decided:
union-find over `equivalent`, then every group with no strictly stronger
AuRUS group. An undecided pair reads as a non-implication, as there; the
archive holds none.

Each group is represented by its lowest class id's representative body. That
body exists only on the host that ran its seed (av2 seeds 0-14, av3 15-29),
reached through the host checkout's experiments/results-aurus-curves symlink.
AURUS_LINKS is a local copy of that directory per host (`campaign.py collect
--outputs results-aurus-curves`), used to check that the link exists and
points at the archived file.

OUT.csv: spec,md5,host,seed,file,path,group_size. `path` is relative to the
host checkout.
"""
import collections
import csv
import glob
import os
import re
import sys

REP = re.compile(r"^(av[123])/aurus-h2h-out/([^/]+)/repeat-(\d+)/([^/]+)$")


def main(pooled: str, links: str, out: str) -> int:
    classes = {}
    with open(os.path.join(pooled, "plan", "classes.csv"), newline="") as fh:
        for r in csv.DictReader(fh):
            if r["sides"] == "A":
                classes[r["cid"]] = r
    parent = {c: c for c in classes}

    def find(c):
        while parent[c] != c:
            parent[c] = parent[parent[c]]
            c = parent[c]
        return c

    stronger = []  # (weaker cid, stronger cid)
    n_pairs = collections.Counter()
    for path in sorted(glob.glob(os.path.join(pooled, "results", "results-pooled20-av*.csv"))):
        with open(path, newline="") as fh:
            for r in csv.DictReader(fh):
                a, _, b = r["id"].partition("|")
                if a not in classes or b not in classes:
                    continue
                rel = r["relation"]
                n_pairs[rel] += 1
                if rel == "equivalent":
                    parent[find(a)] = find(b)
                elif rel == "weaker":
                    stronger.append((a, b))
                elif rel == "stronger":
                    stronger.append((b, a))
    dominated = {find(a) for a, b in stronger if find(a) != find(b)}
    members = collections.defaultdict(list)
    for c in classes:
        members[find(c)].append(c)

    def cid_key(c):
        spec, _, n = c.rpartition("#")
        return spec, int(n)

    rows, missing = [], []
    for root, cids in members.items():
        if root in dominated:
            continue
        rep = classes[min(cids, key=cid_key)]
        m = REP.match(rep["rep"])
        if not m:
            raise SystemExit(f"unexpected rep path {rep['rep']}")
        host, spec, seed, name = m.group(1), m.group(2), int(m.group(3)), m.group(4)
        rel = f"experiments/results-aurus-curves/aurus_{spec}_seed{seed:02d}/accumulated/{name}"
        link = os.path.join(links, host, f"aurus_{spec}_seed{seed:02d}", "accumulated", name)
        want = f"/aurus-h2h-out/{spec}/repeat-{seed:02d}/{name}"
        if not os.path.islink(link) or not os.readlink(link).endswith(want):
            missing.append(link)
        rows.append([spec, rep["md5"], host, seed, name, rel, len(cids)])
    if missing:
        raise SystemExit(f"{len(missing)} representative(s) with no matching host link, e.g. {missing[0]}")
    rows.sort()
    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["spec", "md5", "host", "seed", "file", "path", "group_size"])
        w.writerows(rows)
    per_spec = collections.Counter(r[0] for r in rows)
    print(f"AuRUS-AuRUS pairs read: {dict(n_pairs)}")
    for spec in sorted(per_spec):
        print(f"  {spec:28s} {per_spec[spec]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:4]))
