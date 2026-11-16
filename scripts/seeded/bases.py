#!/usr/bin/env python3
"""Collect the realisable bases of the seeded mutant corpus.

A base is a realisable FRETISH specification with the input/output partition
of the example it comes from. Two sources:

* every ideal under ``examples/<name>/fixes/`` of the small FRETISH set;
* for an unrealisable example with no ideals (``valu3s-uc6`` and rad's
  cores), the example with one live guarantee tombstoned, for each guarantee
  whose removal leaves it realisable.

Each candidate must be realisable, and must pass the single-requirement
screen: every live weakenable guarantee is realisable on its own against the
full assumption side. A guarantee that is unrealisable alone points at a
mislabelled atom, not at a conflict between requirements.

Writes ``<out>/bases/<id>.json`` and ``<out>/bases.csv``. See
experiments/2026-11-13-seeded-corpus/PLAN.md.
"""

from __future__ import annotations

import argparse
import copy
import csv
import json
import random
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
EXAMPLES = REPO / "examples"

# Example -> system cluster. Examples written for this project that share a
# state machine form one cluster, since their bases are not independent.
CLUSTERS: dict[str, str] = {
    "takeoff": "takeoff",
    "fsm": "fsm",
    "fsm-timing": "fsm",
    "fsm-combined": "fsm",
    "mode-arbiter": "mode-arbiter",
    "fsm-lmcps": "lmcps",
    "liquid-mixer": "liquid-mixer",
    "lift-plus-cruise-mini": "lpc",
    "valu3s-uc6": "valu3s",
    "lift-plus-cruise-full": "lpc",
    "ventilator-controller": "ventilator",
    "rad": "rad",
    **{f"rad-core-{c}": "rad" for c in (
        "1-18", "10", "12-18", "17-18", "33", "45", "55", "61", "65", "68")},
}

# Examples whose bases come from dropping one guarantee.
DROP_ONE = ["valu3s-uc6"] + [k for k in CLUSTERS if k.startswith("rad-core-")]


def realize(paths: list[Path], binary: Path, timeout: int) -> dict[Path, str]:
    """REALIZABLE, UNREALIZABLE, TIMEOUT or ERROR for each path."""

    def one(path: Path) -> tuple[Path, str]:
        try:
            out = subprocess.run([str(binary), str(path)], capture_output=True,
                                 text=True, timeout=timeout, check=False)
        except subprocess.TimeoutExpired:
            return path, "TIMEOUT"
        word = out.stdout.strip().split()[-1] if out.stdout.strip() else ""
        if out.returncode != 0 or word not in ("REALIZABLE", "UNREALIZABLE"):
            return path, "ERROR"
        return path, word

    with ThreadPoolExecutor(max_workers=JOBS) as pool:
        return dict(pool.map(one, paths))


def live(spec: dict) -> list[int]:
    return [i for i, g in enumerate(spec["guarantees"]) if not g.get("removed")]


def screen_specs(spec: dict) -> list[dict]:
    """One spec per live weakenable guarantee: that guarantee alone."""
    out = []
    for i in live(spec):
        g = spec["guarantees"][i]
        if g.get("weakenable", True) is False:
            continue
        s = copy.deepcopy(spec)
        s["guarantees"] = [g]
        out.append(s)
    return out


def drop(spec: dict, i: int) -> dict:
    s = copy.deepcopy(spec)
    del s["guarantees"][i]
    return s


JOBS = 8

# Larger examples cut into slices: a few guarantees connected by shared output
# atoms, with the example's own partition restricted to the atoms they use.
SLICE_FROM = ["ventilator-controller", "lift-plus-cruise-full", "rad",
              "valu3s-uc6", "liquid-mixer", "fsm-lmcps", "lift-plus-cruise-mini"]
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def req_atoms(req: dict, universe: set[str]) -> set[str]:
    text = json.dumps({k: v for k, v in req.items() if k != "weakenable"})
    return {t for t in IDENT.findall(text) if t in universe}


