#!/usr/bin/env python3
"""The 2026-10-07-tlsf-rerun analysis: analyse_matched at this campaign's names,
then the three checks this campaign adds.

analyse_matched.py is vendored verbatim beside this file at 953732b and is
the paper-rerun's analysis unchanged. Its defaults name the rematch's files and
its AuRUS reference is a module constant resolved against the vendored copy's
own location, so this wrapper points both at the repository's experiments/
before calling its main(). The reference is experiments/aurus-reference, the
archived AuRUS arm the paper-rerun's printout was read against, so the
registered primary compares like with like.

The three sections it appends:

  HOSTS            the seed split av2 0-12 and 19-24, av3 13-18, av1 25-29,
                   with each host's rows, binary commits, core-hours and kills
  CALIBRATION      av1's 2026-10-07-tlsf-rerun-calib seed-0 runs (gcc 13)
                   against av2's seed-0 runs of the same cells (gcc 11):
                   output byte for byte with wall-clock fields masked, and
                   wall time as a ratio
  ACCUMULATOR      run directories whose accumulated/index.tsv holds two
                   attempts, the runner having re-run a killed run into the
                   directory it had already written; the accumulator appends
                   to index.tsv, and score_curves.read_index reads both

Run from the repository root:

  python3 experiments/2026-10-07-tlsf-rerun/scripts/analyse_rerun.py
"""
import collections
import csv
import json
import math
import re
import statistics
import sys
from math import comb
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
EXP = REPO / "experiments"
sys.path.insert(0, str(HERE))

import analyse_matched as analyse  # noqa: E402

RESULTS = EXP / "results-tlsf-rerun.csv"
CURVES = EXP / "curves-tlsf-rerun.csv"
RUNS = EXP / "results-tlsf-rerun"
CALIB = EXP / "results-tlsf-rerun-calib.csv"
CALIB_RUNS = EXP / "results-tlsf-rerun-calib"
CURVE_DIRS = EXP / "curves-tlsf-rerun"

# campaign.toml at 953732b. av2's seeds 0-12 ran under the first split, at
# 43ab6c2, which differs from 953732b in campaign.toml and PLAN.md alone.
HOSTS = {"av2": set(range(0, 13)) | set(range(19, 25)),
         "av3": set(range(13, 19)),
         "av1": set(range(25, 30))}
ARM_ORDER = ("nsga2-apportion/mrs", "nsga2-apportion/aurus",
             "weighted/mrs", "weighted/aurus")
CAP_S = 7200


def rule(title):
    print()
    print("=" * 78)
    print(title)
    print("=" * 78)


def host_of(seed):
    return next(h for h, seeds in HOSTS.items() if seed in seeds)


def run_name(row):
    return (f"sweep_G_{row['level_name']}_{row['selection']}_wkoff_log_"
            f"{row['spec']}_seed{int(row['seed']):02d}")


def ranges(seeds):
    """0,1,2,5,6 -> 0-2,5-6."""
    spans, start = [], seeds[0]
    for prev, cur in zip(seeds, seeds[1:] + [None]):
        if cur != prev + 1 if cur is not None else True:
            spans.append(f"{start}-{prev}" if prev != start else f"{start}")
            start = cur
    return ",".join(spans)


def hosts(rows):
    rule("HOSTS -- the seed split of campaign.toml at 953732b")
    per = collections.defaultdict(list)
    for row in rows:
        per[host_of(int(row["seed"]))].append(row)
    total_h = 0.0
    for host in ("av2", "av3", "av1"):
        group = per[host]
        commits = collections.Counter(
            (r["commit"], r["dirty"]) for r in group)
        core_h = sum(float(r["wall_time_s"]) for r in group) / 3600
        total_h += core_h
        killed = sum(r["timed_out"] == "1" for r in group)
        seeds = sorted({int(r["seed"]) for r in group})
        print(f"-- {host}: seeds {ranges(seeds)}, {len(seeds)} seeds, "
              f"{len(group)} rows")
        for (commit, dirty), n in sorted(commits.items()):
            seeds_c = sorted({int(r["seed"]) for r in group
                              if r["commit"] == commit})
            print(f"   commit {commit} dirty={dirty}: {n} rows, seeds "
                  f"{','.join(map(str, seeds_c))}")
        print(f"   {core_h:.2f} core-h, {killed} killed at the cap")
        mtimes = [(RUNS / run_name(r) / "run.log").stat().st_mtime
                  for r in group if (RUNS / run_name(r) / "run.log").exists()]
        if mtimes:
            import datetime
            last = datetime.datetime.fromtimestamp(max(mtimes))
            print(f"   last run.log written {last:%Y-%m-%dT%H:%M:%S} "
                  "(local mtime, preserved by rsync -a)")
    print(f"-- total {total_h:.2f} core-h over {len(rows)} rows")


