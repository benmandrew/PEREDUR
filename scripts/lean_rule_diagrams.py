#!/usr/bin/env python3
"""Print the Hasse diagram of each timing table proved in proofs/Fretish/Rules.lean.

proofs/RuleTables.lean prints the four timing tables, `timingTable` under one
scope of each class, for every ordered pair of timings with tick counts up to
4. This script checks that each table is a preorder (reflexive and transitive
on the grid), merges the timings each table makes equivalent, and prints the
covering pairs: the edges a Hasse diagram draws. The paper's timing figure
draws these tables, so its edges should be these, with the families cut at
tick count 4 here and at a representative N there.

The end of each family is an artefact of the cut: `within 4` covers
`eventually` in the global table, and `eventually` covers `after 4` in the
only tables, because no `within 5` is on the grid.

By default it runs `lake env lean --run RuleTables.lean` in proofs/, which
needs Lean (`nix develop '.#proofs'`). With --lean-output it reads that output
from a file instead.

Exits 1 if a table is not a preorder.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from common import lean_output

PANELS = {
    "global": "le0K: global, after",
    "in": "leBK: in, not in, before",
    "onlyIn": "leO leBK: only in, only after",
    "onlyBefore": "leO le0K: only before",
}


def parse(text: str) -> dict[str, dict[tuple[str, str], bool]]:
    tables: dict[str, dict[tuple[str, str], bool]] = {}
    for line in text.splitlines():
        if not line:
            continue
        row = line.split("\t")
        if len(row) != 4 or row[3] not in ("0", "1"):
            sys.exit(f"not `panel<TAB>stronger<TAB>weaker<TAB>0|1`: {line!r}")
        tables.setdefault(row[0], {})[(row[1], row[2])] = row[3] == "1"
    if set(tables) != set(PANELS):
        sys.exit(f"expected panels {sorted(PANELS)}, got {sorted(tables)}")
    return tables


Table = dict[tuple[str, str], bool]


def diagram(table: Table) -> tuple[list[str], list[list[str]], list[tuple[str, str]]]:
    """The preorder failures, the equivalence classes and the covering pairs."""
    names = list(dict.fromkeys(a for a, _ in table))
    le = lambda a, b: table[(a, b)]  # noqa: E731
    failures = [f"{a} not reflexive" for a in names if not le(a, a)]
    failures += [
        f"{a} => {b} => {c} but not {a} => {c}"
        for a in names
        for b in names
        for c in names
        if le(a, b) and le(b, c) and not le(a, c)
    ]
    classes: list[list[str]] = []
    for a in names:
        for cls in classes:
            if le(a, cls[0]) and le(cls[0], a):
                cls.append(a)
                break
        else:
            classes.append([a])
    reps = [cls[0] for cls in classes]
    label = {cls[0]: " = ".join(cls) for cls in classes}
    lt = lambda a, b: le(a, b) and not le(b, a)  # noqa: E731
    covers = [
        (label[a], label[b])
        for a in reps
        for b in reps
        if lt(a, b) and not any(lt(a, c) and lt(c, b) for c in reps)
    ]
    return failures, [cls for cls in classes if len(cls) > 1], covers


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(__doc__ or "").splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--lean-output",
        type=Path,
        default=None,
        help="read the output of proofs/RuleTables.lean from this file",
    )
    args = parser.parse_args()

    tables = parse(lean_output(args, "RuleTables.lean"))
    status = 0
    for panel, title in PANELS.items():
        failures, merged, covers = diagram(tables[panel])
        print(f"{title}")
        for failure in failures[:10]:
            print(f"  NOT A PREORDER: {failure}")
        if failures:
            status = 1
        for cls in merged:
            print(f"  equivalent: {' = '.join(cls)}")
        for a, b in covers:
            print(f"  {a} -> {b}")
        print()
    return status


if __name__ == "__main__":
    sys.exit(main())
