#!/usr/bin/env python3
"""Score one completed run's accumulated repairs as curves over time.

An ablation campaign that varies a termination rule cannot be read off
`n_repairs` alone: two arms that stop at different moments are answering
different questions unless the answer is a function of elapsed time. This turns
a run's output directory into six such metrics, four of them curves and two of
them scalars:

    solutions               -- gate-passing candidates found by time t
    ideal_solutions         -- of those, ones at least as strong as an ideal
    maximal_solutions       -- the maximal antichain of the set found by time t
    maximal_ideal_solutions -- of those survivors, the ideal-implying ones
    time_to_first_repair       (scalar)
    time_to_first_ideal_repair (scalar)

The source is `<run-dir>/accumulated/index.tsv`, the accumulator's flushed
record of every candidate that passed the output gate and when, which is a
superset of the filtered `repair_N` files the run reports. Either front end's
output is read: `compare` and `maximal` both route on the extension the
accumulator wrote, `.tlsf` for TLSF and `.json` for FRETISH. The repairs are
deliberately not scored here: they are the run's answer, and these are its
working.

"Ideal-implying" is `equivalent` or `strictly stronger` against any ideal,
which is the collapse `run_experiments.parse_compare_output` applies to derive
`implies_ideal`; the relation names come from `src/compare.cpp`. One `compare`
invocation covers the whole accumulated directory, its per-repair lines joining
back to the index by file name -- `compare` is quadratic in repairs x ideals,
so one call per file would cost the run over again.

The maximality half is a separate stage behind --maximality, off by default. It
is computed offline, after and apart from the timed run, and must never be read
as part of it. Its cost is why: `maximal` is a pairwise implication sweep that
has taken 19 GB on this corpus, so it runs at a bounded number of time cuts
(default 20, log-spaced) rather than at every candidate.

Usage:
    python scripts/score_curves.py <run-dir>...              # long CSV
    python scripts/score_curves.py <run-dir> --summary       # one row per run
    python scripts/score_curves.py <run-dir> --maximality    # + metrics 5, 6
    python scripts/score_curves.py <run-dir> --cuts 40 --jobs 8
    python scripts/score_curves.py <run-dir> --out curves.csv

A run that found nothing has no time to a first repair. Every such value is
written as an empty field with `censored` set, never as a zero and never as an
omitted row, so a right-censored run survives into the analysis rather than
being silently read as an instantaneous one.
"""

import argparse
import csv
import json
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from run_experiments import (  # noqa: E402
    COMPARE_BIN,
    COMPARE_TIMEOUT_S,
    EXAMPLES_DIR,
    REPO_ROOT,
)

# Overridable as run_experiments' own binaries are, so a worktree with no
# release build can point at another checkout's.
MAXIMAL_BIN = Path(os.environ.get("MAXIMAL_BIN",
                                  REPO_ROOT / "build-release" / "maximal"))

ACCUMULATED_DIR = "accumulated"
INDEX_NAME = "index.tsv"
MANIFEST_NAME = "run.json"
CONFIG_NAME = "config.toml"

# src/compare.cpp classify(): a repair implies an ideal when it is equivalent to
# it or strictly stronger. The other three verdicts (weaker, incomparable,
# timeout) do not, timeout being unknown rather than negative.
IMPLYING_RELATIONS = frozenset({"equivalent", "strictly stronger"})

RELATION_LINE = re.compile(
    r"^(\S+)\s+:\s+"
    r"(equivalent|strictly stronger|strictly weaker|incomparable|timeout)"
)
MAXIMAL_SURVIVOR_LINE = re.compile(r"^class\s+\d+\s+(.+?)(?:\s+\(\+\d+ identical\))?$")

CURVE_METRICS = ("solutions", "ideal_solutions",
                 "maximal_solutions", "maximal_ideal_solutions")
SCALAR_METRICS = ("time_to_first_repair", "time_to_first_ideal_repair")

RUN_FIELDS = ["spec", "seed", "selection_scheme", "status_grading",
              "stopped_by", "generations_run", "run_wall_s"]
CURVE_FIELDS = RUN_FIELDS + ["metric", "elapsed_s", "value", "censored"]
SUMMARY_FIELDS = RUN_FIELDS + ["n_accumulated"] + list(SCALAR_METRICS) + \
    [f"{metric}_final" for metric in CURVE_METRICS] + \
    [f"{metric}_censored" for metric in SCALAR_METRICS]


