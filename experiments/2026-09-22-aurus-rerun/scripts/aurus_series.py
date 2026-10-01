#!/usr/bin/env python3
"""Emit AuRUS's solutions(t) step function from every archived run.

Upstream AuRUS leaves one time-resolved record, the run.log: its iteration line
carries the running length of `ga.solutions` in the #Sol column, and the
`Elapsed Time` line that follows dates it to the second. That is the only
series the archive holds for such a run, and the only one that survives a run
killed at the cap, whose solution files are never written.

The fork at `output-on-timeout` writes `solution-times.csv` as it goes, one row
per solution dated to the microsecond, so the step function is the solutions
themselves and needs no inference. A repeat carrying that file is read from it
and every other repeat from its log, which keeps one archive readable across
both vintages. `dated_by` says which record each row came from.
"""
import csv, os, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import score_aurus_anytime as A  # noqa: E402

ITER = re.compile(r"^(\d+)\t([\d.eE+-]+)\t\S+\t(\d+)\t(\d+)\s*$")
ELAPSED = re.compile(r"^Elapsed Time:\s*(\d+)\s*m\s+(\d+)\s*s")


def log_rows(log):
    """(iter, nsol, elapsed_s) per dated iteration line, in log order."""
    rows, pending = [], None
    with open(log, errors="replace") as handle:
        for line in handle:
            match = ITER.match(line)
            if match:
                pending = (int(match.group(1)), int(match.group(4)))
                continue
            match = ELAPSED.match(line.strip())
            if match and pending is not None:
                rows.append((pending[0], pending[1],
                             int(match.group(1)) * 60 + int(match.group(2))))
                pending = None
    return rows


def solution_rows(run_dir):
    """(generation, nsol, elapsed_s) per solution, in discovery order.

    nsol is the rank, so the series means what the log's #Sol column means: how
    many solutions the run held at that moment.
    """
    times = A.solution_times(run_dir)
    ordered = sorted(times.items(), key=lambda item: (item[1][1], item[0]))
    return [(generation, rank, elapsed)
            for rank, (_, (generation, elapsed)) in enumerate(ordered, 1)]


def main():
    root = os.path.expanduser(sys.argv[1])
    writer = csv.writer(sys.stdout)
    writer.writerow(["spec", "repeat", "iter", "nsol", "elapsed_s",
                     "dated_by"])
    for spec in sorted(os.listdir(root)):
        spec_dir = os.path.join(root, spec)
        if not os.path.isdir(spec_dir):
            continue
        for repeat in sorted(os.listdir(spec_dir)):
            run_dir = os.path.join(spec_dir, repeat)
            rows = solution_rows(run_dir)
            dated_by = "solution-times"
            if not rows:
                log = os.path.join(run_dir, "run.log")
                if not os.path.exists(log):
                    continue
                rows, dated_by = log_rows(log), "run-log"
            for iteration, nsol, elapsed in rows:
                writer.writerow([spec, repeat, iteration, nsol,
                                 f"{elapsed:.6f}", dated_by])


if __name__ == "__main__":
    main()
