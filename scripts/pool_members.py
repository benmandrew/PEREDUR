#!/usr/bin/env python3
"""Pool each subject's members over its seeds and arms into nodes.

    python3 scripts/pool_members.py DESIGN.toml OUT_DIR [--root .] \\
        [--subjects SUBJECT...] [--allow-missing]

Run locally after `campaign.py collect`, since each lab host holds only its
own seeds of a results directory and a pool needs all of them. The output is
committed beside the campaign, so every host's fingerprint phase reads the
same nodes.

DESIGN.toml is read by scripts/pooled_design.py. For each subject, every arm
its contrasts name is read at every declared seed, from
`<root>/<results>/<run_dir>`. A member is a file named in the run's
`accumulated/maximal.tsv` (`members = "maximal"`, the default) or one of its
`repair_N.json` with the fitness record dropped (`members = "output"`).
Members whose JSON is identical once its keys are sorted are one node, as in
the 2026-10-08 FRETISH pass. Nodes are numbered in order of first appearance,
walking arms in design order, seeds in ascending order, and each run's members
in their own order.

A missing run directory fails the subject unless --allow-missing is given,
since a pool built from half the seeds reads as a complete one. A run
directory with no members (no maximal.tsv, or no repair) contributes nothing
and is counted.

Writes OUT_DIR/<subject>.jsonl.gz, one node a line:

    {"node": 0, "arms": ["nsga2-apportion/base"], "n": {"nsga2-apportion/base": 2},
     "members": [["nsga2-apportion/base", 0, "accumulated/gen03_0011.json"], ...],
     "spec": {...}}

`members` lists every (arm, seed, file) holding the node, the file relative
to its run directory. The gzip header carries no name or mtime, so the same
inputs give the same bytes. Also writes OUT_DIR/index.csv with one row per
subject and arm.
"""

import argparse
import csv
import gzip
import hashlib
import json
import os
import re
import sys

import pooled_design as D

REPAIR = re.compile(r"^repair_(\d+)\.json$")


def run_members(run: str, source: str) -> list:
    """(file relative to the run directory, spec) for each member of a run."""
    out = []
    if source == "maximal":
        tsv = os.path.join(run, "accumulated", "maximal.tsv")
        if not os.path.exists(tsv):
            return out
        with open(tsv) as handle:
            lines = handle.read().splitlines()[1:]
        for line in lines:
            name = line.split("\t")[0].strip()
            if name:
                rel = os.path.join("accumulated", name)
                with open(os.path.join(run, rel)) as handle:
                    out.append((rel, json.load(handle)))
        return out
    names = sorted((f for f in os.listdir(run) if REPAIR.match(f)),
                   key=lambda f: int(REPAIR.match(f).group(1)))
    for name in names:
        with open(os.path.join(run, name)) as handle:
            spec = json.load(handle)
        spec.pop("fitness", None)
        out.append((name, spec))
    return out


def pool(root: str, subject: dict, allow_missing: bool) -> tuple:
    nodes: dict = {}
    stats = {arm: {"runs": 0, "missing": 0, "empty": 0, "members": 0}
             for arm in subject["arms"]}
    missing = []
    for arm in subject["arms"]:
        for seed in subject["seeds"]:
            run = os.path.join(root, subject["results"],
                               D.run_dir_name(subject, arm, seed))
            if not os.path.isdir(run):
                stats[arm]["missing"] += 1
                missing.append(run)
                continue
            stats[arm]["runs"] += 1
            members = run_members(run, subject["members"])
            if not members:
                stats[arm]["empty"] += 1
            for rel, spec in members:
                key = json.dumps(spec, sort_keys=True)
                node = nodes.get(key)
                if node is None:
                    node = nodes[key] = {"node": len(nodes), "arms": [],
                                         "n": {}, "members": [], "spec": spec}
                if arm not in node["arms"]:
                    node["arms"].append(arm)
                    node["arms"].sort()
                node["n"][arm] = node["n"].get(arm, 0) + 1
                node["members"].append([arm, seed, rel])
                stats[arm]["members"] += 1
    if missing and not allow_missing:
        raise D.DesignError(
            f"{subject['name']}: {len(missing)} run director(ies) missing, "
            f"first {missing[0]}. Collect every host first, or pass "
            f"--allow-missing to pool what is there.")
    return list(nodes.values()), stats


def write_jsonl_gz(path: str, nodes: list) -> str:
    body = "".join(json.dumps(n, sort_keys=True) + "\n" for n in nodes).encode()
    tmp = path + ".part"
    with open(tmp, "wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as gz:
            gz.write(body)
    os.replace(tmp, path)
    return hashlib.sha256(body).hexdigest()


INDEX_HEADER = ["subject", "arm", "runs", "runs_missing", "runs_empty",
                "members", "nodes", "nodes_total", "sha256_jsonl"]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("design")
    ap.add_argument("out")
    ap.add_argument("--root", default=".",
                    help="directory the design's results paths are under")
    ap.add_argument("--subjects", nargs="*", default=None)
    ap.add_argument("--allow-missing", action="store_true")
    args = ap.parse_args(argv)
    try:
        design = D.load_design(args.design)
    except D.DesignError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    names = args.subjects or list(design["subjects"])
    unknown = [s for s in names if s not in design["subjects"]]
    if unknown:
        print(f"error: the design has no subject {', '.join(unknown)}",
              file=sys.stderr)
        return 2
    os.makedirs(args.out, exist_ok=True)
    index_path = os.path.join(args.out, "index.csv")
    rows = []
    if os.path.exists(index_path):
        with open(index_path, newline="") as handle:
            rows = [r for r in csv.DictReader(handle) if r["subject"] not in names]
    failed = False
    for name in names:
        subject = design["subjects"][name]
        try:
            nodes, stats = pool(args.root, subject, args.allow_missing)
        except D.DesignError as exc:
            print(f"error: {exc}", file=sys.stderr)
            failed = True
            continue
        sha = write_jsonl_gz(os.path.join(args.out, f"{name}.jsonl.gz"), nodes)
        for arm in subject["arms"]:
            s = stats[arm]
            rows.append({"subject": name, "arm": arm, "runs": s["runs"],
                         "runs_missing": s["missing"], "runs_empty": s["empty"],
                         "members": s["members"],
                         "nodes": sum(arm in n["arms"] for n in nodes),
                         "nodes_total": len(nodes), "sha256_jsonl": sha})
        print(f"{name}: {len(nodes)} nodes from "
              f"{sum(s['members'] for s in stats.values())} members", flush=True)
    rows.sort(key=lambda r: (r["subject"], r["arm"]))
    with open(index_path, "w", newline="") as handle:
        w = csv.DictWriter(handle, INDEX_HEADER)
        w.writeheader()
        w.writerows(rows)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
