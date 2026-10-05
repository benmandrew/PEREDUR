#!/usr/bin/env python3
"""Run GLASS, JVTS-Repair and AMT13 over the GR(1) subjects.

The three are the assumption-repair algorithms Brizzio et al. (2023) set
AuRUS against, from the artifact of Maoz, Ringert and Shalom (ICSE 2019,
SymbolicRepairsArtifact.zip). The artifact names them by code, and so does
this script: UF is GLASS, BFS is JVTS-Repair and ALUR is AMT13.

Each (algorithm, spec) job runs StreamRepairs, the driver beside the inputs
in experiments/2026-10-02-maoz-baselines/driver/. It builds each algorithm
with the arguments the artifact's own RepairExporterExec passes, and prints
each repair the moment the algorithm records it, dated from the start of the
search. The exporter prints only when the search ends, so a job stopped at
the cap would otherwise leave nothing behind.

`--maoz-root` is the staged directory: the artifact's RepairExporterExec.jar
and its RepairExporterExec_lib/, StreamRepairs.java with its compiled
classes/, and DIGEST.txt. The digest is the first 12 hex digits of the
SHA-256 of the jar's bytes followed by the driver source's bytes. This
script recomputes it and refuses a directory whose files do not match its
DIGEST.txt or the campaign's `--maoz-digest`.

All three algorithms are deterministic, so one run per job is the design and
the only seed is 0. Each job writes <out-root>/<ALG>/<spec>/: stream.txt (the
driver's stdout), stderr.log and result.json. A job with a result.json is not
re-run, unless the JVM ran out of heap and `--heap-gb` is now larger than the
heap that job had. Such a job's directory moves to <out-root>.oom-<N>g/ first,
so the failed attempt is kept and the adapter never sees it. The campaign's facts go to <out-root>/maoz-manifest-<host>.json,
which is what `campaign.py status` reads.

The driver prints a repair only when its assumption set is new, and stops by
itself at `--max-repairs` distinct repairs (default 1000, AuRUS's
1000-individual stop). A job still running at `--timeout` seconds is stopped
by killing its process group. Every repair printed before either stop is
kept, which is what the found-repair rule counts: a repair held at the stop.

Usage:
    python scripts/maoz_campaign.py --maoz-root ~/tools/maoz-icse2019 \\
        --maoz-digest 0123456789ab --out-root OUT \\
        --inputs experiments/2026-10-02-maoz-baselines/inputs \\
        --specs lift --algorithms UF --timeout 60 --max-repairs 50  # smoke test
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ALGORITHMS = {"UF": "GLASS", "BFS": "JVTS-Repair", "ALUR": "AMT13"}
JAR = "RepairExporterExec.jar"
DRIVER = "StreamRepairs.java"
DIGEST_FILE = "DIGEST.txt"
MANIFEST_STEM = "maoz-manifest"
RESULT = "result.json"
STREAM = "stream.txt"
# The heap each JVM may take, in GB. Ten at once stay under 110 GB on av2
# and av3, which have 125 GB each. A result.json written before the flag
# existed ran with this heap.
HEAP_GB = 10
OOM_MARK = "java.lang.OutOfMemoryError"
RSS_SAMPLE_S = 5.0

REPAIR_RE = re.compile(r"^@@REPAIR (\d+) (\d+) (-?\d+)$", re.MULTILINE)
# @@DONE and @@CAP carry <distinct> <raw> <ms>: the repairs printed, the
# recordRepair calls behind them, and the search time.
DONE_RE = re.compile(r"^@@DONE (\d+) (\d+) (\d+)$", re.MULTILINE)
CAP_RE = re.compile(r"^@@CAP (\d+) (\d+) (\d+)$", re.MULTILINE)


def digest(root: Path) -> str:
    h = hashlib.sha256()
    h.update((root / JAR).read_bytes())
    h.update((root / DRIVER).read_bytes())
    return h.hexdigest()[:12]


def checkout_git() -> dict:
    """The branch and head of the PEREDUR checkout this runs from, which
    `campaign.py status` uses to tell a current manifest from a stale one."""
    def ask(*command: str) -> str:
        try:
            return subprocess.run(command, check=True, capture_output=True,
                                  text=True,
                                  cwd=Path(__file__).parent).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            return "?"
    return {"branch": ask("git", "rev-parse", "--abbrev-ref", "HEAD"),
            "head": ask("git", "rev-parse", "HEAD")}


def peak_rss_mb(pid: int) -> float:
    try:
        with open(f"/proc/{pid}/status") as handle:
            for line in handle:
                if line.startswith("VmHWM:"):
                    return int(line.split()[1]) / 1024.0
    except (OSError, ValueError, IndexError):
        pass
    return 0.0


def parse_stream(text: str) -> dict:
    """The counts and times a job's stream records."""
    times = [int(m.group(2)) / 1000.0 for m in REPAIR_RE.finditer(text)]
    done = DONE_RE.search(text)
    cap = CAP_RE.search(text)
    end = done or cap
    return {"n_repairs": len(times),
            "n_distinct": len(times),
            # Only an ending line knows the raw count; a killed job has none.
            "n_raw": int(end.group(2)) if end else None,
            "capped": cap is not None,
            "first_repair_s": times[0] if times else None,
            "last_repair_s": times[-1] if times else None,
            "finished": done is not None,
            "realizable": "@@REALIZABLE" in text,
            "search_s": int(end.group(3)) / 1000.0 if end else None}


