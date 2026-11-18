"""Helpers that several scripts here share.

Each one had a copy in every script that used it. A difference between the
copies is a parameter here, so a caller states it.

Campaign archives vendor their scripts verbatim, so a script copied into
`experiments/<campaign>/scripts/` must take this file with it.
"""

import argparse
import gzip
import json
import os
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def write_manifest(path: str | Path, manifest: dict, *,
                   make_parents: bool = True) -> None:
    """Write `manifest` as JSON to `path` through a rename, so a reader never
    sees half of it. `make_parents` creates the directory first."""
    path = str(path)
    if make_parents:
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as handle:
        handle.write(json.dumps(manifest, indent=2) + "\n")
    os.replace(tmp, path)


def git_field(*argv: str) -> str:
    """One line of git output about this checkout, or "?" when git fails."""
    try:
        proc = subprocess.run(["git", "-C", str(REPO_ROOT), *argv],
                              capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return "?"
    return proc.stdout.strip() or "?"


def load_nodes(members_dir: str, subject: str) -> list:
    """The pooled nodes of `subject`, one JSON object a line of
    `<members_dir>/<subject>.jsonl.gz` (scripts/pool_members.py)."""
    with gzip.open(os.path.join(members_dir, f"{subject}.jsonl.gz"),
                   "rt") as handle:
        return [json.loads(line) for line in handle if line.strip()]


def lean_output(args: argparse.Namespace, lean_file: str) -> str:
    """The output of `lean_file` in proofs/, from `args.lean_output` when
    given, else from `lake env lean --run`, which needs Lean."""
    if args.lean_output is not None:
        return args.lean_output.read_text()
    return subprocess.run(
        ["lake", "env", "lean", "--run", lean_file],
        cwd=REPO_ROOT / "proofs",
        capture_output=True,
        text=True,
        check=True,
        timeout=1800,
    ).stdout
