#!/usr/bin/env python3
"""Check that this campaign's base runs repeat sweep F's seeded base.

Each rerun nsga2-apportion/base run has a twin of the same name in sweep F's
seeded results. The twins must agree byte for byte in every repair_*.json and
in the accumulated set (the files under accumulated/, less the elapsed-time
column of its index.tsv), so that only wall time differs and the pooled pass
can read sweep F's base. Prints one line per pair that differs, and a
summary; exits 1 if any pair differs or lacks a twin.

Runs on a host, over the seeds that host holds of both campaigns (av3 0-2,
av2 3-4), or locally once both are collected.

Usage: check_repairs.py [EXPERIMENTS_DIR]
"""

import sys
from pathlib import Path

TREE = "results-final-engine-seeded-random"
TWIN_TREE = "results-final-engine-seeded"
GLOB = "sweep_F_base_nsga2-apportion_*"


def files(run):
    """Relative path -> bytes of every file that holds a repair."""
    out = {p.name: p.read_bytes() for p in run.glob("repair_*.json")}
    acc = run / "accumulated"
    if acc.is_dir():
        out.update({f"accumulated/{p.relative_to(acc)}": p.read_bytes()
                    for p in acc.rglob("*") if p.is_file() and p.name != "index.tsv"})
        index = acc / "index.tsv"
        if index.exists():
            # file, generation, elapsed_s: the elapsed time is a timing.
            out["accumulated/index.tsv"] = b"\n".join(
                b"\t".join(line.split(b"\t")[:2])
                for line in index.read_bytes().splitlines())
    return out


def main(argv):
    experiments = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parents[2]
    pairs = differ = missing = 0
    for run in sorted((experiments / TREE).glob(GLOB)):
        other = experiments / TWIN_TREE / run.name
        if not other.is_dir():
            missing += 1
            print(f"no twin: {run.name}")
            continue
        pairs += 1
        a, b = files(run), files(other)
        bad = sorted(k for k in a.keys() | b.keys() if a.get(k) != b.get(k))
        if bad:
            differ += 1
            print(f"differs: {run.name}: {len(bad)} file(s), e.g. {bad[0]}")
    print(f"{pairs} pairs, {differ} differ, {missing} without a twin")
    return 1 if differ or missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
