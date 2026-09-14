#!/usr/bin/env python3
"""Pull experiment results from remote machines and merge them into this repo.

Idempotent: run it as many times as you like. Per-run result directories are
named with their seed (``sweep_A_gen10_fsm_seed17``) so they never collide
between machines; rsync only transfers what changed. The aggregate CSV is
merged by its natural key (see KEY_FIELDS), keeping exactly one row per key, so
re-running never duplicates rows.

``--profile`` selects which CSV to merge, and must match the profile the runs
were executed under: each profile writes its own CSV, so merging a profile's
results needs the same ``--profile`` the runs used.

Configure the remote machines in REMOTES below, then:

    python scripts/merge_experiments.py                  # pull from all REMOTES (full)
    python scripts/merge_experiments.py av2 av3          # only named remotes
    python scripts/merge_experiments.py --dry-run        # show rsync plan, no writes
    python scripts/merge_experiments.py /path/to/copy    # merge an already-rsynced dir

A source may be a remote (``host:/path/to/counter``) or a local path to another
counter checkout. Bare hostnames use REMOTE_ROOT as the repo path.
"""

import argparse
import csv
import re
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).parent.parent

# ── Configure your machines here ──────────────────────────────────────────────
# Each entry is an ssh destination as accepted by ssh/rsync. A bare host uses
# REMOTE_ROOT as the repo path; append ":/custom/path/to/counter" to override.
REMOTES: dict[str, str] = {
    "av2": "benandrew@av2.cs.man.ac.uk",
    "av3": "benandrew@av3.cs.man.ac.uk",
}

# Path to the counter checkout on a remote when only a host is given.
REMOTE_ROOT = "/home/benandrew/projects/counter"
# ──────────────────────────────────────────────────────────────────────────────

