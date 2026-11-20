#!/usr/bin/env python3
"""Check that the Lean model of the PSP lowering agrees with requirement_to_ltl.

proofs/PspLowering.lean prints, for every PSP pattern with counts from 1 to 4
under every scope, the requirement as `.psp` JSON and the formula the Lean
`lowerSyn` builds for it. This script lowers the same requirements with the
C++ `ltl` driver and asks SPOT whether each pair of formulas is equivalent.
`eval_lowerSyn` in proofs/Psp/Syntax.lean proves the Lean formula means what
the proofs assume, so agreement here ties the proofs to the C++ on every
requirement printed.

By default it runs `lake env lean --run PspLowering.lean` in proofs/, which
needs Lean (`nix develop '.#proofs'`). With --lean-output it reads that output
from a file instead.

Exits 1 on any disagreement, or if the control finds none.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from check_lean_lowering import differing
from common import lean_output
from field_orders import default_spot_bin_dir

REPO = Path(__file__).resolve().parent.parent


def cpp_formulas(ltl: Path, guarantees: list[dict]) -> list[str]:
    """Lower every guarantee in one `.psp` spec, in order."""
    spec = {
        "assumptions": [],
        "guarantees": guarantees,
        "in_atoms": ["p", "q", "r"],
        "out_atoms": ["s"],
    }
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "spec.psp"
        path.write_text(json.dumps(spec))
        out = subprocess.run(
            [str(ltl), str(path)],
            capture_output=True,
            text=True,
            check=True,
            timeout=600,
        ).stdout
    formulas = [
        line.strip()[len("LTL:") :].strip()
        for line in out.splitlines()
        if line.strip().startswith("LTL:")
    ]
    if len(formulas) != len(guarantees):
        sys.exit(f"ltl printed {len(formulas)} formulas for {len(guarantees)} guarantees")
    return formulas


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(__doc__ or "").splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--ltl", type=Path, default=REPO / "build" / "ltl")
    parser.add_argument("--spot-bin-dir", type=Path, default=None)
    parser.add_argument(
        "--lean-output",
        type=Path,
        default=None,
        help="read the output of proofs/PspLowering.lean from this file",
    )
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()

    ltlfilt = (args.spot_bin_dir or default_spot_bin_dir()) / "ltlfilt"
    text = lean_output(args, "PspLowering.lean")
    rows = [line.split("\t") for line in text.splitlines() if line]
    if not rows or any(len(row) != 2 for row in rows):
        sys.exit("Lean output is not one `json<TAB>formula` row per line")
    guarantees = [json.loads(row[0]) for row in rows]
    lean = [row[1] for row in rows]
    cpp = cpp_formulas(args.ltl, guarantees)

    bad = differing(ltlfilt, list(zip(lean, cpp)), args.jobs)
    # The check must be able to fail: pairing each Lean formula with the C++
    # formula of the next requirement has to produce disagreements.
    shifted = differing(ltlfilt, list(zip(lean, cpp[1:] + cpp[:1])), args.jobs)

    print(f"{len(rows)} requirements, {len(bad)} disagreement(s)")
    print(f"control: {len(shifted)} of {len(rows)} shifted pairs differ")
    for i in bad[:10]:
        print(f"\n{json.dumps(guarantees[i])}\n  lean: {lean[i]}\n  c++:  {cpp[i]}")
    return 1 if bad or not shifted else 0


if __name__ == "__main__":
    sys.exit(main())
