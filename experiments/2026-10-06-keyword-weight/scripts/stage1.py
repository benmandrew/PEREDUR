#!/usr/bin/env python3
"""Stage 1 of the keyword similarity weight test: offline re-scoring.

Runs `keyword-terms` once per archived run, caches its two TSVs under
`<out>/<run>/`, and computes the PLAN.md measures for every trace weight w:
front agreement (Jaccard), term agreement (Spearman) and spread. Prints a short
report with the decision rule's verdict and writes `<out>/report.csv`.
"""

from __future__ import annotations

import argparse
import csv
import re
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from itertools import combinations
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]

SUBJECTS = ("fsm", "fsm-combined", "fsm-timing", "mode-arbiter", "takeoff")
SEEDS = range(10)
ARM = "sweep_K_mrs_nsga2-apportion_wkoff_log_{subject}_seed{seed:02d}"
WEIGHTS = (0.0, 0.25, 0.5, 0.75, 1.0)
LEGACY = "legacy"
DECISION_WEIGHTS = (0.25, 0.5, 0.75)
DECISION_THRESHOLD = 0.9
SPREAD_THRESHOLD = 0.05


@dataclass
class Run:
    name: str
    subject: str
    path: Path
    candidates: list[Path]
    bound: int | None
    metric: str | None


@dataclass
class Candidate:
    syntactic_order: float
    syntactic_token: float
    # Tombstones the driver restored to align the candidate with its original.
    restored: int
    # (removed, trace, keyword) per changed slot pair.
    pairs: list[tuple[bool, float, float]] = field(default_factory=list)

    def semantic(self, weight: float) -> float:
        if not self.pairs:
            return 1.0
        total = sum(
            0.0 if removed else weight * trace + (1.0 - weight) * keyword
            for removed, trace, keyword in self.pairs
        )
        return total / len(self.pairs)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument(
        "--archive",
        type=Path,
        default=Path(
            "/home/y19056ba/projects/counter/experiments/results-gradsel-fret-m"
        ),
    )
    parser.add_argument("--examples", type=Path, default=REPO / "examples")
    parser.add_argument(
        "--bin", type=Path, default=REPO / "build-release" / "keyword-terms"
    )
    parser.add_argument("--out", type=Path, default=HERE.parent / "out")
    parser.add_argument(
        "--jobs", type=int, default=4, help="driver processes run at once"
    )
    parser.add_argument(
        "--driver-jobs", type=int, default=2, help="--jobs passed to each driver"
    )
    parser.add_argument(
        "--limit", type=int, default=None, help="score only the first N runs"
    )
    return parser.parse_args()


def model_counting_settings(config: Path) -> tuple[int | None, str | None]:
    """The [model_counting] bound and metric, or None where the run left the
    default. Parsed by hand: python 3.10 has no tomllib."""
    bound = metric = None
    section = ""
    for raw in config.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        header = re.fullmatch(r"\[([^\]]+)\]", line)
        if header:
            section = header.group(1).strip()
            continue
        if section != "model_counting" or "=" not in line:
            continue
        key, value = (part.strip() for part in line.split("=", 1))
        if key == "default_bound":
            bound = int(value)
        elif key == "metric":
            metric = value.strip("\"'")
    return bound, metric


def discover_runs(archive: Path) -> list[Run]:
    runs = []
    for subject in SUBJECTS:
        for seed in SEEDS:
            name = ARM.format(subject=subject, seed=seed)
            path = archive / name
            index = path / "accumulated" / "index.tsv"
            if not index.is_file():
                print(f"warning: {name} has no accumulated/index.tsv", file=sys.stderr)
                continue
            with index.open(newline="") as handle:
                files = [row["file"] for row in csv.DictReader(handle, delimiter="\t")]
            bound, metric = model_counting_settings(path / "config.toml")
            runs.append(
                Run(
                    name=name,
                    subject=subject,
                    path=path,
                    candidates=[path / "accumulated" / f for f in files],
                    bound=bound,
                    metric=metric,
                )
            )
    return runs


