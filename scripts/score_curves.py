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
    eps_solutions_<e>       -- of the candidates found by time t, the largest
                               greedy subset no two of which agree on more
                               than 1 - e of the sampled behaviours
    eps_maximal_solutions_<e> -- the same net over the maximal antichain
                               rather than over every candidate, so it counts
                               repairs that are both maximal and distinct
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

The epsilon half is behind --epsilon and is off by default. It reads the
`fingerprint` binary's bit per sampled lasso word, so its distance between two
candidates estimates the measure of their symmetric difference, and separates
a set of near-duplicates from a set of genuinely different repairs -- which
maximality cannot, an antichain being defined by an implication test that no
two of its members pass. It runs over every accumulated candidate rather than
over the maximal set, so it costs one `fingerprint` call a run and no solver
call at all, and both tools' candidates are measured by the same sampling
rather than through implication checks whose timeouts differ between them.

Membership is written, not just counted. `<out>` names a CSV of the curve
counts; beside it go `<stem>.members.tsv`, one row per (cut, surviving file),
and `<stem>.fingerprints.tsv`, one row per candidate and its bit vector. The
counts alone answer one question and no other: which repairs were maximal at
60 s, whether the antichain churns or accretes, and the net at an epsilon
nobody chose in advance all need the sweep run again, which cost 311.7
worker-hours over this campaign in 2026-09-07. The two sidecars are a third
the size of the curve file that replaced them -- the long format repeats eight
run-identifying columns on every row, where a member is a name -- and they
make every one of those questions arithmetic.

The maximality half is a separate stage behind --maximality, off by default. It
is computed offline, after and apart from the timed run, and must never be read
as part of it. It is one `maximal --curve` walk a run: the binary keeps a
running maximal antichain over the index in timestamp order and prints an event
log, and every cut's membership is replayed from that log rather than costing a
sweep of its own. The rows are still reported at a bounded number of time cuts
(default 20, log-spaced), the curve file being long-format and one row per
candidate being 20x the rows for a shape the cuts already carry.

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
import bisect
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
FINGERPRINT_BIN = Path(os.environ.get(
    "FINGERPRINT_BIN", REPO_ROOT / "build-release" / "fingerprint"))

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
    concentrated in the families PEREDUR finds hardest. Matching against the
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


def antichain_walk(accumulated: Path, jobs: int | None,
                   timeout_s: int) -> list[tuple[float, str, str]] | None:
    """Return `maximal --curve`'s event log over the accumulator index.

    Each row is (elapsed_s, file, event) with event one of admit, drop and
    remove. The walk keeps a running maximal antichain and compares each
    arrival against that antichain alone, so it is O(n * |antichain|) rather
    than O(n * |prefix|), and one process answers every cut rather than one
    process per cut re-deciding pairs its predecessor had already decided from
    a cold solver cache.

    A timeout keeps whatever the walk had already printed. The binary flushes
    per wave for exactly that reason, and the log is a prefix of the walk, so
    the cuts it covers are the early ones -- which is where an anytime curve
    carries its information.
    """
    index = accumulated / INDEX_NAME
    command = [str(MAXIMAL_BIN), "--curve", str(index)]
    if jobs:
        command += ["--jobs", str(jobs)]
    text = ""
    try:
        text = subprocess.run(command, check=True, timeout=timeout_s,
                              capture_output=True, text=True).stdout
    except subprocess.TimeoutExpired as exc:
        text = (exc.stdout or b"").decode() if isinstance(exc.stdout, bytes) \
            else (exc.stdout or "")
        print(f"WARN: the antichain walk exceeded {timeout_s}s; keeping the "
              f"{len(text.splitlines())} row(s) it had written",
              file=sys.stderr)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"WARN: the antichain walk failed — {exc}", file=sys.stderr)
        return None
    rows: list[tuple[float, str, str]] = []
    for line in text.splitlines():
        fields = line.split("\t")
        if len(fields) != 4 or fields[0] == "elapsed_s":
            continue
        try:
            rows.append((float(fields[0]), fields[1], fields[2]))
        except ValueError:
            continue
    return rows


def membership_at_cuts(log: list[tuple[float, str, str]],
                       cuts: list[float]) -> list[list[str]]:
    """The antichain's members at each of `cuts`, in admission order.

    One pass over the log for all the cuts, which the log's ordering allows: it
    is non-decreasing in time, and an admission and a removal are both
    permanent. A specification some member dominates stays dominated by
    whatever later dominates that member, implication being transitive, so
    membership at an instant is the admissions up to it less the removals up to
    it and nothing has to be re-derived.
    """
    admitted: list[str] = []
    present: set[str] = set()
    out: list[list[str]] = []
    position = 0
    for cut in cuts:
        while position < len(log) and log[position][0] <= cut:
            _, name, event = log[position]
            if event == "admit":
                admitted.append(name)
                present.add(name)
            elif event == "remove":
                present.discard(name)
            position += 1
        out.append([name for name in admitted if name in present])
    return out