# Mirrors the per-profile results_csv names in run_experiments.py's PROFILES.
# Adding a profile there means adding it here too.
PROFILE_CSVS: dict[str, str] = {
    "full": "results.csv",
    "factorial": "results-factorial.csv",
    "cj-large": "results-cj-large.csv",
    "metric": "results-metric.csv",
    "muc": "results-muc.csv",
    "padd": "results-padd.csv",
    "tlsf": "results-tlsf.csv",
    "wellsep": "results-wellsep.csv",
    # Its own CSV, never wellsep's: the sweep letter and level names differ but
    # the key fields (spec, seed, selection, ...) overlap, so sharing would
    # interleave two campaigns in one dataset. The profile itself is retired --
    # its sweep varied filters.intervals.well_separation, a key that no longer
    # exists -- but the entry stays so the archived CSVs remain mergeable.
    "wellsep-timing": "results-wellsep-timing.csv",
    "arbiter-hp": "results-arbiter-hp.csv",
    "arbiter-padd": "results-arbiter-padd.csv",
    "ablate-fret": "results-ablate-fret.csv",
    # h2h-tlsf shares ablate-tlsf's CSV (that sharing is its dedup mechanism),
    # so merging under either profile name merges the same rows.
    "ablate-tlsf": "results-ablate-tlsf.csv",
    "h2h-tlsf": "results-ablate-tlsf.csv",
    # The 2026-08-14 head-to-head, deliberately NOT sharing the above: it runs
    # 25 families at a 7200s cap where h2h-tlsf ran 12 at 600s, and neither the
    # corpus nor the cap is in the resume key, so shared rows would collide.
    "aurus-h2h": "results-aurus-h2h.csv",
    # The 2026-08-21 re-run of that corpus at the shipping configuration plus
    # the accumulator. Its own CSV for the same reason `aurus-h2h` has one: the
    # arm differs from that campaign's in four keys and none of them is in the
    # resume key, so a shared CSV would let the merge keep whichever row of a
    # pair arrived first.
    "aurus-h2h-ship": "results-aurus-h2h-ship.csv",
    # Retired with sweep U -- two of its operator levels set
    # [tlsf.mutation] p_union_assumption, a key that no longer exists --
    # and kept here for the same reason the ops-* entries are: the
    # archived CSVs stay mergeable.
    "assumption-reach": "results-assumption-reach.csv",
    "monotone": "results-monotone.csv",
    # The 2026-08-26 selection smoke test. Its own CSV rather than sharing
    # `monotone`'s: the two run the same sweep T level over an overlapping
    # corpus and seed range, and the arm that separates them is
    # `selection`, which IS in the key -- but the corpus and the timeout caps
    # are not, and this campaign drops four families and caps far tighter. A
    # shared CSV would put rows from two cap tables under one key.
    "selection-smoke": "results-selection-smoke.csv",
    # The 2026-08-28 selection x grading ablation. Two CSVs, never shared: the
    # calibration runs a 12-family subset at 2 seeds to size the full run, so
    # merging its rows into the campaign's would put a different corpus and a
    # different seed count under one key.
    "curves-calib": "results-curves-calib.csv",
    "curves": "results-curves.csv",
    # The 2026-08-20 operator cross; one CSV per path, never shared, since
    # the two paths run different corpora at different operating points. All
    # five profiles are retired -- their sweep varied genetic.repaired_operators,
    # a key that no longer exists -- but the entries stay so the archived CSVs
    # remain mergeable.
    "opswk-fret": "results-opswk-fret.csv",
    "opswk-tlsf": "results-opswk-tlsf.csv",
    "ops-pilot": "results-ops-pilot.csv",
    "ops-tlsf": "results-ops-tlsf.csv",
    "ops-fret": "results-ops-fret.csv",
    "replicate": "results-replicate.csv",
    # replicate-wkoff shares replicate's CSV (the weakening column separates the
    # rows), so merging under either name merges the same file.
    "replicate-wkoff": "results-replicate.csv",
    # replicate-recap likewise: it replaces rows the original caps censored, in
    # place and under the same keys.
    "replicate-recap": "results-replicate.csv",
    "replicate-tlsf": "results-replicate-tlsf.csv",
    # Its own CSVs, never replicate's: sweep R's level names are the same but
    # the campaign, corpus and commit differ, so sharing would interleave two
    # datasets under one set of keys.
    "arbiter-probe": "results-arbiter-probe.csv",
    # Its own CSV, never ablate-tlsf's: same corpus and sweep letter, but sweep
    # G's arms are the status grading scale rather than ablate-tlsf's C, and the
    # level names do not overlap.
    "status-grading": "results-status-grading.csv",
    # Registered here because test_experiment_paths asserts every profile in
    # run_experiments has both a CSV and a result directory. The profile landed
    # on main with neither, which `campaign.py collect` would have surfaced as a
    # KeyError once the campaign finished.
    "weakening-arbiter": "results-wkarb.csv",
    # The -cm halves share their R half's CSV deliberately: they are the third
    # arm of one design, not a separate campaign, and KEY_FIELDS separates them
    # by sweep and level_name. Merging under either name merges the same file.
    "seldefault-fret": "results-seldefault.csv",
    "seldefault-fret-cm": "results-seldefault.csv",
    "seldefault-tlsf": "results-seldefault-tlsf.csv",
    "seldefault-tlsf-cm": "results-seldefault-tlsf.csv",
    "matched": "results-matched.csv",
    # The 2026-09-04 re-match. Both landed on main with no entry in either
    # table, which `test_experiment_paths` reports and which `campaign.py
    # collect` would have raised as a KeyError after the campaign finished --
    # the same omission the `weakening-arbiter` note above records. The
    # calibration keeps its own CSV for the reason `curves-calib` does: six
    # families at 2 seeds sized the main phase, and merging those rows in would
    # put a different corpus and seed count under one key.
    "rematch": "results-rematch.csv",
    "rematch-calib": "results-rematch-calib.csv",
    # The 2026-09-09 selection x grading cross on the FRETISH path, replicating
    # `curves`. Its own CSV rather than that campaign's: the sweep letter, the
    # corpus and the path all differ, and neither the corpus nor the path is in
    # the key.
    "gradsel-fret": "results-gradsel-fret-m.csv",
}