def score_run(run: Run, args: argparse.Namespace) -> tuple[str, float | None]:
    """Runs the driver for @p run unless both TSVs are cached. Returns the run
    name and the wall time, or None when the cache was used."""
    out = args.out / run.name
    pairs, specs = out / "pairs.tsv", out / "specs.tsv"
    if pairs.is_file() and specs.is_file():
        return run.name, None
    out.mkdir(parents=True, exist_ok=True)
    tmp_pairs, tmp_specs = out / "pairs.tsv.tmp", out / "specs.tsv.tmp"
    command = [
        str(args.bin),
        "--original",
        str(args.examples / run.subject / "spec.json"),
        "--pairs",
        str(tmp_pairs),
        "--specs",
        str(tmp_specs),
        "--jobs",
        str(args.driver_jobs),
    ]
    if run.bound is not None:
        command += ["--bound", str(run.bound)]
    if run.metric is not None:
        command += ["--metric", run.metric]
    started = time.monotonic()
    result = subprocess.run(
        command,
        input="".join(f"{path}\n" for path in run.candidates),
        capture_output=True,
        text=True,
        check=False,
    )
    elapsed = time.monotonic() - started
    if result.returncode != 0:
        raise RuntimeError(f"{run.name}: keyword-terms failed\n{result.stderr}")
    # Renamed only once both are written, so a killed run is redone.
    tmp_pairs.replace(pairs)
    tmp_specs.replace(specs)
    return run.name, elapsed


def load_candidates(out: Path) -> dict[str, Candidate]:
    candidates: dict[str, Candidate] = {}
    with (out / "specs.tsv").open(newline="") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            candidates[Path(row["candidate"]).name] = Candidate(
                float(row["syntactic_order"]),
                float(row["syntactic_token"]),
                int(row["restored"]),
            )
    with (out / "pairs.tsv").open(newline="") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            candidates[Path(row["candidate"]).name].pairs.append(
                (row["removed"] == "1", float(row["trace"]), float(row["keyword"]))
            )
    return candidates


def first_front(points: dict[str, tuple[float, float]]) -> frozenset[str]:
    """The names no other point dominates, both coordinates maximised."""
    front = set()
    for name, (x_a, y_a) in points.items():
        dominated = any(
            x_b >= x_a and y_b >= y_a and (x_b > x_a or y_b > y_a)
            for x_b, y_b in points.values()
        )
        if not dominated:
            front.add(name)
    return frozenset(front)


def jaccard(lhs: frozenset[str], rhs: frozenset[str]) -> float:
    union = lhs | rhs
    return len(lhs & rhs) / len(union) if union else 1.0


def ranks(values: list[float]) -> list[float]:
    """Ranks from 1, ties sharing their mean rank."""
    order = sorted(range(len(values)), key=values.__getitem__)
    result = [0.0] * len(values)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and values[order[j + 1]] == values[order[i]]:
            j += 1
        for k in range(i, j + 1):
            result[order[k]] = (i + j) / 2.0 + 1.0
        i = j + 1
    return result


def spearman(xs: list[float], ys: list[float]) -> float:
    if len(xs) < 2:
        return float("nan")
    try:
        return statistics.correlation(ranks(xs), ranks(ys))
    except statistics.StatisticsError:
        return float("nan")


def weight_label(weight: float | str) -> str:
    return weight if isinstance(weight, str) else f"{weight:g}"