def maximality_rows(base: dict, index: list[tuple[str, int, float]],
                    accumulated: Path, implying: set[str] | None,
                    n_cuts: int, jobs: int | None,
                    timeout_s: int, deadline: float | None,
                    prints: dict[str, int] | None = None,
                    epsilons: list[float] | None = None,
                    n_words: int = 0,
                    members: list[tuple[float, str]] | None = None
                    ) -> list[dict]:
    """Report the maximal antichain over prefixes of the accumulated set.

    One `maximal --curve` walk answers every cut. What it replaced was a
    `maximal` process per cut over the previous cut's survivors plus the new
    arrivals -- sound, because domination is monotone under adding elements,
    and 4.5x cheaper than re-running the whole prefix, but still quadratic in
    the surviving set at every cut and still starting each one with an empty
    solver cache. The walk is quadratic in nothing: an arrival meets the
    running antichain and no more of the prefix than that.

    The cuts are what they were, so the rows are what they were. Only their
    derivation moved.
    """
    rows: list[dict] = []
    epsilons = epsilons or []
    prints = prints or {}
    by_time = sorted(index, key=lambda row: row[2])
    cuts = time_cuts([row[2] for row in by_time], n_cuts)
    # The deadline governs where there is one. `maximal_timeout` bounded one
    # cut of twenty, so applying it to the whole walk would be a fivefold
    # tightening of a budget nobody moved -- and the walk is the cheaper
    # algorithm, so it would cut off runs the per-cut pass finished.
    budget = timeout_s
    if deadline is not None:
        budget = max(1, int(deadline - time.monotonic()))
    log = antichain_walk(accumulated, jobs, budget)
    if log is None:
        return rows
    # A walk cut short covers the cuts up to its last row and no further, so
    # the later ones are dropped rather than reported off a truncated log --
    # which would read as an antichain that stopped growing.
    covered = log[-1][0] if log else -1.0
    cuts = [cut for cut in cuts if cut <= covered]
    # A cut that adds no candidate to the prefix has the antichain the previous
    # cut reported, so it carries no information and is not reported -- which is
    # what the per-cut pass did, there by skipping an invocation and here by
    # skipping a row. Emitting it instead would change the row set of every
    # archived curve file for a value already in it.
    ordered = [row[2] for row in by_time]
    distinct: list[float] = []
    seen_prefix = -1
    for cut in cuts:
        n_prefix = bisect.bisect_right(ordered, cut)
        if n_prefix == seen_prefix:
            continue
        seen_prefix = n_prefix
        distinct.append(cut)
    cuts = distinct
    for cut, survivors in zip(cuts, membership_at_cuts(log, cuts)):
        present = set(survivors)
        if members is not None:
            members += [(cut, name) for name in survivors]
        rows.append({**base, "metric": "maximal_solutions",
                     "elapsed_s": f"{cut:.6f}", "value": len(survivors),
                     "censored": 0})
        rows.append({
            **base, "metric": "maximal_ideal_solutions",
            "elapsed_s": f"{cut:.6f}",
            "value": "" if implying is None else len(present & implying),
            "censored": "" if implying is None else 0,
        })
        # The net over the survivors, in admission order -- which is discovery
        # order among them -- so the representatives the greedy net keeps do
        # not depend on the order anything happened to print them in.
        if prints:
            survivor_prints = [prints[name] for name in survivors
                               if name in prints]
            for epsilon in epsilons:
                rows.append({
                    **base, "metric": f"eps_maximal_solutions_{epsilon:g}",
                    "elapsed_s": f"{cut:.6f}",
                    "value": separated_count(survivor_prints,
                                             int(epsilon * n_words)),
                    "censored": 0})
    return rows


def comma_separated_fractions(text: str) -> list[float]:
    """Parse `--epsilon 0.05,0.2` into a sorted list of fractions in [0, 1)."""
    values = []
    for piece in text.split(","):
        try:
            value = float(piece)
        except ValueError as error:
            raise argparse.ArgumentTypeError(
                f"not a number: {piece}") from error
        if not 0.0 <= value < 1.0:
            raise argparse.ArgumentTypeError(
                f"epsilon must be in [0, 1): {piece}")
        values.append(value)
    return sorted(set(values))


