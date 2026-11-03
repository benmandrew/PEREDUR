#!/usr/bin/env python3
"""Measure the FRETISH field orders from the engine's lowering and Spot.

Nothing here is derived by hand. The `ltl` driver lowers every requirement as
the engine does, and `ltlfilt --satisfiable` decides each implication
`lhs -> rhs` as the unsatisfiability of `lhs & !rhs`, as test/ltl_oracle.hpp
does. The script measures:

1. the timing order under each scope and condition type, the classes of scopes
   that share one order, and the covering pairs of each class;
2. the scope order under each timing and condition type;
3. the condition-type order under each scope and timing;
4. the polarity of each propositional field (condition, response, stop) in
   every cell of the three keyword fields.

It then checks the claims of the timing-order note and the polarities that
`requirement_implies` assumes (RuleSearch in src/filter/implication_check.cpp).

Last, it checks `requirement_implies` itself. The `implication_oracle` test
helper prints every ordered pair the rule proves over a set of requirements
that varies every field at once, with two modes and three stops, and SPOT
decides each one. A pair that moves several fields tests the chain search as
well as each step: a step checked in the wrong context passes every one-field
cell and fails here.

black asks every implication again, as a second solver (cross_check gives the
rule for a pair it leaves undecided); --skip-black turns it off. The script
exits 1 when any check fails.

A polarity needs one pair of queries per cell. Valid LTL formulae are closed
under uniform substitution, so if F(p & q) -> F(p) is valid for fresh atoms
p and q, then F(a) -> F(b) is valid whenever a -> b: substitute b for p and a
for q, and note that b & a is equivalent to a.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import dataclasses
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import defaultdict
from collections.abc import Sequence
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

SCOPES = (
    "Global",
    "In",
    "NotIn",
    "Before",
    "After",
    "OnlyIn",
    "OnlyBefore",
    "OnlyAfter",
)
ONLY_SCOPES = ("OnlyIn", "OnlyBefore", "OnlyAfter")
CTYPES = ("continual", "trigger")
MODE = "m"

# Base atoms, and the stronger value each propositional field takes when its
# polarity is measured.
CONDITION, CONDITION_STRONG = "c", "c & cx"
RESPONSE, RESPONSE_STRONG = "r", "r & rx"
STOP, STOP_STRONG = "s", "s & sx"

# The orders the note states. A scope with no boundary shares the Global
# order; the only scopes split by whether their dual has a boundary.
EXPECTED_CLASSES = (
    ("Global", "After"),
    ("In", "NotIn", "Before"),
    ("OnlyIn", "OnlyAfter"),
    ("OnlyBefore",),
)
DUAL_BASE = {"OnlyIn": "In", "OnlyAfter": "In", "OnlyBefore": "Global"}


def timings(max_ticks: int) -> list[tuple]:
    out: list[tuple] = [("Immediately",), ("NextTimepoint",)]
    for kind in ("WithinTicks", "ForTicks", "AfterTicks"):
        out += [(kind, n) for n in range(1, max_ticks + 1)]
    out += [("Eventually",), ("Always",), ("Until", STOP), ("Before", STOP)]
    return out


def timing_name(tim: tuple) -> str:
    names = {
        "Immediately": "immediately",
        "NextTimepoint": "next",
        "WithinTicks": "within",
        "ForTicks": "for",
        "AfterTicks": "after",
        "Eventually": "eventually",
        "Always": "always",
        "Until": "until",
        "Before": "before",
    }
    return names[tim[0]] + "".join(f" {arg}" for arg in tim[1:])


def timing_json(tim: tuple) -> dict:
    if tim[0] in ("WithinTicks", "ForTicks", "AfterTicks"):
        return {"type": tim[0], "ticks": tim[1]}
    if tim[0] in ("Until", "Before"):
        return {"type": tim[0], "stop": tim[1]}
    return {"type": tim[0]}


def dual(tim: tuple) -> tuple:
    swap = {
        "WithinTicks": "ForTicks",
        "ForTicks": "WithinTicks",
        "Eventually": "Always",
        "Always": "Eventually",
        "Until": "Before",
        "Before": "Until",
    }
    return (swap.get(tim[0], tim[0]),) + tim[1:]


class Lowering:
    """Lowers requirements with the `ltl` driver, one spec for all of them.

    A requirement is (scope, condition type, timing, condition, response,
    mode), and its index is its position among the spec's guarantees.
    """

    def __init__(
        self, ltl: Path, in_atoms: list[str], out_atoms: list[str], modes: list[str]
    ) -> None:
        self.ltl = ltl
        self.atoms = (in_atoms, out_atoms, modes)
        self.index: dict[tuple, int] = {}

    def add(self, req: tuple) -> tuple:
        self.index.setdefault(req, len(self.index))
        return req

    def write(self, path: Path) -> None:
        guarantees = []
        for scope, ctype, tim, cond, resp, mode in self.index:
            entry = {
                "condition": cond,
                "condition-type": ctype,
                "response": resp,
                "timing": timing_json(tim),
            }
            if scope != "Global":
                entry["scope"] = {"type": scope, "mode": mode}
            guarantees.append(entry)
        in_atoms, out_atoms, modes = self.atoms
        spec = {
            "assumptions": [],
            "guarantees": guarantees,
            "in_atoms": in_atoms,
            "out_atoms": out_atoms,
            "modes": modes,
        }
        path.write_text(json.dumps(spec))

    def lower(self, path: Path) -> dict[tuple, str]:
        self.write(path)
        out = subprocess.run(
            [str(self.ltl), str(path)],
            capture_output=True,
            text=True,
            check=True,
            timeout=600,
        ).stdout
        formulae = [
            line.strip()[len("LTL:") :].strip()
            for line in out.splitlines()
            if line.strip().startswith("LTL:")
        ]
        if len(formulae) != len(self.index):
            sys.exit(f"ltl printed {len(formulae)} formulae for {len(self.index)}")
        return {req: formulae[i] for req, i in self.index.items()}


def decide(ltlfilt: Path, pairs: list[tuple[str, str]], jobs: int) -> list[bool]:
    """Whether each `lhs -> rhs` holds, in parallel batches of one ltlfilt run."""

    def batch(chunk: list[tuple[str, str]]) -> list[bool]:
        text = "".join(f"({lhs}) & !({rhs})\n" for lhs, rhs in chunk)
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
        holds = [True] * len(chunk)
        for line in run.stdout.split():
            holds[int(line) - 1] = False
        return holds

    size = max(1, -(-len(pairs) // (jobs * 4)))
    chunks = [pairs[i : i + size] for i in range(0, len(pairs), size)]
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        return [h for part in pool.map(batch, chunks) for h in part]


def is_identifier_char(char: str) -> bool:
    return char.isascii() and (char.isalnum() or char == "_")


def has_weak_operator(formula: str) -> bool:
    """A W or M standing alone, as has_weak_operator in src/runner/ltlfilt.cpp."""
    return any(
        char in "WM"
        and not (pos > 0 and is_identifier_char(formula[pos - 1]))
        and not (pos + 1 < len(formula) and is_identifier_char(formula[pos + 1]))
        for pos, char in enumerate(formula)
    )


BLACK_CONSTANTS = re.compile(r"(?<![A-Za-z0-9_])(true|false|1|0)(?![A-Za-z0-9_])")


def to_black_constants(formula: str) -> str:
    """Respell SPOT's constants as black's, as to_black_constants in black.cpp.

    black reads a bare `true` or `false` as a free variable and rejects `1`
    and `0` outright; it reads `True` and `False` as the constants.
    """
    return BLACK_CONSTANTS.sub(
        lambda m: "True" if m.group(1) in ("true", "1") else "False", formula
    )


@dataclasses.dataclass
class BlackVerdict:
    """black's answer to one implication.

    `holds` is None when black gave no answer, and `reason` then says why:
    "bound" when it searched every lasso up to the bound and found no
    counter-model, "timeout" when its time ran out first, else an error.
    """

    holds: bool | None
    reason: str = ""


def prepare_for_black(ltlfilt: Path, queries: list[str], jobs: int) -> list[str]:
    """Each query as `ltlfilt --remove-wm -p` prints it, in parallel batches.

    The full parenthesisation is for precedence: black reads `a -> b & c` as
    `(a -> b) & c` where SPOT reads `a -> (b & c)`, and the lowering prints
    `m -> G(...) & (...)` in every only-scope formula. The W and M rewrite is
    black_answer's in src/runner/black.cpp: black's encoding accepts a lasso
    that never fulfils an M, so a negated W reads satisfiable. SPOT may fold a
    query to `0` or `1`, which decide_black answers itself.
    """

    def batch(chunk: list[str]) -> list[str]:
        run = subprocess.run(
            [str(ltlfilt), "--remove-wm", "-p", "-F", "-"],
            input="".join(f"{query}\n" for query in chunk),
            capture_output=True,
            text=True,
            check=False,
            timeout=1800,
        )
        lines = run.stdout.splitlines()
        if run.returncode != 0 or run.stderr.strip() or len(lines) != len(chunk):
            sys.exit(
                f"ltlfilt --remove-wm failed (exit {run.returncode}): {run.stderr}"
            )
        return lines

    size = max(1, -(-len(queries) // (jobs * 4)))
    chunks = [queries[i : i + size] for i in range(0, len(queries), size)]
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        return [line for part in pool.map(batch, chunks) for line in part]


def decide_black(
    black: Path,
    ltlfilt: Path,
    pairs: list[tuple[str, str]],
    jobs: int,
    timeout: int,
    bound: int,
) -> list[BlackVerdict]:
    """Whether each `lhs -> rhs` holds by black, one solve per distinct query.

    Each query goes to black as SPOT reprints it (prepare_for_black), then
    with its constants respelled as black_answer in src/runner/black.cpp does.

    black finds a counter-model in milliseconds but proves almost none of
    these implications: at a bound of 30 it still leaves most of them
    undecided, at seconds each. So it runs to a fixed bound, which is a
    search for every counter-model of up to that many states, and is as
    deterministic as the solver.
    """

    def solve(query: str) -> BlackVerdict:
        if query in ("0", "1"):
            return BlackVerdict(query == "0")
        if not query or has_weak_operator(query):
            return BlackVerdict(None, f"--remove-wm left {query!r}")
        # -t is never omitted; the outer bound only catches a black that
        # ignores its own.
        try:
            run = subprocess.run(
                [str(black), "solve", "-k", str(bound), "-t", str(timeout), "-"],
                input=to_black_constants(query),
                capture_output=True,
                text=True,
                check=False,
                timeout=timeout + 30,
            )
        except subprocess.TimeoutExpired:
            return BlackVerdict(None, "timeout")
        # UNSAT before SAT: the former contains the latter.
        if run.returncode == 0 and "UNSAT" in run.stdout:
            return BlackVerdict(True)
        if run.returncode == 0 and "SAT" in run.stdout and "UNKNOWN" not in run.stdout:
            return BlackVerdict(False)
        # "UNKNOWN (stopped at k = N)", from the bound or from -t.
        stopped = re.search(r"UNKNOWN \(stopped at k = (\d+)\)", run.stdout)
        if stopped:
            return BlackVerdict(
                None, "bound" if int(stopped.group(1)) >= bound else "timeout"
            )
        detail = (run.stderr.strip() or run.stdout.strip()).splitlines()
        return BlackVerdict(
            None, f"exit {run.returncode}: {detail[0] if detail else ''}"
        )

    queries = [f"({lhs}) & !({rhs})" for lhs, rhs in pairs]
    distinct = list(dict.fromkeys(queries))
    prepared = prepare_for_black(ltlfilt, distinct, jobs)
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        verdicts = dict(zip(distinct, pool.map(solve, prepared)))
    return [verdicts[query] for query in queries]


def cross_check(
    report: Report,
    pairs: list[tuple[str, str]],
    holds: list[bool],
    verdicts: list[BlackVerdict] | None,
    bound: int,
) -> None:
    """Fail on every pair where black and ltlfilt answer differently.

    An unanswered pair is not agreement. It fails the run, with one exception:
    an implication ltlfilt proves, where black searched to the bound and found
    no counter-model. black proves too few of these to demand a proof, and the
    bounded search is still the check that matters, since a wrong UNSAT has a
    counter-model. An implication ltlfilt refutes needs black's model, since
    SPOT 2.16.1's `--satisfiable` calls some unsatisfiable formulae
    satisfiable.
    """
    if verdicts is None:
        return
    disagree = [
        (pair, spot)
        for pair, spot, v in zip(pairs, holds, verdicts)
        if v.holds is not None and v.holds != spot
    ]
    bounded = sum(spot and v.reason == "bound" for spot, v in zip(holds, verdicts))
    undecided = [
        (pair, spot, v)
        for pair, spot, v in zip(pairs, holds, verdicts)
        if v.holds is None and not (spot and v.reason == "bound")
    ]
    proved = sum(v.holds is True for v in verdicts)
    refuted = sum(v.holds is False for v in verdicts)
    print(
        f"  black: {proved} proved, {refuted} refuted, {bounded} with no "
        f"counter-model up to k = {bound}, {len(undecided)} undecided, "
        f"of {len(pairs)} queries"
    )

    def verdict(holds: bool) -> str:
        return "holds" if holds else "fails"

    report.check(
        not disagree,
        "black agrees with ltlfilt on every implication",
        f"{len(disagree)} disagreement(s):"
        + "".join(
            f"\n         ltlfilt: {verdict(spot)}, black: {verdict(not spot)}"
            f"\n         lhs: {lhs}\n         rhs: {rhs}"
            for (lhs, rhs), spot in disagree[:5]
        ),
    )
    report.check(
        not undecided,
        "black answers every implication it is asked for",
        f"{len(undecided)} undecided:"
        + "".join(
            f"\n         {v.reason}, ltlfilt: {verdict(spot)}"
            f"\n         lhs: {lhs}\n         rhs: {rhs}"
            for (lhs, rhs), spot, v in undecided[:3]
        ),
    )


def covers(elements: Sequence, order: set[tuple]) -> list[tuple]:
    return sorted(
        (a, b)
        for a, b in order
        if not any((a, c) in order and (c, b) in order for c in elements)
    )


def default_spot_bin_dir() -> Path:
    """$PEREDUR_SPOT_BIN_DIR, else the Spot the debug build installed."""
    if "PEREDUR_SPOT_BIN_DIR" in os.environ:
        return Path(os.environ["PEREDUR_SPOT_BIN_DIR"])
    cache = REPO / "build" / "CMakeCache.txt"
    for line in cache.read_text().splitlines() if cache.exists() else []:
        if line.startswith("PEREDUR_THIRD_PARTY_DIR:PATH="):
            return Path(line.split("=", 1)[1]) / "spot" / "bin"
    return REPO / "build" / "third_party" / "spot" / "bin"


def default_black() -> Path:
    """$PEREDUR_BLACK_PATH, else the black the debug build configured."""
    if "PEREDUR_BLACK_PATH" in os.environ:
        return Path(os.environ["PEREDUR_BLACK_PATH"])
    cache = REPO / "build" / "CMakeCache.txt"
    for line in cache.read_text().splitlines() if cache.exists() else []:
        if line.startswith("BLACK_EXECUTABLE:FILEPATH="):
            return Path(line.split("=", 1)[1])
    return Path("black")


def requirement_name(req: tuple) -> str:
    scope, ctype, tim, cond, resp, mode = req
    where = "" if scope == "Global" else f"{scope} {mode}, "
    return f"{where}{ctype} ({cond}), {timing_name(tim)} ({resp})"


def check_requirement_implies(
    args: argparse.Namespace, ltlfilt: Path, report: Report
) -> None:
    print("\nrequirement_implies")
    tims: list[tuple] = [
        ("Immediately",),
        ("NextTimepoint",),
        ("WithinTicks", 1),
        ("WithinTicks", 2),
        ("ForTicks", 1),
        ("ForTicks", 2),
        ("AfterTicks", 1),
        ("Eventually",),
        ("Always",),
    ]
    # A comparable and an incomparable stop, within one kind and across them.
    for stop in ("s", "s & t", "t"):
        tims += [("Until", stop), ("Before", stop)]
    scopes = [("Global", MODE)] + [
        (scope, mode) for scope in SCOPES if scope != "Global" for mode in ("m", "n")
    ]
    lowering = Lowering(args.ltl, ["a", "b", "s", "t"], ["x", "y"], ["m", "n"])
    # One stronger and one weaker value per propositional field: by uniform
    # substitution the pair stands for any a -> b.
    for cond in ("a", "a & b"):
        for resp in ("x", "x & y"):
            for tim in tims:
                for ctype in CTYPES:
                    for scope, mode in scopes:
                        lowering.add((scope, ctype, tim, cond, resp, mode))
    reqs = list(lowering.index)
    with tempfile.TemporaryDirectory() as tmp:
        spec = Path(tmp) / "spec.json"
        formulae = lowering.lower(spec)

        def shard(k: int) -> str:
            return subprocess.run(
                [str(args.oracle), str(spec), str(k), str(args.jobs)],
                capture_output=True,
                text=True,
                check=True,
                timeout=7200,
            ).stdout

        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            out = "".join(pool.map(shard, range(args.jobs)))
    claimed = [tuple(map(int, line.split())) for line in out.splitlines()]
    # Distinct requirements can share a lowering; SPOT is asked each distinct
    # pair of lowerings once.
    asked: dict[tuple[str, str], tuple[int, int]] = {}
    for i, j in claimed:
        lhs, rhs = formulae[reqs[i]], formulae[reqs[j]]
        if lhs != rhs:
            asked.setdefault((lhs, rhs), (i, j))
    pairs = list(asked)
    holds = decide(ltlfilt, pairs, args.jobs)
    verdicts = black_verdicts(args, ltlfilt, pairs)
    false = [asked[pair] for pair, ok in zip(pairs, holds) if not ok]
    print(
        f"  {len(reqs)} requirements, {len(reqs) * (len(reqs) - 1)} ordered pairs, "
        f"{len(claimed)} proved, {len(pairs)} distinct lowerings asked"
    )
    report.check(
        not false and bool(claimed),
        "every pair requirement_implies proves holds",
        "; ".join(
            f"{requirement_name(reqs[i])} => {requirement_name(reqs[j])}"
            for i, j in false[:20]
        ),
    )
    cross_check(report, pairs, holds, verdicts, args.black_bound)


def black_verdicts(
    args: argparse.Namespace, ltlfilt: Path, pairs: list[tuple[str, str]]
) -> list[BlackVerdict] | None:
    """black's verdict on each pair, or None under --skip-black."""
    if args.skip_black:
        return None
    return decide_black(
        args.black, ltlfilt, pairs, args.jobs, args.black_timeout, args.black_bound
    )


