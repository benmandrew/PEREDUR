#!/usr/bin/env python3
"""Seed unrealisable PSP mutants from the realisable bases.

Each mutant O is a base B with one guarantee edited by a fault of
``psp::seeded_edits`` (``sample-mutants --psp-seeded``): a step up the
pattern, scope or count order, a narrowed formula field, or a sideways move
(an incomparable pattern or scope, swapped delimiters, a flipped literal or a
swapped signal). The filters are those of the FRETISH sideways corpus
(``2026-11-13-seeded-corpus/scripts/seed.py --sideways``), cheapest first:
``compare`` finds O strictly stronger than B or incomparable with it; O is
unrealisable; every live guarantee of O is realisable alone, so the conflict
needs two or more requirements; and the conjunction of O's requirements is
satisfiable. B is then O's known repair.

The bases are the lines of ``bases.tsv`` (id, path, source, max_candidates),
so a realisable SYNTECH17 import joins the corpus as one more line. Where
``max_candidates`` is set, that many of the base's candidates are drawn with
``--seed`` and the rest are not checked: on a large import each check costs a
whole-specification ``realize``. A base with no weakenable guarantee gives no
candidate.

Writes, under ``<out>`` (``examples/psp-seeded``),
``subjects/<id>/{spec.psp,fixes/base.psp,seed.json}``,
``<out>/mutants.csv`` (every candidate and the stage that dropped it) and
``<out>/selected.txt`` (at most ``--cap`` kept mutants per base, drawn with
``--seed``).
"""

from __future__ import annotations

import argparse
import csv
import json
import random
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
# The corpus is tracked under examples/, since experiments/ tracks no data.
CORPUS = REPO / "examples" / "psp-seeded"
WORK = Path(__file__).resolve().parents[1] / "work"

ADMIT = {"strictly stronger than", "incomparable"}


def run(cmd: list[str], timeout: int, stdin: str | None = None):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, input=stdin,
                              timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        return None


def read_bases(path: Path) -> list[dict]:
    lines = [ln for ln in path.read_text().splitlines()
             if ln.strip() and not ln.startswith("#")]
    return list(csv.DictReader(lines, delimiter="\t"))


def realize(binary: Path, path: Path, timeout: int) -> str:
    out = run([str(binary / "realize"), str(path)], timeout)
    if out is None:
        return "TIMEOUT"
    words = out.stdout.split()
    return words[-1] if out.returncode == 0 and words else "ERROR"


def relation(binary: Path, work: Path, mutant: Path, base: Path,
             timeout: int) -> str:
    r, i = work / "r", work / "i"
    for d in (r, i):
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
    shutil.copy(mutant, r / "o.psp")
    shutil.copy(base, i / "b.psp")
    out = run([str(binary / "compare"), "--repairs", str(r), "--ideals",
               str(i), "--timeout", "60"], timeout)
    if out is None:
        return "timeout"
    # compare names the ideal after every relation but "incomparable".
    m = re.search(r"o\.psp\s*:\s*(?:(incomparable)\s*$|(.+?) b\.psp)",
                  out.stdout, re.M)
    if not m:
        return "error"
    return (m.group(1) or m.group(2)).strip()


def lowered(binary: Path, path: Path, timeout: int) -> list[str] | None:
    out = run([str(binary / "ltl"), str(path)], timeout)
    if out is None or out.returncode != 0:
        return None
    return [ln.split("LTL:", 1)[1].strip() for ln in out.stdout.splitlines()
            if "LTL:" in ln]


def satisfiable(binary: Path, path: Path, timeout: int) -> bool | None:
    parts = lowered(binary, path, timeout)
    if not parts:
        return None
    phi = " & ".join(f"({p})" for p in parts)
    sat = run(["ltlfilt", "--satisfiable", "-F", "-"], timeout,
              stdin=phi + "\n")
    if sat is None:
        return None
    return bool(sat.stdout.strip())


def live_guarantees(spec: dict) -> list[int]:
    return [i for i, g in enumerate(spec["guarantees"])
            if "fixed" not in g and not g.get("removed")]


def singles_realisable(binary: Path, work: Path, spec: dict,
                       timeout: int) -> bool:
    for j in live_guarantees(spec):
        s = dict(spec)
        s["guarantees"] = [spec["guarantees"][j]]
        p = work / f"single{j}.psp"
        p.write_text(json.dumps(s))
        if realize(binary, p, timeout) != "REALIZABLE":
            return False
    return True


def event_scopes(spec: dict) -> int:
    """Guarantee slots under before, after, between or after-until."""
    return sum(1 for g in spec["guarantees"]
               if "fixed" not in g and not g.get("removed")
               and g.get("scope", "globally") != "globally")


