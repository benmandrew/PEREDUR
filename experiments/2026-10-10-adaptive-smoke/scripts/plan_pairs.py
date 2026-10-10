#!/usr/bin/env python3
"""Plan the cross-arm repair pairs of the adaptive smoke test.

    python3 plan_pairs.py RESULTS_DIR PAIRS.csv

For every (subject, seed) and each contrast in CONTRASTS, one row per pair of
final repairs (repair_N.json), first arm against second, in the format
scripts/compare_pairs.py reads, with dirs = both. A unit where either arm
shipped no repair plans nothing.

Stdlib only, on python 3.10.
"""

import csv
import re
import sys
from pathlib import Path

CONTRASTS = (("adaptive", "directed"), ("adaptive", "uniform"),
             ("uniform", "directed"))
RUN = re.compile(r"sweep_O_(\w+)_nsga2-apportion_wkoff_log_(.+)_seed(\d+)$")


def main() -> None:
    results, out = Path(sys.argv[1]), sys.argv[2]
    repairs: dict = {}
    for run in sorted(results.iterdir()):
        match = RUN.match(run.name)
        if match is None:
            continue
        arm, subject, seed = match.group(1), match.group(2), int(match.group(3))
        files = sorted(run.glob("repair_*.json"),
                       key=lambda p: int(p.stem.split("_")[1]))
        repairs[(subject, seed, arm)] = files
    with open(out, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["id", "a_path", "b_path", "dirs"])
        for subject, seed in sorted({(s, d) for s, d, _ in repairs}):
            for first, second in CONTRASTS:
                for i, a in enumerate(repairs.get((subject, seed, first), [])):
                    for j, b in enumerate(
                            repairs.get((subject, seed, second), [])):
                        writer.writerow(
                            [f"{subject}|{seed}|{first}|{second}|{i}|{j}",
                             a.resolve(), b.resolve(), "both"])


if __name__ == "__main__":
    main()
