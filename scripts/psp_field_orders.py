#!/usr/bin/env python3
"""Measure the PSP field orders from the body-and-wrapper lowering and Spot.

The lowering here is the one PLAN.md D4 fixes, and P1 ports it to C++: a body
per pattern over an optional boundary B, and a wrapper per scope. Scopes are
closed on the left and open on the right, and after Q opens at the first Q.
Every implication `lhs -> rhs` is decided as the unsatisfiability of
`lhs & !rhs` with `ltlfilt --satisfiable`, batched as `decide` in
field_orders.py does. The script measures:

1. each lowering against the Dwyer web-table row where one exists, with
   precedence after Q pinned as strictly stronger than its row, and the
   counter-trace `q; p; q; cycle{}` that separates them;
2. the pattern order under each scope, and the classes of scopes that share
   one order;
3. the scope order under each pattern, and the cells of patterns that share
   one order;
4. the polarity of P, S, Q and R in every pattern x scope cell;
5. the joint steps: implications between (pattern, scope) nodes that change
   both fields and that no chain of one-field implications reaches;
6. the count chain of each bounded pattern under each scope, and its edges to
   the unbounded pattern it refines.

It then checks the claims of the PSP suitability report (section 4) on the
patterns that report measured, and the chain claims of PLAN.md D4.

black asks every implication again, as a second solver (cross_check in
field_orders.py gives the rule for a pair it leaves undecided); --skip-black
turns it off. The script exits 1 when any check fails.

A polarity needs one pair of queries per cell, by uniform substitution, as in
field_orders.py: the field takes `f & fx` against `f`.

Atoms: p is the trigger or forbidden proposition, s the target, q the scope
opener and r the scope closer. Dwyer's P is s in universality, existence and
bounded recurrence, and p in absence and bounded existence.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from field_orders import (  # noqa: E402
    Report,
    black_verdicts,
    covers,
    cross_check,
    decide,
    default_black,
    default_spot_bin_dir,
)

REPO = Path(__file__).resolve().parent.parent

SCOPES = ("globally", "before", "after", "between", "afteruntil")
BASE = ("absence", "universality", "existence", "precedence", "response")
BOUNDED = (
    "bounded-response",
    "bounded-invariance",
    "bounded-existence",
    "bounded-recurrence",
)
# The fields each pattern and scope reads.
PATTERN_FIELDS = {
    "absence": "P",
    "universality": "S",
    "existence": "S",
    "precedence": "PS",
    "response": "PS",
    "bounded-response": "PS",
    "bounded-invariance": "PS",
    "bounded-existence": "P",
    "bounded-recurrence": "S",
}
SCOPE_FIELDS = {
    "globally": "",
    "before": "R",
    "after": "Q",
    "between": "QR",
    "afteruntil": "QR",
}
ATOMS = {"P": "p", "S": "s", "Q": "q", "R": "r"}
# Alternative boundary bodies, each the same as the named pattern with no
# boundary: the prototype's bounded response and invariance, which the
# report's section 4 measured, and a strict bounded recurrence.
ALTERNATIVE = {
    "report-bounded-response": "bounded-response",
    "report-bounded-invariance": "bounded-invariance",
    "strict-bounded-recurrence": "bounded-recurrence",
}

# Dwyer, Avrunin and Corbett, as the pattern website prints them
# (matthewbdwyer.github.io/psp/patterns/ltl.html), with Dwyer's P renamed as
# in the module docstring. Bounded existence is the published instance k = 2,
# with its parentheses balanced.
DWYER = {
    ("absence", "globally"): "G(!p)",
    ("absence", "before"): "F r -> (!p U r)",
    ("absence", "after"): "G(q -> G(!p))",
    ("absence", "between"): "G((q & !r & F r) -> (!p U r))",
    ("absence", "afteruntil"): "G((q & !r) -> (!p W r))",
    ("existence", "globally"): "F(s)",
    ("existence", "before"): "!r W (s & !r)",
    ("existence", "after"): "G(!q) | F(q & F(s))",
    ("existence", "between"): "G((q & !r) -> (!r W (s & !r)))",
    ("existence", "afteruntil"): "G((q & !r) -> (!r U (s & !r)))",
    ("universality", "globally"): "G(s)",
    ("universality", "before"): "F r -> (s U r)",
    ("universality", "after"): "G(q -> G(s))",
    ("universality", "between"): "G((q & !r & F r) -> (s U r))",
    ("universality", "afteruntil"): "G((q & !r) -> (s W r))",
    ("precedence", "globally"): "!p W s",
    ("precedence", "before"): "F r -> (!p U (s | r))",
    ("precedence", "after"): "G(!q) | F(q & (!p W s))",
    ("precedence", "between"): "G((q & !r & F r) -> (!p U (s | r)))",
    ("precedence", "afteruntil"): "G((q & !r) -> (!p W (s | r)))",
    ("response", "globally"): "G(p -> F s)",
    ("response", "before"): "F r -> ((p -> (!r U (s & !r))) U r)",
    ("response", "after"): "G(q -> G(p -> F s))",
    ("response", "between"): "G((q & !r & F r) -> ((p -> (!r U (s & !r))) U r))",
    ("response", "afteruntil"): "G((q & !r) -> ((p -> (!r U (s & !r))) W r))",
    ("bounded-existence", "globally"): "!p W (p W (!p W (p W G(!p))))",
    ("bounded-existence", "before"): (
        "F r -> ((!p & !r) U (r | ((p & !r) U (r | ((!p & !r) U (r | "
        "((p & !r) U (r | (!p U r)))))))))"
    ),
    ("bounded-existence", "after"): (
        "F q -> (!q U (q & (!p W (p W (!p W (p W G(!p)))))))"
    ),
    ("bounded-existence", "between"): (
        "G((q & F r) -> ((!p & !r) U (r | ((p & !r) U (r | ((!p & !r) U "
        "(r | ((p & !r) U (r | (!p U r))))))))))"
    ),
    ("bounded-existence", "afteruntil"): (
        "G(q -> ((!p & !r) U (r | ((p & !r) U (r | ((!p & !r) U (r | "
        "((p & !r) U (r | (!p W r) | G(p))))))))))"
    ),
}
DWYER_COUNT = 2  # the bounded-existence instance the table prints
# The one published row the lowering departs from (D4), and the trace that
# separates them: the second q would open the published row's scope.
STRONGER_THAN_DWYER = ("precedence", "after")
COUNTER_TRACE = "q&!p&!r&!s; p&!q&!r&!s; q&!p&!r&!s; cycle{!p&!q&!r&!s}"

# The suitability report's results (section 4), over the base patterns and
# bounded response and invariance.
REPORT_CLASSES = (("after", "globally"), ("afteruntil", "between"), ("before",))
REPORT_CELLS = {
    ("absence", "universality", "bounded-invariance"): {
        ("globally", "before"),
        ("globally", "after"),
        ("globally", "between"),
        ("globally", "afteruntil"),
        ("after", "between"),
        ("after", "afteruntil"),
        ("afteruntil", "between"),
    },
    ("existence",): {("afteruntil", "between")},
    ("precedence",): {("globally", "before"), ("afteruntil", "between")},
    ("response", "bounded-response"): {
        ("globally", "after"),
        ("afteruntil", "between"),
    },
}
REPORT_EDGES = {"globally": 41, "between": 36, "before": 35}  # counts 1 to 3


def report_polarity(kind: str, scope: str, field: str) -> str:
    """The polarity the report states, '+' monotone and '-' antitone."""
    if field == "P":
        return "-"
    if field == "S":
        return "+"
    if field == "Q":
        if scope == "after" and kind in ("existence", "precedence"):
            return "none"
        return "-"
    if scope == "before":
        return "-" if kind == "existence" else "none"
    if scope == "between":
        return "none"
    keeps = ("absence", "universality", "precedence", "bounded-invariance")
    return "+" if kind in keeps else "none"


def x(i: int, f: str) -> str:
    """X^i f, with every X operand parenthesised for black."""
    for _ in range(i):
        f = f"X({f})"
    return f


def conj(parts: list[str]) -> str:
    return "(" + " & ".join(parts) + ")"


def disj(parts: list[str]) -> str:
    return "(" + " | ".join(parts) + ")"


def within(n: int, f: str) -> str:
    """F[0,n] f as an X chain."""
    return disj([x(j, f) for j in range(n + 1)])


def throughout(n: int, f: str) -> str:
    """G[0,n] f as an X chain."""
    return conj([x(j, f) for j in range(n + 1)])


def body(pat: tuple, b: str | None, p: str = "p", s: str = "s") -> str:
    """The pattern's body at the scope's first point, with boundary b or none.

    Under a boundary, the point where b holds is outside the scope.
    """
    kind = pat[0]
    if kind == "absence":
        return f"G(!{p})" if b is None else f"(!{p} W {b})"
    if kind == "universality":
        return f"G({s})" if b is None else f"({s} W {b})"
    if kind == "existence":
        return f"F({s})" if b is None else f"(!{b} U ({s} & !{b}))"
    if kind == "precedence":
        return f"(!{p} W {s})" if b is None else f"(!{p} W ({s} | {b}))"
    if kind == "response":
        if b is None:
            return f"G({p} -> F({s}))"
        return f"(({p} -> (!{b} U ({s} & !{b}))) W {b})"
    n = pat[1]
    if kind == "bounded-response":
        if b is None:
            return f"G({p} -> {within(n, s)})"
        # !b U[0,n] (s & !b): s within n, and before b.
        strict = disj(
            [
                conj([x(j, f"{s} & !{b}")] + [x(i, f"!{b}") for i in range(j)])
                for j in range(n + 1)
            ]
        )
        return f"(({p} -> {strict}) W {b})"
    if kind == "bounded-invariance":
        if b is None:
            return f"G({p} -> {throughout(n, s)})"
        # s at each of the next n points, up to and excluding the first b.
        held = conj(
            [disj([x(j, s)] + [x(i, b) for i in range(j + 1)]) for j in range(n + 1)]
        )
        return f"(({p} -> {held}) W {b})"
    if kind == "bounded-existence":
        # At most n stretches of p, alternating with stretches of !p.
        if b is None:
            out = f"G(!{p})"
            for _ in range(n):
                out = f"(!{p} W ({p} W {out}))"
            return out
        out = f"(!{p} W {b})"
        for _ in range(n):
            out = f"(!{p} W ({b} | ({p} W ({b} | {out}))))"
        return out
    if kind == "bounded-recurrence":
        if b is None:
            return f"G{within(n, s)}"
        return f"({within(n, f'{s} | {b}')} W {b})"
    # Bodies measured for comparison only, never lowered by the engine.
    if b is None and kind in ALTERNATIVE:
        return body((ALTERNATIVE[kind], n), b, p, s)
    if kind == "report-bounded-response":
        return f"(({p} -> {within(n, f'{s} & !{b}')}) W {b})"
    if kind == "report-bounded-invariance":
        return f"(({p} -> ({throughout(n, s)} | ({b} R {s}))) W {b})"
    if kind == "strict-bounded-recurrence":
        strict = disj(
            [
                conj([x(j, f"{s} & !{b}")] + [x(i, f"!{b}") for i in range(j)])
                for j in range(n + 1)
            ]
        )
        return f"({strict} W {b})"
    raise ValueError(pat)


def lower(scope: str, pat: tuple, q="q", r="r", p="p", s="s") -> str:
    """The scope's wrapper around the pattern's body."""
    if scope == "globally":
        return body(pat, None, p, s)
    if scope == "before":
        return f"(F({r}) -> {body(pat, r, p, s)})"
    if scope == "after":
        return f"(!{q} W ({q} & {body(pat, None, p, s)}))"
    if scope == "between":
        return f"G(({q} & !{r} & F({r})) -> {body(pat, r, p, s)})"
    if scope == "afteruntil":
        return f"G(({q} & !{r}) -> {body(pat, r, p, s)})"
    raise ValueError(scope)


def patterns(max_count: int) -> list[tuple]:
    out: list[tuple] = [(kind,) for kind in BASE]
    for kind in BOUNDED:
        out += [(kind, n) for n in range(1, max_count + 1)]
    return out


def name(pat: tuple) -> str:
    return pat[0] + "".join(f" {arg}" for arg in pat[1:])


def node_name(node: tuple) -> str:
    return f"{name(node[0])}/{node[1]}"


def is_partial_order(rel: set) -> bool:
    transitive = all(
        (a, c) in rel for a, b in rel for b2, c in rel if b == b2 and a != c
    )
    return transitive and not any((b, a) in rel for a, b in rel)


def accepts(ltlfilt: Path, formula: str, word: str) -> bool:
    run = subprocess.run(
        [str(ltlfilt), f"--accept-word={word}", "-f", formula],
        capture_output=True,
        text=True,
        check=False,
        timeout=600,
    )
    if run.returncode not in (0, 1) or run.stderr.strip():
        sys.exit(f"ltlfilt --accept-word failed: {run.stderr}")
    return bool(run.stdout.strip())


def classes_of(keys: list, rel: dict) -> list[tuple]:
    """The keys grouped by equal relations, each group in key order."""
    groups: dict[frozenset, list] = defaultdict(list)
    for key in keys:
        groups[frozenset(rel[key])].append(key)
    return list(map(tuple, groups.values()))


def base_kind(pat: tuple) -> str:
    return ALTERNATIVE.get(pat[0], pat[0])


@dataclasses.dataclass
class Orders:
    """The relations one implication set gives over pats x SCOPES."""

    pats: list[tuple]
    pattern: dict[str, set]  # scope -> {(pat, pat)}
    scope: dict[tuple, set]  # pat -> {(scope, scope)}
    classes: list[tuple]  # scopes sharing one pattern order
    cells: list[tuple]  # patterns sharing one scope order
    cross: list[tuple]  # implications changing both fields
    one_intermediate: int  # of those, reached through one intermediate node
    joint: list[tuple]  # of those, reached by no chain of one-field steps


def orders(pats: list[tuple], imp: set[tuple]) -> Orders:
    nodes = [(pat, scope) for pat in pats for scope in SCOPES]
    pattern = {
        scope: {
            (a, b)
            for a in pats
            for b in pats
            if a != b and ((a, scope), (b, scope)) in imp
        }
        for scope in SCOPES
    }
    scope = {
        pat: {
            (a, b)
            for a in SCOPES
            for b in SCOPES
            if a != b and ((pat, a), (pat, b)) in imp
        }
        for pat in pats
    }
    # The closure of the one-field edges: a chain may change the pattern and
    # the scope any number of times.
    succ: dict[tuple, set] = defaultdict(set)
    for a in nodes:
        for b in nodes:
            if (a, b) in imp and (a[0] == b[0]) != (a[1] == b[1]):
                succ[a].add(b)

    def reach(start: tuple) -> set:
        seen, todo = set(), [start]
        while todo:
            for nxt in succ[todo.pop()]:
                if nxt not in seen:
                    seen.add(nxt)
                    todo.append(nxt)
        return seen

    cross = sorted(
        (a, b)
        for a in nodes
        for b in nodes
        if a[0] != b[0] and a[1] != b[1] and (a, b) in imp
    )
    one = sum(
        ((a, (b[0], a[1])) in imp and ((b[0], a[1]), b) in imp)
        or ((a, (a[0], b[1])) in imp and ((a[0], b[1]), b) in imp)
        for a, b in cross
    )
    reached = {node: reach(node) for node in nodes}
    return Orders(
        pats=pats,
        pattern=pattern,
        scope=scope,
        classes=sorted(tuple(sorted(c)) for c in classes_of(list(SCOPES), pattern)),
        cells=classes_of(pats, scope),
        cross=cross,
        one_intermediate=one,
        joint=[(a, b) for a, b in cross if b not in reached[a]],
    )


def cell_kinds(found: Orders) -> dict[frozenset, set]:
    """Each scope-order cell's edges, with the pattern kinds that share it."""
    return {
        frozenset(found.scope[cell[0]]): {base_kind(p) for p in cell}
        for cell in found.cells
    }