def check(task):
    """Run every filter on one candidate; returns its row."""
    binary, row, mutant, base_path, work, timeout = task
    work.mkdir(parents=True, exist_ok=True)
    rel = relation(binary, work, mutant, base_path, timeout)
    row["relation"] = rel
    if rel not in ADMIT:
        row["stage"] = "relation"
        return row
    v = realize(binary, mutant, timeout)
    row["realize"] = v
    if v != "UNREALIZABLE":
        row["stage"] = "realize"
        return row
    spec = json.loads(mutant.read_text())
    if not singles_realisable(binary, work, spec, timeout):
        row["stage"] = "singles"
        return row
    sat = satisfiable(binary, mutant, timeout)
    row["satisfiable"] = sat
    if not sat:
        row["stage"] = "satisfiable"
        return row
    row["stage"] = "kept"
    return row


def main() -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    ap.add_argument("--bases", type=Path, default=CORPUS / "bases.tsv")
    ap.add_argument("--out", type=Path, default=CORPUS)
    ap.add_argument("--bin", type=Path, default=REPO / "build-release",
                    help="directory holding realize, compare, ltl and "
                         "sample-mutants")
    ap.add_argument("--cap", type=int, default=8,
                    help="kept mutants per base in selected.txt")
    ap.add_argument("--seed", type=int, default=20261120)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--timeout", type=int, default=300)
    args = ap.parse_args()

    work_root = WORK
    tasks = []
    bases = read_bases(args.bases)
    for b in bases:
        base_path = REPO / b["path"]
        cand = work_root / b["id"] / "candidates"
        shutil.rmtree(cand, ignore_errors=True)
        out = run([str(args.bin / "sample-mutants"), str(base_path),
                   "--psp-seeded", "--out", str(cand)], args.timeout)
        if out is None or out.returncode != 0:
            print(f"{b['id']}: sample-mutants failed", file=sys.stderr)
            return 1
        rows = list(csv.DictReader((cand / "index.tsv").open(),
                                   delimiter="\t"))
        if not rows:
            print(f"{b['id']}: no candidates", file=sys.stderr)
            continue
        if realize(args.bin, base_path, args.timeout) != "REALIZABLE":
            print(f"{b['id']}: the base is not realisable", file=sys.stderr)
            return 1
        n_all = len(rows)
        limit = int(b.get("max_candidates") or 0)
        if limit and limit < len(rows):
            draw = random.Random(f"{args.seed}:{b['id']}")
            rows = [rows[k] for k in sorted(draw.sample(range(len(rows)),
                                                        limit))]
        for r in rows:
            mid = f"{b['id']}__{Path(r['file']).stem}"
            row = {"id": mid, "base": b["id"], "source": b["source"],
                   "slot": r["slot"], "edit": r["edit"], "field": r["field"],
                   "rule": r["rule"],
                   # The loader prefixes every atom; the corpus shows them bare.
                   "before": r["before"].replace("iap_", ""),
                   "after": r["after"].replace("iap_", ""), "stage": ""}
            tasks.append((args.bin, row, cand / r["file"], base_path,
                          work_root / b["id"] / mid, args.timeout))
        print(f"{b['id']}: {len(rows)} of {n_all} candidates", file=sys.stderr)

    rows = []
    subjects = args.out / "subjects"
    shutil.rmtree(subjects, ignore_errors=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for (_, _, mutant, base_path, _, _), row in zip(
                tasks, pool.map(check, tasks)):
            rows.append(row)
            if row["stage"] != "kept":
                continue
            spec = json.loads(mutant.read_text())
            row["event_scopes"] = event_scopes(spec)
            row["n_guarantees"] = len(live_guarantees(spec))
            d = subjects / row["id"]
            (d / "fixes").mkdir(parents=True, exist_ok=True)
            shutil.copy(mutant, d / "spec.psp")
            shutil.copy(base_path, d / "fixes" / "base.psp")
            seed = {k: row[k] for k in ("base", "source", "edit", "field",
                                        "rule", "relation", "before",
                                        "after")}
            seed["slot"] = int(row["slot"])
            seed["base_path"] = str(base_path.relative_to(REPO))
            (d / "seed.json").write_text(json.dumps(seed, indent=2) + "\n")

    fields = ["id", "base", "source", "slot", "edit", "field", "rule",
              "relation", "realize", "satisfiable", "stage", "event_scopes",
              "n_guarantees", "before", "after"]
    with (args.out / "mutants.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    rng = random.Random(args.seed)
    selected = []
    for b in bases:
        kept = sorted(r["id"] for r in rows
                      if r["base"] == b["id"] and r["stage"] == "kept")
        selected += sorted(rng.sample(kept, min(args.cap, len(kept))))
    (args.out / "selected.txt").write_text(
        "# Written by scripts/seed.py: at most --cap kept mutants per base.\n"
        + "".join(f"{s}\n" for s in selected))
    kept_n = sum(r["stage"] == "kept" for r in rows)
    print(f"{kept_n} of {len(rows)} candidates kept, {len(selected)} "
          f"selected", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
