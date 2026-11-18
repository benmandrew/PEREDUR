#!/usr/bin/env python3
"""Reachability of each sideways operator's undo by the engine's mutation.

For each operator, draw single edits on the seeded corpus's kept bases (one
guarantee changed, the rest as in B). From each edited specification E,
`sample-mutants` draws chains of up to 3 mutations under a config. A chain
reverts the edit at depth d when, after d mutations, the edited guarantee is
equal to B's, with its condition and response compared by truth table and
its other fields exactly (other guarantees may differ). Reports, for each operator and config,
the share of edits reverted by some chain at depth 1, 2 and 3, and the mean
share of chains that revert at each depth.

    python3 reach.py --corpus ~/projects/peredur/experiments/2026-11-13-seeded-corpus \
        --bin BUILD_DIR --config base=CFG --config full=CFG:0.5 --out OUT
"""
from __future__ import annotations

import argparse, csv, json, os, random, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts" / "seeded"))
import seed  # noqa: E402

OPS = ["swap-signal", "flip-literal", "reverse-implication"]


def norm(g: dict):
    """Exact key of the non-propositional fields."""
    return (g.get("condition-type", "trigger"), json.dumps(g.get("timing"), sort_keys=True),
            json.dumps(g.get("scope"), sort_keys=True), bool(g.get("removed", False)))


def ev(f, env):
    op = f.op
    if op == "atom":
        return env[f.name]
    if op in ("true", "false"):
        return op == "true"
    if op == "not":
        return not ev(f.args[0], env)
    a, b = ev(f.args[0], env), ev(f.args[1], env)
    return {"and": a and b, "or": a or b, "imp": (not a) or b, "iff": a == b}[op]


def equiv(x: str, y: str) -> bool:
    """Propositional equivalence by truth table."""
    try:
        f, g = seed.parse(x or "true"), seed.parse(y or "true")
    except ValueError:
        return x == y
    names = sorted(seed.atoms(f) | seed.atoms(g))
    if len(names) > 16:
        return f == g
    for m in range(1 << len(names)):
        env = {n: bool(m >> k & 1) for k, n in enumerate(names)}
        if ev(f, env) != ev(g, env):
            return False
    return True


def same(g: dict, h: dict) -> bool:
    """Guarantees equal up to propositional equivalence of their formulas."""
    return (norm(g) == norm(h) and equiv(g.get("condition", "true"), h.get("condition", "true"))
            and equiv(g.get("response", "true"), h.get("response", "true")))


def edits(bases, op, per_base, rng):
    out = []
    for bid, base in bases:
        seen, tries = set(), 0
        while len(seen) < per_base and tries < 40:
            tries += 1
            slots = seed.editable(base)
            if not slots:
                break
            i = rng.choice(slots)
            try:
                new = seed.SIDEWAYS_EDITS[op](base, base["guarantees"][i], rng)
            except ValueError:
                new = None
            if new is None or same(new, base["guarantees"][i]):
                continue
            key = (i, json.dumps(new, sort_keys=True))
            if key in seen:
                continue
            seen.add(key)
            spec = json.loads(json.dumps(base))
            spec["guarantees"][i] = new
            out.append((bid, i, spec, base["guarantees"][i]))
    return out


def probe(task):
    bin_dir, cfg, parent, draws, seed_, spec, slot, target = task
    with tempfile.TemporaryDirectory() as d:
        e = Path(d) / "e.json"
        e.write_text(json.dumps(spec))
        cmd = [str(bin_dir / "sample-mutants"), str(e), "--config", str(cfg), "--out", f"{d}/o",
               "--draws", str(draws), "--depth", "3", "--seed", str(seed_)]
        if parent is not None:
            cmd += ["--unrealizable-parent", str(parent)]
        env = {**os.environ, "LD_LIBRARY_PATH": os.environ.get("PEREDUR_LD_LIBRARY_PATH", "")}
        r = subprocess.run(cmd, capture_output=True, text=True, env=env, check=False)
        if r.returncode != 0:
            return None
        hit = {}
        for f in Path(f"{d}/o").glob("s*.json"):
            g = json.loads(f.read_text())["guarantees"]
            hit[f.name] = slot < len(g) and same(g[slot], target)
        counts = {1: 0, 2: 0, 3: 0}
        for row in csv.DictReader(open(f"{d}/o/index.tsv"), delimiter="\t"):
            if hit.get(row["file"]):
                counts[int(row["depth"])] += int(row["count"])
        return counts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", type=Path, required=True)
    ap.add_argument("--bin", type=Path, required=True)
    ap.add_argument("--config", action="append", required=True,
                    help="NAME=PATH or NAME=PATH:PARENT_STATUS")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--per-base", type=int, default=2)
    ap.add_argument("--draws", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=20261118)
    ap.add_argument("--jobs", type=int, default=4)
    a = ap.parse_args()
    rows = [r for r in csv.DictReader(open(a.corpus / "bases.csv"))
            if r["kept"] == "1" and 3 <= int(r["n_guarantees"]) <= 20]
    bases = [(r["id"], json.loads((a.corpus / "bases" / f"{r['id']}.json").read_text())) for r in rows]
    cfgs = []
    for c in a.config:
        name, rest = c.split("=", 1)
        path, _, parent = rest.partition(":")
        cfgs.append((name, Path(path), float(parent) if parent else None))
    rng = random.Random(a.seed)
    out = []
    for op in OPS:
        es = edits(bases, op, a.per_base, rng)
        for name, path, parent in cfgs:
            tasks = [(a.bin, path, parent, a.draws, a.seed + k, s, i, t) for k, (_, i, s, t) in enumerate(es)]
            with ThreadPoolExecutor(a.jobs) as pool:
                res = [r for r in pool.map(probe, tasks) if r is not None]
            n = len(res)
            row = {"operator": op, "config": name, "edits": n, "bases": len({b for b, *_ in es}),
                   "failed": len(tasks) - n}
            for d in (1, 2, 3):
                row[f"reached_by_{d}"] = sum(any(r[k] for k in range(1, d + 1)) for r in res) / n if n else float("nan")
                row[f"rate_{d}"] = sum(r[d] for r in res) / (n * a.draws) if n else float("nan")
            row["never"] = 1 - row["reached_by_3"] if n else float("nan")
            out.append(row)
            print(json.dumps(row), file=sys.stderr)
    with open(a.out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(out[0]))
        w.writeheader(); w.writerows(out)


if __name__ == "__main__":
    main()
