#!/usr/bin/env python3
"""Estimate each seeded mutant's trivial-reversion probability.

For a mutant O with base B, ``sample-mutants`` draws chains of one and two
undirected mutations from O with the search's own operator (the ``base``
arm's config, no parent context) and writes each distinct result with its
draw count per depth. A result counts as a reversion when it is realisable
and implies B. ``compare`` decides the implication (forward direction only);
a result that also implies O is unrealisable and skips ``realize``.

``p1`` and ``p2`` are the shares of the draws at depth 1 and depth 2 that are
reversions. Writes ``<out>/probe.csv``.
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BIN = REPO / "build-release"
CONFIG = (REPO / "experiments" / "configs-fretish-seeded" / "nsga2-apportion"
          / "wkoff" / "log" / "sweep_O_base.toml")
LINE = re.compile(r"^(s\d+\.json)\s*:\s*(.+?) (?:b|o)\.json\s*$")


def implies(samples: Path, ideal: Path, work: Path, timeout: int) -> set[str]:
    """The sample files that compare proves imply the ideal."""
    idir = work / ("ideal-" + ideal.stem)
    shutil.rmtree(idir, ignore_errors=True)
    idir.mkdir(parents=True)
    shutil.copy(ideal, idir / ideal.name)
    env = {**os.environ, "COMPARE_DIRECTIONS": "fwd"}
    out = subprocess.run([str(BIN / "compare"), "--repairs", str(samples),
                          "--ideals", str(idir), "--timeout", "30"],
                         capture_output=True, text=True, env=env,
                         timeout=timeout, check=False)
    hits = set()
    for ln in out.stdout.splitlines():
        m = LINE.match(ln.strip())
        if m and m.group(2) in ("equivalent to", "strictly stronger than"):
            hits.add(m.group(1))
    return hits


def realizable(paths: list[Path], timeout: int) -> set[str]:
    hits = set()
    for p in paths:
        try:
            out = subprocess.run([str(BIN / "realize"), str(p)],
                                 capture_output=True, text=True,
                                 timeout=timeout, check=False)
        except subprocess.TimeoutExpired:
            continue
        if out.stdout.strip().endswith("REALIZABLE") and \
                not out.stdout.strip().endswith("UNREALIZABLE"):
            hits.add(p.name)
    return hits


def probe(task):
    subject, root, draws, timeout = task
    work = root / "probe-work" / subject.name
    shutil.rmtree(work, ignore_errors=True)
    samples = work / "samples"
    subprocess.run([str(BIN / "sample-mutants"), str(subject / "spec.json"),
                    "--config", str(CONFIG), "--out", str(samples),
                    "--draws", str(draws), "--depth", "2", "--seed", "1"],
                   capture_output=True, text=True, check=True, timeout=timeout)
    index = list(csv.DictReader((samples / "index.tsv").open(), delimiter="\t"))
    b = work / "b.json"
    o = work / "o.json"
    shutil.copy(subject / "fixes" / "base.json", b)
    shutil.copy(subject / "spec.json", o)
    above_b = implies(samples, b, work, timeout)
    subset = work / "above-base"
    subset.mkdir()
    for f in above_b:
        shutil.copy(samples / f, subset / f)
    above_o = implies(subset, o, work, timeout) if above_b else set()
    candidates = sorted(above_b - above_o)
    real = realizable([samples / f for f in candidates], timeout)
    row = {"subject": subject.name, "distinct": len({r["file"] for r in index}),
           "implies_base": len(above_b), "implies_mutant": len(above_o),
           "reversions": len(real)}
    for d in ("1", "2"):
        total = sum(int(r["count"]) for r in index if r["depth"] == d)
        hit = sum(int(r["count"]) for r in index
                  if r["depth"] == d and r["file"] in real)
        row[f"p{d}"] = hit / total if total else 0.0
    shutil.rmtree(samples, ignore_errors=True)
    return row


def main() -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--draws", type=int, default=5000)
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--timeout", type=int, default=3600)
    args = ap.parse_args()
    subjects = sorted(p for p in (args.out / "subjects").iterdir() if p.is_dir())
    rows = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for row in pool.map(probe, [(s, args.out, args.draws, args.timeout)
                                    for s in subjects]):
            rows.append(row)
            print(f"{row['subject']}: p1={row['p1']:.4f} p2={row['p2']:.4f} "
                  f"({row['reversions']} of {row['distinct']})", file=sys.stderr)
    with (args.out / "probe.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
