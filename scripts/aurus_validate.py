#!/usr/bin/env python3
"""Validate AuRUS's claimed repairs with PEREDUR's own checkers.

Post-processing for the AuRUS baseline campaign (scripts/aurus_campaign.py):
AuRUS checks realizability with Strix, PEREDUR with ltlsynt, so every repair
AuRUS emits (spec0.tlsf, spec1.tlsf, ... per <out-root>/<spec>/<repeat-nn>/)
is (a) re-checked with `realize` and any disagreement recorded, and (b) scored
against the family's genuine fixes (examples/<spec>/fixes/) with `compare`,
under the same assume-guarantee implication order — and the same output
parsing (run_experiments.parse_compare_output, imported, not copied) — as the
implies_ideal column of PEREDUR's own runs, so implies_genuine is directly
comparable.

AuRUS appends //fitness trailer comment lines to each repair TLSF. No
stripping is needed: PEREDUR's TLSF lexer skips // line comments (and /* */
blocks), verified against real AuRUS output files — `realize` and `compare`
both consume them as-is, and `compare --repairs <repeat dir>` is safe because
only the spec*.tlsf files in a repeat dir carry the .tlsf extension.

Every repair that survives (a) is then asked whether it is well-separated —
whether the system can force the spec's own assumptions to fail. AuRUS ran
with -addA, so it may add assumptions freely, and a repair that reaches
realizability by adding assumptions it then defeats is the cheat PEREDUR's
status objective was rewritten to stop paying for. Without this column a
claimed repair count says nothing about how many of the repairs mean
anything. The query goes through check_well_separated.check_one (imported,
not copied) rather than a PEREDUR binary: PEREDUR's in-run checker caches a
realizability timeout as "unrealizable", which for this query reads as
"well-separated", so a verdict taken from it would silently launder the
timeouts. Unrealizable repairs are not asked — there is no strategy to
defeat the assumptions with.

Two CSVs land next to each other:
    <out-csv>                — one row per (spec, repeat): n_claimed,
                               n_realize_ok, n_disagree (claimed but not
                               REALIZABLE here — the detail CSV splits real
                               UNREALIZABLE flips from TIMEOUT/ERROR),
                               n_ill_separated, n_sep_undecided,
                               best_relation, implies_genuine, n_implies
    <out-csv stem>_details.csv — one row per repair file with its verdict:
                               REALIZABLE / UNREALIZABLE / TIMEOUT / ERROR,
                               and separation: well-separated /
                               not-well-separated / undecided, empty where
                               the repair was not realizable or the check
                               was skipped

Resumable: a (spec, repeat) already present in the out CSV is skipped, and
its detail rows are not re-emitted. Concurrency is per repeat (--jobs).

Usage:
    python scripts/aurus_validate.py --aurus-out ~/aurus-results
    python scripts/aurus_validate.py --aurus-out out --jobs 8 --timeout 300
"""

import argparse
import contextlib
import csv
import re
import subprocess
import tempfile
import sys
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from run_experiments import parse_compare_output  # noqa: E402
from check_well_separated import (  # noqa: E402
    DEFAULT_LTLSYNT, VERDICT_NOT_WELL_SEPARATED, VERDICT_UNDECIDED,
    check_one,
)

SPEC_TLSF_RE = re.compile(r"^spec(\d+)\.tlsf$")
REPEAT_RE = re.compile(r"^repeat-(\d+)$")

CSV_FIELDS = [
    "spec", "repeat", "n_claimed", "n_realize_ok", "n_disagree",
    "n_ill_separated", "n_sep_undecided", "n_scored",
    "best_relation", "implies_genuine", "n_implies",
    "best_relation_all", "implies_genuine_all", "n_implies_all",
]
DETAIL_FIELDS = ["spec", "repeat", "file", "realize_verdict", "separation"]


def realize_verdict(realize_bin: Path, tlsf: Path, timeout: int) -> str:
    """Run `realize` on one repair; return its verdict as a CSV token."""
    try:
        result = subprocess.run(
            [str(realize_bin), str(tlsf)],
            timeout=timeout, capture_output=True, text=True)
    except subprocess.TimeoutExpired:
        return "TIMEOUT"
    out = result.stdout.strip()
    if result.returncode == 0 and out in ("REALIZABLE", "UNREALIZABLE"):
        return out
    return "ERROR"


def separation_verdict(tlsf: Path, ltlsynt: Path, timeout: int) -> str:
    """Well-separated / not-well-separated / undecided for one repair.

    Delegates to check_well_separated.check_one rather than asking `realize`,
    for the reason that script's docstring gives: PEREDUR's in-run checker
    maps a realizability timeout to "unrealizable" and caches it, which for
    this query reads as "well-separated". A CLI built on the same cache would
    inherit the collapse. check_one shells to ltlsynt directly and reports a
    timeout as undecided, so the two failure modes stay distinguishable in
    the CSV.

    fast_path stays off: the short-circuit for input-only assumptions is the
    C++ filter's, and AuRUS ran with -addA, so the specs this scores are
    exactly the ones whose assumptions may reference an output.
    """
    return check_one(tlsf, ltlsynt, float(timeout), fast_path=False).verdict


