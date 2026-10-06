#!/usr/bin/env python3
"""Run `compare` on each planned pair of repairs, one pair per call.

    python3 scripts/compare_pairs.py PAIRS.csv OUT.csv [--jobs 28] \\
        [--black-timeout 300] [--wall-timeout 700] [--vmem-kb 8000000] \\
        [--manifest experiments/compare-manifests/NAME.json]

What a `kind = "compare"` phase in campaign.toml runs. PAIRS.csv has the
columns id,a_path,b_path. Each pair goes to one `compare --repairs <a>
--ideals <b> --timeout BLACK` call, through two temporary directories holding
one symlink each, under `ulimit -v` and an outer `timeout WALL`. One row
id,relation,rc,secs is appended to OUT.csv per pair. `relation` is a's
relation to b: `weaker` means b strictly implies a. A pair whose output names
no relation is `error`, and one the outer timeout killed is `undecided`.

Resumable: an id already in OUT.csv is skipped. The exit status is 0 only when
every id in PAIRS.csv is in OUT.csv once the pass ends, so a tick never marks a
killed or partial pass done.

A port of experiments/2026-10-01-fretish-mixed/diversity/subsumption/run.py
with the same output. The flags default to the environment variables that
script read (JOBS, BLACK_T, WALL_T, VMEM_KB, CMP), and the binaries default to
this checkout's build-release, which is the build a tick has just verified.

Stdlib only, on python 3.10: this runs on the lab hosts from a cron tick.
"""

import argparse
import csv
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD = REPO_ROOT / "build-release"
THIRD_PARTY = BUILD / "third_party"

# The values a compare phase takes when campaign.toml omits a key. campaign.py
# reads them from here, so the declaration and the CLI cannot disagree.
DEFAULTS = {
    "jobs": 28,
    "black_timeout": 300,
    "wall_timeout": 700,
    "vmem_kb": 8000000,
}

# compare's per-repair verdicts, by prefix, to the relation recorded.
KEYS = (("equivalent", "equivalent"), ("strictly weaker", "weaker"),
        ("strictly stronger", "stronger"), ("incomparable", "incomparable"),
        ("timeout", "undecided"))
TIMEOUT_RC = 124
PROGRESS_EVERY = 2000


def relation_of(stdout: str, rc: int) -> str:
    rel = "error"
    for line in stdout.splitlines():
        if " : " in line and not line.startswith("Summary"):
            s = line.split(" : ", 1)[1]
            rel = next((r for k, r in KEYS if s.startswith(k)), "error")
    if rc == TIMEOUT_RC:
        rel = "undecided"
    return rel


def compare(a: str, b: str, args, env: dict, tmp: str) -> tuple:
    d = tempfile.mkdtemp(dir=tmp)
    try:
        for sub, src in (("a", a), ("b", b)):
            os.mkdir(f"{d}/{sub}")
            # Absolute, so a relative path in the pair list still resolves
            # from inside the temporary directory.
            src = os.path.abspath(src)
            os.symlink(src, f"{d}/{sub}/{os.path.basename(src)}")
        cmd = (f"ulimit -v {args.vmem_kb}; exec timeout {args.wall_timeout} "
               f"{args.compare} --repairs {d}/a --ideals {d}/b "
               f"--timeout {args.black_timeout}")
        t = time.time()
        p = subprocess.run(["bash", "-c", cmd], env=env, capture_output=True,
                           text=True)
        return (relation_of(p.stdout, p.returncode), p.returncode,
                round(time.time() - t, 2))
    finally:
        shutil.rmtree(d, ignore_errors=True)


def read_ids(path: str) -> set:
    if not os.path.exists(path):
        return set()
    with open(path, newline="") as handle:
        return {r["id"] for r in csv.DictReader(handle)}


