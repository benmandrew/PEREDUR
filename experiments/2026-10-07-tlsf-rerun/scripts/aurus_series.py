#!/usr/bin/env python3
"""Emit AuRUS's per-iteration solution series from every archived run.log.

Main.java's iteration line carries the running length of `ga.solutions` in its
#Sol column, and the `Elapsed Time` line that follows dates it to the second,
so the pair is AuRUS's own solutions(t) step function -- the only time-resolved
record the archive holds, and the one that survives a run killed at the cap,
whose solution files are never written.
"""
import csv, os, re, sys

ITER = re.compile(r"^(\d+)\t([\d.eE+-]+)\t\S+\t(\d+)\t(\d+)\s*$")
ELAPSED = re.compile(r"^Elapsed Time:\s*(\d+)\s*m\s+(\d+)\s*s")

root = os.path.expanduser(sys.argv[1])
w = csv.writer(sys.stdout)
w.writerow(["spec", "repeat", "iter", "nsol", "elapsed_s"])
for spec in sorted(os.listdir(root)):
    sd = os.path.join(root, spec)
    if not os.path.isdir(sd):
        continue
    for rep in sorted(os.listdir(sd)):
        log = os.path.join(sd, rep, "run.log")
        if not os.path.exists(log):
            continue
        pending = None
        with open(log, errors="replace") as h:
            for line in h:
                m = ITER.match(line)
                if m:
                    pending = (int(m.group(1)), int(m.group(4)))
                    continue
                m = ELAPSED.match(line.strip())
                if m and pending is not None:
                    w.writerow([spec, rep, pending[0], pending[1],
                                int(m.group(1)) * 60 + int(m.group(2))])
                    pending = None
