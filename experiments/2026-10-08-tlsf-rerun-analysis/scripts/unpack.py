#!/usr/bin/env python3
"""Restore the committed work-tree bundles on a host; run by the build step.

    python3 experiments/2026-10-08-tlsf-rerun-analysis/scripts/unpack.py

Each bundle in data/bundles.json is checked against its sha256 and extracted
into experiments/analysis-tlsf-rerun-work/, then stamped there, so a later
build with the same bundle does nothing. Extraction only writes the planned
inputs: a phase's outputs (result.json, the walk cache, compare's CSVs) are
never in a bundle and are left alone. Run from the checkout root.

Stdlib only, on python 3.10.
"""
import hashlib
import json
import os
import sys
import tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), "data")
WORK = os.path.join("experiments", "analysis-tlsf-rerun-work")


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def safe_members(tar: tarfile.TarFile):
    for m in tar.getmembers():
        name = os.path.normpath(m.name)
        if name.startswith(("/", "..")) or not (m.isfile() or m.isdir()):
            raise SystemExit(f"refusing bundle member {m.name!r}")
        yield m


def main() -> int:
    with open(os.path.join(DATA, "bundles.json")) as fh:
        bundles = json.load(fh)
    os.makedirs(WORK, exist_ok=True)
    for name, digest in sorted(bundles.items()):
        stamp = os.path.join(WORK, f".unpacked-{name}")
        if os.path.exists(stamp) and open(stamp).read().strip() == digest:
            continue
        path = os.path.join(DATA, name)
        got = sha256(path)
        if got != digest:
            print(f"unpack: {name} has sha256 {got}, bundles.json says {digest}", file=sys.stderr)
            return 1
        with tarfile.open(path, "r:xz") as tar:
            tar.extractall(WORK, members=safe_members(tar))
        with open(stamp, "w") as fh:
            fh.write(digest + "\n")
        print(f"unpack: {name} -> {WORK}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