def read_manifest(run_dir: Path) -> dict:
    """Return the run manifest, or {} when the directory has none.

    A directory without a manifest is not fatal: the index alone still yields
    every count, and only the arm-identifying columns go unknown.
    """
    try:
        with open(run_dir / MANIFEST_NAME) as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"WARN: {run_dir}/{MANIFEST_NAME} unreadable — {exc}",
              file=sys.stderr)
        return {}


def read_index(run_dir: Path) -> list[tuple[str, int, float]]:
    """Return (file, generation, elapsed_s) rows in accumulation order.

    An absent or empty accumulated/ directory is a legitimate outcome — a run
    with `accumulate_repairs` off, or one that passed nothing through the gate,
    creates neither the directory nor the index — so it reads as no rows rather
    than as an error.
    """
    path = run_dir / ACCUMULATED_DIR / INDEX_NAME
    try:
        text = path.read_text()
    except OSError:
        return []
    rows: list[tuple[str, int, float]] = []
    for line in text.splitlines():
        fields = line.split("\t")
        if len(fields) != 3 or fields[0] == "file":
            continue
        try:
            rows.append((fields[0], int(fields[1]), float(fields[2])))
        except ValueError:
            print(f"WARN: {path}: cannot parse row {line!r}", file=sys.stderr)
    return rows


SEED_SUFFIX = re.compile(r"_seed(\d+)$")

# Read out of config.toml, which a censored run keeps where it has no manifest.
# The file is flat enough that two anchored patterns beat a TOML parser, which
# the lab hosts' Python 3.10 has none of in the standard library.
CONFIG_KEY = {
    "selection_scheme": re.compile(r'^\s*selection_scheme\s*=\s*"([^"]*)"',
                                   re.MULTILINE),
    "status_grading": re.compile(r'^\s*status_grading\s*=\s*"([^"]*)"',
                                 re.MULTILINE),
}


def spec_from_dir_name(run_dir: Path) -> str:
    """Name the family from the run directory, matching it against examples/.

    A censored run has no manifest to read the input path out of, and those are
    not the runs to drop: they hold 17.2% of the accumulated candidates and are
    concentrated in the families counter finds hardest. Matching against the
    example directories rather than splitting on "_" keeps a family name that
    contains a separator readable, and reports nothing rather than a wrong
    family when no name matches.
    """
    name = run_dir.name
    candidates = [d.name for d in EXAMPLES_DIR.iterdir() if d.is_dir()] \
        if EXAMPLES_DIR.is_dir() else []
    matches = [c for c in candidates if f"_{c}_seed" in name]
    return max(matches, key=len) if matches else ""


def seed_from_dir_name(run_dir: Path) -> str:
    match = SEED_SUFFIX.search(run_dir.name)
    return str(int(match.group(1))) if match else ""


def read_config(run_dir: Path) -> dict:
    """Return the arm-identifying keys from a run's config.toml, or {}."""
    try:
        text = (run_dir / CONFIG_NAME).read_text()
    except OSError:
        return {}
    found = {}
    for key, pattern in CONFIG_KEY.items():
        match = pattern.search(text)
        if match:
            found[key] = match.group(1)
    return found


def spec_of(manifest: dict, override: str | None, run_dir: Path) -> str:
    """Name the example family behind a run, from its manifest input path."""
    if override:
        return override
    source = manifest.get("input")
    return Path(source).parent.name if source else spec_from_dir_name(run_dir)


def run_columns(manifest: dict, spec: str, run_dir: Path) -> dict:
    config = manifest.get("config", {})
    fallback = read_config(run_dir) if not manifest else {}
    return {
        "spec": spec,
        "seed": manifest.get("seed", seed_from_dir_name(run_dir)),
        "selection_scheme": config.get("genetic", {}).get(
            "selection_scheme", fallback.get("selection_scheme", "")),
        "status_grading": config.get("fitness", {}).get(
            "status_grading", fallback.get("status_grading", "")),
        "stopped_by": manifest.get("stopped_by", ""),
        "generations_run": manifest.get("generations_run", ""),
        "run_wall_s": manifest.get("wall_s", ""),
    }