@contextlib.contextmanager
def subset_dir(repeat_dir: Path, keep: list[str]):
    """A directory holding symlinks to `keep` alone, for a restricted compare.

    `compare --repairs` globs a directory, so scoring a subset means giving it
    a directory that contains only the subset. Symlinks rather than copies:
    a repeat can hold hundreds of files and nothing is written through them.
    """
    with tempfile.TemporaryDirectory(prefix="aurus-scored-") as tmp:
        out = Path(tmp)
        for name in keep:
            (out / name).symlink_to((repeat_dir / name).resolve())
        yield out


def compare_repeat(compare_bin: Path, repeat_dir: Path, ideals_dir: Path,
                   timeout: int) -> tuple[str, int, int]:
    """Return (best_relation, implies_genuine, n_implies) for one repeat.

    Mirrors run_experiments.run_one's compare step: a timeout or non-zero
    exit records "unknown" rather than failing the row.
    """
    try:
        result = subprocess.run(
            [str(compare_bin), "--repairs", str(repeat_dir),
             "--ideals", str(ideals_dir)],
            check=True, timeout=timeout, capture_output=True, text=True)
    except (subprocess.TimeoutExpired, subprocess.CalledProcessError) as e:
        print(f"    [{repeat_dir}] WARN: compare failed — {e}")
        return "unknown", 0, 0
    return parse_compare_output(result.stdout)


def claimed_repairs(repeat_dir: Path) -> list[Path]:
    """The specN.tlsf files of one repeat, in ascending N order."""
    found = []
    for entry in repeat_dir.iterdir():
        if m := SPEC_TLSF_RE.match(entry.name):
            found.append((int(m.group(1)), entry))
    return [path for _, path in sorted(found)]