def out_of_heap(job: Path) -> int | None:
    """The heap in GB a finished job ran out of, or None if it did not."""
    try:
        record = json.loads((job / RESULT).read_text())
        stderr = (job / "stderr.log").read_text(errors="replace")
    except (OSError, ValueError):
        return None
    if record.get("killed") or record.get("exit_code") == 0 \
            or OOM_MARK not in stderr:
        return None
    return int(record.get("heap_gb", HEAP_GB))


def run_one(root: Path, alg: str, spectra: Path, job: Path,
            timeout: int, max_repairs: int, heap_gb: int) -> dict:
    """Run one job; return its result record."""
    job.mkdir(parents=True, exist_ok=True)
    cmd = ["java", f"-Xmx{heap_gb}g", "-cp", f"classes{os.pathsep}{JAR}",
           "StreamRepairs", str(spectra), alg, "-1", str(max_repairs)]
    killed = 0
    rss = {"peak": 0.0}
    stop = threading.Event()
    t0 = time.monotonic()
    with open(job / STREAM, "wb") as out, open(job / "stderr.log", "wb") as err:
        proc = subprocess.Popen(cmd, cwd=root, stdout=out, stderr=err,
                                start_new_session=True)

        def sample() -> None:
            while not stop.wait(RSS_SAMPLE_S):
                rss["peak"] = max(rss["peak"], peak_rss_mb(proc.pid))

        threading.Thread(target=sample, daemon=True).start()
        try:
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            killed = 1
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            proc.wait()
    stop.set()
    wall = round(time.monotonic() - t0, 2)
    record = {"algorithm": alg, "tool": ALGORITHMS[alg],
              "spec": spectra.stem, "input": str(spectra),
              "wall_s": wall, "killed": killed, "exit_code": proc.returncode,
              "timeout_s": timeout, "max_repairs": max_repairs,
              "heap_gb": heap_gb, "peak_rss_mb": round(rss["peak"], 1)}
    record.update(parse_stream((job / STREAM).read_text(errors="replace")))
    return record


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--maoz-root", type=Path, required=True,
                        metavar="PATH", help="the staged artifact directory")
    parser.add_argument("--maoz-digest", required=True, metavar="HEX",
                        help="the digest the staged directory must have")
    parser.add_argument("--out-root", type=lambda p: Path(p).resolve(),
                        required=True, metavar="PATH")
    parser.add_argument("--inputs", type=lambda p: Path(p).resolve(),
                        required=True, metavar="DIR",
                        help="directory of <spec>.spectra inputs")
    parser.add_argument("--specs", nargs="+", metavar="SPEC",
                        help="specs to run (default: every input)")
    parser.add_argument("--algorithms", nargs="+", choices=list(ALGORITHMS),
                        default=list(ALGORITHMS), metavar="ALG")
    parser.add_argument("--timeout", type=int, default=7200, metavar="S")
    parser.add_argument("--concurrency", type=int, default=8, metavar="N")
    parser.add_argument("--max-repairs", type=int, default=1000, metavar="N",
                        help="stop a job at this many distinct repairs "
                             "(AuRUS stops at 1000 individuals)")
    parser.add_argument("--heap-gb", type=int, default=HEAP_GB, metavar="N",
                        help="each JVM's -Xmx; a job that ran out of a "
                             "smaller heap is run again")
    parser.add_argument("--seeds", nargs="+", type=int, default=[0],
                        metavar="N",
                        help="accepted from campaign.py; the algorithms are "
                             "deterministic, so only seed 0 is meaningful")
    parser.add_argument("--adapt", type=Path, metavar="DIR",
                        help="run maoz_adapt.py over --out-root into DIR "
                             "once every job has finished")
    args = parser.parse_args()

    if args.seeds != [0]:
        sys.exit("the three algorithms are deterministic: declare seed 0 only")
    if min(args.concurrency, args.timeout, args.max_repairs,
           args.heap_gb) < 1:
        sys.exit("--concurrency, --timeout, --max-repairs and --heap-gb "
                 "must be positive")
    root = args.maoz_root.expanduser().resolve()
    for need in (JAR, DRIVER, "classes/StreamRepairs.class"):
        if not (root / need).exists():
            sys.exit(f"not a staged artifact: {root / need} missing")
    staged = (root / DIGEST_FILE).read_text().split()[0]
    actual = digest(root)
    if not (staged == actual == args.maoz_digest):
        sys.exit(f"{root}: files hash to {actual}, {DIGEST_FILE} says "
                 f"{staged} and the campaign declares {args.maoz_digest}")

    specs = args.specs or sorted(p.stem for p in args.inputs.glob("*.spectra"))
    for spec in specs:
        if not (args.inputs / f"{spec}.spectra").exists():
            sys.exit(f"no input {args.inputs / spec}.spectra")
    # Algorithm-major with GLASS first: it takes milliseconds per spec, so a
    # status poll sees a whole tool's column early.
    tasks = [(a, s) for a in args.algorithms for s in specs]
    retry = []
    for a, s in tasks:
        old = out_of_heap(args.out_root / a / s)
        if old is not None and old < args.heap_gb:
            retry.append((a, s, old))
    for a, s, old in retry:
        aside = args.out_root.with_name(f"{args.out_root.name}.oom-{old}g")
        (aside / a).mkdir(parents=True, exist_ok=True)
        (args.out_root / a / s).rename(aside / a / s)
        print(f"[retry] {a}/{s} ran out of a {old} GB heap; moved to "
              f"{aside / a / s}", flush=True)
    to_run = [(a, s) for a, s in tasks
              if not (args.out_root / a / s / RESULT).exists()]

    print(f"Maoz baselines: {len(args.algorithms)} algorithms x {len(specs)} "
          f"specs, {len(to_run)} to run, timeout {args.timeout}s, "
          f"concurrency {args.concurrency}, heap {args.heap_gb} GB, artifact {actual}", flush=True)

    args.out_root.mkdir(parents=True, exist_ok=True)
    host = os.uname().nodename
    manifest_path = args.out_root / f"{MANIFEST_STEM}-{host}.json"
    manifest = {
        "written_by": "scripts/maoz_campaign.py",
        "hostname": host,
        "git": checkout_git(),
        "maoz_root": str(root),
        "maoz_digest": actual,
        "out": str(args.out_root),
        "inputs": str(args.inputs),
        "timeout_s": args.timeout,
        "max_repairs": args.max_repairs,
        "heap_gb": args.heap_gb,
        "retried_out_of_heap": [f"{a}/{s}" for a, s, _ in retry],
        "concurrency": args.concurrency,
        "algorithms": list(args.algorithms),
        "specs": specs,
        "seeds": [0],
        "started": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "finished": None,
        "counts": {"planned": len(tasks), "to_run": len(to_run), "done": 0},
    }
    lock = threading.Lock()

    def write_manifest() -> None:
        part = manifest_path.with_suffix(".part")
        part.write_text(json.dumps(manifest, indent=2) + "\n")
        part.replace(manifest_path)

    write_manifest()
    t0 = time.monotonic()

    def execute(task: tuple[str, str]) -> None:
        alg, spec = task
        job = args.out_root / alg / spec
        with lock:
            print(f"[start] {alg}/{spec}", flush=True)
        record = run_one(root, alg, args.inputs / f"{spec}.spectra", job,
                         args.timeout, args.max_repairs, args.heap_gb)
        (job / RESULT).write_text(json.dumps(record, indent=2) + "\n")
        with lock:
            manifest["counts"]["done"] += 1
            write_manifest()
            note = (" KILLED" if record["killed"]
                    else " CAPPED" if record["capped"] else "")
            print(f"[{manifest['counts']['done']}/{len(to_run)}] {alg}/{spec} "
                  f"{record['n_repairs']} repairs in {record['wall_s']}s{note}",
                  flush=True)

    with ThreadPoolExecutor(max_workers=args.concurrency) as pool:
        list(pool.map(execute, to_run))

    manifest["finished"] = \
        dt.datetime.now().astimezone().isoformat(timespec="seconds")
    manifest["wall_s"] = round(time.monotonic() - t0, 1)
    write_manifest()

    if args.adapt is not None:
        adapt = [sys.executable, str(Path(__file__).parent / "maoz_adapt.py"),
                 "--root", str(args.out_root), "--inputs", str(args.inputs),
                 "--out", str(args.adapt), "--force"]
        print("$ " + " ".join(adapt), flush=True)
        rc = subprocess.run(adapt).returncode
        if rc != 0:
            sys.exit(f"maoz_adapt.py exited {rc}")


if __name__ == "__main__":
    main()