def masked(directory):
    """Every file of a run directory, wall-clock fields masked."""
    out = {}
    for path in sorted(directory.rglob("*")):
        if path.is_dir():
            continue
        rel = str(path.relative_to(directory))
        if rel == "run.json":
            continue
        text = path.read_text(errors="replace")
        if rel == "run.log":
            text = re.sub(r"time: *[0-9.]+s", "", text)
            text = re.sub(r"Done in [0-9.]+s", "", text)
            text = re.sub(r"experiments/results-tlsf-rerun(-calib)?/",
                          "experiments/X/", text)
            text = re.sub(r"[ \t]+", " ", text)
        if rel.endswith("index.tsv"):
            text = "\n".join("\t".join(line.split("\t")[:2])
                             for line in text.splitlines())
        out[rel] = text
    return out


def tool_timeouts(directory):
    try:
        manifest = json.loads((directory / "run.json").read_text())
    except OSError:
        return None
    return sum(v.get("timeouts", 0)
               for v in manifest.get("tool_calls", {}).values())


def sign_p(up, n):
    """Exact two-sided sign test."""
    k = max(up, n - up)
    return min(1.0, 2 * sum(comb(n, i) for i in range(k, n + 1)) / 2 ** n)


def calibration(rows):
    rule("CALIBRATION -- av1 (gcc 13) seed 0 against av2 (gcc 11) seed 0")
    with open(CALIB, newline="") as handle:
        calib = list(csv.DictReader(handle))
    main = {(r["selection"], r["level_name"], r["spec"]): r
            for r in rows if r["seed"] == "0"}
    commits = collections.Counter((r["commit"], r["dirty"]) for r in calib)
    print(f"calibration rows {len(calib)}: "
          + ", ".join(f"{c} dirty={d} {n}" for (c, d), n in commits.items()))
    print("Masked: run.json, run.log time columns and the run directory "
          "path, index.tsv elapsed_s.")
    identical, ratios_ident, ratios_both = [], [], []
    print(f"   {'arm':22s} {'family':20s} {'output':8s} {'av1 s':>8s} "
          f"{'av2 s':>8s} {'ratio':>6s} {'tool t/o av1/av2':>17s}")
    for row in sorted(calib, key=lambda r: (r["selection"], r["level_name"],
                                            r["spec"])):
        key = (row["selection"], row["level_name"], row["spec"])
        other = main[key]
        name = run_name(row)
        a, b = masked(CALIB_RUNS / name), masked(RUNS / name)
        diff = [f for f in set(a) | set(b) if a.get(f) != b.get(f)]
        wa, wb = float(row["wall_time_s"]), float(other["wall_time_s"])
        ta, tb = tool_timeouts(CALIB_RUNS / name), tool_timeouts(RUNS / name)
        same = not diff
        identical.append(same)
        if same:
            ratios_ident.append(wa / wb)
        if row["timed_out"] == "0" and other["timed_out"] == "0":
            ratios_both.append(wa / wb)
        print(f"   {key[0] + '/' + key[1]:22s} {key[2]:20s} "
              f"{'same' if same else f'{len(diff)} files':8s} {wa:8.1f} "
              f"{wb:8.1f} {wa / wb:6.3f} {str(ta) + '/' + str(tb):>17s}")
    print(f"-- output identical on {sum(identical)} of {len(identical)}")
    for label, ratios in (("identical output", ratios_ident),
                          ("both finished", ratios_both)):
        up = sum(r > 1 for r in ratios)
        geo = math.exp(statistics.mean(math.log(r) for r in ratios))
        print(f"-- wall av1/av2 over {label}: n {len(ratios)}, median "
              f"{statistics.median(ratios):.3f}, geometric mean {geo:.3f}, "
              f"range {min(ratios):.3f}-{max(ratios):.3f}, av1 slower on "
              f"{up}, exact sign test p = {sign_p(up, len(ratios)):.4f}")
    # Seed noise: within each (arm, family) cell over the 30 seeds of the main
    # campaign, the median distance of a finished run's wall time from its
    # cell's median, as a factor.
    families = sorted({r["spec"] for r in calib})
    print("-- seed noise, median |log(wall / cell median)| over finished "
          "runs of the main campaign, as a factor:")
    for family in families:
        devs = []
        for arm in ARM_ORDER:
            sel, grd = arm.split("/")
            walls = [float(r["wall_time_s"]) for r in rows
                     if r["spec"] == family and r["selection"] == sel
                     and r["level_name"] == grd and r["timed_out"] == "0"]
            if len(walls) < 2:
                continue
            med = statistics.median(walls)
            devs += [abs(math.log(w / med)) for w in walls]
        ratios = [float(r["wall_time_s"]) / float(main[(r["selection"],
                  r["level_name"], r["spec"])]["wall_time_s"])
                  for r in calib if r["spec"] == family]
        print(f"   {family:20s} noise x{math.exp(statistics.median(devs)):.3f}"
              f"   calibration ratios "
              + " ".join(f"{x:.3f}" for x in sorted(ratios)))


