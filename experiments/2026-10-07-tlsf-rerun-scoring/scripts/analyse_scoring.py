#!/usr/bin/env python3
"""The 2026-10-07-tlsf-rerun-scoring close: what each pass cost and wrote.

Reads the collected outputs of the two phases from experiments/:

  curves-tlsf-rerun.csv, curves-tlsf-rerun/<host>/        maximality
  separation-recount-tlsf-rerun-s0.csv, ...-s0/<host>/     recount

and prints, per phase and host, the attempts in timings.txt, their worker
hours, return codes and any run at its deadline, the warnings by kind, and
per arm the mean count at the 7200 s cut of every metric the phase owns,
over all 750 runs an arm (a run without the metric counts zero).

Run from the repository root:

  python3 experiments/2026-10-07-tlsf-rerun-scoring/scripts/analyse_scoring.py
"""
import collections
import csv
import json
import re
import sys
from pathlib import Path

EXP = Path(__file__).resolve().parents[3] / "experiments"
PHASES = {"maximality": ("curves-tlsf-rerun", 7500),
          "recount": ("separation-recount-tlsf-rerun-s0", 900)}
OWNED = {"maximality": ("solutions", "ideal_solutions", "maximal_solutions",
                        "maximal_ideal_solutions"),
         "recount": tuple(f"eps_{kind}_{eps}" for kind in
                          ("solutions", "maximal_solutions")
                          for eps in ("0.05", "0.2", "0.5"))}
ARMS = ("nsga2-apportion/mrs", "nsga2-apportion/aurus",
        "weighted/mrs", "weighted/aurus")
CUT = 7200.0
RUNS_PER_ARM = 750


def rule(title):
    print()
    print("=" * 78)
    print(title)
    print("=" * 78)


def ledger(name, deadline):
    total_h = 0.0
    for host_dir in sorted((EXP / name).iterdir()):
        if not host_dir.is_dir():
            continue
        lines = [line.split() for line in
                 (host_dir / "timings.txt").read_text().splitlines() if line]
        runs = {fields[3] for fields in lines}
        hours = sum(float(fields[1]) for fields in lines) / 3600
        total_h += hours
        rcs = collections.Counter(fields[2] for fields in lines)
        longest = max(float(fields[1]) for fields in lines)
        at_deadline = sum(float(fields[1]) >= deadline for fields in lines)
        failures = (host_dir / "failures.txt").read_text().split("\n")
        failures = [f for f in failures if f.strip()]
        manifest = json.loads(
            next(host_dir.glob("score-manifest-*.json")).read_text())
        print(f"-- {host_dir.name}: {len(lines)} attempts over {len(runs)} "
              f"runs, {hours:.2f} worker-h, rc "
              + ", ".join(f"{k}: {v}" for k, v in sorted(rcs.items()))
              + f", longest {longest:.0f} s, {at_deadline} at the "
              f"{deadline} s deadline, {len(failures)} failures")
        print(f"   manifest: commit {manifest['git']['head']}, counts "
              f"{json.dumps(manifest.get('counts', {}), sort_keys=True)}")
        kinds = collections.Counter()
        for line in (host_dir / "warnings.log").read_text().splitlines():
            if line.startswith("===") or not line.strip():
                continue
            line = re.sub(r"/\S*/results-tlsf-rerun/sweep_G[^/]*/", "<run>/",
                          line)
            kinds[re.sub(r"\[Errno \d+\] ", "", line)[:72]] += 1
        for kind, n in kinds.most_common():
            print(f"   warning x{n}: {kind}")
    print(f"-- total {total_h:.2f} worker-h")


def means(name, metrics):
    finals = collections.defaultdict(dict)
    arm_of = {}
    with open(EXP / f"{name}.csv", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["metric"] not in metrics or not row["elapsed_s"]:
                continue
            if float(row["elapsed_s"]) > CUT:
                continue
            key = (row["spec"], row["seed"], row["selection_scheme"],
                   row["status_grading"])
            arm_of[key] = f"{row['selection_scheme']}/{row['status_grading']}"
            previous = finals[key].get(row["metric"], (-1.0, 0))
            when = float(row["elapsed_s"])
            if when >= previous[0]:
                finals[key][row["metric"]] = (when, int(row["value"]))
    print(f"   {'metric':32s}" + "".join(f"{a:>23s}" for a in ARMS))
    for metric in metrics:
        sums = collections.Counter()
        for key, per in finals.items():
            sums[arm_of[key]] += per.get(metric, (0, 0))[1]
        print(f"   {metric:32s}" + "".join(
            f"{sums[a] / RUNS_PER_ARM:23.2f}" for a in ARMS))


def main():
    for phase, (name, deadline) in PHASES.items():
        rule(f"{phase.upper()} -- {name}")
        ledger(name, deadline)
        print(f"-- mean per run at the {CUT:.0f} s cut, over {RUNS_PER_ARM} "
              "runs an arm")
        means(name, OWNED[phase])
    return 0


if __name__ == "__main__":
    sys.exit(main())
