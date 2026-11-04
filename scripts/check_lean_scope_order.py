#!/usr/bin/env python3
"""Check that the Lean transcription of scope_implies agrees with the C++.

proofs/ScopeOrderTable.lean prints `scopeImplies` from
proofs/Fretish/ScopeOrder.lean for every condition type, timing with tick
counts up to 4 and ordered pair of scopes, the weaker at the stronger's mode
or another one. The test helper `scope_order_table` prints `scope_implies`
over the same grid. This script compares the two tables cell by cell.
`scope_implies_exact` in proofs/Fretish/ScopeOrder.lean proves the
transcription against the lowering, so agreement here ties the scope order to
the C++ function.

`scope_edges` branches only on whether the timing is `eventually` or `always`
and on the condition type, so the grid reaches every branch.

By default it runs `lake env lean --run ScopeOrderTable.lean` in proofs/,
which needs Lean (`nix develop '.#proofs'`). With --lean-output it reads that
output from a file instead.

Exits 1 on any disagreement.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

Key = tuple[str, str, str, str, str]
Table = dict[Key, str]


def lean_output(args: argparse.Namespace) -> str:
    if args.lean_output is not None:
        return args.lean_output.read_text()
    return subprocess.run(
        ["lake", "env", "lean", "--run", "ScopeOrderTable.lean"],
        cwd=REPO / "proofs",
        capture_output=True,
        text=True,
        check=True,
        timeout=1800,
    ).stdout


def cpp_output(helper: Path) -> str:
    return subprocess.run(
        [str(helper)], capture_output=True, text=True, check=True, timeout=600
    ).stdout


def parse(text: str, source: str) -> Table:
    table: Table = {}
    for line in text.splitlines():
        if not line:
            continue
        row = line.split("\t")
        if len(row) != 6 or row[5] not in ("0", "1"):
            sys.exit(
                f"{source}: not `type<TAB>timing<TAB>stronger<TAB>weaker"
                f"<TAB>mode<TAB>0|1`: {line!r}"
            )
        key = (row[0], row[1], row[2], row[3], row[4])
        if key in table:
            sys.exit(f"{source}: duplicate row {key}")
        table[key] = row[5]
    if not table:
        sys.exit(f"{source}: no rows")
    return table


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(__doc__ or "").splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--helper", type=Path, default=REPO / "build" / "test" / "scope_order_table"
    )
    parser.add_argument(
        "--lean-output",
        type=Path,
        default=None,
        help="read the output of proofs/ScopeOrderTable.lean from this file",
    )
    args = parser.parse_args()

    lean = parse(lean_output(args), "Lean")
    cpp = parse(cpp_output(args.helper), "C++")
    if lean.keys() != cpp.keys():
        only_lean = sorted(lean.keys() - cpp.keys())
        only_cpp = sorted(cpp.keys() - lean.keys())
        sys.exit(
            f"the tables cover different cells: Lean only {only_lean[:5]}, "
            f"C++ only {only_cpp[:5]}"
        )

    bad = sorted(key for key in lean if lean[key] != cpp[key])
    # The check must be able to fail: reading the C++ table with its two
    # scopes swapped has to disagree with the Lean one.
    swapped = sum(
        1
        for (ctype, tim, a, b, mode) in lean
        if lean[(ctype, tim, a, b, mode)] != cpp[(ctype, tim, b, a, mode)]
    )
    edges = sum(1 for value in cpp.values() if value == "1")

    print(f"{len(lean)} cells, {edges} edges, {len(bad)} disagreement(s)")
    print(f"control: {swapped} of {len(lean)} swapped cells differ")
    for key in bad[:10]:
        print(f"  {key}: lean {lean[key]}, c++ {cpp[key]}")
    return 1 if bad or not swapped else 0


if __name__ == "__main__":
    sys.exit(main())