def compare_version(binary: str) -> dict:
    """`compare --version` as key=value fields; empty where it will not say."""
    try:
        proc = subprocess.run([binary, "--version"], capture_output=True,
                              text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return {}
    out = {}
    for line in proc.stdout.splitlines():
        key, sep, value = line.strip().partition("=")
        if sep:
            out[key.strip()] = value.strip()
    return out


def git_field(*argv: str) -> str:
    try:
        proc = subprocess.run(["git", "-C", str(REPO_ROOT), *argv],
                              capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return "?"
    return proc.stdout.strip() or "?"


def write_manifest(path: str, manifest: dict) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as handle:
        handle.write(json.dumps(manifest, indent=2) + "\n")
    os.replace(tmp, path)


def parse_args(argv=None) -> argparse.Namespace:
    env = os.environ
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pairs", help="CSV with columns id,a_path,b_path")
    parser.add_argument("out", help="CSV to append id,relation,rc,secs to")
    parser.add_argument("--jobs", type=int,
                        default=int(env.get("JOBS", DEFAULTS["jobs"])))
    parser.add_argument("--black-timeout", type=int,
                        default=int(env.get("BLACK_T",
                                            DEFAULTS["black_timeout"])),
                        help="compare --timeout, per solver call (s)")
    parser.add_argument("--wall-timeout", type=int,
                        default=int(env.get("WALL_T",
                                            DEFAULTS["wall_timeout"])),
                        help="outer timeout per pair (s)")
    parser.add_argument("--vmem-kb", type=int,
                        default=int(env.get("VMEM_KB", DEFAULTS["vmem_kb"])),
                        help="ulimit -v per pair (KB)")
    parser.add_argument("--compare", default=env.get("CMP",
                                                     str(BUILD / "compare")),
                        help="the compare binary (default: this checkout's "
                             "build-release/compare)")
    parser.add_argument("--manifest", default=None,
                        help="write a JSON manifest here, for "
                             "`campaign.py status`")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    src, out = args.pairs, args.out
    env = dict(os.environ)
    # Point compare at the checkout's solvers only where they sit at the
    # source-built layout. Where they do not (av1 unpacks black from a .deb
    # into third_party/black/black), compare's built-in paths already name
    # this build's copies.
    for var, path in (("PEREDUR_BLACK_PATH", THIRD_PARTY / "black" / "install" / "bin" / "black"),
                      ("PEREDUR_SPOT_BIN_DIR", THIRD_PARTY / "spot" / "bin")):
        if path.exists():
            env.setdefault(var, str(path))
    tmp = os.environ.get("COMPARE_PAIRS_TMP") or tempfile.gettempdir()
    os.makedirs(tmp, exist_ok=True)

    with open(src, newline="") as handle:
        rows = list(csv.DictReader(handle))
    wanted = {r["id"] for r in rows}
    done = read_ids(out)
    todo = [r for r in rows if r["id"] not in done]
    if os.path.dirname(out):
        os.makedirs(os.path.dirname(out), exist_ok=True)
    new = not os.path.exists(out)
    fh = open(out, "a", newline="")
    w = csv.writer(fh)
    if new:
        w.writerow(["id", "relation", "rc", "secs"])
        fh.flush()

    manifest = None
    if args.manifest:
        manifest = {
            "kind": "compare",
            "hostname": socket.gethostname(),
            "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "finished": None,
            "pairs": src,
            "pairs_resolved": os.path.abspath(src),
            "out": out,
            "out_resolved": os.path.abspath(out),
            "pairs_total": len(wanted),
            "done_at_start": len(done & wanted),
            "jobs": args.jobs,
            "black_timeout": args.black_timeout,
            "wall_timeout": args.wall_timeout,
            "vmem_kb": args.vmem_kb,
            "binaries": {"compare": {"path": args.compare,
                                     **compare_version(args.compare)}},
            "git": {"branch": git_field("rev-parse", "--abbrev-ref", "HEAD"),
                    "head": git_field("rev-parse", "HEAD")},
        }
        write_manifest(args.manifest, manifest)

    lock, count, start = threading.Lock(), [0], time.time()
    print(f"{len(todo)} pairs to run, {len(done)} done", flush=True)

    def one(r):
        # A pair that raises gets no row, so the final check reports the pass
        # incomplete and a rerun retries it; the other pairs carry on.
        try:
            res = compare(r["a_path"], r["b_path"], args, env, tmp)
        except Exception as exc:  # noqa: BLE001
            print(f"pair {r['id']}: {exc!r}", file=sys.stderr, flush=True)
            return
        with lock:
            w.writerow([r["id"], *res])
            fh.flush()
            count[0] += 1
            if count[0] % PROGRESS_EVERY == 0:
                print(f"{count[0]}/{len(todo)} {time.time() - start:.0f}s",
                      flush=True)

    try:
        with ThreadPoolExecutor(args.jobs) as ex:
            list(ex.map(one, todo))
    finally:
        fh.close()
    print(f"done {time.time() - start:.0f}s", flush=True)

    missing = wanted - read_ids(out)
    if manifest is not None:
        manifest["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
        manifest["missing"] = len(missing)
        write_manifest(args.manifest, manifest)
    if missing:
        print(f"incomplete: {len(missing)} of {len(wanted)} id(s) not in "
              f"{out}", file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