def fingerprints_of(accumulated: Path, spec: str, n_words: int,
                    seed: int) -> dict[str, int] | None:
    """Return each accumulated candidate's fingerprint as an integer bit set.

    The words are drawn over the family's *original* specification rather than
    over each candidate's own signal list, so every candidate of a family --
    and every candidate of the other tool's runs on that family -- is measured
    against one word set. `fingerprint` derives the words from that file, the
    count and the seed alone, so nothing has to travel between the hosts that
    score the two sides.
    """
    signals = EXAMPLES_DIR / spec / "spec.tlsf"
    if not signals.is_file():
        print(f"WARN: no specification at {signals}", file=sys.stderr)
        return None
    command = [str(FINGERPRINT_BIN), "--signals", str(signals),
               "--words", str(n_words), "--seed", str(seed), str(accumulated)]
    try:
        result = subprocess.run(command, capture_output=True, text=True,
                                check=False)
    except OSError as error:
        print(f"WARN: {FINGERPRINT_BIN}: {error}", file=sys.stderr)
        return None
    prints: dict[str, int] = {}
    for line in result.stdout.splitlines():
        name, _, digits = line.partition("\t")
        if digits:
            prints[name] = int(digits, 16)
    # A non-zero status means some file failed, and the rows for the rest are
    # still on stdout: a run keeps the curve its readable candidates support
    # rather than losing it to one bad file, which is what the maximality
    # stage does with a failed cut.
    if result.returncode != 0:
        print(f"WARN: fingerprint reported {result.returncode} on "
              f"{accumulated}: {result.stderr.strip()}", file=sys.stderr)
    return prints or None


def separated_count(prints: list[int], threshold: int) -> int:
    """The greedy epsilon-net over `prints`, in the order given.

    Greedy rather than a maximum independent set, which is NP-hard and would
    be a different number at every cut for no gain in what the curve says.
    Discovery order makes the count non-decreasing as the prefix grows -- an
    earlier candidate's admission never depends on a later one -- which is
    what an anytime curve has to be.
    """
    kept: list[int] = []
    for value in prints:
        if all(bin(value ^ other).count("1") > threshold for other in kept):
            kept.append(value)
    return len(kept)


def epsilon_rows(base: dict, index: list[tuple[str, int, float]],
                 prints: dict[str, int], epsilons: list[float],
                 n_words: int, n_cuts: int, end_s) -> list[dict]:
    """One curve per epsilon, at the cuts the other curves use."""
    by_time = sorted(index, key=lambda row: row[2])
    ordered = [(row[2], prints[row[0]]) for row in by_time
               if row[0] in prints]
    rows: list[dict] = []
    for epsilon in epsilons:
        # Strictly greater than the threshold, so epsilon = 0 keeps one
        # representative of each distinct fingerprint rather than all of them.
        threshold = int(epsilon * n_words)
        points: list[tuple[float, int]] = []
        for cut in time_cuts([moment for moment, _ in ordered], n_cuts):
            prefix = [value for moment, value in ordered if moment <= cut]
            points.append((cut, separated_count(prefix, threshold)))
        rows += curve_rows(base, f"eps_solutions_{epsilon:g}", points, end_s)
    return rows


def score_run(run_dir: Path, args, deadline: float | None = None,
              sidecars: dict | None = None) -> list[dict]:
    """Return every long-format row for one run directory.

    `sidecars`, when given, is filled with the membership and fingerprint
    tables the caller writes beside the CSV.
    """
    manifest = read_manifest(run_dir)
    spec = spec_of(manifest, args.spec, run_dir)
    base = run_columns(manifest, spec, run_dir)
    index = read_index(run_dir)
    accumulated = run_dir / ACCUMULATED_DIR

    ideals = Path(args.ideals) if args.ideals else EXAMPLES_DIR / spec / "fixes"
    implying: set[str] | None = None
    if getattr(args, "skip_ideals", False):
        # Left unknown rather than empty, which is the same distinction the
        # missing-ideals branch below draws: no candidate was found not to
        # imply an ideal, the question was not asked. `compare` is quadratic
        # in candidates times ideals and is 99% of an epsilon-only pass -- 7.2s
        # of 7.3s on a 421-candidate run -- so a pass that wants the
        # separation curves alone should not pay for a relation the
        # maximality pass already recorded.
        pass
    elif index and ideals.is_dir():
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
    # getattr rather than a plain attribute: score_run is called with an
    # argument namespace of the caller's own making in the script tests, and a
    # curve stage added later must not break one built before it existed.
    epsilons = getattr(args, "epsilon", None)
    n_words = getattr(args, "fingerprint_words", 256)
    prints: dict[str, int] | None = None
    if epsilons and index:
        prints = fingerprints_of(accumulated, spec, n_words,
                                 getattr(args, "fingerprint_seed", 0))
    members: list[tuple[float, str]] = []
    if args.maximality and index:
        rows += maximality_rows(base, index, accumulated, implying,
                                args.cuts, args.jobs, args.maximal_timeout,
                                deadline, prints, epsilons or [], n_words,
                                members)
    if sidecars is not None:
        sidecars["members"] = members
        sidecars["fingerprints"] = prints or {}
        sidecars["n_words"] = n_words
    if epsilons and index and prints is not None:
        rows += epsilon_rows(base, index, prints, epsilons, n_words,
                             args.cuts, end_s)
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