# Per-run output directory each profile writes under experiments/. Most profiles
# share "results"; cj-large uses its own so its runs never collide with another
# profile's per-run dirs. Both the remote source and local dest use this name.
PROFILE_RESULT_DIRS: dict[str, str] = {
    "full": "results",
    "factorial": "results",
    "cj-large": "results-cj-large",
    "metric": "results-metric",
    "muc": "results-muc",
    "padd": "results-padd",
    "tlsf": "results-tlsf",
    "wellsep": "results-wellsep",
    "wellsep-timing": "results-wellsep-timing",
    "arbiter-hp": "results-arbiter-hp",
    "arbiter-padd": "results-arbiter-padd",
    "ablate-fret": "results-ablate-fret",
    "ablate-tlsf": "results-ablate-tlsf",
    "h2h-tlsf": "results-ablate-tlsf",
    "aurus-h2h": "results-aurus-h2h",
    "aurus-h2h-ship": "results-aurus-h2h-ship",
    # Retired with sweep U, and kept for the same reason as above.
    "assumption-reach": "results-assumption-reach",
    "monotone": "results-monotone",
    "selection-smoke": "results-selection-smoke",
    "curves-calib": "results-curves-calib",
    "curves": "results-curves",
    # Retired with their sweep, and kept for the same reason as above.
    "opswk-fret": "results-opswk-fret",
    "opswk-tlsf": "results-opswk-tlsf",
    "ops-pilot": "results-ops-pilot",
    "ops-tlsf": "results-ops-tlsf",
    "ops-fret": "results-ops-fret",
    "replicate": "results-replicate",
    "replicate-wkoff": "results-replicate",
    "replicate-recap": "results-replicate",
    "replicate-tlsf": "results-replicate-tlsf",
    "arbiter-probe": "results-arbiter-probe",
    "status-grading": "results-status-grading",
    "weakening-arbiter": "results-wkarb",
    "seldefault-fret": "results-seldefault",
    "seldefault-fret-cm": "results-seldefault",
    "seldefault-tlsf": "results-seldefault-tlsf",
    "seldefault-tlsf-cm": "results-seldefault-tlsf",
    "matched": "results-matched",
    "rematch": "results-rematch",
    "rematch-calib": "results-rematch-calib",
    "gradsel-fret": "results-gradsel-fret-m",
}

# Natural key of a results row: one run per (sweep, level_name, selection,
# weakening, metric, repair_mode, spec, seed). The factor columns are part of it
# because a profile may run every level under both selection schemes
# (factorial), both weakening states (cj-large), both similarity metrics
# (metric), and both repair modes (muc) — without them the crossed rows collapse
# onto one key and half are silently dropped. Rows written before any of these
# columns existed carry the legacy defaults below (see run_experiments.py's
# LEGACY_* constants).
#
# `commit` is deliberately NOT a key field. It is not a factor of the design —
# two rows differing only in the binary that produced them are the same cell,
# not two cells — and adding it would make every archived row (which has no
# commit at all) miss on resume and silently re-run whole campaigns.
KEY_FIELDS = ("sweep", "level_name", "selection", "weakening", "metric",
              "repair_mode", "spec", "seed")
LEGACY_SELECTION = "nsga2"
LEGACY_WEAKENING = "wkon"
LEGACY_METRIC = "direct"
LEGACY_REPAIR = "mono"
LEGACY_COMMIT = "unknown"

# The two NSGA-II schemes were renamed -- "nsga2" -> "nsga2-truncate" and
# "nsga2-replicate" -> "nsga2-apportion" -- and the binary rejects the old
# spellings outright. Only the name moved: the scheme each one selects is
# unchanged, so a row recorded under either spelling names the same cell of the
# design. 224,861 archived rows carry "nsga2" and the replicate campaign's rows
# carry "nsga2-replicate"; canonicalising the `selection` value wherever it
# enters a KEY_FIELDS key is what lets an archived row and a new one join as one
# row rather than merging into two. Duplicated from run_experiments.py rather
# than imported, like every other LEGACY_* constant here: both scripts are
# standalone and are vendored per campaign under experiments/<campaign>/scripts/,
# where a cross-import would not resolve. The stored `selection` value itself is
# never rewritten -- the merged CSV keeps whatever each campaign recorded.
SCHEME_ALIASES: dict[str, str] = {
    "nsga2": "nsga2-truncate",
    "nsga2-replicate": "nsga2-apportion",
}


def canonical_scheme(name: str) -> str:
    """Current spelling of a selection scheme; any other name passes through."""
    return SCHEME_ALIASES.get(name, name)