def compare_relations(repairs_dir: Path, ideals_dir: Path,
                      timeout_s: int) -> dict[str, str] | None:
    """Return each accumulated file's strongest relation to any ideal.

    None means the comparison did not happen, which is not the same as a run
    with no ideal-implying repair and is reported as unknown rather than as
    zero downstream.
    """
    try:
        result = subprocess.run(
            [str(COMPARE_BIN), "--repairs", str(repairs_dir),
             "--ideals", str(ideals_dir)],
            check=True, timeout=timeout_s, capture_output=True, text=True)
    except (OSError, subprocess.TimeoutExpired,
            subprocess.CalledProcessError) as exc:
        print(f"WARN: compare failed on {repairs_dir} — {exc}", file=sys.stderr)
        return None
    relations = {}
    for line in result.stdout.splitlines():
        match = RELATION_LINE.match(line.strip())
        if match:
            relations[match.group(1)] = match.group(2)
    return relations


def maximal_over(files: list[Path], jobs: int | None,
                 timeout_s: int) -> set[str] | None:
    """Return the file names surviving the implication filter over `files`.

    `maximal` collapses structural duplicates and prints one line per survivor
    naming the first file of each, so the survivors are distinct specifications
    — which is what both maximality metrics count.

    The wall bound is what `compare_relations` beside it always had and this
    did not. `maximal` bounds each black call at 20 seconds of its own accord,
    but a batch of 169 files is 14,196 pairs decided in both directions, so
    28,392 calls at that budget leave 39 hours of headroom on four jobs and
    nothing capped the total. One humanoid-531 batch ran for 17.6 hours and
    eight workers a host returned nothing in thirteen. Abandoning the cut
    instead costs that cut alone: the caller keeps the survivors and the
    consumed index from the last cut that succeeded, so the run goes on and
    yields the cuts it can decide.
    """
    if not files:
        return set()
    command = [str(MAXIMAL_BIN)] + [str(path) for path in files]
    if jobs:
        command += ["--jobs", str(jobs)]
    try:
        result = subprocess.run(command, check=True, timeout=timeout_s,
                                capture_output=True, text=True)
    except subprocess.TimeoutExpired:
        print(f"WARN: maximal exceeded {timeout_s}s over {len(files)} file(s); "
              f"this cut is skipped", file=sys.stderr)
        return None
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"WARN: maximal failed over {len(files)} file(s) — {exc}",
              file=sys.stderr)
        return None
    survivors = set()
    for line in result.stdout.splitlines():
        match = MAXIMAL_SURVIVOR_LINE.match(line.strip())
        if match:
            survivors.add(Path(match.group(1)).name)
    return survivors


def time_cuts(times: list[float], n_cuts: int) -> list[float]:
    """Return at most `n_cuts` log-spaced cut times covering `times`.

    Log spacing because the interesting part of a repair curve is its start:
    the first minute of a 7200 s run holds most of what the search will ever
    find, and linear cuts spend nearly every solver call on a plateau.
    """
    distinct = sorted(set(times))
    if len(distinct) <= n_cuts:
        return distinct
    low = max(distinct[0], 1e-3)
    high = max(distinct[-1], low)
    if high <= low:
        return [high]
    step = (math.log(high) - math.log(low)) / (n_cuts - 1)
    cuts = sorted({math.exp(math.log(low) + step * i) for i in range(n_cuts)})
    cuts[-1] = high
    return cuts


def cumulative_points(times: list[float]) -> list[tuple[float, int]]:
    """Collapse event times into (time, count-so-far) step-function points."""
    points: list[tuple[float, int]] = []
    for index, moment in enumerate(sorted(times), start=1):
        if points and points[-1][0] == moment:
            points[-1] = (moment, index)
        else:
            points.append((moment, index))
    return points


def curve_rows(base: dict, metric: str, points: list[tuple[float, int]],
               end_s) -> list[dict]:
    """Emit a step function, plus a terminal point at the run's own end.

    The terminal point is what makes a run with nothing accumulated a row
    rather than a gap: an arm that found no repair at all still has a curve,
    and it reads zero for the whole run.
    """
    rows = [{**base, "metric": metric, "elapsed_s": f"{moment:.6f}",
             "value": value, "censored": 0} for moment, value in points]
    final = points[-1][1] if points else 0
    if isinstance(end_s, (int, float)) and (not points or end_s > points[-1][0]):
        rows.append({**base, "metric": metric, "elapsed_s": f"{end_s:.6f}",
                     "value": final, "censored": 0})
    return rows