def discover_repeats(out_root: Path) -> list[tuple[str, int, Path]]:
    """All (spec, repeat, repeat_dir) under the campaign tree, sorted."""
    repeats = []
    for spec_dir in sorted(p for p in out_root.iterdir() if p.is_dir()):
        for rep_dir in sorted(p for p in spec_dir.iterdir() if p.is_dir()):
            if m := REPEAT_RE.match(rep_dir.name):
                repeats.append((spec_dir.name, int(m.group(1)), rep_dir))
    return repeats


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    repo_root = Path(__file__).parent.parent
    parser.add_argument("--aurus-out", type=Path, required=True,
                        metavar="PATH",
                        help="aurus_campaign.py results tree "
                             "(<spec>/repeat-NN/ run dirs)")
    parser.add_argument("--examples-dir", type=Path,
                        default=repo_root / "examples", metavar="PATH",
                        help="Directory holding <spec>/fixes/ ideal dirs "
                             "(default: %(default)s)")
    parser.add_argument("--realize-bin", type=Path,
                        default=repo_root / "build-release" / "realize",
                        metavar="PATH",
                        help="realize binary (default: %(default)s)")
    parser.add_argument("--ltlsynt", type=Path, default=DEFAULT_LTLSYNT,
                        metavar="PATH",
                        help="ltlsynt for the well-separation query "
                             "(default: %(default)s)")
    parser.add_argument("--no-well-separation", action="store_true",
                        help="skip the well-separation check; the "
                             "n_ill_separated and n_sep_undecided columns "
                             "then read 0 and the separation column is empty")
    parser.add_argument("--compare-bin", type=Path,
                        default=repo_root / "build-release" / "compare",
                        metavar="PATH",
                        help="compare binary (default: %(default)s)")
    parser.add_argument("--out-csv", type=Path, default=None, metavar="PATH",
                        help="Per-repeat summary CSV (default: "
                             "<aurus-out>/aurus_validation.csv); the "
                             "per-repair detail CSV lands alongside as "
                             "<stem>_details.csv")
    parser.add_argument("--timeout", type=int, default=120, metavar="S",
                        help="Timeout per external call (default: 120); "
                             "raise for compare on large repeats — its grid "
                             "is n_repairs x n_ideals x 2 implication checks")
    parser.add_argument("--jobs", type=int, default=4, metavar="N",
                        help="Concurrent repeat validations (default: 4)")
    args = parser.parse_args()

    if args.jobs < 1:
        sys.exit("--jobs must be >= 1")
    if not args.aurus_out.is_dir():
        sys.exit(f"Not a directory: {args.aurus_out}")
    for bin_path in (args.realize_bin, args.compare_bin):
        if not bin_path.exists():
            sys.exit(f"Binary not found: {bin_path}")

    out_csv = args.out_csv or args.aurus_out / "aurus_validation.csv"
    details_csv = out_csv.with_name(f"{out_csv.stem}_details.csv")

    done: set[tuple[str, int]] = set()
    if out_csv.exists():
        with open(out_csv, newline="") as f:
            for row in csv.DictReader(f):
                done.add((row["spec"], int(row["repeat"])))

    repeats = discover_repeats(args.aurus_out)
    to_run = [(s, r, d) for s, r, d in repeats if (s, r) not in done]

    print("=" * 64)
    print(f"  AuRUS validation: {len(repeats)} repeats found, "
          f"{len(to_run)} to validate, {len(repeats) - len(to_run)} done")
    print(f"    realize: {args.realize_bin}")
    print(f"    compare: {args.compare_bin}")
    print(f"    out:     {out_csv}")
    print("=" * 64)

    lock = threading.Lock()
    state = {"completed": 0}

    def append_rows(csv_path: Path, fields: list[str],
                    rows: list[dict]) -> None:
        write_header = not csv_path.exists() or csv_path.stat().st_size == 0
        with open(csv_path, "a", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fields)
            if write_header:
                writer.writeheader()
            writer.writerows(rows)

    def validate(task: tuple[str, int, Path]) -> None:
        spec, rep, rep_dir = task
        run_id = f"{spec}/repeat-{rep:02d}"
        repairs = claimed_repairs(rep_dir)

        details = []
        for tlsf in repairs:
            verdict = realize_verdict(args.realize_bin, tlsf, args.timeout)
            # Only realizable repairs are asked. Well-separation qualifies a
            # repair that exists; on an unrealizable one there is no strategy
            # to defeat the assumptions with, and asking anyway would spend an
            # ltlsynt call per rejected candidate for a verdict nothing reads.
            separation = ""
            if verdict == "REALIZABLE" and not args.no_well_separation:
                separation = separation_verdict(
                    tlsf, args.ltlsynt, args.timeout)
            details.append({"spec": spec, "repeat": rep, "file": tlsf.name,
                            "realize_verdict": verdict,
                            "separation": separation})
        n_ok = sum(d["realize_verdict"] == "REALIZABLE" for d in details)
        n_ill = sum(d["separation"] == VERDICT_NOT_WELL_SEPARATED
                    for d in details)
        n_sep_undecided = sum(d["separation"] == VERDICT_UNDECIDED
                              for d in details)

        # The scored set is the repairs that are realizable AND well-separated.
        # An ill-separated repair is one the system satisfies by defeating its
        # own assumptions, and PEREDUR's output gate rejects those
        # unconditionally, so crediting AuRUS for one would score the two arms
        # by different standards. The unfiltered rate is kept alongside as
        # implies_genuine_all because it is what PLAN.md pre-registered before
        # the gap could be measured; see the 2026-08-17 amendment there.
        scored = [d["file"] for d in details
                  if d["realize_verdict"] == "REALIZABLE"
                  and d["separation"] != VERDICT_NOT_WELL_SEPARATED]

        if repairs:
            ideals_dir = args.examples_dir / spec / "fixes"
            if ideals_dir.is_dir():
                best_all, implies_all, n_implies_all = compare_repeat(
                    args.compare_bin, rep_dir, ideals_dir, args.timeout)
                if len(scored) == len(repairs):
                    best_rel, implies, n_implies = (
                        best_all, implies_all, n_implies_all)
                elif scored:
                    with subset_dir(rep_dir, scored) as only:
                        best_rel, implies, n_implies = compare_repeat(
                            args.compare_bin, only, ideals_dir, args.timeout)
                else:
                    best_rel, implies, n_implies = "none", 0, 0
            else:
                print(f"    [{run_id}] WARN: no ideals dir {ideals_dir}")
                best_rel, implies, n_implies = "unknown", 0, 0
                best_all, implies_all, n_implies_all = "unknown", 0, 0
        else:
            best_rel, implies, n_implies = "none", 0, 0
            best_all, implies_all, n_implies_all = "none", 0, 0

        row = {"spec": spec, "repeat": rep, "n_claimed": len(repairs),
               "n_realize_ok": n_ok, "n_disagree": len(repairs) - n_ok,
               "n_ill_separated": n_ill, "n_sep_undecided": n_sep_undecided,
               "n_scored": len(scored),
               "best_relation": best_rel, "implies_genuine": implies,
               "n_implies": n_implies,
               "best_relation_all": best_all,
               "implies_genuine_all": implies_all,
               "n_implies_all": n_implies_all}
        with lock:
            append_rows(out_csv, CSV_FIELDS, [row])
            if details:
                append_rows(details_csv, DETAIL_FIELDS, details)
            state["completed"] += 1
            n = state["completed"]
            note = ("" if row["n_disagree"] == 0
                    else f"  DISAGREE {row['n_disagree']}")
            if n_ill:
                note += f"  ILL-SEPARATED {n_ill}"
            print(f"[{n}/{len(to_run)}]  {run_id}  "
                  f"claimed {row['n_claimed']}, realize-ok {n_ok}, "
                  f"best {best_rel}{note}", flush=True)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(validate, to_run))

    print(f"\nDone. Summary: {out_csv}\nDetails: {details_csv}")


if __name__ == "__main__":
    main()
