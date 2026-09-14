#!/usr/bin/env python3
"""Independently check whether emitted TLSF specifications are well-separated.

This deliberately does not go through `peredur`. The in-run filter's verdict is
untrustworthy for four separate reasons (issues #72, #73, #74, #77): elites are
appended after every filter stage and never re-screened, the fallback stage
re-admits the whole unfiltered offspring set when the chain empties the
population, a realizability timeout is mapped to "unrealizable" and *cached* —
which for this query reads as "well-separated" — and the seed population is
never filtered at all. So this script re-derives the query from the TLSF text
and calls `ltlsynt` directly, with its own generous timeout and no cache.

The query (mirroring `tlsf_make_well_separation_filter`, src/tlsf/filter.cpp):

    assumption_ltl := conjunction of the non-empty terms
                      conj(INITIALLY), G(conj(REQUIRE)), conj(ASSUME)
    ask ltlsynt --realizability for   (assumption_ltl) -> (false)
    REALIZABLE   => the system can force its own assumptions to fail
                 => NOT well-separated
    UNREALIZABLE => well-separated

A timeout or an unparseable answer is `undecided`, never `well-separated`.
That asymmetry is the point: it is exactly the collapse the in-run checker makes
(src/runner/spot.cpp:326-328) that this script exists to avoid.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LTLSYNT = REPO_ROOT / "build-release" / "third_party" / "spot" / "bin" / "ltlsynt"

# Section aliases accepted by src/tlsf/parser.cpp (lines 458-475).
SECTION_ALIASES = {
    "INITIALLY": "initially",
    "PRESET": "preset",
    "REQUIRE": "require",
    "REQUIREMENTS": "require",
    "ASSUME": "assume",
    "ASSUMPTIONS": "assume",
    "ASSERT": "assert",
    "INVARIANTS": "assert",
    "GUARANTEE": "guarantee",
    "GUARANTEES": "guarantee",
}

# Selection schemes gen_configs.py can name in a run directory (SCHEMES, line
# 35). Needed because both the level name and the spec name may contain
# hyphens but never underscores, so the scheme token is the only reliable
# anchor for splitting a run_id.
#
# "nsga2" and "nsga2-replicate" are the pre-rename spellings of
# "nsga2-truncate" and "nsga2-apportion". gen_configs.py emits the new names
# only, but every run directory archived under experiments/ is named with the
# old ones, and those are the directories this script is usually pointed at --
# so dropping them here would lose the anchor on exactly the layout it reads
# most. Membership is tested per token, so the order is presentational.
SCHEMES = ("nsga2-truncate", "nsga2-apportion", "nsga2-replicate", "nsga2",
           "weighted")

# Crossed-factor tags run_experiments.py splices between the scheme and the
# spec name (wk_tag/mx_tag/rp_tag, line 1756-1758). Only present on profiles
# that cross the factor; skipped so the spec name is recovered either way.
FACTOR_TAGS = ("wkon", "wkoff", "direct", "log", "monolithic", "muc")

RUN_ID_RE = re.compile(r"^sweep_(?P<sweep>[^_]+)_(?P<rest>.+)_seed(?P<seed>\d+)$")

VERDICT_WELL_SEPARATED = "well-separated"
VERDICT_NOT_WELL_SEPARATED = "not-well-separated"
VERDICT_UNDECIDED = "undecided"


# --------------------------------------------------------------------------
# TLSF parsing
# --------------------------------------------------------------------------


def strip_comments(text: str) -> str:
    """Removes // and /* */ comments, leaving quoted strings intact."""
    out: list[str] = []
    i = 0
    n = len(text)
    in_string = False
    while i < n:
        char = text[i]
        if in_string:
            if char == "\\" and i + 1 < n:
                out.append(text[i : i + 2])
                i += 2
                continue
            if char == '"':
                in_string = False
            out.append(char)
            i += 1
            continue
        if char == '"':
            in_string = True
            out.append(char)
            i += 1
            continue
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
            continue
        out.append(char)
        i += 1
    return "".join(out)


def find_block(text: str, keyword: str) -> str | None:
    """Returns the body of `keyword { ... }`, matched by brace depth."""
    match = re.search(rf"\b{keyword}\b\s*{{", text)
    if match is None:
        return None
    start = match.end()
    depth = 1
    i = start
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i]
        i += 1
    raise ValueError(f"unterminated {keyword} block")


def split_statements(body: str) -> list[str]:
    """Splits a section body on `;`. TLSF statements are `;`-terminated and no
    formula operator contains one."""
    return [stmt.strip() for stmt in body.split(";") if stmt.strip()]


