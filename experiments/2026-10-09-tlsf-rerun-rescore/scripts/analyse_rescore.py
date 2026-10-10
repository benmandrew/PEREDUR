#!/usr/bin/env python3
"""The 2026-10-09-tlsf-rerun-rescore close: what the re-score cost and moved.

Reads from experiments/ the collected outputs of the two phases and the two
spliced files splice.py wrote:

  curves-tlsf-rerun-rescore/<host>/                    maximality
  separation-recount-tlsf-rerun-s0-rescore/<host>/     recount
  curves-tlsf-rerun-spliced.csv
  separation-recount-tlsf-rerun-s0-spliced.csv

and from --original-dir the parent archive's merged files, which it only
reads:

  curves-tlsf-rerun.csv
  separation-recount-tlsf-rerun-s0.csv

It prints, per phase and host, the timings ledger of the parent's
analyse_scoring.py, the scorer starts in warnings.log and any curve without
a members sidecar; then per arm the mean count at the 7200 s cut of every
metric the phase owns, over all 750 runs an arm (a run without the metric
counts zero), before the splice, after it, and the difference; then the
re-scored runs whose curve lacks a metric the original held.

Run from the repository root:

  python3 experiments/2026-10-09-tlsf-rerun-rescore/scripts/analyse_rescore.py \
      --original-dir <checkout holding the parent's merged CSVs>/experiments
"""
import argparse
import collections
import csv
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
EXP = HERE.parent
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
NAME = re.compile(r"^sweep_G_(mrs|aurus)_(nsga2-apportion|weighted)_wkoff_log_"
                  r"(.+)_seed(\d+)$")


def rule(title):
    print()
    print("=" * 78)
    print(title)
    print("=" * 78)


def rescored_keys():
    keys = set()
    for line in (HERE / "runs.txt").read_text().split():
        grading, selection, spec, seed = NAME.match(line).groups()
        keys.add((spec, seed, selection, grading))
    return keys


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
        failures = [f for f in
                    (host_dir / "failures.txt").read_text().split("\n")
                    if f.strip()]
        manifest = json.loads(
            next(host_dir.glob("score-manifest-*.json")).read_text())
        print(f"-- {host_dir.name}: {len(lines)} attempts over {len(runs)} "
              f"runs, {hours:.2f} worker-h, rc "
              + ", ".join(f"{k}: {v}" for k, v in sorted(rcs.items()))
              + f", longest {longest:.0f} s, {at_deadline} at the "
              f"{deadline} s deadline, {len(failures)} failures")
        print(f"   manifest: commit {manifest['git']['head']}, started "
              f"{manifest['started']}, finished {manifest['finished']}, "
              f"counts {json.dumps(manifest.get('counts', {}), sort_keys=True)}")
        kinds = collections.Counter()
        starts = 0
        for line in (host_dir / "warnings.log").read_text().splitlines():
            if line.startswith("==="):
                starts += 1
                continue
            if not line.strip():
                continue
            line = re.sub(r"/\S*/results-tlsf-rerun/sweep_G[^/]*/", "<run>/",
                          line)
            line = re.sub(r"sweep_G_\S+_seed\d+", "<run>", line)
            kinds[re.sub(r"\[Errno \d+\] ", "", line)[:72]] += 1
        print(f"   scorer starts in warnings.log: {starts}")
        for kind, n in kinds.most_common():
            print(f"   warning x{n}: {kind}")
        curves = {p.name[:-4] for p in host_dir.glob("*.csv")}
        members = {p.name[:-len(".members.tsv")]
                   for p in host_dir.glob("*.members.tsv")}
        for run in sorted(curves - members):
            print(f"   curve without a members sidecar: {run}")
    print(f"-- total {total_h:.2f} worker-h")


def finals_at_cut(path, metrics):
    finals = collections.defaultdict(dict)
    with open(path, newline="") as handle:
        for row in csv.DictReader(handle):
            if row["metric"] not in metrics or not row["elapsed_s"]:
                continue
            if float(row["elapsed_s"]) > CUT:
                continue
            key = (row["spec"], row["seed"], row["selection_scheme"],
                   row["status_grading"])
            previous = finals[key].get(row["metric"], (-1.0, 0))
            when = float(row["elapsed_s"])
            if when >= previous[0]:
                finals[key][row["metric"]] = (when, int(row["value"]))
    return finals


def arm_means(finals, metric):
    sums = collections.Counter()
    for key, per in finals.items():
        sums[f"{key[2]}/{key[3]}"] += per.get(metric, (0, 0))[1]
    return [sums[arm] / RUNS_PER_ARM for arm in ARMS]


def compare(before, after, metrics, keys):
    print(f"   {'metric':28s}{'':9s}" + "".join(f"{a:>23s}" for a in ARMS))
    for metric in metrics:
        old, new = arm_means(before, metric), arm_means(after, metric)
        for label, values in (("original", old), ("spliced", new),
                              ("change", [n - o for o, n in zip(old, new)])):
            fmt = "{:+23.2f}" if label == "change" else "{:23.2f}"
            print(f"   {metric if label == 'original' else '':28s}"
                  f"{label:9s}" + "".join(fmt.format(v) for v in values))
    untouched = sum(before[key] != after[key]
                    for key in set(before) | set(after) if key not in keys)
    print(f"-- runs outside runs.txt whose values at the cut differ: "
          f"{untouched}")
    per_arm = collections.Counter(f"{k[2]}/{k[3]}" for k in keys)
    print("-- re-scored runs an arm: "
          + ", ".join(f"{arm} {per_arm[arm]}" for arm in ARMS))
    for metric in metrics:
        changed = [key for key in keys
                   if before[key].get(metric, (0, 0))[1]
                   != after[key].get(metric, (0, 0))[1]]
        drop = sum(before[key].get(metric, (0, 0))[1]
                   - after[key].get(metric, (0, 0))[1] for key in keys)
        print(f"-- {metric}: {len(changed)} of {len(keys)} re-scored runs "
              f"changed at the cut, summed fall {drop}")
    for key in sorted(keys):
        lost = [m for m in metrics
                if m in before[key] and m not in after[key]]
        if lost:
            print(f"-- {'/'.join(key)}: re-scored curve lacks "
                  + ", ".join(f"{m} (original {before[key][m][1]})"
                              for m in lost))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original-dir", type=Path, default=EXP)
    args = parser.parse_args()
    keys = rescored_keys()
    for phase, (name, deadline) in PHASES.items():
        rule(f"{phase.upper()} -- {name}-rescore")
        ledger(f"{name}-rescore", deadline)
        print(f"-- mean per run at the {CUT:.0f} s cut, over {RUNS_PER_ARM} "
              f"runs an arm: {name}.csv against {name}-spliced.csv")
        compare(finals_at_cut(args.original_dir / f"{name}.csv", OWNED[phase]),
                finals_at_cut(EXP / f"{name}-spliced.csv", OWNED[phase]),
                OWNED[phase], keys)
    return 0


if __name__ == "__main__":
    sys.exit(main())