def sidecar_paths(out: Path) -> tuple[Path, Path]:
    """The two membership files beside the curve CSV at @p out.

    `.part` is stripped before the stem is taken, so a scorer writing
    `<run>.csv.part` produces `<run>.members.tsv` rather than
    `<run>.csv.members.tsv`; score_campaign.py renames all three together.
    """
    stem = out.name
    for suffix in (".part", ".csv"):
        if stem.endswith(suffix):
            stem = stem[: -len(suffix)]
    return (out.parent / f"{stem}.members.tsv",
            out.parent / f"{stem}.fingerprints.tsv")


def write_sidecars(out: Path, sidecars: dict) -> None:
    """Write membership and fingerprints beside the curve CSV.

    Written whenever the maximality stage ran and `--out` names a file, with
    no flag of its own. A flag is how the last one went missing: the 2026-09-07
    pass held every survivor set in memory and wrote only their sizes, so the
    2026-09-09 question about them cost the whole pass again.
    """
    members_path, prints_path = sidecar_paths(out)
    with open(members_path, "w", newline="") as handle:
        handle.write("cut_s\tfile\n")
        for cut, name in sidecars.get("members", []):
            handle.write(f"{cut:.6f}\t{name}\n")
    prints = sidecars.get("fingerprints") or {}
    if not prints:
        return
    digits = max(1, (sidecars.get("n_words", 0) + 3) // 4)
    with open(prints_path, "w", newline="") as handle:
        handle.write("file\tfingerprint\n")
        for name in sorted(prints):
            handle.write(f"{name}\t{prints[name]:0{digits}x}\n")


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
    parser.add_argument("--epsilon", type=comma_separated_fractions,
                        default=[],
                        help="comma-separated separation thresholds; each "
                             "adds an eps_solutions_<e> curve over the "
                             "accumulated candidates (default: none)")
    parser.add_argument("--fingerprint-words", type=int, default=256,
                        help="lasso words sampled per family for --epsilon "
                             "(default: 256)")
    parser.add_argument("--fingerprint-seed", type=int, default=0,
                        help="word-sampling seed for --epsilon (default: 0)")
    parser.add_argument("--cuts", type=int, default=20,
                        help="time cuts for --maximality (default: 20)")
    parser.add_argument("--jobs", type=int, default=0,
                        help="solver calls in flight for maximal")
    parser.add_argument("--spec", help="override the family name and ideals")
    parser.add_argument("--ideals", help="override the ideals directory")
    parser.add_argument("--skip-ideals", action="store_true",
                        help="do not run compare; the ideal-implying metrics "
                             "are written as unknown")
    parser.add_argument("--deadline-s", type=int, default=0,
                        help="budget for the antichain walk in seconds, in "
                             "place of --maximal-timeout; the cuts the walk "
                             "covered are written (0: no deadline)")
    parser.add_argument("--maximal-timeout", type=int, default=900,
                        help="wall budget for the antichain walk where no "
                             "deadline is set (default: 900)")
    parser.add_argument("--compare-timeout", type=int,
                        default=COMPARE_TIMEOUT_S,
                        help="compare budget in seconds "
                             f"(default: {COMPARE_TIMEOUT_S})")
    parser.add_argument("--out", type=Path, help="write the CSV here")
    args = parser.parse_args()

    if args.cuts < 1:
        parser.error("--cuts expects a positive integer")
    if args.epsilon and args.fingerprint_words < 1:
        parser.error("--fingerprint-words expects a positive integer")

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
        sidecars: dict = {}
        rows = score_run(run_dir, args, deadline, sidecars)
        long_rows += rows
        summaries.append(summarise(rows))
        # One run directory per invocation is what the scorer does, so writing
        # here rather than after the loop keeps a sidecar named for the run it
        # describes. A multi-directory invocation writes the last one's, which
        # is why score_campaign.py passes one at a time.
        if args.out and args.maximality and sidecars.get("members"):
            write_sidecars(args.out, sidecars)

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