def scalar_row(base: dict, metric: str, moment: float | None,
               known: bool) -> dict:
    """One row for a time-to-event metric, censored when the event never came.

    `known` is false only when the event could not be decided at all (a failed
    comparison), which is a different fact from an event that did not happen
    and is written as an empty censoring flag rather than a set one.
    """
    return {**base, "metric": metric, "elapsed_s": "",
            "value": "" if moment is None else f"{moment:.6f}",
            "censored": "" if not known else int(moment is None)}


def maximality_rows(base: dict, index: list[tuple[str, int, float]],
                    accumulated: Path, implying: set[str] | None,
                    n_cuts: int, jobs: int | None,
                    timeout_s: int, deadline: float | None) -> list[dict]:
    """Run `maximal` over prefixes of the accumulated set at bounded cuts.

    Each cut is given the previous cut's survivors plus the candidates that
    arrived since, rather than the whole prefix again. That is sound because
    domination is monotone under adding elements: if A was dominated by B in
    the prefix at one cut, B is still present at the next, so A can never
    re-enter the antichain. The maximal set of the whole prefix therefore
    equals the maximal set of (previous survivors + new arrivals). Measured
    over this campaign's 546,282 candidates it takes the pairwise sweep from
    272.7M comparisons to 60.1M, 4.5x fewer, because a prefix of 186 is
    replaced by a batch of about 40 survivors and the handful that are new.

    `maximal` quotients its survivors by mutual implication and names one
    representative per class, so carrying representatives forward keeps the
    class count. It cannot change the ideal count either: every member of a
    class implies exactly the same ideals as its representative, implication
    being transitive through the equivalence.

    Cuts landing on the same prefix are skipped rather than recomputed, which
    removes 29% of the invocations over this campaign for no change in output.

    The cheaper algorithm this is still not: walk the set in timestamp order
    keeping a running maximal antichain and compare each arrival against that
    antichain alone, which is O(n * |antichain|). It needs a pairwise
    implication oracle over two specification files, and no binary exposes one
    -- `maximal` takes a whole set and reports its filter's verdict, not the
    individual implications behind it.
    """
    rows: list[dict] = []
    by_time = sorted(index, key=lambda row: row[2])
    survivors: set[str] = set()
    seen_prefix = -1
    consumed = 0
    for cut in time_cuts([row[2] for row in by_time], n_cuts):
        # Stop adding cuts rather than being killed from outside holding
        # nothing. The cuts are log-spaced, so the ones already computed are
        # the early ones, which is where an anytime curve carries its
        # information; a hard run yields a short curve instead of no curve.
        if deadline is not None and time.monotonic() > deadline:
            print(f"WARN: deadline reached after {len(rows) // 2} cut(s); "
                  f"writing a partial curve", file=sys.stderr)
            break
        prefix = [row for row in by_time if row[2] <= cut]
        if len(prefix) == seen_prefix:
            continue
        seen_prefix = len(prefix)
        batch = sorted(survivors) + [row[0] for row in prefix[consumed:]]
        found = maximal_over([accumulated / name for name in batch],
                             jobs, timeout_s)
        if found is None:
            # Stop, rather than try the next cut: nothing was consumed, so
            # the next batch is this one plus more and cannot do better in
            # the same budget. Continuing burned the whole deadline on cuts
            # that all failed -- 752 of them over the matched campaign, 48%
            # of its worker-hours, yielding no row.
            break
        survivors = found
        consumed = len(prefix)
        rows.append({**base, "metric": "maximal_solutions",
                     "elapsed_s": f"{cut:.6f}", "value": len(survivors),
                     "censored": 0})
        rows.append({
            **base, "metric": "maximal_ideal_solutions",
            "elapsed_s": f"{cut:.6f}",
            "value": "" if implying is None else len(survivors & implying),
            "censored": "" if implying is None else 0,
        })
    return rows


