#!/usr/bin/env python3
"""Tests that docs/dev/psp-orders.json still holds psp_field_orders.py's lowering.

No pytest dependency: run it directly (``python3 scripts/test_psp_field_orders.py``)
and it exits non-zero on the first failure. The C++ lowering's test
(``test/psp/lowering_tests.cpp``) reads the JSON's "lowering" table as its
fixture, so this is what keeps that fixture equal to the script. No Spot
binary is needed: it compares strings.
"""

import json
import sys
from pathlib import Path

import psp_field_orders as P  # noqa: E402

ORDERS = Path(__file__).resolve().parent.parent / "docs" / "dev" / "psp-orders.json"


def test_lowering_table_matches_the_script() -> None:
    recorded = json.loads(ORDERS.read_text())
    lowering = recorded["lowering"]
    expected = {
        P.node_name((pat, scope)): P.lower(scope, pat)
        for pat in P.patterns(recorded["max_count"])
        for scope in P.SCOPES
    }
    assert set(lowering) == set(expected), "the table covers every node"
    for node, formula in expected.items():
        assert lowering[node] == formula, f"{node}: {lowering[node]} != {formula}"
    assert recorded["max_count"] >= 4, "the table reaches count 4"


def test_dwyer_rows_match_the_script() -> None:
    recorded = json.loads(ORDERS.read_text())["dwyer"]
    for (kind, scope), row in P.DWYER.items():
        assert recorded[f"{kind}/{scope}"]["published"] == row, (kind, scope)


def main() -> int:
    tests = [test_lowering_table_matches_the_script, test_dwyer_rows_match_the_script]
    for test in tests:
        test()
    print(f"{len(tests)} tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
