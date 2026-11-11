#!/usr/bin/env python3
"""Pool each subject's members over its seeds and arms into nodes.

    python3 scripts/pool_members.py DESIGN.toml OUT_DIR [--root .] \\
        [--subjects SUBJECT... | --subjects-file FILE] [--copy-from DIR] \\
        [--allow-missing] [--manifest experiments/compare-manifests/NAME.json]

A pool needs every seed of a subject. Where the hosts split a subject's seeds,
run this locally after `campaign.py collect` and commit the output. Where one
host runs every seed of a subject, a `kind = "pool"` campaign phase runs it
there, with --copy-from naming the committed pool: a subject already in
that directory is copied byte for byte with its index rows, and only the
others are pooled from results. The phase writes under an ignored directory,
so the checkout stays clean for the next stage.

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
since a pool built from half the seeds reads as a complete one. So does one
without run.json, which the search writes last: a killed run leaves its
directory behind without it. A finished run directory with no members (no
maximal.tsv, or no repair) contributes nothing and is counted.

Writes OUT_DIR/<subject>.jsonl.gz, one node a line:

    {"node": 0, "arms": ["nsga2-apportion/base"], "n": {"nsga2-apportion/base": 2},
     "members": [["nsga2-apportion/base", 0, "accumulated/gen03_0011.json"], ...],
     "spec": {...}}

`members` lists every (arm, seed, file) holding the node, the file relative
to its run directory. The gzip header carries no name or mtime, so the same
inputs give the same bytes. Also writes OUT_DIR/index.csv with one row per
subject and arm, rewritten after each subject, and one progress row per
finished subject to OUT_DIR/done-<subjects file stem>.csv (done.csv with
--subjects), which a --manifest names for `campaign.py status`. Exits 0 only
when every subject is pooled or copied.
"""

import argparse
import csv
import gzip
import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time

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
            if not os.path.isfile(os.path.join(run, "run.json")):
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
            f"{subject['name']}: {len(missing)} run director(ies) missing or "
            f"unfinished (no run.json), first {missing[0]}. Finish or collect "
            f"every seed first, or pass --allow-missing to pool what is there.")
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


def read_index(path: str) -> list:
    if not os.path.exists(path):
        return []
    with open(path, newline="") as handle:
        return list(csv.DictReader(handle))