def score_run(run_dir: Path, args, deadline: float | None = None) -> list[dict]:
    """Return every long-format row for one run directory."""
    manifest = read_manifest(run_dir)
    spec = spec_of(manifest, args.spec, run_dir)
    base = run_columns(manifest, spec, run_dir)
    index = read_index(run_dir)
    accumulated = run_dir / ACCUMULATED_DIR

    ideals = Path(args.ideals) if args.ideals else EXAMPLES_DIR / spec / "fixes"
    implying: set[str] | None = None
    if index and ideals.is_dir():
        relations = compare_relations(accumulated, ideals, args.compare_timeout)
        if relations is not None:
            implying = {name for name, relation in relations.items()
                        if relation in IMPLYING_RELATIONS}
    elif not index:
        implying = set()
    else:
        print(f"WARN: no ideals directory at {ideals}", file=sys.stderr)

    times = [row[2] for row in index]
    ideal_times = ([row[2] for row in index if row[0] in implying]
                   if implying is not None else [])
    end_s = manifest.get("wall_s", "")

    rows = curve_rows(base, "solutions", cumulative_points(times), end_s)
    if implying is None:
        rows.append({**base, "metric": "ideal_solutions", "elapsed_s": "",
                     "value": "", "censored": ""})
    else:
        rows += curve_rows(base, "ideal_solutions",
                           cumulative_points(ideal_times), end_s)
    rows.append(scalar_row(base, "time_to_first_repair",
                           min(times) if times else None, True))
    rows.append(scalar_row(base, "time_to_first_ideal_repair",
                           min(ideal_times) if ideal_times else None,
                           implying is not None))
    if args.maximality and index:
        rows += maximality_rows(base, index, accumulated, implying,
                                args.cuts, args.jobs, args.maximal_timeout,
                                deadline)
    return rows


def summarise(rows: list[dict]) -> dict:
    """Collapse one run's long rows into its scalars and end-of-run counts."""
    by_metric: dict[str, list[dict]] = {}
    for row in rows:
        by_metric.setdefault(row["metric"], []).append(row)
    summary = {key: rows[0][key] for key in RUN_FIELDS}
    solutions = by_metric.get("solutions", [])
    summary["n_accumulated"] = solutions[-1]["value"] if solutions else 0
    for metric in SCALAR_METRICS:
        row = by_metric.get(metric, [{}])[-1]
        summary[metric] = row.get("value", "")
        summary[f"{metric}_censored"] = row.get("censored", "")
    for metric in CURVE_METRICS:
        tail = by_metric.get(metric, [])
        summary[f"{metric}_final"] = tail[-1]["value"] if tail else ""
    return summary


def write_csv(handle, fieldnames: list[str], rows: list[dict]) -> None:
    writer = csv.DictWriter(handle, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run_dir", nargs="+", type=Path,
                        help="a completed run's output directory")
    parser.add_argument("--summary", action="store_true",
                        help="one row per run instead of the long format")
    parser.add_argument("--maximality", action="store_true",
                        help="also run the implication filter over time cuts")
    parser.add_argument("--cuts", type=int, default=20,
                        help="time cuts for --maximality (default: 20)")
    parser.add_argument("--jobs", type=int, default=0,
                        help="solver calls in flight for maximal")
    parser.add_argument("--spec", help="override the family name and ideals")
    parser.add_argument("--ideals", help="override the ideals directory")
    parser.add_argument("--deadline-s", type=int, default=0,
                        help="stop adding maximality cuts after this many "
                             "seconds and write what was computed (0: no "
                             "deadline)")
    parser.add_argument("--maximal-timeout", type=int, default=900,
                        help="wall budget for one `maximal` call, per cut "
                             "(default: 900)")
    parser.add_argument("--compare-timeout", type=int,
                        default=COMPARE_TIMEOUT_S,
                        help="compare budget in seconds "
                             f"(default: {COMPARE_TIMEOUT_S})")
    parser.add_argument("--out", type=Path, help="write the CSV here")
    args = parser.parse_args()

    if args.cuts < 1:
        parser.error("--cuts expects a positive integer")

    # The deadline is per run directory, not per invocation: the launcher
    # feeds one directory at a time, and a budget shared across many would
    # spend it all on the first.
    long_rows: list[dict] = []
    summaries: list[dict] = []
    for run_dir in args.run_dir:
        if not run_dir.is_dir():
            print(f"WARN: no such run directory: {run_dir}", file=sys.stderr)
            continue
        deadline = (time.monotonic() + args.deadline_s
                    if args.deadline_s > 0 else None)
        rows = score_run(run_dir, args, deadline)
        long_rows += rows
        summaries.append(summarise(rows))

    fields = SUMMARY_FIELDS if args.summary else CURVE_FIELDS
    rows = summaries if args.summary else long_rows
    if args.out:
        with open(args.out, "w", newline="") as handle:
            write_csv(handle, fields, rows)
    else:
        write_csv(sys.stdout, fields, rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
