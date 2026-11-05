#!/usr/bin/env python3
"""Check that the Lean model of the lowering agrees with requirement_to_ltl.

proofs/Lowering.lean prints, for every small requirement, the requirement as
`ltl` driver JSON and the formula the Lean `lowerSyn` builds for it. This
script lowers the same requirements with the C++ `ltl` driver and asks SPOT
whether each pair of formulas is equivalent. `eval_lowerSyn` in
proofs/Fretish/Syntax.lean proves the Lean formula means what the proofs
assume, so agreement here ties the proofs to the C++ on every requirement
printed.

By default it runs `lake env lean --run Lowering.lean` in proofs/, which needs
Lean (`nix develop '.#proofs'`). With --lean-output it reads that output from
a file instead.

Exits 1 on any disagreement.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from field_orders import default_spot_bin_dir

REPO = Path(__file__).resolve().parent.parent


def lean_output(args: argparse.Namespace) -> str:
    if args.lean_output is not None:
        return args.lean_output.read_text()
    return subprocess.run(
        ["lake", "env", "lean", "--run", "Lowering.lean"],
        cwd=REPO / "proofs",
        capture_output=True,
        text=True,
        check=True,
        timeout=1800,
    ).stdout


def cpp_formulas(ltl: Path, guarantees: list[dict], jobs: int) -> list[str]:
    """Lower each guarantee in a spec of its own.

    The loader drops a guarantee identical to an earlier one, and it reads
    `within 0` and `for 0` as `immediately`, so one spec holding every
    guarantee would print fewer formulas than it was given.
    """

    def lower(guarantee: dict) -> str:
        spec = {
            "assumptions": [],
            "guarantees": [guarantee],
            "in_atoms": ["c", "s"],
            "out_atoms": ["r"],
            "modes": ["m"],
        }
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "spec.json"
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
        if len(formulas) != 1:
            sys.exit(
                f"ltl printed {len(formulas)} formulas for {json.dumps(guarantee)}"
            )
        return formulas[0]

    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        return list(pool.map(lower, guarantees))


def differing(ltlfilt: Path, pairs: list[tuple[str, str]], jobs: int) -> list[int]:
    """Indices of the pairs whose formulas are not equivalent."""

    def batch(start: int, chunk: list[tuple[str, str]]) -> list[int]:
        text = "".join(f"!(({a}) <-> ({b}))\n" for a, b in chunk)
        run = subprocess.run(
            [str(ltlfilt), "--satisfiable", "--format=%L", "-F", "-"],
            input=text,
            capture_output=True,
            text=True,
            check=False,
            timeout=1800,
        )
        # ltlfilt exits 0 when some line is satisfiable and 1 when none is.
        if run.returncode not in (0, 1) or run.stderr.strip():
            sys.exit(f"ltlfilt failed (exit {run.returncode}): {run.stderr}")
        return [start + int(line) - 1 for line in run.stdout.split()]

    size = max(1, -(-len(pairs) // (jobs * 4)))
    starts = range(0, len(pairs), size)
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        parts = pool.map(lambda s: batch(s, pairs[s : s + size]), starts)
        return sorted(i for part in parts for i in part)


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
        help="read the output of proofs/Lowering.lean from this file",
    )
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()

    ltlfilt = (args.spot_bin_dir or default_spot_bin_dir()) / "ltlfilt"
    rows = [line.split("\t") for line in lean_output(args).splitlines() if line]
    if not rows or any(len(row) != 2 for row in rows):
        sys.exit("Lean output is not one `json<TAB>formula` row per line")
    guarantees = [json.loads(row[0]) for row in rows]
    lean = [row[1] for row in rows]
    cpp = cpp_formulas(args.ltl, guarantees, args.jobs)

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
