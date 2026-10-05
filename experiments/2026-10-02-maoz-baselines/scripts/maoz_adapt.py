#!/usr/bin/env python3
"""Materialise GLASS, JVTS-Repair and AMT13 output as PEREDUR-shaped run
directories.

    python3 scripts/maoz_adapt.py --root OUT --inputs INPUTS --out RESULTS

maoz_campaign.py leaves one stream.txt per (algorithm, spec). Each repair in
it is a list of `asm ...;` assumptions over the Spectra input's variables.
This script translates each assumption into the subject TLSF's encoding with
scripts/spectra_ltl.py and adds it to a copy of examples/<spec>/spec.tlsf:
to REQUIRE, INITIALLY or ASSUMPTIONS according to its shape (see
spectra_ltl.assumption_section). The copy is accumulated/spec<i>.tlsf in a
run directory named `maoz-<tool>_<spec>_seed00`, with the index.tsv and
run.json that score_curves.py, `maximal` and check_well_separated.py read,
the same layout aurus_adapt.py writes.

A repair with any assumption the translator cannot read is left out and
counted, never written in part. The counts go to <out>/maoz-adapt.json and
one row per repair to <out>/maoz-translation.csv.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from maoz_campaign import ALGORITHMS, REPAIR_RE, RESULT, STREAM  # noqa: E402
from spectra_ltl import Untranslatable, assumption_section, parse_spec  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
ACCUMULATED_DIR = "accumulated"
INDEX_NAME = "index.tsv"
MANIFEST_NAME = "run.json"
SUMMARY_NAME = "maoz-adapt.json"
TRANSLATION_CSV = "maoz-translation.csv"

# The names a section may carry in a TLSF file (src/tlsf/parser.cpp).
ALIASES = {
    "INITIALLY": ("INITIALLY",),
    "REQUIRE": ("REQUIRE", "REQUIREMENTS"),
    "ASSUMPTIONS": ("ASSUMPTIONS", "ASSUME"),
}


def run_dir_name(alg: str, spec: str) -> str:
    """`maoz-glass_lift_seed00`: the tool name is a single hyphenated token,
    so score_curves.spec_from_dir_name still finds the spec between the
    underscores."""
    return f"maoz-{ALGORITHMS[alg].lower()}_{spec}_seed00"


def repairs(text: str) -> list[tuple[float, list[str]]]:
    """(elapsed seconds, assumption bodies) per repair, in stream order.

    A block with no `@@END` was cut mid-write by the kill at the cap. The tool
    never held it whole, so it is dropped."""
    out = []
    for m in REPAIR_RE.finditer(text):
        end = text.find("@@END", m.end())
        if end < 0:
            break
        block = text[m.end():end]
        bodies = []
        for stmt in block.split(";"):
            stmt = stmt.strip()
            if not stmt:
                continue
            if not stmt.startswith("asm"):
                raise ValueError(f"not an assumption: {stmt[:60]!r}")
            bodies.append(stmt[3:].strip())
        out.append((int(m.group(2)) / 1000.0, bodies))
    return out


def comment_mask(text: str) -> str:
    """`text` with each comment blanked, character for character, so a match
    on the result is an offset into the original."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        else:
            i += 1
            continue
        for k in range(i, j):
            if out[k] != "\n":
                out[k] = " "
        i = j
    return "".join(out)


def add_assumptions(tlsf: str, added: dict[str, list[str]]) -> str:
    """`tlsf` with each section's formulas appended to that section, which
    is created inside MAIN when the file has none."""
    for section, formulas in added.items():
        if not formulas:
            continue
        lines = "".join(f"\n    {f}; // added by repair" for f in formulas)
        masked = comment_mask(tlsf)
        names = "|".join(ALIASES[section])
        m = re.search(rf"\b(?:{names})\s*\{{", masked)
        if m:
            tlsf = tlsf[:m.end()] + lines + tlsf[m.end():]
            continue
        main = re.search(r"\bMAIN\s*\{", masked)
        if main is None:
            raise ValueError("no MAIN block")
        tlsf = (tlsf[:main.end()] + f"\n  {section} {{" + lines + "\n  }\n"
                + tlsf[main.end():])
    return tlsf