def main() -> int:
    args = parse_args()
    if not args.bin.is_file():
        print(f"error: no driver at {args.bin}", file=sys.stderr)
        return 1
    runs = discover_runs(args.archive)
    if args.limit is not None:
        runs = runs[: args.limit]
    if not runs:
        print("error: no runs found", file=sys.stderr)
        return 1

    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for name, elapsed in pool.map(lambda run: score_run(run, args), runs):
            note = "cached" if elapsed is None else f"{elapsed:.1f}s"
            print(f"{name}: {note}", file=sys.stderr)
    scoring_wall = time.monotonic() - started

    labels: list[float | str] = [*WEIGHTS, LEGACY]
    jaccards: dict[tuple[str, str], list[float]] = {
        (weight_label(a), weight_label(b)): [] for a, b in combinations(labels, 2)
    }
    traces: list[float] = []
    keywords: list[float] = []
    n_candidates = n_moved = n_realigned = 0
    moved_by_subject: dict[str, list[int]] = {}

    for run in runs:
        candidates = load_candidates(args.out / run.name)
        fronts: dict[str, frozenset[str]] = {}
        for weight in WEIGHTS:
            fronts[weight_label(weight)] = first_front(
                {
                    name: (cand.syntactic_token, cand.semantic(weight))
                    for name, cand in candidates.items()
                }
            )
        fronts[LEGACY] = first_front(
            {
                name: (cand.syntactic_order, cand.semantic(1.0))
                for name, cand in candidates.items()
            }
        )
        for a, b in jaccards:
            jaccards[(a, b)].append(jaccard(fronts[a], fronts[b]))
        tally = moved_by_subject.setdefault(run.subject, [0, 0])
        for cand in candidates.values():
            for removed, trace, keyword in cand.pairs:
                if not removed:
                    traces.append(trace)
                    keywords.append(keyword)
            moved = abs(cand.semantic(0.0) - cand.semantic(1.0)) > SPREAD_THRESHOLD
            n_moved += moved
            tally[0] += moved
            tally[1] += 1
        n_candidates += len(candidates)
        n_realigned += sum(cand.restored > 0 for cand in candidates.values())

    rows: list[tuple[str, str, str, str, float]] = []
    print(f"runs: {len(runs)}  candidates: {n_candidates}")
    print(f"driver wall time: {scoring_wall:.1f}s")
    print("\nfront Jaccard (median / min over runs)")
    for (a, b), values in jaccards.items():
        median, minimum = statistics.median(values), min(values)
        print(f"  w={a:>6} vs w={b:>6}: {median:.3f} / {minimum:.3f}")
        rows.append(("front_jaccard_median", "all", a, b, median))
        rows.append(("front_jaccard_min", "all", a, b, minimum))

    rho = spearman(traces, keywords)
    print(f"\nSpearman(trace, keyword) over {len(traces)} pairs: {rho:.3f}")
    rows.append(("spearman_trace_keyword", "all", "", "", rho))
    rows.append(("n_pairs", "all", "", "", float(len(traces))))

    share = n_moved / n_candidates if n_candidates else float("nan")
    print(f"share moving > {SPREAD_THRESHOLD} between w=0 and w=1: {share:.3f}")
    rows.append(("spread_share", "all", "0", "1", share))
    for subject, (moved, total) in sorted(moved_by_subject.items()):
        rows.append(("spread_share", subject, "0", "1", moved / total))
        print(f"  {subject}: {moved}/{total}")

    realigned_share = n_realigned / n_candidates if n_candidates else float("nan")
    print(
        f"candidates realigned around removed requirements: {n_realigned} "
        f"({realigned_share:.3f})"
    )
    rows.append(("realigned_share", "all", "", "", realigned_share))

    failing = [
        (a, b)
        for a, b in combinations(map(weight_label, DECISION_WEIGHTS), 2)
        if statistics.median(jaccards[(a, b)]) < DECISION_THRESHOLD
    ]
    if not failing:
        print(
            f"\nverdict: every median front Jaccard over w in "
            f"{{0.25, 0.5, 0.75}} is >= {DECISION_THRESHOLD}; w does not "
            "matter in practice. Fix w = 0.5 and skip stage 2."
        )
    else:
        named = ", ".join(f"{a} vs {b}" for a, b in failing)
        print(
            f"\nverdict: median front Jaccard below {DECISION_THRESHOLD} for "
            f"{named}; run stage 2 on those weights plus w = 1."
        )
    rows.append(("stage2_needed", "all", "", "", float(bool(failing))))

    args.out.mkdir(parents=True, exist_ok=True)
    with (args.out / "report.csv").open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(("measure", "scope", "w_a", "w_b", "value"))
        writer.writerows(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