def write_index(path: str, rows: list) -> None:
    rows = sorted(rows, key=lambda r: (r["subject"], r["arm"]))
    tmp = path + ".part"
    with open(tmp, "w", newline="") as handle:
        w = csv.DictWriter(handle, INDEX_HEADER, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    os.replace(tmp, path)


def copy_pool(subject: dict, src_dir: str, out: str) -> tuple:
    """A committed pool, copied byte for byte, and its index rows.

    Refused unless the source index has one row for each of the subject's
    design arms and no other, and its checksum matches the copied body, so a
    pool built under another design, or a truncated file, never passes as
    this one.
    """
    name = subject["name"]
    src = os.path.join(src_dir, f"{name}.jsonl.gz")
    rows = [r for r in read_index(os.path.join(src_dir, "index.csv"))
            if r["subject"] == name]
    if sorted(r["arm"] for r in rows) != sorted(subject["arms"]):
        raise D.DesignError(
            f"{name}: {src_dir}/index.csv lists arms "
            f"{sorted(r['arm'] for r in rows)}, the design "
            f"{sorted(subject['arms'])}")
    with gzip.open(src, "rb") as handle:
        sha = hashlib.sha256(handle.read()).hexdigest()
    if any(r["sha256_jsonl"] != sha for r in rows):
        raise D.DesignError(f"{name}: {src} does not match the checksum "
                            f"{src_dir}/index.csv records")
    dest = os.path.join(out, f"{name}.jsonl.gz")
    shutil.copyfile(src, dest + ".part")
    os.replace(dest + ".part", dest)
    return rows, int(rows[0]["nodes_total"]), sha


def write_manifest(path: str, manifest: dict) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as handle:
        handle.write(json.dumps(manifest, indent=2) + "\n")
    os.replace(tmp, path)


def checkout_head() -> dict:
    """The checkout's branch and commit, for status's provenance columns."""
    out = {}
    for key, argv in (("branch", ["rev-parse", "--abbrev-ref", "HEAD"]),
                      ("head", ["rev-parse", "HEAD"])):
        try:
            proc = subprocess.run(["git", *argv], capture_output=True,
                                  text=True, timeout=30)
            out[key] = proc.stdout.strip() or "?"
        except (OSError, subprocess.SubprocessError):
            out[key] = "?"
    return out


def subject_names(args, design: dict) -> tuple:
    """The subjects to pool, and the progress file's stem."""
    if args.subjects_file:
        with open(args.subjects_file) as handle:
            names = [ln.strip() for ln in handle
                     if ln.strip() and not ln.startswith("#")]
        stem = os.path.splitext(os.path.basename(args.subjects_file))[0]
        return names, f"done-{stem}"
    return args.subjects or list(design["subjects"]), "done"


def pool_one(args, subject: dict) -> tuple:
    """(index rows, nodes, members, sha, source) for one subject."""
    name = subject["name"]
    source = (args.copy_from if args.copy_from and os.path.exists(
        os.path.join(args.copy_from, f"{name}.jsonl.gz")) else None)
    if source:
        rows, n_nodes, sha = copy_pool(subject, source, args.out)
        return rows, n_nodes, sum(int(r["members"]) for r in rows), sha, source
    nodes, stats = pool(args.root, subject, args.allow_missing)
    sha = write_jsonl_gz(os.path.join(args.out, f"{name}.jsonl.gz"), nodes)
    rows = []
    for arm in subject["arms"]:
        s = stats[arm]
        rows.append({"subject": name, "arm": arm, "runs": s["runs"],
                     "runs_missing": s["missing"], "runs_empty": s["empty"],
                     "members": s["members"],
                     "nodes": sum(arm in n["arms"] for n in nodes),
                     "nodes_total": len(nodes), "sha256_jsonl": sha})
    return (rows, len(nodes), sum(s["members"] for s in stats.values()), sha,
            None)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("design")
    ap.add_argument("out")
    ap.add_argument("--root", default=".",
                    help="directory the design's results paths are under")
    pick = ap.add_mutually_exclusive_group()
    pick.add_argument("--subjects", nargs="*", default=None)
    pick.add_argument("--subjects-file", default=None,
                      help="text file, one subject a line")
    ap.add_argument("--copy-from", default=None,
                    help="committed pool directory; a subject found there is "
                         "copied rather than pooled")
    ap.add_argument("--allow-missing", action="store_true")
    ap.add_argument("--manifest", default=None)
    args = ap.parse_args(argv)
    try:
        design = D.load_design(args.design)
        names, stem = subject_names(args, design)
    except (OSError, D.DesignError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    unknown = [s for s in names if s not in design["subjects"]]
    if unknown:
        print(f"error: the design has no subject {', '.join(unknown)}",
              file=sys.stderr)
        return 2
    if args.copy_from and (os.path.abspath(args.copy_from)
                           == os.path.abspath(args.out)):
        print("error: --copy-from and OUT_DIR are one directory",
              file=sys.stderr)
        return 2
    os.makedirs(args.out, exist_ok=True)
    index_path = os.path.join(args.out, "index.csv")
    progress = os.path.join(args.out, f"{stem}.csv")
    rows = [r for r in read_index(index_path) if r["subject"] not in names]
    manifest = None
    if args.manifest:
        manifest = {
            "kind": "pool", "hostname": socket.gethostname(),
            "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "finished": None,
            "design": args.design, "subjects": args.subjects_file,
            "subject_list": names, "copy_from": args.copy_from,
            "out_dir": args.out,
            # status counts this file's rows against `planned`.
            "out": progress, "planned": len(names), "git": checkout_head()}
        write_manifest(args.manifest, manifest)
    # Pooling is cheap and deterministic, so a rerun redoes every subject
    # rather than trusting what a killed pass left.
    if os.path.exists(progress):
        os.remove(progress)
    failed = []
    for name in names:
        try:
            new_rows, n_nodes, n_members, sha, source = pool_one(
                args, design["subjects"][name])
        except (OSError, ValueError, D.DesignError) as exc:
            print(f"error: {exc}", file=sys.stderr, flush=True)
            failed.append(name)
            continue
        rows += new_rows
        write_index(index_path, rows)
        new = not os.path.exists(progress)
        with open(progress, "a", newline="") as handle:
            w = csv.writer(handle)
            if new:
                w.writerow(["subject", "source", "nodes", "members",
                            "sha256_jsonl"])
            w.writerow([name, "copied" if source else "pooled", n_nodes,
                        n_members, sha])
        print(f"{name}: {n_nodes} nodes from {n_members} members"
              + (f" (copied from {source})" if source else ""), flush=True)
    write_index(index_path, rows)
    if manifest is not None:
        manifest["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
        manifest["missing"] = len(failed)
        write_manifest(args.manifest, manifest)
    if failed:
        print(f"incomplete: {', '.join(failed)}", file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