def adapt_one(alg: str, spec: str, job: Path, inputs: Path, out: Path,
              force: bool, rows: list) -> dict:
    record = {"algorithm": alg, "tool": ALGORITHMS[alg], "spec": spec}
    result = json.loads((job / RESULT).read_text())
    record.update({k: result.get(k) for k in
                   ("n_repairs", "n_raw", "capped", "killed", "finished",
                    "wall_s")})
    run_dir = out / run_dir_name(alg, spec)
    accumulated = run_dir / ACCUMULATED_DIR
    if (accumulated / INDEX_NAME).exists() and not force:
        record["status"] = "present"
        return record
    if accumulated.is_dir():
        shutil.rmtree(accumulated)
    accumulated.mkdir(parents=True)

    sspec = parse_spec((inputs / f"{spec}.spectra").read_text())
    base = (REPO_ROOT / "examples" / spec / "spec.tlsf").read_text()
    written, untranslatable, duplicates = [], 0, 0
    seen: set[frozenset[str]] = set()
    found = repairs((job / STREAM).read_text(errors="replace"))
    for index, (elapsed, bodies) in enumerate(found, start=1):
        # The driver already prints each assumption set once; this guards a
        # stream written by a driver that did not.
        key = frozenset(" ".join(b.split()) for b in bodies)
        if key in seen:
            duplicates += 1
            continue
        seen.add(key)
        added: dict[str, list[str]] = {k: [] for k in ALIASES}
        try:
            for body in bodies:
                section, ltl = assumption_section(body, sspec)
                added[section].append(ltl)
        except Untranslatable as exc:
            untranslatable += 1
            rows.append({"algorithm": alg, "spec": spec, "repair": index,
                         "elapsed_s": elapsed, "file": "",
                         "status": f"untranslatable: {exc}"})
            continue
        name = f"spec{len(written)}.tlsf"
        (accumulated / name).write_text(add_assumptions(base, added))
        written.append((name, elapsed))
        rows.append({"algorithm": alg, "spec": spec, "repair": index,
                     "elapsed_s": elapsed, "file": name, "status": "written",
                     **{f"n_{k.lower()}": len(v) for k, v in added.items()}})

    with open(accumulated / INDEX_NAME, "w") as handle:
        handle.write("file\tgeneration\telapsed_s\n")
        for name, elapsed in written:
            handle.write(f"{name}\t0\t{elapsed:.6f}\n")
    # `tool` marks this as no PEREDUR manifest; the fields beside it are the
    # ones score_curves.run_columns reads.
    manifest = {
        "tool": f"maoz-{ALGORITHMS[alg].lower()}",
        "written_by": "scripts/maoz_adapt.py",
        "input": f"examples/{spec}/spec.tlsf",
        "seed": 0,
        "stopped_by": "deadline" if result.get("killed") else "exhausted",
        "generations_run": "",
        "wall_s": result.get("wall_s"),
        "config": {"genetic": {"selection_scheme": ""},
                   "fitness": {"status_grading": ""}},
    }
    (run_dir / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2) + "\n")
    record.update({"status": "written", "n_written": len(written),
                   "n_untranslatable": untranslatable,
                   "n_duplicate": duplicates,
                   "run_dir": run_dir.name})
    return record


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, required=True,
                    help="maoz_campaign.py's --out-root")
    ap.add_argument("--inputs", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    records, rows = [], []
    for alg in ALGORITHMS:
        for job in sorted((args.root / alg).glob(f"*/{RESULT}")):
            spec = job.parent.name
            record = adapt_one(alg, spec, job.parent, args.inputs, args.out,
                               args.force, rows)
            records.append(record)
            print(f"{alg:<5} {spec:<14} {record['status']:<8} "
                  f"{record.get('n_written', '')} written, "
                  f"{record.get('n_untranslatable', '')} untranslatable")
    (args.out / SUMMARY_NAME).write_text(json.dumps(records, indent=2) + "\n")
    fields = ["algorithm", "spec", "repair", "elapsed_s", "file", "status",
              "n_initially", "n_require", "n_assumptions"]
    with open(args.out / TRANSLATION_CSV, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
