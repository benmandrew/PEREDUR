#!/usr/bin/env python3
"""Estimate each PSP seeded mutant's trivial-reversion probability.

A port of the FRETISH corpus's probe (``scripts/seeded/probe.py`` on
``campaign/seeded-corpus-sideways``) to the PSP seeded corpus. For a mutant O
with base B, ``sample-mutants`` draws chains of one and two undirected
mutations from O with the search's own operator (``psp::mutate_specification``
under the ``base`` arm's config, no parent context) and writes each distinct
result with its draw count per depth. A result counts as a reversion when it
is realisable and implies B. ``compare`` decides the implication (forward
direction only); a result that also implies O is unrealisable and skips
``realize``.

``p1`` and ``p2`` are the shares of the draws at depth 1 and depth 2 that are
reversions. A subject is headline when both are below 0.01, as on FRETISH;
the others are trivial, since one or two blind mutations would repair them.

The subjects are ``run_experiments.PSP_SEEDED_SPECS``, so a mutant added to
the corpus's ``selected.txt`` is probed too. Writes
``<out>/probe<tag>.csv`` (default ``<out>`` is this campaign's directory).
Each finished subject also leaves ``<out>/probe-work<tag>/<subject>/row.json``,
and a rerun reuses it. ``--config`` swaps the arm whose mutation settings
drive the chains, and ``--unrealizable-parent`` gives every draw a gated
parent, as the FRETISH reruns did.

    python3 experiments/2026-11-20-psp-order/scripts/probe.py --jobs 4
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

CAMPAIGN = Path(__file__).resolve().parents[1]
REPO = CAMPAIGN.parents[1]
sys.path.insert(0, str(REPO / "scripts"))
import run_experiments  # noqa: E402

CONFIG = (REPO / "experiments" / "configs-psp-order" / "nsga2-apportion"
          / "wkoff" / "log" / "sweep_F_base.toml")
LINE = re.compile(r"^(s\d+\.psp)\s*:\s*(.+?) (?:b|o)\.psp\s*$")
FIELDS = ("subject", "distinct", "implies_base", "implies_mutant",
          "reversions", "p1", "p2")


def implies(bin_dir: Path, samples: Path, ideal: Path, work: Path,
            timeout: int) -> set[str]:
    """The sample files that compare proves imply the ideal."""
    idir = work / ("ideal-" + ideal.stem)
    shutil.rmtree(idir, ignore_errors=True)
    idir.mkdir(parents=True)
    shutil.copy(ideal, idir / ideal.name)
    env = {**os.environ, "COMPARE_DIRECTIONS": "fwd"}
    out = subprocess.run([str(bin_dir / "compare"), "--repairs", str(samples),
                          "--ideals", str(idir), "--timeout", "30"],
                         capture_output=True, text=True, env=env,
                         timeout=timeout, check=False)
    hits = set()
    for ln in out.stdout.splitlines():
        m = LINE.match(ln.strip())
        if m and m.group(2) in ("equivalent to", "strictly stronger than"):
            hits.add(m.group(1))
    return hits


def realizable(bin_dir: Path, paths: list[Path], timeout: int) -> set[str]:
    hits = set()
    for p in paths:
        try:
            out = subprocess.run([str(bin_dir / "realize"), str(p)],
                                 capture_output=True, text=True,
                                 timeout=timeout, check=False)
        except subprocess.TimeoutExpired:
            continue
        verdict = out.stdout.strip()
        if verdict.endswith("REALIZABLE") and not verdict.endswith("UNREALIZABLE"):
            hits.add(p.name)
    return hits


def probe(task: tuple) -> dict:
    name, spec, args = task
    work = args.out / f"probe-work{args.tag}" / name
    done = work / "row.json"
    if done.exists():  # resume: a finished subject keeps its row
        return json.loads(done.read_text())
    shutil.rmtree(work, ignore_errors=True)
    samples = work / "samples"
    parent = ([] if args.unrealizable_parent is None
              else ["--unrealizable-parent", str(args.unrealizable_parent)])
    subprocess.run([str(args.bin / "sample-mutants"), str(spec["input"]),
                    "--config", str(args.config), "--out", str(samples),
                    "--draws", str(args.draws), "--depth", "2", "--seed", "1",
                    *parent],
                   capture_output=True, text=True, check=True,
                   timeout=args.timeout)
    index = list(csv.DictReader((samples / "index.tsv").open(), delimiter="\t"))
    b = work / "b.psp"
    o = work / "o.psp"
    shutil.copy(Path(spec["ideals_dir"]) / "base.psp", b)
    shutil.copy(spec["input"], o)
    above_b = implies(args.bin, samples, b, work, args.timeout)
    subset = work / "above-base"
    subset.mkdir()
    for f in above_b:
        shutil.copy(samples / f, subset / f)
    above_o = implies(args.bin, subset, o, work, args.timeout) if above_b else set()
    candidates = sorted(above_b - above_o)
    real = realizable(args.bin, [samples / f for f in candidates], args.timeout)
    row = {"subject": name, "distinct": len({r["file"] for r in index}),
           "implies_base": len(above_b), "implies_mutant": len(above_o),
           "reversions": len(real)}
    for d in ("1", "2"):
        total = sum(int(r["count"]) for r in index if r["depth"] == d)
        hit = sum(int(r["count"]) for r in index
                  if r["depth"] == d and r["file"] in real)
        row[f"p{d}"] = hit / total if total else 0.0
    shutil.rmtree(samples, ignore_errors=True)
    done.write_text(json.dumps(row) + "\n")
    return row


def main() -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    ap.add_argument("--out", type=Path, default=CAMPAIGN,
                    help="directory for probe<tag>.csv and probe-work<tag>/")
    ap.add_argument("--bin", type=Path, default=REPO / "build-release",
                    help="directory holding sample-mutants, compare, realize")
    ap.add_argument("--draws", type=int, default=5000)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=3600)
    ap.add_argument("--config", type=Path, default=CONFIG,
                    help="arm config whose mutation keys drive the chains")
    ap.add_argument("--tag", default="",
                    help="suffix for probe-work<tag>/ and probe<tag>.csv")
    ap.add_argument("--unrealizable-parent", type=float, default=None,
                    metavar="STATUS",
                    help="pass sample-mutants an unrealizable parent context, "
                         "so the gate acts")
    ap.add_argument("--subjects", nargs="*", default=None,
                    help="probe only these subjects (default: every "
                         "PSP_SEEDED_SPECS subject)")
    args = ap.parse_args()
    specs = run_experiments.PSP_SEEDED_SPECS
    names = sorted(specs) if args.subjects is None else args.subjects
    unknown = sorted(set(names) - set(specs))
    if unknown:
        sys.exit(f"probe: not in PSP_SEEDED_SPECS: {unknown}")
    if not names:
        sys.exit("probe: no subjects")
    rows = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for row in pool.map(probe, [(n, specs[n], args) for n in names]):
            rows.append(row)
            print(f"{row['subject']}: p1={row['p1']:.4f} p2={row['p2']:.4f} "
                  f"({row['reversions']} of {row['distinct']})", file=sys.stderr)
    with (args.out / f"probe{args.tag}.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(FIELDS))
        w.writeheader()
        w.writerows(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
