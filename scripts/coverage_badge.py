#!/usr/bin/env python3
"""Measure line coverage of `src/` and `include/` and draw the badge the README embeds.

    python scripts/coverage_badge.py           # build, run the suite, write the badge
    python scripts/coverage_badge.py --check   # fail if the committed badge is stale
    python scripts/coverage_badge.py --json r.json   # reuse an llvm-cov export

The badge is a file in the repository rather than a call out to a badge service, so
the README renders the same on a fork with no secrets and in an offline clone. That
only works if the file is regenerated when the number moves, which is what `--check`
is for.

Measurement is clang's source-based coverage: the `coverage` preset compiles with
`-fprofile-instr-generate -fcoverage-mapping`, its test preset points
`LLVM_PROFILE_FILE` at `build-coverage/profraw/`, and `llvm-cov export` reads the
merged profile back against every instrumented binary. GCC cannot build that preset
-- the flags are clang's, and `--coverage` with gcov would be a different number
from a different tool.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BADGE = ROOT / "docs" / "coverage.svg"
BUILD = ROOT / "build-coverage"
PROFRAW_DIR = BUILD / "profraw"
PROFDATA = BUILD / "coverage.profdata"

# Every instrumented binary, not just the test binary: the badge is a claim about
# src/ and include/, and a driver nothing runs is uncovered code rather than
# absent code. Naming them explicitly means a new binary that nothing measures
# fails here rather than quietly leaving its lines out of the denominator.
BINARIES = (
    "peredur",
    "compare",
    "fingerprint",
    "lint-ideals",
    "ltl",
    "mucs",
    "realize",
    "signal_tracer",
    "test/peredur_tests",
)

# The measured tree. `include/` is half the code -- the templates and the inline
# headers -- so a badge over `src/` alone would be measuring the smaller half.
SOURCES = ("src", "include")

LABEL = "coverage"

# Thresholds and colours are the shields.io flat palette, the same steps
# github.com/benmandrew/wayfare uses, so the two repositories' badges read alike.
COLOURS: tuple[tuple[float, str], ...] = (
    (95.0, "#44cc11"),
    (85.0, "#97ca00"),
    (75.0, "#a4a61d"),
    (65.0, "#dfb317"),
    (50.0, "#fe7d37"),
    (0.0, "#e05d44"),
)

# Advance widths of Verdana at 11px, in hundredths of a pixel, for the alphabet a
# badge can contain. Baked in rather than measured at run time: `--check` compares
# bytes, and measuring through the local font stack would have the badge depend on
# which fonts the machine happens to have, so two developers would generate two
# different files from one coverage run. Verdana because it is what the SVG asks
# for first, and what shields.io sizes its own badges with.
_ADVANCE: dict[str, int] = {
    " ": 387, "%": 1184, ".": 400, "0": 699, "1": 699, "2": 699, "3": 699, "4": 699,
    "5": 699, "6": 699, "7": 699, "8": 699, "9": 699, "a": 661, "b": 685, "c": 573,
    "d": 685, "e": 655, "f": 387, "g": 685, "h": 696, "i": 302, "j": 379, "k": 651,
    "l": 302, "m": 1070, "n": 696, "o": 668, "p": 685, "q": 685, "r": 469, "s": 573,
    "t": 433, "u": 696, "v": 651, "w": 900, "x": 651, "y": 651, "z": 578,
}  # fmt: skip

# 5px of clear space each side of every string, which is the shields.io flat metric.
PADDING = 10
HEIGHT = 20

# How far a fresh measurement may sit outside the committed badge's rounding
# band before --check calls the badge stale. The suite spawns real tools and
# branches on their timings and peak resident set, so the figure moves by up to
# two tenths of a point between runs of one binary -- and a badge whose true
# value sits a tenth above a rounding boundary would otherwise fail on the run
# that landed the other side of it. A drop this wide is jitter; anything wider
# is the number actually moving.
CHECK_SLACK = 0.25


def text_width(s: str) -> int:
    """Width of `s` in whole pixels at 11px Verdana."""
    missing = sorted(set(s) - set(_ADVANCE))
    if missing:
        raise ValueError(
            f"no baked advance width for {missing}; extend _ADVANCE by measuring the "
            "character in Verdana at 11px"
        )
    return round(sum(_ADVANCE[c] for c in s) / 100)


def shown_percent(percent: float) -> int:
    """The whole number the badge prints.

    Rounding is held below 100 until coverage actually reaches it, because a badge
    reading 100% is a claim about the suite that 99.6% does not support.
    """
    shown = round(percent)
    return 99 if shown == 100 and percent < 100.0 else shown


def colour_for(shown: int) -> str:
    """The band colour, taken from the printed number rather than the measured one.

    Reading the true value here instead puts 49.9% on the badge as a red `50%`, so
    the two halves disagree about which side of a threshold the run landed on.
    """
    return next(colour for floor, colour in COLOURS if shown >= floor)


def render(percent: float) -> str:
    shown = shown_percent(percent)
    status = f"{shown}%"
    label_w = text_width(LABEL) + PADDING
    status_w = text_width(status) + PADDING
    total = label_w + status_w
    label_x = label_w / 2
    status_x = label_w + status_w / 2
    return f"""\
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"
     width="{total}" height="{HEIGHT}" role="img" aria-label="{LABEL}: {status}">
  <title>{LABEL}: {status}</title>

  <linearGradient id="smooth" x2="0" y2="100%">
    <stop offset="0" stop-color="#bbb" stop-opacity=".1"/>
    <stop offset="1" stop-opacity=".1"/>
  </linearGradient>

  <mask id="round">
    <rect width="{total}" height="{HEIGHT}" rx="3" fill="#fff"/>
  </mask>

  <g mask="url(#round)">
    <rect width="{label_w}" height="{HEIGHT}" fill="#555"/>
    <rect x="{label_w}" width="{status_w}" height="{HEIGHT}" fill="{colour_for(shown)}"/>
    <rect width="{total}" height="{HEIGHT}" fill="url(#smooth)"/>
  </g>

  <g fill="#fff" text-anchor="middle"
     font-family="Verdana,Geneva,DejaVu Sans,sans-serif" font-size="11">
    <text x="{label_x:g}" y="15" fill="#010101" fill-opacity=".3">{LABEL}</text>
    <text x="{label_x:g}" y="14">{LABEL}</text>
    <text x="{status_x:g}" y="15" fill="#010101" fill-opacity=".3">{status}</text>
    <text x="{status_x:g}" y="14">{status}</text>
  </g>
