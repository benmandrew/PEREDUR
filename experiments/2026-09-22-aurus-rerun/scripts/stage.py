#!/usr/bin/env python3
"""Build the staging directory prepare.py reads.

The re-run's collected CSVs are too large to carry here, and are gitignored in
the campaign worktrees the way the PEREDUR side's are, so this reconstructs
everything derived from them: the AuRUS reference directory, the two
family-filtered curve inputs, and the archive stage naming the PEREDUR arm
under the filenames prepare.py expects.

Three things about the inputs would pass silently if you rebuilt this by hand.
The re-run's curve pass scored humanoid-741 where the archive's did not, so the
curve and separation inputs are filtered to the paper's families or the AuRUS
denominator reads 780 runs against the corpus's 750. aurus_full.csv keeps every
family regardless, as the archive's own copy does, because FAMILIES gates it
downstream anyway. aurus_series.csv has no counterpart in the pass, so it is
derived here or load_aurus() returns (None, None). And every AuRUS solution
time is moved onto PEREDUR's clock here, by adding its run's offset from
aurus-offsets.csv: the fork times a solution from a clock it starts after the
JVM has loaded and parsed its spec, where PEREDUR's starts before it parses
anything. offsets.py says how the offsets were measured.

Run it from inside this directory; see PROVENANCE.txt for the argument values.
"""
import argparse
import collections
import csv
from pathlib import Path

from prepare import FAMILIES

FULL_COLUMNS = ["spec", "repeat", "index", "verdict", "hit", "found_iter",
                "found_elapsed_s", "final_nsol", "files_written", "status"]
# The archive stage names the PEREDUR arm under the filenames prepare.py reads
# from an archive directory, which are the rematch campaign's, not the
# re-run's.
ARCHIVE_LINKS = {
    "results-rematch.csv": "results-paper-rerun.csv",
    "results-rematch-calib.csv": "results-paper-rerun-calib.csv",
    "curves-rematch.csv": "curves-paper-rerun.csv",
}


def read_offsets(path):
    """{(spec, repeat): seconds} from launch to the start of AuRUS's clock."""
    with open(path, newline="") as handle:
        return {(row["spec"], int(row["repeat"])): float(row["offset_s"])
                for row in csv.DictReader(handle)}


def shifted(moment, offset):
    """A solution time moved onto the launch clock, at the input's precision."""
    return f"{float(moment) + offset:.6f}"


def filter_families(source, out, offsets):
    """Copy source to out, keeping only rows naming one of the paper's families.

    Each solution time is moved onto the launch clock by its run's offset: a
    count's elapsed_s, and a time_to_first metric's value, which is where that
    metric keeps its time. The one point a count carries at run_wall_s is left
    where it is, the harness having timed the run from launch already.
    """
    kept = dropped = 0
    with open(source, newline="") as handle, open(out, "w", newline="") as sink:
        reader = csv.DictReader(handle)
        writer = csv.DictWriter(sink, reader.fieldnames, lineterminator="\n")
        writer.writeheader()
        for row in reader:
            if row["spec"] in FAMILIES:
                field = ("value" if row["metric"].startswith("time_to_first")
                         else "elapsed_s")
                at_wall = (row["elapsed_s"] and row["run_wall_s"]
                           and float(row["elapsed_s"]) == float(row["run_wall_s"]))
                if row[field] and not at_wall:
                    row[field] = shifted(
                        row[field], offsets[(row["spec"], int(row["seed"]))])
                writer.writerow(row)
                kept += 1
            else:
                dropped += 1
    print(f"{out.name}: {kept} rows kept, {dropped} outside the paper's families")


def write_reference(anytime, out, offsets):
    """Write the reference directory analyse_matched reads.

    Each found_elapsed_s is moved onto the launch clock by its run's offset.
    """
    out.mkdir(parents=True, exist_ok=True)
    with open(anytime, newline="") as handle:
        rows = list(csv.DictReader(handle))
    for row in rows:
        if row["found_elapsed_s"]:
            repeat = int(row["repeat"].split("-")[1])
            row["found_elapsed_s"] = shifted(
                row["found_elapsed_s"], offsets[(row["spec"], repeat)])

    with open(out / "aurus_full.csv", "w", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(FULL_COLUMNS)
        for row in rows:
            writer.writerow([row[name] for name in FULL_COLUMNS])

    # The series: solutions accumulate over a run's iterations, so a row is one
    # iteration that produced at least one, nsol the running count and
    # elapsed_s the last solution moment within it.
    per_iter = collections.defaultdict(lambda: collections.defaultdict(list))
    for row in rows:
        if row["status"] != "ok" or not row["found_iter"]:
            continue
        per_iter[(row["spec"], row["repeat"])][int(row["found_iter"])].append(
            float(row["found_elapsed_s"]))

    with open(out / "aurus_series.csv", "w", newline="") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(["spec", "repeat", "iter", "nsol", "elapsed_s", "host"])
        for (spec, repeat), iters in sorted(per_iter.items()):
            host = "av2" if int(repeat.split("-")[1]) < 15 else "av3"
            nsol = 0
            for index in sorted(iters):
                moments = iters[index]
                nsol += len(moments)
                writer.writerow([spec, repeat, index, nsol,
                                 f"{max(moments):g}", host])

    runs = {(r["spec"], r["repeat"]) for r in rows}
    print(f"aurus_full.csv: {len(rows)} rows, {len(runs)} runs")
    print(f"aurus_series.csv: {sum(len(v) for v in per_iter.values())} rows, "
          f"{len(per_iter)} runs")


def write_archive_stage(counter, out):
    """Link the PEREDUR arm's CSVs under the names prepare.py expects."""
    out.mkdir(parents=True, exist_ok=True)
    for name, source in ARCHIVE_LINKS.items():
        link, target = out / name, (counter / "experiments" / source).resolve()
        if not target.exists():
            raise SystemExit(f"missing {target}")
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(target)
    print(f"archive stage: {len(ARCHIVE_LINKS)} links into {counter}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--anytime", type=Path, required=True,
                        help="the re-run's anytime-aurus-uncensored.csv")
    parser.add_argument("--curves", type=Path, required=True,
                        help="the re-run's curves-aurus-uncensored.csv")
    parser.add_argument("--separation", type=Path, required=True,
                        help="the re-run's separation-aurus-uncensored.csv")
    parser.add_argument("--counter", type=Path, required=True,
                        help="the worktree holding the PEREDUR arm's CSVs")
    parser.add_argument("--out", type=Path, required=True,
                        help="the staging directory to build")
    parser.add_argument("--offsets", type=Path,
                        default=Path(__file__).parent / "aurus-offsets.csv",
                        help="per-run clock offsets, from offsets.py")
    args = parser.parse_args()

    offsets = read_offsets(args.offsets)
    args.out.mkdir(parents=True, exist_ok=True)
    write_reference(args.anytime, args.out / "aurus-reference-uncensored",
                    offsets)
    filter_families(args.curves, args.out / "curves-aurus-uncensored-25.csv",
                    offsets)
    filter_families(args.separation,
                    args.out / "separation-aurus-uncensored-25.csv", offsets)
    write_archive_stage(args.counter, args.out / "archive-stage")


if __name__ == "__main__":
    main()
