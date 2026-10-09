#!/usr/bin/env python3
"""maoz_score.py's relations.csv from the coverage phase's compare output.

    python3 coverage_relations.py OUT/relations.csv COVERAGE-<host>.csv...

Each input row's id is cov|tool|spec|tool_file|peredur_md5 and its relation
is the PEREDUR pool repair's relation to the tool's repair, the direction
maoz_score.py's coverage pass records. compare_pairs.py's `error` (no verdict
printed) is recorded as `undecided`, as the archived pass recorded a pair
whose chunk and single runs both failed. `source` is `pair`: every pair here
ran alone, where the archive ran chunks of 50 first.
"""
import csv
import sys

FIELDS = ["tool", "spec", "tool_file", "peredur_md5", "relation", "source"]
KNOWN = {"equivalent", "stronger", "weaker", "incomparable", "undecided"}


def main(argv) -> int:
    out, inputs = argv[0], argv[1:]
    rows = {}
    for path in inputs:
        with open(path, newline="") as fh:
            for r in csv.DictReader(fh):
                tag, tool, spec, tool_file, md5 = r["id"].split("|")
                assert tag == "cov", r["id"]
                rel = r["relation"] if r["relation"] in KNOWN else "undecided"
                rows[(tool, spec, tool_file, md5)] = rel
    import os
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(FIELDS)
        for key in sorted(rows):
            w.writerow([*key, rows[key], "pair"])
    print(f"{len(rows)} relations from {len(inputs)} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