def resolve_source(name: str) -> tuple[str, str]:
    """Map a CLI argument to (label, rsync_root).

    ``name`` may be a key in REMOTES, a raw ``host:/path`` / ``host`` spec, or a
    local filesystem path to another counter checkout. A ``host`` with no path
    component gets REMOTE_ROOT appended.
    """
    if name in REMOTES:
        # A configured entry is always an ssh destination — a bare host (no
        # colon) or an explicit host:path. Append REMOTE_ROOT when no path given.
        host, sep, path = REMOTES[name].partition(":")
        root = f"{host}:{path}" if sep else f"{REMOTES[name]}:{REMOTE_ROOT}"
        return name, root
    # A raw argument: an existing local path is another checkout to merge;
    # anything else is treated as a remote spec (host or host:path).
    local = Path(name).expanduser()
    if local.exists():
        return name, str(local)
    host, sep, _ = name.partition(":")
    root = name if sep else f"{host}:{REMOTE_ROOT}"
    return name, root


def safe_stem(label: str) -> str:
    """Filename-safe stem for a source label.

    A label may be an absolute path or a ``host:/path`` spec. Joining one onto
    tmp_dir unsanitised escapes it entirely — ``Path("/tmp/x") / "/a/b.csv"``
    is ``/a/b.csv``, since an absolute right-hand side discards the base. The
    whole label is folded (rather than just its basename) so that two sources
    sharing a basename stay distinct.
    """
    stem = re.sub(r"[^A-Za-z0-9._-]+", "_", label).strip("_")
    # Keep filenames well inside the usual 255-byte limit; the tail is the
    # discriminating part of a path, so keep that end.
    if len(stem) > 100:
        stem = stem[-100:].lstrip("_")
    return stem or "source"


def run_rsync(src: str, dst: str, dry_run: bool) -> None:
    cmd = ["rsync", "-av"]
    if dry_run:
        cmd.append("--dry-run")
    cmd += [src, dst]
    print(f"  $ {' '.join(cmd)}")
    subprocess.run(cmd, check=True)


def pull_source(
    label: str, root: str, dry_run: bool, tmp_dir: Path, csv_name: str,
    result_dir: str | None,
) -> Path | None:
    """rsync one source's per-run result dir and its profile CSV locally.

    ``result_dir`` is the profile's per-run directory name, or None to skip the
    (potentially large) per-run pull and merge only the CSV. Returns the local
    path of the pulled CSV (under tmp_dir), or None if that machine had no CSV
    for this profile.
    """
    print(f"[{label}] {root}")
    if result_dir is not None:
        dst = REPO_ROOT / "experiments" / result_dir
        dst.mkdir(parents=True, exist_ok=True)
        # Trailing slash on the source dir: copy contents in, merging.
        run_rsync(f"{root}/experiments/{result_dir}/", f"{dst}/", dry_run)

    csv_local = tmp_dir / f"{safe_stem(label)}.{csv_name}"
    try:
        run_rsync(f"{root}/experiments/{csv_name}", str(csv_local), dry_run)
    except subprocess.CalledProcessError:
        print(f"  (no {csv_name} on {label}, skipping its rows)")
        return None
    return csv_local if csv_local.exists() else None


def read_rows(path: Path) -> tuple[list[str], list[dict]]:
    if not path.exists():
        return [], []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        return list(reader.fieldnames or []), list(reader)


FIELD_DEFAULTS = {"selection": LEGACY_SELECTION, "weakening": LEGACY_WEAKENING,
                  "metric": LEGACY_METRIC, "repair_mode": LEGACY_REPAIR,
                  "commit": LEGACY_COMMIT}


def fill_defaults(row: dict, fieldnames: list[str]) -> dict:
    """Give a row every column of the merged header.

    A CSV predating a column has rows without it. Merged against one that has
    it, those rows would otherwise be written blank; the LEGACY_* value says
    what they actually were (or, for `commit`, that nobody recorded it). `dirty`
    has no legacy value and stays empty, which is honest: a row whose commit was
    never recorded has no known clean-or-dirty state either.
    """
    return {f: row.get(f, FIELD_DEFAULTS.get(f, "")) for f in fieldnames}