def index_segments(path):
    segments = []
    for line in path.read_text().splitlines():
        fields = line.split("\t")
        if fields[0] == "file":
            segments.append([])
            continue
        if len(fields) == 3:
            segments[-1].append((fields[0], int(fields[1]), float(fields[2])))
    return segments


def curve_points(run):
    """{metric: [(elapsed_s, value)]} from the run's curve CSV, any host."""
    for host_dir in CURVE_DIRS.iterdir():
        path = host_dir / f"{run}.csv"
        if path.exists():
            points = collections.defaultdict(list)
            with open(path, newline="") as handle:
                for row in csv.DictReader(handle):
                    if row["elapsed_s"]:
                        points[row["metric"]].append(
                            (float(row["elapsed_s"]), int(row["value"])))
            return points
    return None


def step_at(points, moment):
    value = 0
    for when, count in points:
        if when <= moment:
            value = count
    return value


def accumulator(rows):
    rule("ACCUMULATOR -- run directories holding two attempts")
    print("A run killed by a dequeue is re-run under the resume key into the")
    print("same directory. The accumulator appends to accumulated/index.tsv,")
    print("so the index holds both attempts, and score_curves.read_index")
    print("skips the second header and counts every row.")
    affected = []
    for row in rows:
        path = RUNS / run_name(row) / "accumulated" / "index.tsv"
        if not path.exists():
            continue
        segments = index_segments(path)
        if len(segments) > 1:
            affected.append((row, segments))
    print(f"-- {len(affected)} of {len(rows)} run directories hold "
          f"{'/'.join(sorted({str(len(s)) for _, s in affected}))} attempts")
    by_seed = collections.Counter(int(r["seed"]) for r, _ in affected)
    by_host = collections.Counter(host_of(int(r["seed"])) for r, _ in affected)
    print("   by seed: " + ", ".join(f"{s}: {n}" for s, n in
                                     sorted(by_seed.items())))
    print("   by host: " + ", ".join(f"{h}: {n}" for h, n in
                                     sorted(by_host.items())))
    by_arm = collections.Counter(f"{r['selection']}/{r['level_name']}"
                                 for r, _ in affected)
    print("   by arm:  " + ", ".join(f"{a}: {by_arm[a]}" for a in ARM_ORDER))
    prefix = diverge = stale_names = 0
    corrected = collections.defaultdict(lambda: [0.0, 0.0, 0.0, 0.0])
    for row, segments in affected:
        first, last = segments[0], segments[-1]
        same = [x[:2] for x in first] == [x[:2] for x in last[:len(first)]]
        prefix += same
        diverge += not same
        names_last = {x[0] for x in last}
        stale_names += len({x[0] for x in first} - names_last)
        arm = f"{row['selection']}/{row['level_name']}"
        points = curve_points(run_name(row))
        every = [x for s in segments for x in s]
        ideal_times = set()
        if points:
            previous = 0
            for when, count in points.get("ideal_solutions", []):
                if count > previous:
                    ideal_times.add(when)
                previous = count
        implying = {x[0] for x in every if x[2] in ideal_times}
        at_cap = [x for x in every if x[2] <= CAP_S]
        last_cap = [x for x in last if x[2] <= CAP_S]
        corrected[arm][0] += len(at_cap)
        corrected[arm][1] += len(last_cap)
        corrected[arm][2] += sum(x[0] in implying for x in at_cap)
        corrected[arm][3] += sum(x[0] in implying for x in last_cap)
    print(f"-- {prefix} runs repeat the killed attempt's rows as a prefix "
          f"(same files, same generations); {diverge} diverge")
    print(f"   {stale_names} file names indexed only by a killed attempt; "
          "their files are on disk from that attempt and are scored")
    print("-- effect at the 7200 s cut on the per-arm mean over 750 runs")
    print("   solutions counts every index row; the corrected count keeps the")
    print("   last attempt's rows alone. ideal_solutions likewise, an indexed")
    print("   row counting as ideal when its time is a step of the run's")
    print("   ideal_solutions curve.")
    for arm in ARM_ORDER:
        s_now, s_fix, i_now, i_fix = corrected[arm]
        print(f"   {arm:22s} solutions over-count {s_now - s_fix:6.0f} "
              f"rows ({(s_now - s_fix) / 750:+.2f} a run), ideal_solutions "
              f"{i_now - i_fix:4.0f} rows ({(i_now - i_fix) / 750:+.3f} a run)")
    print("   maximal_solutions and the epsilon counts read a file once, so a")
    print("   repeated row cannot double them; only the diverging runs' stale")
    print("   files can move them.")
    return affected


def main():
    analyse.AURUS_DIR = EXP / "aurus-reference"
    sys.argv = ["analyse_matched.py", "--results", str(RESULTS),
                "--curves", str(CURVES), "--runs-dir", str(RUNS),
                "--primary", "nsga2-apportion/mrs"]
    analyse.main()
    sys.stdout.flush()
    with open(RESULTS, newline="") as handle:
        rows = list(csv.DictReader(handle))
    hosts(rows)
    calibration(rows)
    accumulator(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