</svg>
"""


def llvm_tool(name: str) -> str:
    """Locate an LLVM tool, preferring an explicit override.

    The profile format is versioned against the clang that wrote it, so a system
    llvm-profdata older than the dev shell's clang reads the run as corrupt. The
    override is how a host with several LLVMs installed names the right one.
    """
    override = os.environ.get(name.replace("-", "_").upper())
    found = override or shutil.which(name)
    if not found:
        raise SystemExit(
            f"{name} is not on PATH; enter the dev shell (`nix develop`) or set "
            f"{name.replace('-', '_').upper()} to its path"
        )
    return found


def run(cmd, capture=False):
    return subprocess.run(
        cmd, cwd=ROOT, text=True, check=False, capture_output=capture
    )


def build_and_test(ctest_args: list[str]) -> None:
    """Configure, build and run the suite under instrumentation."""
    # Profiles accumulate: a file left by a previous run is merged into this one,
    # crediting lines that this build may no longer execute or even contain.
    shutil.rmtree(PROFRAW_DIR, ignore_errors=True)

    if run(["cmake", "--preset", "coverage"]).returncode != 0:
        raise SystemExit("configure failed; badge not written")
    if run(["cmake", "--build", "--preset", "coverage"]).returncode != 0:
        raise SystemExit("build failed; badge not written")
    # A failing suite means the number is measured against code that does not
    # work, so refuse rather than stamp it onto the badge.
    if run(["ctest", "--preset", "coverage", *ctest_args]).returncode != 0:
        raise SystemExit("the test suite failed; badge not written")


def export_report(path: Path) -> None:
    """Merge the raw profiles and write an llvm-cov summary export to `path`."""
    profraws = sorted(PROFRAW_DIR.glob("*.profraw"))
    if not profraws:
        raise SystemExit(
            f"no profiles under {PROFRAW_DIR.relative_to(ROOT)}; the test preset sets "
            "LLVM_PROFILE_FILE, so running the suite any other way leaves nothing to "
            "measure"
        )

    merge = run(
        [llvm_tool("llvm-profdata"), "merge", "-sparse", "-o", str(PROFDATA),
         *(str(p) for p in profraws)],
        capture=True,
    )
    if merge.returncode != 0:
        raise SystemExit(
            f"llvm-profdata merge failed:\n{merge.stderr}\n"
            "A version mismatch between llvm-profdata and the clang that built the "
            "tree reads as a corrupt profile; both come from the dev shell."
        )

    missing = [b for b in BINARIES if not (BUILD / b).exists()]
    if missing:
        raise SystemExit(
            f"not built under {BUILD.relative_to(ROOT)}: {', '.join(missing)}"
        )
    # The first binary is positional and the rest take `-object`, which is
    # llvm-cov's own shape: give the first one `-object` too and the first
    # source directory is read as the binary, which fails as "is a directory".
    first, *rest = (str(BUILD / b) for b in BINARIES)
    objects = [arg for obj in rest for arg in ("-object", obj)]

    export = run(
        [llvm_tool("llvm-cov"), "export", "--summary-only",
         f"-instr-profile={PROFDATA}", first, *objects,
         *(str(ROOT / s) for s in SOURCES)],
        capture=True,
    )
    if export.returncode != 0:
        raise SystemExit(f"llvm-cov export failed:\n{export.stderr}")
    path.write_text(export.stdout)


def committed_percent(svg: str) -> int | None:
    """The whole number the committed badge prints, or None if it prints none."""
    match = re.search(r'aria-label="coverage: (\d+)%"', svg)
    return int(match.group(1)) if match else None


def read_report(path: Path) -> float:
    # CI hands `--json` the report the test step wrote, and runs this step even when
    # that step failed, so that a build slip does not hide a broken test. A run that
    # died before the export leaves no report at all, and the bare traceback from
    # that reads as a fault in the badge rather than in the run that produced it.
    if not path.exists():
        raise SystemExit(f"{path} does not exist; the run that writes it did not finish")
    totals = json.loads(path.read_text())["data"][0]["totals"]
    percent: float = totals["lines"]["percent"]
    return percent


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--check",
        action="store_true",
        help="exit non-zero if the committed badge does not match a fresh measurement",
    )
    ap.add_argument(
        "--json",
        type=Path,
        help="read an existing llvm-cov export instead of building and running the suite",
    )
    ap.add_argument(
        "--no-run",
        action="store_true",
        help="export from the profiles already under build-coverage/profraw",
    )
    ap.add_argument(
        "ctest_args",
        nargs="*",
        help="extra arguments forwarded to ctest, e.g. -R tlsf",
    )
    args = ap.parse_args()

    if args.json:
        report = args.json
    else:
        if not args.no_run:
            build_and_test(args.ctest_args)
        report = BUILD / "coverage.json"
        export_report(report)

    percent = read_report(report)
    svg = render(percent)

    if args.check:
        current = BADGE.read_text() if BADGE.exists() else ""
        if current == svg:
            print(f"{BADGE.relative_to(ROOT)} is current at {percent:.1f}%")
            return
        committed = committed_percent(current)
        if committed is not None and abs(percent - committed) <= 0.5 + CHECK_SLACK:
            print(
                f"{BADGE.relative_to(ROOT)} reads {committed}% against a fresh "
                f"{percent:.1f}%, within the {CHECK_SLACK:.2f}-point slack."
            )
            return
        raise SystemExit(
            f"{BADGE.relative_to(ROOT)} is stale: coverage is {percent:.1f}%. "
            "Run `python scripts/coverage_badge.py` and commit the result."
        )

    BADGE.parent.mkdir(parents=True, exist_ok=True)
    BADGE.write_text(svg)
    print(f"coverage {percent:.1f}% -> {BADGE.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
