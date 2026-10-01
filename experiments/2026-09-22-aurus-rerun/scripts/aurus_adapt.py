#!/usr/bin/env python3
"""Materialise AuRUS h2h output as PEREDUR-shaped run directories.

    python3 scripts/aurus_adapt.py --root ~/aurus-h2h-out \\
        --out experiments/results-aurus-curves [--seeds 0-14] [--copy]

`score_curves.py` reads one run directory per (spec, seed): an `accumulated/`
directory of candidate files, an `accumulated/index.tsv` dating each of them,
and `run.json` beside it naming the arm. AuRUS writes none of that, and how a
solution gets its time depends on which AuRUS wrote the repeat.

Upstream (3f6f01f) dumps `ga.solutions` in list order as spec<i>.tlsf when the
run ends, so no solution file carries a discovery time and the run.log is the
only record that does -- its per-iteration `#Sol` column is that list's length,
so spec_i was found at the first iteration whose `#Sol` reaches i+1, dated to
the second by the `Elapsed Time` line that follows.

The fork at `output-on-timeout` (e1cfadf) writes each solution as it is found
and dates it in `solution-times.csv` to the microsecond, which is the run's own
reading rather than this side's inference. score_aurus_anytime.py holds both
readers and this imports them rather than restating either, so one tree may mix
the two vintages and each repeat is dated by its own best record.

What comes out is a results directory that score_campaign.py scores with no
flag of its own: run directories named `aurus_<spec>_seed<NN>`, which is what
its seed split, its smallest-first queue and its resume already key on, and
what a `kind = "score"` phase in campaign.toml points its `results` key at.
Both arms of a head-to-head then run through one scorer under one set of
budgets, which is what makes the two sets of curves comparable at all. A
repeat maps to a seed, so `hosts = { av2 = "0-14", av3 = "15-29" }` splits the
30 repeats the way the campaign's own split already reads.

Under upstream AuRUS a repeat killed at its 7200 s cap wrote no solution files
at all -- they die with the JVM -- and such a repeat is skipped by name rather
than materialised empty: an empty run directory scores as a run that found
nothing, which is a different claim from a run whose output was lost. The fork
keeps what it found, so a killed repeat now materialises like any other and its
manifest reads `stopped_by = "deadline"` against `"individuals"` for a run that
reached its own budget. A solution file that has no time on either record is
still skipped, since the alternative is inventing a discovery time for it.
Every skip is counted in the summary, which is written to
`<out>/aurus-adapt.json` alongside the tree.

The candidate files are symlinked. There are 287,006 of them across the two
lab hosts and the pass runs on the host that holds them; `--copy` makes the
tree self-contained where it has to move instead.
"""

import argparse
import csv
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import score_aurus_anytime as A  # noqa: E402

REPEAT_DIR = re.compile(r"^repeat-(\d+)$")
SPEC_FILE = re.compile(r"^spec(\d+)\.tlsf$")
RESULTS_CSV = "aurus_results.csv"
SUMMARY_NAME = "aurus-adapt.json"
ACCUMULATED_DIR = "accumulated"
INDEX_NAME = "index.tsv"
MANIFEST_NAME = "run.json"

# AuRUS stops when 1000 individuals have been bred; its own 7200 s deadline
# fires in none of the 780 archived logs, so a run that overran was killed by
# the harness instead. The vocabulary is PEREDUR's `SearchBudget`, so a curve
# CSV reads the same in both arms.
STOPPED_BY = "individuals"
STOPPED_BY_KILLED = "deadline"


def parse_seeds(text):
    """Inclusive ranges as campaign.toml writes them: "0-9,20", or None."""
    if text is None:
        return None
    seeds = set()
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part[1:]:
            low, _, high = part.partition("-")
            seeds.update(range(int(low), int(high) + 1))
        else:
            seeds.add(int(part))
    return seeds


def repeat_seed(name):
    match = REPEAT_DIR.match(name)
    return int(match.group(1)) if match else None


def run_dir_name(prefix, spec, seed):
    """The runner's own shape, `..._<spec>_seed<NN>`.

    score_curves.spec_from_dir_name matches `_<family>_seed` against the
    example directories and score_campaign.seed_of reads the suffix, so this
    name is the whole of what makes the tree scorable without a new flag.
    """
    return f"{prefix}_{spec}_seed{seed:02d}"


def discover(root, specs, seeds):
    """(spec, seed, repeat directory) for every repeat the filters admit."""
    found = []
    for spec_dir in sorted(p for p in root.iterdir() if p.is_dir()):
        if specs and spec_dir.name not in specs:
            continue
        for repeat in sorted(p for p in spec_dir.iterdir() if p.is_dir()):
            seed = repeat_seed(repeat.name)
            if seed is None or (seeds is not None and seed not in seeds):
                continue
            found.append((spec_dir.name, seed, repeat))
    return found


