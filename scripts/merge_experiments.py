"""Merge per-host results CSVs on their natural key.

The merge half of ``campaign.py collect``, which pulls each host's results
with pull_source() and joins them with merge_csv(). Idempotent: the aggregate
CSV is merged by its natural key (see KEY_FIELDS), keeping exactly one row per
key, so re-running never duplicates rows. Per-run result directories are named
with their seed (``sweep_A_gen10_fsm_seed17``) so they never collide between
machines; rsync only transfers what changed.

Vendored into every campaign archive beside run_experiments.py and
gen_configs.py.
"""

import csv
import re
import subprocess
from pathlib import Path

# The LEGACY_* defaults and canonical_scheme() are run_experiments.py's, so the
# merge key and the resume key cannot drift apart. Each archive vendors both
# scripts into one directory, where this import resolves as it does here.
from run_experiments import (
    LEGACY_COMMIT, LEGACY_METRIC, LEGACY_REPAIR, LEGACY_SELECTION,
    LEGACY_WEAKENING, PROFILES, canonical_scheme)

REPO_ROOT = Path(__file__).parent.parent

# Path to the PEREDUR checkout on a lab host.
REMOTE_ROOT = "/home/benandrew/projects/counter"

# The CSV and per-run directory each profile writes, both directly under
# experiments/ on every host. Derived from run_experiments.PROFILES, so a new
# profile needs nothing here. A retired profile drops out with its PROFILES
# entry; collect its archived CSV with ``campaign.py collect --csv``.
PROFILE_CSVS: dict[str, str] = {
    name: p["results_csv"].name for name, p in PROFILES.items()}
PROFILE_RESULT_DIRS: dict[str, str] = {
    name: p["results_dir"].name for name, p in PROFILES.items()}

# Natural key of a results row: one run per (sweep, level_name, selection,
# weakening, metric, repair_mode, spec, seed). The factor columns are part of it
# because a profile may run every level under both selection schemes
# (factorial), both weakening states (cj-large), both similarity metrics
# (metric), and both repair modes (muc) — without them the crossed rows collapse
# onto one key and half are silently dropped. Rows written before any of these
# columns existed carry the legacy defaults imported above.
#
# `commit` is deliberately NOT a key field. It is not a factor of the design —
# two rows differing only in the binary that produced them are the same cell,
# not two cells — and adding it would make every archived row (which has no
# commit at all) miss on resume and silently re-run whole campaigns.
KEY_FIELDS = ("sweep", "level_name", "selection", "weakening", "metric",
              "repair_mode", "spec", "seed")


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
