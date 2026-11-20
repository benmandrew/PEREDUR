#!/usr/bin/env python3
"""Check that the Lean transcriptions of the PSP order tables agree with the C++.

proofs/PspOrders.lean prints `patternImplies`, `scopeImplies` and
`fieldPolarity` from proofs/Psp/Cpp.lean for every PSP pattern with counts
from 1 to 4 and every scope. This script compares:

* the pattern table with the test helper `psp_pattern_order_table`, and with
  the strict edges SPOT measured in `pattern_order` of docs/dev/psp-orders.json;
* the scope table with `psp_scope_order_table`, and with `scope_order`;
* the polarities with `polarity`.

`pattern_implies_exact` and `scope_implies_exact` in proofs/Psp/Cpp.lean prove
the transcribed tables exact against the lowering at every count, and
`polarity_sound` in proofs/Psp/Polarity.lean proves each polarity, so
agreement here ties those theorems to the C++ functions. Agreement with the
JSON checks the proofs against SPOT on the measured range.

By default it runs `lake env lean --run PspOrders.lean` in proofs/, which
needs Lean (`nix develop '.#proofs'`). With --lean-output it reads that output
from a file instead.

Exits 1 on any disagreement, or if a control finds none.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from common import lean_output

REPO = Path(__file__).resolve().parent.parent

Table = dict[tuple[str, str, str], str]


def cpp_output(helper: Path) -> str:
    return subprocess.run(
        [str(helper)], capture_output=True, text=True, check=True, timeout=600
    ).stdout


def parse(lines: list[list[str]], source: str) -> Table:
    table: Table = {}
    for row in lines:
        if len(row) != 4 or row[3] not in ("0", "1"):
            sys.exit(f"{source}: not `x<TAB>stronger<TAB>weaker<TAB>0|1`: {row!r}")
        key = (row[0], row[1], row[2])
        if key in table:
            sys.exit(f"{source}: duplicate row {key}")
        table[key] = row[3]
    if not table:
        sys.exit(f"{source}: no rows")
    return table


def cpp_table(helper: Path, source: str) -> Table:
    return parse([line.split("\t") for line in cpp_output(helper).splitlines() if line], source)


def measured(edges: dict[str, list[list[str]]], keys: Table) -> Table:
    """The JSON's strict edges as a table over the cells of `keys`, reflexive
    cells as 1."""
    out: Table = {}
    present = {(x, a, b) for x, pairs in edges.items() for a, b in pairs}
    for key in keys:
        x, a, b = key
        out[key] = "1" if a == b or (x, a, b) in present else "0"
    return out


def compare(name: str, lean: Table, other: Table) -> tuple[int, int, list]:
    """Disagreements, and the control: `other` read with stronger and weaker
    swapped must disagree with `lean` somewhere."""
    if lean.keys() != other.keys():
        only_lean = sorted(lean.keys() - other.keys())
        only_other = sorted(other.keys() - lean.keys())
        sys.exit(
            f"{name}: the tables cover different cells: Lean only {only_lean[:5]}, "
            f"other only {only_other[:5]}"
        )
    bad = sorted(key for key in lean if lean[key] != other[key])
    swapped = sum(1 for (x, a, b) in lean if lean[(x, a, b)] != other[(x, b, a)])
    edges = sum(1 for value in lean.values() if value == "1")
    print(
        f"{name}: {len(lean)} cells, {edges} edges, {len(bad)} disagreement(s); "
        f"control: {swapped} swapped cells differ"
    )
    for key in bad[:10]:
        print(f"  {key}: lean {lean[key]}, other {other[key]}")
    return len(bad), swapped, bad


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(__doc__ or "").splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    build = REPO / "build" / "test"
    parser.add_argument("--pattern-helper", type=Path, default=build / "psp_pattern_order_table")
    parser.add_argument("--scope-helper", type=Path, default=build / "psp_scope_order_table")
    parser.add_argument("--orders", type=Path, default=REPO / "docs" / "dev" / "psp-orders.json")
    parser.add_argument(
        "--lean-output",
        type=Path,
        default=None,
        help="read the output of proofs/PspOrders.lean from this file",
    )
    args = parser.parse_args()

    rows = [line.split("\t") for line in lean_output(args, "PspOrders.lean").splitlines() if line]
    pattern = parse([row[1:] for row in rows if row[0] == "pattern"], "Lean pattern")
    scope = parse([row[1:] for row in rows if row[0] == "scope"], "Lean scope")
    polarity: dict[str, dict[str, str]] = {}
    for row in rows:
        if row[0] == "polarity":
            if len(row) != 4:
                sys.exit(f"Lean polarity: not `polarity<TAB>cell<TAB>field<TAB>sign`: {row!r}")
            polarity.setdefault(row[1], {})[row[2]] = row[3]
    if len(rows) != len(pattern) + len(scope) + sum(len(v) for v in polarity.values()):
        sys.exit("Lean output has rows of an unknown kind")

    orders = json.loads(args.orders.read_text())
    results = [
        compare("pattern vs C++", pattern, cpp_table(args.pattern_helper, "C++ pattern")),
        compare("pattern vs SPOT", pattern, measured(orders["pattern_order"], pattern)),
        compare("scope vs C++", scope, cpp_table(args.scope_helper, "C++ scope")),
        compare("scope vs SPOT", scope, measured(orders["scope_order"], scope)),
    ]

    spot_polarity: dict[str, dict[str, str]] = orders["polarity"]
    bad_polarity = sorted(
        cell for cell in polarity.keys() | spot_polarity.keys()
        if polarity.get(cell) != spot_polarity.get(cell)
    )
    # Control: the polarity table with P and S swapped must disagree.
    swap = {"P": "S", "S": "P", "Q": "Q", "R": "R"}
    swapped_polarity = sum(
        1 for cell, fields in polarity.items()
        if {swap[f]: v for f, v in fields.items()} != spot_polarity.get(cell)
    )
    print(
        f"polarity vs SPOT: {len(polarity)} cells, {len(bad_polarity)} disagreement(s); "
        f"control: {swapped_polarity} cells differ with P and S swapped"
    )
    for cell in bad_polarity[:10]:
        print(f"  {cell}: lean {polarity.get(cell)}, spot {spot_polarity.get(cell)}")

    failed = any(bad or not swapped for bad, swapped, _ in results)
    failed = failed or bool(bad_polarity) or not swapped_polarity
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
