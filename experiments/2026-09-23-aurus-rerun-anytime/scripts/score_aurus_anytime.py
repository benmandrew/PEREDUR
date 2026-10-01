#!/usr/bin/env python3
"""Score every AuRUS solution and date it, from the run's own record.

Two records exist, and which one a repeat carries says which AuRUS wrote it.

Upstream AuRUS (3f6f01f) writes all of a run's solutions in one batch when the
run ends, so no solution file carries its discovery time. The run.log does.
Main.java:170-187 writes `ga.solutions` in list order as spec<i>.tlsf, and the
log's per-iteration `#Sol` column is that list's length at that iteration, so
spec_i was found at the first iteration whose #Sol reaches i+1. That
iteration's `Elapsed Time` dates it, to second resolution. A run killed at the
cap writes nothing at all -- its solutions die with the JVM -- so such a repeat
yields `files = 0` against a positive final #Sol, and is emitted with status
`lost-at-cap` rather than dropped, because the gap between what AuRUS found and
what it returned is the thing worth seeing.

The fork at `output-on-timeout` (e1cfadf) writes each solution as it is found
and appends its discovery time to `solution-times.csv`, to the microsecond. A
repeat carrying that file needs neither inference: the time is AuRUS's own and
a run killed at the cap keeps everything it found. `solution_times()` reads it
and every caller prefers it where it is present, so one archive may hold
repeats of both vintages and each is dated the best way it can be.

One row per (spec, repeat, index): the verdict, whether it implies an ideal,
and the elapsed seconds at which it was found.
"""
import csv, os, re, subprocess, sys, concurrent.futures as cf

ITER = re.compile(r"^(\d+)\t([\d.eE+-]+)\t\S+\t(\d+)\t(\d+)\s*$")
ELAPSED = re.compile(r"^Elapsed Time:\s*(\d+)\s*m\s+(\d+)\s*s")
VERDICT = re.compile(r"^(spec\d+\.tlsf)\s*:\s*(.+?)\s*$")
SPEC_I = re.compile(r"^spec(\d+)\.tlsf$")
HIT = ("equivalent to", "strictly stronger than")
TIMES_NAME = "solution-times.csv"
FIELDS = ["spec", "repeat", "index", "verdict", "hit", "found_iter",
          "found_elapsed_s", "final_nsol", "files_written", "status",
          "dated_by"]


def iteration_series(log):
    """[(iter, nsol, elapsed_s)] in order. Elapsed follows its iteration line."""
    out, pending = [], None
    try:
        with open(log, errors="replace") as h:
            for line in h:
                m = ITER.match(line)
                if m:
                    pending = (int(m.group(1)), int(m.group(4)))
                    continue
                m = ELAPSED.match(line.strip())
                if m and pending is not None:
                    out.append((pending[0], pending[1],
                                int(m.group(1)) * 60 + int(m.group(2))))
                    pending = None
    except OSError:
        return []
    return out


def found_at(series, index):
    """Elapsed seconds at which solution `index` entered ga.solutions."""
    for it, nsol, elapsed in series:
        if nsol >= index + 1:
            return it, elapsed
    return "", ""


def solution_times(run_dir):
    """{index: (generation, elapsed_s)} from the fork's per-solution log.

    `solution-times.csv` is written a row at a time as each solution is found:
    `solution,seconds,generation,fitness,timestamp`, where `seconds` is elapsed
    from the run's start to the microsecond and `generation` is -1 for the
    solutions AuRUS confirms after its search loop. A row whose file is missing
    from the directory is dropped rather than dated, and the last write of a
    killed run may be short, so a malformed final row is ignored instead of
    failing the repeat.
    """
    path = os.path.join(run_dir, TIMES_NAME)
    times = {}
    try:
        with open(path, newline="", errors="replace") as handle:
            for row in csv.DictReader(handle):
                match = SPEC_I.match((row.get("solution") or "").strip())
                if not match:
                    continue
                try:
                    elapsed = float(row["seconds"])
                    generation = int(row["generation"])
                except (KeyError, TypeError, ValueError):
                    continue
                times[int(match.group(1))] = (generation, elapsed)
    except OSError:
        return {}
    return times


def score_dir(binary, repairs, ideals, timeout_s):
    try:
        p = subprocess.run([binary, "--repairs", repairs, "--ideals", ideals],
                           capture_output=True, text=True, timeout=timeout_s)
    except subprocess.TimeoutExpired:
        return None, "timeout"
    if p.returncode != 0:
        return None, f"exit-{p.returncode}"
    out = {}
    for line in p.stdout.splitlines():
        m = VERDICT.match(line.strip())
        if m:
            out[m.group(1)] = m.group(2)
    return out, "ok"