@dataclass
class TlsfSpec:
    inputs: list[str]
    outputs: list[str]
    sections: dict[str, list[str]]

    def assumption_ltl(self) -> str:
        """Mirrors tlsf::Specification::assumption_ltl(): collect_side over
        INITIALLY (verbatim), REQUIRE (G-wrapped), ASSUME (verbatim), with an
        absent section contributing no term and an empty result being `true`."""

        def conj(formulae: list[str]) -> str:
            return " & ".join(f"({f})" for f in formulae)

        terms: list[str] = []
        if self.sections.get("initially"):
            terms.append(conj(self.sections["initially"]))
        if self.sections.get("require"):
            terms.append(f"G({conj(self.sections['require'])})")
        if self.sections.get("assume"):
            terms.append(conj(self.sections["assume"]))
        if not terms:
            return "true"
        return " & ".join(f"({t})" for t in terms)

    def assumptions_reference_output(self) -> bool:
        """Mirrors assumptions_reference_output (src/tlsf/filter.cpp:62-82).
        Only informational here — the shortcut is not applied unless
        --fast-path is passed."""
        if not self.outputs:
            return False
        outputs = set(self.outputs)
        text = " ".join(
            f
            for key in ("initially", "require", "assume")
            for f in self.sections.get(key, [])
        )
        return bool(outputs & set(re.findall(r"[A-Za-z_][A-Za-z_0-9]*", text)))


def parse_tlsf(path: Path) -> TlsfSpec:
    text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    main = find_block(text, "MAIN")
    if main is None:
        raise ValueError("no MAIN block")
    inputs_body = find_block(main, "INPUTS")
    outputs_body = find_block(main, "OUTPUTS")
    sections: dict[str, list[str]] = {}
    for keyword, canonical in SECTION_ALIASES.items():
        body = find_block(main, keyword)
        if body is not None:
            sections.setdefault(canonical, []).extend(split_statements(body))
    return TlsfSpec(
        inputs=split_statements(inputs_body) if inputs_body else [],
        outputs=split_statements(outputs_body) if outputs_body else [],
        sections=sections,
    )


# --------------------------------------------------------------------------
# Run identity
# --------------------------------------------------------------------------


@dataclass
class RunId:
    sweep: str = ""
    arm: str = ""
    scheme: str = ""
    spec: str = ""
    seed: str = ""


def parse_run_id(name: str) -> RunId | None:
    """Parses a run directory name such as
    `sweep_V_final-only_nsga2_arbiter_seed03` — the layout run_experiments.py
    builds at line 1759."""
    match = RUN_ID_RE.match(name)
    if match is None:
        return None
    parts = match.group("rest").split("_")
    scheme_at = next(
        (i for i, part in enumerate(parts) if part in SCHEMES), None
    )
    if scheme_at is None:
        # No crossed-factor tags to disambiguate: assume <arm>_<scheme>_<spec>.
        if len(parts) < 3:
            return RunId(sweep=match.group("sweep"), seed=match.group("seed"))
        scheme_at = 1
    spec_at = scheme_at + 1
    while spec_at < len(parts) - 1 and parts[spec_at] in FACTOR_TAGS:
        spec_at += 1
    return RunId(
        sweep=match.group("sweep"),
        arm="_".join(parts[:scheme_at]),
        scheme=parts[scheme_at],
        spec="_".join(parts[spec_at:]),
        seed=match.group("seed"),
    )


def identity_for(path: Path) -> RunId:
    for parent in path.resolve().parents:
        run_id = parse_run_id(parent.name)
        if run_id is not None:
            return run_id
    # An `examples/<spec>/spec.tlsf`-shaped path carries a spec name but no run.
    parents = path.resolve().parents
    if len(parents) >= 2 and parents[1].name == "examples":
        return RunId(spec=parents[0].name)
    if len(parents) >= 1 and parents[0].name == "fixes" and len(parents) >= 3:
        return RunId(spec=parents[1].name)
    return RunId()


# --------------------------------------------------------------------------
# The check
# --------------------------------------------------------------------------


@dataclass
class Result:
    path: Path
    run: RunId
    verdict: str
    elapsed_s: float
    refs_output: str
    detail: str


