#!/usr/bin/env python3
"""Replace the re-scored runs' rows in a merged curves CSV.

    splice.py --runs runs.txt --original ORIG.csv --rescore NEW.csv --out OUT.csv

Every row of ORIG whose run (spec, seed, selection_scheme, status_grading) is
named in runs.txt is dropped, and NEW's rows for that run take the place of
its first row, so the file keeps its order. NEW must hold exactly the runs
runs.txt names. ORIG is read only; OUT must not exist.
"""
import argparse
import csv
import re
import sys
from pathlib import Path

NAME = re.compile(r"^sweep_G_(mrs|aurus)_(nsga2-apportion|weighted)_wkoff_log_"
                  r"(.+)_seed(\d+)$")


def run_key(row):
    return (row["spec"], int(row["seed"]), row["selection_scheme"],
            row["status_grading"])


def read_runs(path):
    keys = set()
    for line in path.read_text().splitlines():
        if not line.strip():
            continue
        match = NAME.match(line.strip())
        if not match:
            sys.exit(f"unparsable run name {line!r}")
        grading, selection, spec, seed = match.groups()
        keys.add((spec, int(seed), selection, grading))
    return keys


def final_solutions(rows):
    values = [int(r["value"]) for r in rows
              if r["metric"] == "solutions" and r["value"]]
    return max(values, default=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=Path, required=True)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--rescore", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        sys.exit(f"{args.out} exists; splice writes a new file only")
    keys = read_runs(args.runs)

    with args.rescore.open(newline="") as handle:
        reader = csv.DictReader(handle)
        new_header = reader.fieldnames
        fresh = {}
        for row in reader:
            fresh.setdefault(run_key(row), []).append(row)
    if set(fresh) != keys:
        sys.exit(f"{args.rescore} holds {len(fresh)} runs; runs.txt names "
                 f"{len(keys)}: missing {sorted(keys - set(fresh))[:5]}, "
                 f"extra {sorted(set(fresh) - keys)[:5]}")

    dropped, old = 0, {}
    with args.original.open(newline="") as handle, \
            args.out.open("x", newline="") as out:
        reader = csv.DictReader(handle)
        if reader.fieldnames != new_header:
            sys.exit("the two CSVs' headers differ")
        # The merged CSVs end the header in \n and each row in \r\n; both
        # are copied from the original, so an unchanged run splices back to
        # the same bytes.
        with args.original.open("rb") as raw:
            header = raw.readline()
            row_end = "\r\n" if raw.readline().endswith(b"\r\n") else "\n"
        out.write(header.decode())
        writer = csv.DictWriter(out, fieldnames=reader.fieldnames,
                                lineterminator=row_end)
        n_in = n_out = 0
        for row in reader:
            n_in += 1
            key = run_key(row)
            if key not in keys:
                writer.writerow(row)
                n_out += 1
                continue
            dropped += 1
            if key not in old:
                old[key] = []
                writer.writerows(fresh[key])
                n_out += len(fresh[key])
            old[key].append(row)
    if set(old) != keys:
        args.out.unlink()
        sys.exit(f"{args.original} lacks {len(keys - set(old))} of the runs")

    added = sum(len(v) for v in fresh.values())
    print(f"{args.original.name}: {n_in} rows in, {dropped} dropped, "
          f"{added} added, {n_out} out -> {args.out}")
    print("run\tsolutions_before\tsolutions_after")
    for key in sorted(keys):
        print(f"{key}\t{final_solutions(old[key])}\t"
              f"{final_solutions(fresh[key])}")


if __name__ == "__main__":
    main()