def one_repeat(args):
    binary, examples, d, timeout_s = args
    repeat = os.path.basename(d)
    spec = os.path.basename(os.path.dirname(d))
    ideals = os.path.join(examples, spec, "fixes")
    series = iteration_series(os.path.join(d, "run.log"))
    times = solution_times(d)
    # The fork writes a solution before the iteration line that would count it,
    # so its own file count is the later record of the two.
    final_nsol = max(len(times), series[-1][1] if series else 0)
    files = sorted(f for f in os.listdir(d) if SPEC_I.match(f))
    base = {"spec": spec, "repeat": repeat, "final_nsol": final_nsol,
            "files_written": len(files),
            "dated_by": "solution-times" if times else "run-log"}
    if not os.path.isdir(ideals):
        return [dict(base, index="", verdict="", hit="", found_iter="",
                     found_elapsed_s="", status="no-ideals-dir")]
    if not files:
        # Upstream: killed before the single write at the end. The fork writes
        # as it finds, so under it this pairing means the files went missing
        # after the run, which is the same loss and reads the same way.
        return [dict(base, index="", verdict="", hit="", found_iter="",
                     found_elapsed_s="",
                     status="lost-at-cap" if final_nsol else "no-solutions")]
    verdicts, status = score_dir(binary, d, ideals, timeout_s)
    if verdicts is None:
        return [dict(base, index="", verdict="", hit="", found_iter="",
                     found_elapsed_s="", status=status)]
    rows = []
    for fname, verdict in verdicts.items():
        m = SPEC_I.match(fname)
        if not m:
            continue
        i = int(m.group(1))
        if i in times:
            it, elapsed = times[i]
            dated_by = "solution-times"
        else:
            it, elapsed = found_at(series, i)
            dated_by = "run-log" if elapsed != "" else ""
        rows.append(dict(base, index=i, verdict=verdict,
                         hit=int(verdict.startswith(HIT)),
                         found_iter=it, found_elapsed_s=elapsed, status="ok",
                         dated_by=dated_by))
    return rows


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--root",
                    help="an AuRUS tree; every <spec>/repeat-* under it is "
                         "scored")
    # The alternative to walking a whole tree: the repeat directories to
    # score, named explicitly. aurus_score_campaign.py enforces one host's
    # seed split, and the split has to be enforced where the campaign's
    # declaration is read rather than rediscovered from a directory listing
    # here -- two hosts sharing a tree would otherwise each walk all of it.
    ap.add_argument("--dirs", nargs="+", metavar="DIR",
                    help="repeat directories to score, in place of --root")
    ap.add_argument("--examples", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=3600)
    a = ap.parse_args()
    if bool(a.root) == bool(a.dirs):
        ap.error("give exactly one of --root and --dirs")

    # Normalised because spec and repeat are read off the last two path
    # components, and a trailing slash makes the repeat the empty string.
    dirs = sorted(os.path.normpath(d) for d in a.dirs) if a.dirs else sorted(
        os.path.join(a.root, s, r)
        for s in os.listdir(a.root)
        if os.path.isdir(os.path.join(a.root, s))
        for r in os.listdir(os.path.join(a.root, s))
        if r.startswith("repeat-"))
    done = set()
    if os.path.exists(a.out):
        with open(a.out) as h:
            done = {(r["spec"], r["repeat"]) for r in csv.DictReader(h)}
    todo = [d for d in dirs
            if (os.path.basename(os.path.dirname(d)), os.path.basename(d)) not in done]
    print(f"{len(dirs)} repeats, {len(done)} scored, {len(todo)} to do", flush=True)

    fresh = not os.path.exists(a.out)
    with open(a.out, "a", newline="") as h:
        w = csv.DictWriter(h, fieldnames=FIELDS)
        if fresh:
            w.writeheader()
        work = [(a.binary, a.examples, d, a.timeout) for d in todo]
        with cf.ProcessPoolExecutor(max_workers=a.jobs) as ex:
            for i, rows in enumerate(ex.map(one_repeat, work), 1):
                for row in rows:
                    w.writerow(row)
                h.flush()
                if i % 10 == 0 or i == len(work):
                    print(f"[{i}/{len(work)}]", flush=True)


if __name__ == "__main__":
    main()