def default_oracle() -> Path:
    """The release build's implication_oracle, else the debug build's."""
    for build in ("build-release", "build"):
        path = REPO / build / "test" / "implication_oracle"
        if path.exists():
            return path
    return REPO / "build" / "test" / "implication_oracle"


class Report:
    def __init__(self) -> None:
        self.failures: list[str] = []

    def check(self, ok: bool, claim: str, detail: str = "") -> None:
        print(f"  [{'ok' if ok else 'FAIL'}] {claim}")
        if not ok:
            self.failures.append(claim)
            if detail:
                print(f"         {detail}")


def main() -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").split("\n")[0])
    parser.add_argument("--ltl", type=Path, default=REPO / "build" / "ltl")
    parser.add_argument("--spot-bin-dir", type=Path, default=default_spot_bin_dir())
    parser.add_argument("--max-ticks", type=int, default=3)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--json", type=Path, help="write every measured relation here")
    parser.add_argument("--oracle", type=Path, default=default_oracle())
    parser.add_argument(
        "--skip-requirement-implies",
        action="store_true",
        help="measure the orders only, without the slow check of the rule",
    )
    parser.add_argument("--black", type=Path, default=default_black())
    parser.add_argument(
        "--skip-black",
        action="store_true",
        help="decide each implication with ltlfilt alone, not also with black",
    )
    parser.add_argument(
        "--black-timeout",
        type=int,
        default=10,
        help="seconds black may spend on one query (its -t)",
    )
    parser.add_argument(
        "--black-bound",
        type=int,
        default=10,
        help="the longest counter-model black searches for (its -k)",
    )
    args = parser.parse_args()
    ltlfilt = args.spot_bin_dir / "ltlfilt"
    version = subprocess.run(
        [str(ltlfilt), "--version"], capture_output=True, text=True, check=True
    ).stdout.splitlines()[0]
    tims = timings(args.max_ticks)

    lowering = Lowering(args.ltl, ["c", "cx", "s", "sx"], ["r", "rx"], [MODE])

    def req(scope, ctype, tim, cond=CONDITION, resp=RESPONSE) -> tuple:
        return lowering.add((scope, ctype, tim, cond, resp, MODE))

    # Every query, as (family, cell, lhs requirement, rhs requirement).
    queries: list[tuple[str, tuple, tuple, tuple]] = []
    for scope in SCOPES:
        for ctype in CTYPES:
            for t1 in tims:
                for t2 in tims:
                    if t1 != t2:
                        queries.append(
                            (
                                "timing",
                                (scope, ctype, t1, t2),
                                req(scope, ctype, t1),
                                req(scope, ctype, t2),
                            )
                        )
    for tim in tims:
        for ctype in CTYPES:
            for s1 in SCOPES:
                for s2 in SCOPES:
                    if s1 != s2:
                        queries.append(
                            (
                                "scope",
                                (tim, ctype, s1, s2),
                                req(s1, ctype, tim),
                                req(s2, ctype, tim),
                            )
                        )
    for scope in SCOPES:
        for tim in tims:
            for c1 in CTYPES:
                for c2 in CTYPES:
                    if c1 != c2:
                        queries.append(
                            (
                                "ctype",
                                (scope, tim, c1, c2),
                                req(scope, c1, tim),
                                req(scope, c2, tim),
                            )
                        )
    for scope in SCOPES:
        for ctype in CTYPES:
            for tim in tims:
                variants = {
                    "condition": (
                        req(scope, ctype, tim, cond=CONDITION_STRONG),
                        req(scope, ctype, tim),
                    ),
                    "response": (
                        req(scope, ctype, tim, resp=RESPONSE_STRONG),
                        req(scope, ctype, tim),
                    ),
                }
                if tim[0] in ("Until", "Before"):
                    variants["stop"] = (
                        req(scope, ctype, (tim[0], STOP_STRONG)),
                        req(scope, ctype, tim),
                    )
                for field, (strong, base) in variants.items():
                    cell = (scope, ctype, tim, field)
                    queries.append(("polarity+", cell, strong, base))
                    queries.append(("polarity-", cell, base, strong))

    with tempfile.TemporaryDirectory() as tmp:
        formulae = lowering.lower(Path(tmp) / "spec.json")
    print(f"{len(formulae)} requirements, {len(queries)} implications, {version}")
    pairs = [(formulae[lhs], formulae[rhs]) for _, _, lhs, rhs in queries]
    holds = decide(ltlfilt, pairs, args.jobs)
    verdicts = black_verdicts(args, ltlfilt, pairs)

    timing_rel: dict[tuple, set] = defaultdict(set)
    scope_rel: dict[tuple, set] = defaultdict(set)
    ctype_rel: dict[tuple, set] = defaultdict(set)
    polarity: dict[tuple, set] = defaultdict(set)
    for (family, cell, _, _), ok in zip(queries, holds):
        if family.startswith("polarity"):
            # A cell where neither direction holds still needs checking.
            polarity.setdefault(cell, set())
        if not ok:
            continue
        if family == "timing":
            timing_rel[cell[:2]].add(cell[2:])
        elif family == "scope":
            scope_rel[cell[:2]].add(cell[2:])
        elif family == "ctype":
            ctype_rel[cell[:2]].add(cell[2:])
        else:
            polarity[cell].add(family[-1])

    report = Report()

    if verdicts is not None:
        print("\nSecond solver")
        cross_check(report, pairs, holds, verdicts, args.black_bound)

    print("\nTiming order")
    report.check(
        all(timing_rel[(s, "continual")] == timing_rel[(s, "trigger")] for s in SCOPES),
        "the condition type moves no timing edge",
    )
    for scope in SCOPES:
        rel = timing_rel[(scope, "continual")]
        transitive = all(
            (a, c) in rel for a, b in rel for b2, c in rel if b == b2 and a != c
        )
        antisymmetric = not any((b, a) in rel for a, b in rel)
        report.check(transitive and antisymmetric, f"{scope}: a partial order")
    classes: dict[frozenset, list[str]] = defaultdict(list)
    for scope in SCOPES:
        classes[frozenset(timing_rel[(scope, "continual")])].append(scope)
    found = sorted(tuple(c) for c in classes.values())
    report.check(
        found == sorted(EXPECTED_CLASSES),
        f"{len(EXPECTED_CLASSES)} classes of scopes share an order",
        f"found {found}",
    )
    global_rel = timing_rel[("Global", "continual")]
    bounded_rel = timing_rel[("In", "continual")]
    lost = {
        (t, ("Eventually",))
        for t in tims
        if t[0] in ("NextTimepoint", "WithinTicks", "AfterTicks")
    }
    report.check(
        bounded_rel == global_rel - lost,
        "=>_B is =>_0 less (next | within n | after n, eventually)",
        f"extra {sorted(bounded_rel - global_rel)}, "
        f"missing {sorted((global_rel - lost) - bounded_rel)}",
    )
    for scope in ONLY_SCOPES:
        rel = timing_rel[(scope, "continual")]
        base = timing_rel[(DUAL_BASE[scope], "continual")]
        plain = [t for t in tims if t[0] != "AfterTicks"]
        bad = [
            (a, b)
            for a in plain
            for b in plain
            if a != b and ((a, b) in rel) != ((dual(a), dual(b)) in base)
        ]
        report.check(
            not bad,
            f"{scope}: t => t' iff dual(t) => dual(t') under {DUAL_BASE[scope]}",
            f"mismatches {bad}",
        )
        after_pairs = {(a, b) for a, b in rel if "AfterTicks" in (a[0], b[0])}
        expected_after = {
            (w, a)
            for a in tims
            if a[0] == "AfterTicks"
            for w in tims
            if w == ("Eventually",) or (w[0] == "WithinTicks" and w[1] > a[1])
        }
        report.check(
            after_pairs == expected_after,
            f"{scope}: after n is implied by eventually and within m > n only",
            f"extra {sorted(after_pairs - expected_after)}, "
            f"missing {sorted(expected_after - after_pairs)}",
        )
        reversed_ = sum((b, a) in rel for a, b in global_rel)
        kept = sum((a, b) in rel for a, b in global_rel)
        print(
            f"  {scope}: {reversed_} of the {len(global_rel)} strict pairs of "
            f"=>_0 reverse, {kept} keep their direction"
        )
    for members in EXPECTED_CLASSES:
        rel = timing_rel[(members[0], "continual")]
        print(f"  covers under {', '.join(members)}:")
        for a, b in covers(tims, rel):
            print(f"    {timing_name(a)} -> {timing_name(b)}")

    print("\nScope order")
    scope_classes: dict[frozenset, list[str]] = defaultdict(list)
    for tim in tims:
        for ctype in CTYPES:
            scope_classes[frozenset(scope_rel[(tim, ctype)])].append(
                f"{timing_name(tim)}/{ctype}"
            )
    print(f"  {len(scope_classes)} distinct orders over {len(tims) * 2} cells")
    for rel, cells in scope_classes.items():
        print(f"  {', '.join(cells)}:")
        edges = covers(list(SCOPES), set(rel))
        print("    " + (", ".join(f"{a} -> {b}" for a, b in edges) or "none"))

    print("\nCondition type")
    report.check(
        all(
            ("continual", "trigger") in ctype_rel[(s, t)] for s in SCOPES for t in tims
        ),
        "continual implies trigger in every cell",
    )
    coincide = sorted(
        f"{s}/{timing_name(t)}"
        for s in SCOPES
        for t in tims
        if ("trigger", "continual") in ctype_rel[(s, t)]
    )
    expected = sorted(
        f"{s}/{timing_name(t)}"
        for s in SCOPES
        for t in tims
        if t == (("Eventually",) if s in ONLY_SCOPES else ("Always",))
    )
    report.check(
        coincide == expected,
        "trigger implies continual only at always (eventually under only)",
        f"found {coincide}",
    )

    print("\nPropositional polarity ('+' monotone, '-' antitone)")

    def claimed(scope: str, ctype: str, tim: tuple, field: str) -> set:
        # The steps RuleSearch takes: a direction it may move the field in.
        only = scope in ONLY_SCOPES
        if field == "condition":
            return {"-"} if ctype == "continual" else set()
        if field == "response":
            if tim[0] == "AfterTicks":
                return set()
            return {"-"} if only else {"+"}
        return {"-"} if (tim[0] == "Before") != only else {"+"}

    unsound, missed = [], []
    table: dict[tuple, str] = {}
    for (scope, ctype, tim, field), measured in sorted(polarity.items()):
        claim = claimed(scope, ctype, tim, field)
        cell = f"{scope}/{ctype}/{timing_name(tim)}/{field}"
        table[(scope, ctype, tim, field)] = "".join(sorted(measured)) or "none"
        if claim - measured:
            unsound.append(f"{cell}: claims {claim}, measured {measured or '{}'}")
        if measured - claim:
            missed.append(f"{cell}: claims {claim or '{}'}, measured {measured}")
    for field in ("condition", "response", "stop"):
        summary: dict[str, list[str]] = defaultdict(list)
        for (scope, ctype, tim, fld), value in table.items():
            if fld == field:
                summary[value].append(f"{scope}/{ctype}/{timing_name(tim)}")
        print(f"  {field}:")
        for value, cells in sorted(summary.items()):
            print(f"    {value:>4}: {len(cells)} cells")
    report.check(
        not unsound, "every polarity requirement_implies uses holds", "; ".join(unsound)
    )
    print(f"  {len(missed)} measured polarities requirement_implies does not use")
    for line in missed:
        print(f"    {line}")

    if args.json:

        def key(tim: tuple) -> str:
            return timing_name(tim)

        args.json.write_text(
            json.dumps(
                {
                    "spot": version,
                    "max_ticks": args.max_ticks,
                    "timing": {
                        f"{s}/{c}": sorted(
                            [key(a), key(b)] for a, b in timing_rel[(s, c)]
                        )
                        for s in SCOPES
                        for c in CTYPES
                    },
                    "scope": {
                        f"{key(t)}/{c}": sorted([a, b] for a, b in scope_rel[(t, c)])
                        for t in tims
                        for c in CTYPES
                    },
                    "condition_type": {
                        f"{s}/{key(t)}": sorted([a, b] for a, b in ctype_rel[(s, t)])
                        for s in SCOPES
                        for t in tims
                    },
                    "polarity": {
                        "/".join([s, c, key(t), f]): v
                        for (s, c, t, f), v in table.items()
                    },
                },
                indent=1,
            )
        )

    if not args.skip_requirement_implies:
        check_requirement_implies(args, ltlfilt, report)

    print(f"\n{len(report.failures)} failed check(s)")
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(main())
