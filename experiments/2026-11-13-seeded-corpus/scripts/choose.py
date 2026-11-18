#!/usr/bin/env python3
"""Choose the seeded subjects the campaign runs, and write selected.txt.

A subject is *headline* when both reversion probabilities from probe.py are
below the threshold (default 0.01: under one expected reversion among the
first 100 mutations of the mutant). The rest are *trivial*.

The fsm cluster holds most mutants, so each cluster is capped (default 40),
drawn round-robin over its bases so that no base dominates. A fixed number of
trivial subjects (default 10) also runs as a control stratum.

Writes ``<out>/selected.txt`` and ``<out>/subjects.csv`` (every probed
subject with its cluster, base, seed size, p1, p2, stratum and whether it was
selected).
"""

from __future__ import annotations

import argparse
import csv
import json
import random
import sys
from collections import defaultdict
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--threshold", type=float, default=0.01)
    ap.add_argument("--cap", type=int, default=40)
    ap.add_argument("--trivial", type=int, default=10)
    ap.add_argument("--seed", type=int, default=20261113)
    args = ap.parse_args()

    probe = {r["subject"]: r for r in csv.DictReader((args.out / "probe.csv").open())}
    rows = []
    for name, p in sorted(probe.items()):
        seed = json.loads((args.out / "subjects" / name / "seed.json").read_text())
        p1, p2 = float(p["p1"]), float(p["p2"])
        rows.append({"subject": name, "cluster": seed["cluster"],
                     "base": seed["base"], "n_edits": len(seed["edits"]),
                     "edits": "+".join(e["edit"] for e in seed["edits"]),
                     "p1": p1, "p2": p2,
                     "stratum": "headline" if max(p1, p2) < args.threshold
                     else "trivial"})

    rng = random.Random(args.seed)
    chosen: set[str] = set()
    by_cluster: dict[str, dict[str, list[str]]] = defaultdict(lambda: defaultdict(list))
    for r in rows:
        if r["stratum"] == "headline":
            by_cluster[r["cluster"]][r["base"]].append(r["subject"])
    for cluster, bases in sorted(by_cluster.items()):
        queues = {b: rng.sample(s, len(s)) for b, s in sorted(bases.items())}
        order = rng.sample(sorted(queues), len(queues))
        taken = 0
        while taken < args.cap and any(queues.values()):
            for b in order:
                if queues[b] and taken < args.cap:
                    chosen.add(queues[b].pop())
                    taken += 1
    trivial = [r["subject"] for r in rows if r["stratum"] == "trivial"]
    chosen |= set(rng.sample(sorted(trivial), min(args.trivial, len(trivial))))

    for r in rows:
        r["selected"] = int(r["subject"] in chosen)
    with (args.out / "subjects.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    (args.out / "selected.txt").write_text(
        "# Written by this campaign's scripts/choose.py; see subjects.csv.\n"
        + "".join(f"{s}\n" for s in sorted(chosen)))
    counts = defaultdict(lambda: [0, 0])
    for r in rows:
        if r["selected"]:
            counts[(r["cluster"], r["stratum"])][0] += 1
        counts[(r["cluster"], r["stratum"])][1] += 1
    for k, (sel, tot) in sorted(counts.items()):
        print(f"{k[0]:14} {k[1]:9} {sel:4} of {tot}", file=sys.stderr)
    print(f"{len(chosen)} subjects selected", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