def solution_files(repeat_dir):
    """spec<i>.tlsf by index, the order Main.java wrote ga.solutions in."""
    files = []
    for entry in os.listdir(repeat_dir):
        match = SPEC_FILE.match(entry)
        if match:
            files.append((int(match.group(1)), entry))
    files.sort()
    return files


def index_rows(repeat_dir):
    """(file, iteration, elapsed_s) per dated solution, plus the undated.

    A repeat written by the fork carries its own times, one row per solution to
    the microsecond, and every file it holds is dated. Otherwise the log's
    series is the only record, and a solution index it never reaches has no
    time this side can supply: those are returned separately rather than dated
    at the run's end, which would put a fabricated arrival on a curve the whole
    comparison is read off.
    """
    times = A.solution_times(str(repeat_dir))
    series = A.iteration_series(str(repeat_dir / "run.log"))
    rows, undated = [], []
    for index, name in solution_files(repeat_dir):
        if index in times:
            iteration, elapsed = times[index]
        else:
            iteration, elapsed = A.found_at(series, index)
        if elapsed == "":
            undated.append(name)
            continue
        rows.append((name, int(iteration), float(elapsed)))
    # Accumulation order, which is what the index means; the file index
    # breaks a tie, the log dating to the second and finding many in one.
    rows.sort(key=lambda row: (row[2], int(SPEC_FILE.match(row[0]).group(1))))
    final_iteration = series[-1][0] if series else ""
    final_elapsed = series[-1][2] if series else ""
    if times:
        # The log stops at the last iteration AuRUS printed, which a killed run
        # never reaches; its own rows run later. -1 marks a solution confirmed
        # after the search, so it dates nothing.
        generations = [row[1] for row in rows if row[1] >= 0]
        if generations:
            final_iteration = max(generations)
        final_elapsed = max((row[2] for row in rows), default=final_elapsed)
    return rows, undated, final_iteration, final_elapsed, bool(times)


def write_index(accumulated, rows):
    """The accumulator's own format, header included (accumulator.hpp)."""
    part = accumulated / (INDEX_NAME + ".part")
    with open(part, "w") as handle:
        handle.write("file\tgeneration\telapsed_s\n")
        for name, iteration, elapsed in rows:
            handle.write(f"{name}\t{iteration}\t{elapsed:.6f}\n")
    part.replace(accumulated / INDEX_NAME)


def manifest(spec, seed, scheme, grading, generations_run, wall_s, stopped_by):
    """The fields score_curves.run_columns reads, and a marker.

    `tool` is there so a reader that finds this file cannot mistake it for a
    PEREDUR manifest: it carries none of the diagnostics, config or cache
    blocks a real one does, and claiming a `schema_version` would say it
    does.
    """
    return {
        "tool": "aurus",
        "written_by": "scripts/aurus_adapt.py",
        "input": f"examples/{spec}/spec.tlsf",
        "seed": seed,
        "stopped_by": stopped_by,
        "generations_run": generations_run,
        "wall_s": wall_s,
        "config": {"genetic": {"selection_scheme": scheme},
                   "fitness": {"status_grading": grading}},
    }


def read_run_facts(root):
    """{(spec, repeat): (wall_time_s, killed)} from the tree's own results CSV.

    The series ends at the last iteration AuRUS logged, which is before it
    writes its solutions, so the CSV's wall is the longer and truer of the
    two. It is also the only record of which repeats the harness killed at the
    cap, which is what `stopped_by` says in the manifest. Its absence costs the
    terminal point of each curve a few seconds and leaves every run reading as
    a budget stop, so a missing file warns and carries on.
    """
    path = root / RESULTS_CSV
    facts = {}
    try:
        handle = open(path)
    except OSError as exc:
        print(f"WARN: no wall times from {path} — {exc}", file=sys.stderr)
        return facts
    with handle:
        for row in csv.DictReader(handle):
            try:
                key = (row["spec"], int(row["repeat"]))
                facts[key] = (float(row["wall_time_s"]),
                              str(row.get("killed", "")).strip() in ("1", "true", "True"))
            except (KeyError, TypeError, ValueError):
                continue
    return facts


def place(source, target, copy):
    if target.exists() or target.is_symlink():
        target.unlink()
    if copy:
        shutil.copyfile(source, target)
    else:
        target.symlink_to(source.resolve())