def grow_slice(spec: dict, rng: random.Random, size: int) -> tuple[int, ...] | None:
    outs = set(spec["out_atoms"])
    gs = spec["guarantees"]
    out_of = [req_atoms(g, outs) for g in gs]
    start = rng.randrange(len(gs))
    chosen = {start}
    while len(chosen) < size:
        shared = set().union(*(out_of[i] for i in chosen))
        frontier = [j for j in range(len(gs))
                    if j not in chosen and out_of[j] & shared]
        if not frontier:
            break
        chosen.add(rng.choice(frontier))
    if len(chosen) < 3:
        return None
    return tuple(sorted(chosen))


def slice_spec(spec: dict, idx: tuple[int, ...]) -> dict:
    s = copy.deepcopy(spec)
    s["guarantees"] = [spec["guarantees"][i] for i in idx]
    used: set[str] = set()
    universe = set(spec["in_atoms"]) | set(spec["out_atoms"])
    for r in s["guarantees"] + s.get("assumptions", []):
        used |= req_atoms(r, universe)
    s["in_atoms"] = [a for a in spec["in_atoms"] if a in used]
    s["out_atoms"] = [a for a in spec["out_atoms"] if a in used]
    return s


def main() -> int:
    global JOBS
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--realize", type=Path,
                    default=REPO / "build-release" / "realize")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--slices", type=int, default=12,
                    help="distinct slices drawn per sliced example")
    ap.add_argument("--seed", type=int, default=20261113)
    args = ap.parse_args()
    JOBS = args.jobs

    candidates: list[tuple[str, str, str, dict]] = []  # id, example, origin, spec
    for ex in CLUSTERS:
        fixes = EXAMPLES / ex / "fixes"
        if fixes.is_dir():
            for f in sorted(fixes.glob("*.json")):
                candidates.append((f"{ex}__{f.stem}", ex, f"fix:{f.name}",
                                   json.loads(f.read_text())))
    for ex in DROP_ONE:
        spec = json.loads((EXAMPLES / ex / "spec.json").read_text())
        for i in range(len(spec["guarantees"])):
            if spec["guarantees"][i].get("weakenable", True) is False:
                continue
            candidates.append((f"{ex}__drop{i}", ex, f"drop:{i}", drop(spec, i)))
    rng = random.Random(args.seed)
    for ex in SLICE_FROM:
        spec = json.loads((EXAMPLES / ex / "spec.json").read_text())
        seen: set[tuple[int, ...]] = set()
        for _ in range(args.slices * 10):
            if len(seen) >= args.slices:
                break
            idx = grow_slice(spec, rng, rng.randint(3, 8))
            if idx is None or idx in seen:
                continue
            seen.add(idx)
            name = "s" + "-".join(map(str, idx))
            candidates.append((f"{ex}__{name}", ex, f"slice:{name}",
                               slice_spec(spec, idx)))

    tmp = Path(tempfile.mkdtemp(prefix="bases-", dir=args.out))
    paths: dict[str, Path] = {}
    for cid, _, _, spec in candidates:
        p = tmp / f"{cid}.json"
        p.write_text(json.dumps(spec, indent=2))
        paths[cid] = p
    whole = realize(list(paths.values()), args.realize, args.timeout)

    rows = []
    bases_dir = args.out / "bases"
    bases_dir.mkdir(parents=True, exist_ok=True)
    for cid, ex, origin, spec in candidates:
        verdict = whole[paths[cid]]
        screen = "skipped"
        if verdict == "REALIZABLE":
            singles = screen_specs(spec)
            sp = []
            for j, s in enumerate(singles):
                p = tmp / f"{cid}__single{j}.json"
                p.write_text(json.dumps(s, indent=2))
                sp.append(p)
            res = realize(sp, args.realize, args.timeout)
            bad = [p.stem for p in sp if res[p] != "REALIZABLE"]
            screen = "pass" if not bad else "fail:" + ";".join(bad)
        keep = verdict == "REALIZABLE" and screen == "pass"
        if keep:
            (bases_dir / f"{cid}.json").write_text(json.dumps(spec, indent=2) + "\n")
        rows.append({"id": cid, "example": ex, "cluster": CLUSTERS[ex],
                     "origin": origin, "n_guarantees": len(live(spec)),
                     "n_assumptions": len(spec.get("assumptions", [])),
                     "realize": verdict, "screen": screen, "kept": int(keep)})
        print(f"{cid}: {verdict} screen={screen}", file=sys.stderr)

    with (args.out / "bases.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    print(f"{sum(r['kept'] for r in rows)} of {len(rows)} candidates kept",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