def node_pairs(nodes: list[tuple], same_scope: bool = False) -> list[tuple]:
    return [
        (a, b)
        for a in nodes
        for b in nodes
        if a != b and (not same_scope or a[1] == b[1])
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").split("\n")[0])
    parser.add_argument("--spot-bin-dir", type=Path, default=default_spot_bin_dir())
    parser.add_argument("--max-count", type=int, default=4)
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument(
        "--json",
        type=Path,
        default=REPO / "docs" / "dev" / "psp-orders.json",
        help="write every measured relation here",
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
        # At 10 s black leaves the deep bounded-existence queries undecided,
        # and has called two valid ones satisfiable (docs/dev/psp.md).
        default=120,
        help="seconds black may spend on one query (its -t)",
    )
    parser.add_argument(
        "--black-bound",
        type=int,
        default=10,
        help="the longest counter-model black searches for (its -k)",
    )
    args = parser.parse_args()
    if args.max_count < 3:
        parser.error("--max-count must be at least 3, the report's counts")
    ltlfilt = args.spot_bin_dir / "ltlfilt"
    version = subprocess.run(
        [str(ltlfilt), "--version"], capture_output=True, text=True, check=True
    ).stdout.splitlines()[0]
    # The path ltlfilt prints is the measuring machine's; keep the version.
    version = Path(version.split(" ", 1)[0]).name + " " + version.split(" ", 1)[1]

    pats = patterns(args.max_count)
    nodes = [(pat, scope) for pat in pats for scope in SCOPES]
    # The report's patterns, with its prototype's bounded bodies.
    report_pats = [(kind,) for kind in BASE] + [
        (kind, n)
        for kind in ("report-bounded-response", "report-bounded-invariance")
        for n in (1, 2, 3)
    ]
    report_nodes = [(pat, scope) for pat in report_pats for scope in SCOPES]
    # Every pattern, with the strict bounded recurrence in place of D4's.
    strict_pats = [p for p in pats if p[0] != "bounded-recurrence"] + [
        ("strict-bounded-recurrence", n) for n in range(1, args.max_count + 1)
    ]
    strict_nodes = [(pat, scope) for pat in strict_pats for scope in SCOPES]
    formula = {
        node: lower(node[1], node[0]) for node in nodes + report_nodes + strict_nodes
    }

    # Every query, as (family, cell, lhs, rhs).
    queries: list[tuple[str, tuple, str, str]] = []
    for family, pairs_ in (
        ("node", node_pairs(nodes)),
        ("report", node_pairs(report_nodes)),
        ("strict", node_pairs(strict_nodes, same_scope=True)),
    ):
        for a, b in pairs_:
            queries.append((family, (a, b), formula[a], formula[b]))
    dwyer_keys = list(DWYER)
    for kind, scope in dwyer_keys:
        pat = (kind, DWYER_COUNT) if kind in BOUNDED else (kind,)
        row, ours = DWYER[(kind, scope)], lower(scope, pat)
        queries.append(("dwyer=>ours", (kind, scope), row, ours))
        queries.append(("ours=>dwyer", (kind, scope), ours, row))
    polarity: dict[tuple, set] = {}
    for pat, scope in nodes:
        for field in PATTERN_FIELDS[pat[0]] + SCOPE_FIELDS[scope]:
            atoms = {f.lower(): ATOMS[f] for f in ATOMS}
            strong = dict(atoms)
            strong[field.lower()] = f"({ATOMS[field]} & {ATOMS[field]}x)"
            lo, hi = lower(scope, pat, **strong), lower(scope, pat, **atoms)
            cell = (pat, scope, field)
            polarity[cell] = set()
            queries.append(("polarity+", cell, lo, hi))
            queries.append(("polarity-", cell, hi, lo))

    print(f"{len(nodes)} requirements, {len(queries)} implications, {version}")
    pairs = [(lhs, rhs) for _, _, lhs, rhs in queries]
    holds = decide(ltlfilt, pairs, args.jobs)
    verdicts = black_verdicts(args, ltlfilt, pairs)

    imp: dict[str, set] = defaultdict(set)
    dwyer: dict[tuple, dict] = defaultdict(dict)
    for (family, cell, _, _), ok in zip(queries, holds):
        if family in ("node", "report", "strict"):
            if ok:
                imp[family].add(cell)
        elif family.startswith(("dwyer", "ours")):
            dwyer[cell][family] = ok
        elif ok:
            polarity[cell].add(family[-1])

    report = Report()
    if verdicts is not None:
        print("\nSecond solver")
        cross_check(report, pairs, holds, verdicts, args.black_bound)

    print("\nThe Dwyer table")
    verdict_of = {}
    for key in dwyer_keys:
        fwd, bwd = dwyer[key]["dwyer=>ours"], dwyer[key]["ours=>dwyer"]
        verdict_of[key] = (
            "equivalent"
            if fwd and bwd
            else "stronger" if bwd else "weaker" if fwd else "incomparable"
        )
    base_rows = [k for k in dwyer_keys if k[0] in BASE]
    off = {k: verdict_of[k] for k in base_rows if verdict_of[k] != "equivalent"}
    report.check(
        off == {STRONGER_THAN_DWYER: "stronger"},
        f"{len(base_rows) - 1} of {len(base_rows)} base rows are equivalent, and "
        "precedence after Q is strictly stronger than its row",
        "; ".join(f"{k[0]}/{k[1]}: {v}" for k, v in off.items()),
    )
    row, ours = DWYER[STRONGER_THAN_DWYER], lower("after", ("precedence",))
    report.check(
        accepts(ltlfilt, row, COUNTER_TRACE)
        and not accepts(ltlfilt, ours, COUNTER_TRACE),
        f"the published row accepts {COUNTER_TRACE} and the lowering rejects it",
    )
    bounded_rows = [k for k in dwyer_keys if k[0] in BOUNDED]
    report.check(
        all(verdict_of[k] == "equivalent" for k in bounded_rows),
        f"bounded existence {DWYER_COUNT} is equivalent to its row in every scope",
        "; ".join(f"{k[1]}: {verdict_of[k]}" for k in bounded_rows),
    )

    print("\nThe report's measurements, with its prototype's bodies")
    proto = orders(report_pats, imp["report"])
    report.check(
        proto.classes == sorted(REPORT_CLASSES),
        f"{len(REPORT_CLASSES)} classes of scopes share a pattern order",
        f"found {proto.classes}",
    )
    edges = {scope: len(proto.pattern[scope]) for scope in REPORT_EDGES}
    report.check(
        edges == REPORT_EDGES,
        "the classes have 41, 36 and 35 edges",
        f"found {edges}",
    )
    expected_cells = {frozenset(e): set(k) for k, e in REPORT_CELLS.items()}
    report.check(
        cell_kinds(proto) == expected_cells,
        f"{len(REPORT_CELLS)} scope-order cells",
        f"found {[(sorted(v), sorted(k)) for k, v in cell_kinds(proto).items()]}",
    )
    report.check(
        len(proto.cross) == 251 and not proto.joint,
        "251 implications change both fields, and none is a joint step",
        f"found {len(proto.cross)} and {len(proto.joint)}",
    )

    found = orders(pats, imp["node"])
    d3 = [p for p in pats if p[0] in BASE + BOUNDED[:2]]
    d3_found = orders(d3, imp["node"])

    print("\nPattern order")
    for scope in SCOPES:
        report.check(
            is_partial_order(found.pattern[scope]), f"{scope}: a partial order"
        )
    print(
        f"  the report's patterns at counts 1 to {args.max_count}: classes "
        f"{d3_found.classes}, edges "
        + ", ".join(f"{s} {len(d3_found.pattern[s])}" for s in SCOPES)
    )
    print(f"  every pattern: classes {found.classes}")
    for members in found.classes:
        rel = found.pattern[members[0]]
        print(f"  {', '.join(members)}: {len(rel)} edges, covers:")
        for a, b in covers(pats, rel):
            print(f"    {name(a)} -> {name(b)}")
    # Where the classes differ, and why.
    for s1, s2 in (("globally", "between"), ("between", "before")):
        lost = found.pattern[s1] - found.pattern[s2]
        print(
            f"  {s1}, not {s2}: "
            + ", ".join(f"{name(a)} => {name(b)}" for a, b in covers(pats, lost))
        )
        gained = found.pattern[s2] - found.pattern[s1]
        if gained:
            print(
                f"  {s2}, not {s1}: "
                + ", ".join(f"{name(a)} => {name(b)}" for a, b in sorted(gained))
            )

    print("\nScope order")
    for pat in pats:
        report.check(
            is_partial_order(found.scope[pat]), f"{name(pat)}: a partial order"
        )
    report.check(
        cell_kinds(d3_found) == expected_cells,
        f"the report's patterns: its {len(REPORT_CELLS)} scope-order cells",
        f"found {[(sorted(v), sorted(k)) for k, v in cell_kinds(d3_found).items()]}",
    )
    for cell in found.cells:
        print(f"  {', '.join(name(p) for p in cell)}:")
        edges_ = covers(list(SCOPES), found.scope[cell[0]])
        print("    " + (", ".join(f"{a} -> {b}" for a, b in edges_) or "none"))

    print("\nPolarity ('+' monotone, '-' antitone)")
    pol = {cell: "".join(sorted(v)) or "none" for cell, v in polarity.items()}
    differ = [
        f"{name(pat)}/{scope}/{field}: report {claim}, measured {value}"
        for (pat, scope, field), value in pol.items()
        if pat[0] in BASE + BOUNDED[:2]
        and (claim := report_polarity(pat[0], scope, field)) != value
    ]
    print(f"  {len(differ)} differences from the report on its patterns")
    for line in differ:
        print(f"    {line}")
    for field in "PSQR":
        summary: dict[str, int] = defaultdict(int)
        for (_, _, fld), value in pol.items():
            if fld == field:
                summary[value] += 1
        print(
            f"  {field}: "
            + ", ".join(f"{v} in {c}" for v, c in sorted(summary.items()))
        )
    for kind in BOUNDED[2:]:
        for field in PATTERN_FIELDS[kind] + "QR":
            vals = defaultdict(list)
            for (p, s, f), v in pol.items():
                if p[0] == kind and f == field:
                    vals[v].append(f"{name(p)}/{s}")
            print(
                f"  {kind} {field}: "
                + "; ".join(f"{v} in {len(c)}" for v, c in sorted(vals.items()))
            )
    report.check(
        all(
            v == "none"
            for (p, _, f), v in pol.items()
            if p[0] == "bounded-existence" and f == "P"
        ),
        "bounded existence: P has no polarity in any cell",
    )

    print("\nJoint steps")
    print(
        f"  {len(found.cross)} implications change both fields, "
        f"{found.one_intermediate} through one intermediate node, "
        f"{len(found.joint)} reached by no one-field chain"
    )
    report.check(
        not d3_found.joint,
        f"the report's patterns at counts 1 to {args.max_count}: no joint step",
        "; ".join(f"{node_name(a)} => {node_name(b)}" for a, b in d3_found.joint[:10]),
    )
    for a, b in found.joint[:20]:
        print(f"    {node_name(a)} => {node_name(b)}")

    print("\nCount chains")
    # Each bounded pattern's unbounded neighbours, and whether the neighbour
    # is weaker ('+') or stronger ('-').
    unbounded = {
        "bounded-response": [(("response",), "+")],
        "bounded-invariance": [(("universality",), "-")],
        "bounded-existence": [(("absence",), "-")],
        "bounded-recurrence": [(("universality",), "-"), (("existence",), "+")],
    }
    # '+' when the pattern weakens as its count grows.
    direction = {
        "bounded-response": "+",
        "bounded-invariance": "-",
        "bounded-existence": "+",
        "bounded-recurrence": "+",
    }
    holds_in = imp["node"]
    chains: dict[str, dict] = {}
    for scope in SCOPES:
        chains[scope] = {}
        for kind in BOUNDED:
            counts = list(range(1, args.max_count + 1))
            if direction[kind] == "-":
                counts.reverse()
            # Strongest first: each step weakens.
            steps, back = {}, {}
            for lo, hi in zip(counts, counts[1:]):
                a, b = ((kind, lo), scope), ((kind, hi), scope)
                steps[f"{name(a[0])} => {name(b[0])}"] = (a, b) in holds_in
                back[f"{name(b[0])} => {name(a[0])}"] = (b, a) in holds_in
            others = {}
            for other, sense in unbounded[kind]:
                for n in range(1, args.max_count + 1):
                    a, b = (kind, n), other
                    if sense == "-":
                        a, b = b, a
                    others[f"{name(a)} => {name(b)}"] = (
                        (a, scope),
                        (b, scope),
                    ) in holds_in
            chains[scope][kind] = {
                "steps": steps,
                "reverse": back,
                "unbounded": others,
            }
            report.check(
                all(steps.values()) and not any(back.values()),
                f"{scope}: {kind} is a strict chain in its count",
                f"steps {steps}, reverse {back}",
            )
            # Bounded recurrence need not imply existence: a boundary may
            # close the scope before S.
            required = {
                k: v
                for k, v in others.items()
                if kind != "bounded-recurrence" or k.startswith("universality")
            }
            report.check(
                all(required.values()),
                f"{scope}: {kind} is ordered against its unbounded pattern",
                str(required),
            )
            print(
                f"  {scope}/{kind}: "
                + ", ".join(f"{k} {'yes' if v else 'no'}" for k, v in others.items())
            )

    print("\nStrict bounded recurrence (not lowered; for comparison)")
    strict = orders(strict_pats, imp["strict"])
    print(f"  classes {strict.classes}")
    for scope in SCOPES:
        added = [
            f"{name(a)} => {name(b)}"
            for a, b in covers(strict_pats, strict.pattern[scope])
            if "bounded-recurrence" in a[0] + b[0]
        ]
        print(f"  {scope} covers: " + (", ".join(added) or "none"))

    if args.json:
        args.json.write_text(
            json.dumps(
                {
                    "spot": version,
                    "max_count": args.max_count,
                    "patterns": [name(p) for p in pats],
                    "scopes": list(SCOPES),
                    "lowering": {node_name(n): formula[n] for n in nodes},
                    "dwyer": {
                        f"{k[0]}/{k[1]}": {
                            "published": DWYER[k],
                            "count": DWYER_COUNT if k[0] in BOUNDED else None,
                            "lowering": verdict_of[k],
                        }
                        for k in dwyer_keys
                    },
                    "counter_trace": {
                        "word": COUNTER_TRACE,
                        "published_accepts": True,
                        "lowering_accepts": False,
                    },
                    "pattern_order": {
                        scope: sorted(
                            [name(a), name(b)] for a, b in found.pattern[scope]
                        )
                        for scope in SCOPES
                    },
                    "pattern_order_covers": {
                        scope: [
                            [name(a), name(b)]
                            for a, b in covers(pats, found.pattern[scope])
                        ]
                        for scope in SCOPES
                    },
                    "pattern_order_classes": [list(c) for c in found.classes],
                    "pattern_order_classes_report_patterns": [
                        list(c) for c in d3_found.classes
                    ],
                    "scope_order": {
                        name(p): sorted([a, b] for a, b in found.scope[p]) for p in pats
                    },
                    "scope_order_cells": [
                        {
                            "patterns": [name(p) for p in cell],
                            "order": sorted([a, b] for a, b in found.scope[cell[0]]),
                            "covers": [
                                list(e)
                                for e in covers(list(SCOPES), found.scope[cell[0]])
                            ],
                        }
                        for cell in found.cells
                    ],
                    "polarity": {
                        f"{name(p)}/{s}": {
                            f: pol[(p, s, f)]
                            for f in PATTERN_FIELDS[p[0]] + SCOPE_FIELDS[s]
                        }
                        for p, s in nodes
                    },
                    "polarity_differences_from_report": differ,
                    "joint_steps": {
                        "cross_implications": len(found.cross),
                        "through_one_intermediate": found.one_intermediate,
                        "joint": [[node_name(a), node_name(b)] for a, b in found.joint],
                    },
                    "count_chains": chains,
                    "report_prototype": {
                        "pattern_order_classes": [list(c) for c in proto.classes],
                        "edges": {s: len(proto.pattern[s]) for s in SCOPES},
                        "cross_implications": len(proto.cross),
                        "joint": len(proto.joint),
                    },
                    "strict_bounded_recurrence": {
                        "pattern_order_classes": [list(c) for c in strict.classes],
                        "joint": len(strict.joint),
                    },
                },
                indent=1,
            )
            + "\n"
        )
        print(f"\nwrote {args.json}")

    print(f"\n{len(report.failures)} failed check(s)")
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(main())