def check_one(
    path: Path, ltlsynt: Path, timeout_s: float, fast_path: bool
) -> Result:
    run = identity_for(path)
    start = time.monotonic()
    try:
        spec = parse_tlsf(path)
    except (OSError, ValueError) as exc:
        return Result(path, run, VERDICT_UNDECIDED, 0.0, "", f"parse error: {exc}")

    refs = spec.assumptions_reference_output()
    if fast_path and not refs:
        # Input-only assumptions cannot be violated by the system, so the
        # C++ filter short-circuits to "keep". Sound except when the
        # assumptions are themselves unsatisfiable, which the vacuity filter
        # owns; off by default so the ltlsynt answer is the only authority.
        return Result(
            path, run, VERDICT_WELL_SEPARATED, 0.0, str(refs).lower(),
            "fast-path: assumptions reference no output",
        )

    formula = f"({spec.assumption_ltl()}) -> (false)"
    command = [str(ltlsynt), "--realizability", "-f", formula]
    if spec.inputs:
        command.append("--ins=" + ",".join(spec.inputs))
    if spec.outputs:
        command.append("--outs=" + ",".join(spec.outputs))

    try:
        proc = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=timeout_s,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return Result(
            path, run, VERDICT_UNDECIDED, time.monotonic() - start,
            str(refs).lower(), f"ltlsynt timeout after {timeout_s}s",
        )
    except OSError as exc:
        return Result(
            path, run, VERDICT_UNDECIDED, time.monotonic() - start,
            str(refs).lower(), f"ltlsynt exec failed: {exc}",
        )

    elapsed = time.monotonic() - start
    out = proc.stdout + proc.stderr
    if "UNREALIZABLE" in out:
        return Result(path, run, VERDICT_WELL_SEPARATED, elapsed, str(refs).lower(), "")
    if "REALIZABLE" in out:
        return Result(
            path, run, VERDICT_NOT_WELL_SEPARATED, elapsed, str(refs).lower(), ""
        )
    return Result(
        path, run, VERDICT_UNDECIDED, elapsed, str(refs).lower(),
        f"unrecognised ltlsynt output (rc={proc.returncode}): "
        f"{out.strip()[:200].replace(chr(10), ' ')}",
    )


def collect_paths(targets: list[str], pattern: str) -> list[Path]:
    paths: list[Path] = []
    for target in targets:
        path = Path(target)
        if path.is_dir():
            paths.extend(sorted(path.rglob(pattern)))
        elif path.exists():
            paths.append(path)
        else:
            print(f"warning: no such path: {target}", file=sys.stderr)
    return paths


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check whether emitted TLSF specifications are "
                    "well-separated, independently of the run that made them.",
    )
    parser.add_argument(
        "targets", nargs="+", metavar="PATH",
        help="TLSF files, or directories walked for --pattern",
    )
    parser.add_argument(
        "--pattern", default="repair_*.tlsf",
        help="glob applied when a target is a directory (default: %(default)s)",
    )
    parser.add_argument(
        "--ltlsynt", type=Path, default=DEFAULT_LTLSYNT,
        help="ltlsynt binary (default: %(default)s)",
    )
    parser.add_argument(
        "--timeout", type=float, default=60.0,
        help="per-file ltlsynt timeout in seconds; a timeout is reported as "
             "undecided, never as well-separated (default: %(default)s)",
    )
    parser.add_argument(
        "--jobs", type=int, default=4,
        help="concurrent ltlsynt calls (default: %(default)s)",
    )
    parser.add_argument(
        "--fast-path", action="store_true",
        help="short-circuit specs whose assumptions reference no output to "
             "well-separated, as the C++ filter does; off by default so every "
             "verdict comes from ltlsynt",
    )
    parser.add_argument(
        "--out", type=Path, default=None,
        help="write the CSV here instead of stdout",
    )
    parser.add_argument(
        "--summary", action="store_true",
        help="print a per-arm verdict tally to stderr when done",
    )
    args = parser.parse_args()

    ltlsynt = args.ltlsynt
    if not ltlsynt.exists():
        found = shutil.which(ltlsynt.name)
        if found is None:
            print(f"error: ltlsynt not found at {ltlsynt}", file=sys.stderr)
            return 2
        ltlsynt = Path(found)

    paths = collect_paths(args.targets, args.pattern)
    if not paths:
        print("error: no input files", file=sys.stderr)
        return 2

    results: list[Result] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(check_one, p, ltlsynt, args.timeout, args.fast_path): p
            for p in paths
        }
        for future in concurrent.futures.as_completed(futures):
            results.append(future.result())
    results.sort(key=lambda r: str(r.path))

    stream = open(args.out, "w", newline="", encoding="utf-8") if args.out else sys.stdout
    try:
        writer = csv.writer(stream)
        writer.writerow(
            ["path", "sweep", "arm", "scheme", "spec", "seed",
             "refs_output", "verdict", "elapsed_s", "detail"]
        )
        for r in results:
            writer.writerow([
                str(r.path), r.run.sweep, r.run.arm, r.run.scheme,
                r.run.spec, r.run.seed, r.refs_output, r.verdict,
                f"{r.elapsed_s:.3f}", r.detail,
            ])
    finally:
        if args.out:
            stream.close()

    if args.summary:
        tally: dict[tuple[str, str], int] = {}
        for r in results:
            tally[(r.run.arm or "-", r.verdict)] = (
                tally.get((r.run.arm or "-", r.verdict), 0) + 1
            )
        total_time = sum(r.elapsed_s for r in results)
        print(f"\n{len(results)} file(s), {total_time:.1f}s of ltlsynt, "
              f"mean {total_time / len(results):.3f}s/file", file=sys.stderr)
        for (arm, verdict), count in sorted(tally.items()):
            print(f"  {arm:<16} {verdict:<20} {count}", file=sys.stderr)

    return 1 if any(r.verdict == VERDICT_UNDECIDED for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
