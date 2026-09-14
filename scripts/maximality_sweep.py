#!/usr/bin/env python3
"""Run PEREDUR's TLSF maximality filter over both arms of the head-to-head.

The comparison this exists for is diversity, not yield. AuRUS emits every
candidate its search accepted -- a median of 490 per repeat, all syntactically
distinct -- while PEREDUR emits the maximal antichain of its final population,
so the raw counts are not measuring the same thing. Putting both arms through
the same filter makes them comparable, and running it on PEREDUR's own output
is also the idempotence check: PEREDUR's pipeline already applied it, so
`maximal` must equal `distinct` on every PEREDUR row.

Two numbers per directory, because they answer different questions:

  maximal  -- specs no other spec strictly dominates. Whole equivalence classes
              survive together, which is the filter PEREDUR runs.
  classes  -- those survivors quotiented by mutual implication. The count of
              genuinely distinct strongest repairs.

AuRUS's own MaximalSolutions filter reports the second while looking like the
first: it marks `to` subsumed on any `from => to`, so each equivalence class
collapses to one member, and which member depends on execution order.
"""
import argparse
import csv
import pathlib
import re
import subprocess
import sys
import time

SUMMARY = re.compile(r"^(files|distinct|maximal|classes|unparsed)\s+(\d+)$", re.M)

# The PEREDUR arm's run directories are named by gen_configs.py's factor cross.
# Only the family and seed matter here; the rest is constant across this
# campaign and is dropped rather than parsed.
PEREDUR_DIR = re.compile(r"^sweep_C_default_nsga2-apportion_log_(.+)_seed(\d+)$")

FIELDS = ["arm", "spec", "repeat", "files", "distinct", "maximal", "classes",
          "unparsed", "wall_s", "status"]


def measure(binary, directory, jobs, timeout_s, wall_cap_s):
    started = time.monotonic()
    try:
        proc = subprocess.run(
            [binary, str(directory), "--jobs", str(jobs),
             "--timeout", str(timeout_s)],
            capture_output=True, text=True, timeout=wall_cap_s)
    except subprocess.TimeoutExpired:
        return None, time.monotonic() - started, "wall-cap"
    wall = time.monotonic() - started
    if proc.returncode != 0:
        return None, wall, f"exit-{proc.returncode}"
    counts = {k: int(v) for k, v in SUMMARY.findall(proc.stdout)}
    if "maximal" not in counts:
        return None, wall, "unparsed-output"
    return counts, wall, "ok"


def aurus_dirs(root):
    for host in sorted(p.name for p in root.iterdir() if p.is_dir()):
        for spec in sorted(p for p in (root / host).iterdir() if p.is_dir()):
            for repeat in sorted(p for p in spec.iterdir() if p.is_dir()):
                if any(repeat.glob("*.tlsf")):
                    yield spec.name, f"{host}/{repeat.name}", repeat


def peredur_dirs(root):
    for run in sorted(p for p in root.iterdir() if p.is_dir()):
        match = PEREDUR_DIR.match(run.name)
        if match is None or not any(run.glob("*.tlsf")):
            continue
        yield match.group(1), f"seed{match.group(2)}", run


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default="build-release/maximal")
    ap.add_argument("--aurus-raw", type=pathlib.Path,
                    help="raw/ of the AuRUS arm, holding <host>/<spec>/repeat-NN")
    ap.add_argument("--peredur-results", type=pathlib.Path,
                    help="results-aurus-h2h/ of the PEREDUR arm")
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--timeout", type=int, default=20,
                    help="per-black-call budget, seconds")
    # A per-directory wall cap rather than a per-call one: a single hard family
    # can otherwise hold the sweep for hours, and a capped row is recoverable
    # (re-run that directory alone) where a lost sweep is not.
    ap.add_argument("--wall-cap", type=int, default=1800,
                    help="per-directory wall budget, seconds")
    ap.add_argument("--only", help="restrict to families matching this regex")
    args = ap.parse_args()

    only = re.compile(args.only) if args.only else None
    work = []
    if args.aurus_raw:
        work += [("aurus", *item) for item in aurus_dirs(args.aurus_raw)]
    if args.peredur_results:
        work += [("peredur", *item) for item in peredur_dirs(args.peredur_results)]
    if only:
        work = [w for w in work if only.search(w[1])]
    if not work:
        sys.exit("nothing to measure")

    # Appended rather than rewritten, and pre-read for its keys, so an
    # interrupted sweep resumes instead of re-paying for what it already did.
    done = set()
    if args.out.exists():
        with args.out.open() as handle:
            done = {(r["arm"], r["spec"], r["repeat"])
                    for r in csv.DictReader(handle)}
    fresh = not args.out.exists()
    with args.out.open("a", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        if fresh:
            writer.writeheader()
        for i, (arm, spec, repeat, directory) in enumerate(work, 1):
            if (arm, spec, repeat) in done:
                continue
            counts, wall, status = measure(
                args.binary, directory, args.jobs, args.timeout, args.wall_cap)
            row = {"arm": arm, "spec": spec, "repeat": repeat,
                   "wall_s": f"{wall:.1f}", "status": status}
            for key in ("files", "distinct", "maximal", "classes", "unparsed"):
                row[key] = (counts or {}).get(key, "")
            writer.writerow(row)
            handle.flush()
            print(f"[{i}/{len(work)}] {arm} {spec} {repeat} {status} "
                  f"{row['maximal']}/{row['distinct']} in {wall:.0f}s",
                  flush=True)


if __name__ == "__main__":
    main()