def adapt_one(spec, seed, repeat_dir, out, args, facts):
    """Materialise one repeat; return its record for the summary."""
    record = {"spec": spec, "seed": seed, "source": str(repeat_dir)}
    wall, killed = facts if facts is not None else (None, False)
    files = solution_files(repeat_dir)
    if not files:
        series = A.iteration_series(str(repeat_dir / "run.log"))
        record["status"] = "lost-at-cap" if series and series[-1][1] \
            else "no-solutions"
        return record
    rows, undated, final_iteration, final_elapsed, dated = \
        index_rows(repeat_dir)
    record["undated"] = len(undated)
    record["dated_by"] = "solution-times" if dated else "run-log"
    record["killed"] = killed
    if not rows:
        record["status"] = "undated"
        return record

    run_dir = out / run_dir_name(args.prefix, spec, seed)
    accumulated = run_dir / ACCUMULATED_DIR
    if (accumulated / INDEX_NAME).exists() and not args.force:
        record["status"] = "present"
        record["n_candidates"] = len(rows)
        return record
    # Rebuilt rather than written over: a rerun under narrower filters would
    # otherwise leave the previous run's candidate files in place, and the
    # index names them nowhere while `maximal` and `compare` both take the
    # whole directory.
    if accumulated.is_dir():
        shutil.rmtree(accumulated)
    accumulated.mkdir(parents=True, exist_ok=True)
    for name, _, _ in rows:
        place(repeat_dir / name, accumulated / name, args.copy)
    write_index(accumulated, rows)
    with open(run_dir / MANIFEST_NAME, "w") as handle:
        json.dump(manifest(spec, seed, args.scheme, args.grading,
                           final_iteration,
                           wall if wall is not None else final_elapsed,
                           STOPPED_BY_KILLED if killed else STOPPED_BY),
                  handle, indent=2)
        handle.write("\n")
    record["status"] = "written"
    record["n_candidates"] = len(rows)
    record["run_dir"] = run_dir.name
    return record


def head_commit():
    try:
        return subprocess.run(["git", "rev-parse", "HEAD"], check=True,
                              capture_output=True, text=True,
                              cwd=Path(__file__).parent).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def summarise(records, args, root, out):
    by_status = {}
    by_dating = {}
    by_family = {}
    killed = 0
    for record in records:
        by_status[record["status"]] = by_status.get(record["status"], 0) + 1
        if "dated_by" in record:
            key = record["dated_by"]
            by_dating[key] = by_dating.get(key, 0) + 1
        killed += 1 if record.get("killed") else 0
        if record["status"] in ("written", "present"):
            family = by_family.setdefault(record["spec"],
                                          {"runs": 0, "candidates": 0})
            family["runs"] += 1
            family["candidates"] += record["n_candidates"]
    return {
        "written_by": "scripts/aurus_adapt.py",
        "commit": head_commit(),
        "root": str(root),
        "out": str(out),
        "host": os.uname().nodename,
        "prefix": args.prefix,
        "selection_scheme": args.scheme,
        "status_grading": args.grading,
        "seeds": args.seeds or "all",
        "linked": not args.copy,
        "runs": sum(f["runs"] for f in by_family.values()),
        "candidates": sum(f["candidates"] for f in by_family.values()),
        "by_status": by_status,
        "by_dating": by_dating,
        "killed_at_cap": killed,
        "by_family": by_family,
        "skipped": [r for r in records
                    if r["status"] not in ("written", "present")],
    }


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", required=True, type=Path,
                        help="AuRUS output tree (<spec>/repeat-NN/)")
    parser.add_argument("--out", required=True, type=Path,
                        help="results directory to materialise")
    parser.add_argument("--specs", help="comma-separated families to include")
    parser.add_argument("--seeds", help="inclusive repeat ranges, e.g. 0-9,20")
    parser.add_argument("--prefix", default="aurus",
                        help="run directory prefix (default: aurus)")
    parser.add_argument("--scheme", default="aurus",
                        help="selection_scheme column (default: aurus)")
    parser.add_argument("--grading", default="aurus",
                        help="status_grading column (default: aurus)")
    parser.add_argument("--copy", action="store_true",
                        help="copy the candidate files instead of linking")
    parser.add_argument("--force", action="store_true",
                        help="rewrite run directories that already exist")
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would be written and stop")
    args = parser.parse_args()

    root = args.root.expanduser()
    out = args.out.expanduser()
    if not root.is_dir():
        sys.exit(f"no such AuRUS output tree: {root}")
    specs = {s for s in (args.specs or "").split(",") if s}
    seeds = parse_seeds(args.seeds)

    repeats = discover(root, specs, seeds)
    if not repeats:
        sys.exit(f"no repeats under {root} match the filters")
    if args.dry_run:
        for spec, seed, repeat in repeats:
            print(f"{run_dir_name(args.prefix, spec, seed)} <- {repeat}")
        print(f"{len(repeats)} repeat(s) would be materialised under {out}")
        return 0

    facts = read_run_facts(root)
    out.mkdir(parents=True, exist_ok=True)
    records = []
    for spec, seed, repeat in repeats:
        record = adapt_one(spec, seed, repeat, out, args,
                           facts.get((spec, seed)))
        records.append(record)
        if record["status"] not in ("written", "present"):
            print(f"SKIP {spec} repeat-{seed:02d}: {record['status']}",
                  file=sys.stderr)
    summary = summarise(records, args, root, out)
    with open(out / SUMMARY_NAME, "w") as handle:
        json.dump(summary, handle, indent=2)
        handle.write("\n")
    print(f"{summary['runs']} run(s), {summary['candidates']} candidate(s) "
          f"under {out}")
    for status, count in sorted(summary["by_status"].items()):
        print(f"  {status:12s} {count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