def key_of(row: dict) -> tuple:
    # A CSV predating the selection/weakening columns carries only nsga2 / wkon
    # runs, so an absent value keys as that default rather than "" — otherwise
    # merging an old and a new copy of the same run would produce two rows.
    # canonical_scheme is the same argument one step on: a row saying "nsga2"
    # and a row saying "nsga2-truncate" describe one cell under two spellings of
    # one scheme, so they must key alike or the merge splits them.
    def value(field: str) -> str:
        v = row.get(field) or FIELD_DEFAULTS.get(field, "")
        return canonical_scheme(v) if field == "selection" else v

    return tuple(value(f) for f in KEY_FIELDS)


def merge_csv(pulled: list[Path], results_csv: Path) -> None:
    """Merge pulled CSVs into results_csv, one row per key. Local rows win.

    Ordering is deterministic (sorted by key), so the file is byte-stable across
    repeated runs once every source has been merged in.
    """
    header, local_rows = read_rows(results_csv)

    merged: dict[tuple, dict] = {}
    # Local first so existing rows take precedence over remote copies of the
    # same key; remotes only contribute keys we don't already have.
    for row in local_rows:
        merged[key_of(row)] = row
    added = 0
    for csv_path in pulled:
        remote_header, remote_rows = read_rows(csv_path)
        if not header:
            header = remote_header
        # Union rather than "local wins": a remote running a newer script
        # records columns the local CSV predates, and dropping them here would
        # throw away exactly the provenance they were added to keep.
        header = list(header) + [f for f in remote_header if f not in header]
        for row in remote_rows:
            k = key_of(row)
            if k not in merged:
                merged[k] = row
                added += 1

    if not header:
        print("No CSV data found anywhere — nothing to merge.")
        return

    seed_idx = KEY_FIELDS.index("seed")

    def sort_key(k: tuple):
        # Sort seeds numerically, everything else lexicographically. Derived from
        # KEY_FIELDS so a new key field cannot silently break the arity here.
        parts: list = list(k)
        try:
            parts[seed_idx] = (0, int(k[seed_idx]), "")
        except (TypeError, ValueError):
            parts[seed_idx] = (1, 0, str(k[seed_idx]))
        return tuple(parts)

    results_csv.parent.mkdir(parents=True, exist_ok=True)
    with open(results_csv, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=header)
        writer.writeheader()
        for k in sorted(merged, key=sort_key):
            writer.writerow(fill_defaults(merged[k], header))

    print(
        f"\nMerged CSV: {len(merged)} rows total "
        f"({len(local_rows)} local, {added} new from remotes) → {results_csv}"
    )
    seeds = sorted({int(r["seed"]) for r in merged.values() if r.get("seed", "").isdigit()})
    if seeds:
        print(f"Seeds present: {min(seeds)}–{max(seeds)} ({len(seeds)} distinct)")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "sources", nargs="*", metavar="SOURCE",
        help="Remotes to pull (names from REMOTES, host:/path specs, or local "
             "paths). Default: all entries in REMOTES.",
    )
    parser.add_argument("--profile", choices=list(PROFILE_CSVS), default="full",
                        help="Which profile's CSV to merge; must match the profile "
                             "the runs used (default: full).")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show the rsync commands without transferring or writing.")
    parser.add_argument("--no-results", action="store_true",
                        help="Merge only the CSV; skip rsyncing the (potentially "
                             "large) per-run result directories.")
    args = parser.parse_args()

    csv_name = PROFILE_CSVS[args.profile]
    result_dir = None if args.no_results else PROFILE_RESULT_DIRS[args.profile]
    results_csv = REPO_ROOT / "experiments" / csv_name
    print(f"Profile: {args.profile} → {csv_name}\n")

    names = args.sources or list(REMOTES)
    tmp_dir = REPO_ROOT / "experiments" / ".merge_tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)

    pulled: list[Path] = []
    for name in names:
        label, root = resolve_source(name)
        csv_path = pull_source(
            label, root, args.dry_run, tmp_dir, csv_name, result_dir)
        if csv_path is not None:
            pulled.append(csv_path)

    if args.dry_run:
        print("\nDry run — no files written. CSV merge skipped.")
        return

    if not pulled:
        print(f"\nNo {csv_name} found on any source — nothing merged. "
              f"Did those runs use --profile {args.profile}?")
        return

    merge_csv(pulled, results_csv)


if __name__ == "__main__":
    main()
