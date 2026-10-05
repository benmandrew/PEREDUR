#!/usr/bin/env python3
"""Check that each Spectra input of the Maoz baseline arm means what the
PEREDUR subject of the same name means.

GLASS, JVTS-Repair and AMT13 read Spectra, and the arm feeds them the files
in `<inputs>/<spec>.spectra`. Their repairs are added to
`examples/<spec>/spec.tlsf` afterwards, so the two files must state one
specification, or a repair is computed for one problem and scored on another.

Both files are reduced to four formulas under scripts/spectra_ltl.py's
encoding: the environment's initial condition (INITIALLY), the rest of the
environment's side (REQUIRE, under G, and ASSUMPTIONS), the system's side
(PRESET, ASSERT under G, and GUARANTEES), and the system's variable domains
are left implicit on both. Each pair is checked for equivalence with
`ltlfilt --equivalent-to`. The system side pools PRESET with the rest because
the TLSF files sometimes move a guarantee's first instant there (pcar-v2-888
does, with a comment saying so).

A statement the translator cannot read fails the check: an input passes only
when every one of its statements is accounted for.

Usage:
    python scripts/maoz_inputs_check.py \\
        experiments/2026-10-02-maoz-baselines/inputs [SPEC ...]
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from check_well_separated import parse_tlsf  # noqa: E402
from spectra_ltl import Untranslatable, parse_spec, statement_ltl  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent


def conj(items: list[str]) -> str:
    return " & ".join(f"({i})" for i in items) if items else "1"


def tlsf_groups(path: Path) -> dict[str, str]:
    s = parse_tlsf(path).sections
    g = lambda key: [f"G ({f})" for f in s.get(key, [])]  # noqa: E731
    return {
        "env init": conj(s.get("initially", [])),
        "env rest": conj(g("require") + s.get("assume", [])),
        "sys": conj(s.get("preset", []) + g("assert") + s.get("guarantee", [])),
    }


def spectra_groups(path: Path) -> tuple[dict[str, str], list[str]]:
    spec = parse_spec(path.read_text())
    groups: dict[str, list[str]] = {"env init": [], "env rest": [], "sys": []}
    bad = []
    for kind, body in spec.statements:
        try:
            ltl, initial = statement_ltl(body, spec)
        except Untranslatable as e:
            bad.append(f"{kind} {' '.join(body.split())[:70]!r}: {e}")
            continue
        if kind == "gar":
            groups["sys"].append(ltl)
        else:
            groups["env init" if initial else "env rest"].append(ltl)
    return {k: conj(v) for k, v in groups.items()}, bad


def equivalent(a: str, b: str, ltlfilt: str) -> bool:
    r = subprocess.run([ltlfilt, "-F", "-", "--equivalent-to", b], input=a,
                       capture_output=True, text=True, timeout=1800)
    if r.returncode not in (0, 1):
        raise RuntimeError(r.stderr.strip())
    return bool(r.stdout.strip())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", type=Path)
    ap.add_argument("specs", nargs="*")
    ap.add_argument("--examples", type=Path, default=REPO_ROOT / "examples")
    ap.add_argument("--ltlfilt", default="ltlfilt")
    args = ap.parse_args()
    specs = args.specs or sorted(p.stem for p in args.inputs.glob("*.spectra"))
    failed = 0
    for name in specs:
        theirs, bad = spectra_groups(args.inputs / f"{name}.spectra")
        ours = tlsf_groups(args.examples / name / "spec.tlsf")
        verdicts = {k: equivalent(theirs[k], ours[k], args.ltlfilt) for k in ours}
        ok = not bad and all(verdicts.values())
        failed += not ok
        cells = "  ".join(f"{k}: {'same' if v else 'DIFFERENT'}" for k, v in verdicts.items())
        print(f"{name:<14} {'MATCH   ' if ok else 'MISMATCH'}  {cells}")
        for b in bad:
            print(f"    untranslatable {b}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
