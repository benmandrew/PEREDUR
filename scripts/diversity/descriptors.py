#!/usr/bin/env python3
"""Descriptor cells of FRETISH repairs: which slots of the original O a repair
changed, and how.

    python3 scripts/diversity/descriptors.py SPEC.json RUN_DIR...

prints, per run, the number of distinct cells among its repair_N.json files.

A repair's cell is the set of (slot, kind) pairs for the slots it changed:

- `removed`: an original requirement with no counterpart in the repair. JSON
  drops a removed requirement rather than marking it, so each side's list is
  aligned to the original's in order, a slot left unmatched being removed.
- `assumption`: an original assumption changed, or an assumption the repair
  added (slots `A+0`, `A+1`, ... in the repair's order of the extras).
- `timing`: a guarantee whose timing, scope or condition type changed and
  whose condition and response did not.
- `propositional`: a guarantee whose condition or response changed and whose
  temporal fields did not.
- `timing+propositional`: both.

Formulas compare by parse tree, with the grammar of src/prop_formula/parser.cpp
(`<->` loosest, then `->` right-associative, `|`, `&`, U/R/W, then `!`, X, F,
G), so the repair printer's extra parentheses do not read as a change.
"""

import json
import os
import re
import sys

TOKEN = re.compile(r"\s*(<->|->|[()!~&|]|[A-Za-z_][A-Za-z0-9_]*)")
UNARY = {"!": "!", "~": "!", "X": "X", "F": "F", "G": "G"}
TEMPORAL = {"U", "R", "W"}


class _Parser:
    def __init__(self, text: str):
        self.toks, pos = [], 0
        text = text.rstrip()
        while pos < len(text):
            m = TOKEN.match(text, pos)
            if not m:
                raise ValueError(f"cannot read {text[pos:]!r}")
            self.toks.append(m.group(1))
            pos = m.end()
        self.i = 0

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else None

    def take(self, tok):
        if self.peek() == tok:
            self.i += 1
            return True
        return False

    def iff(self):
        lhs = self.implies()
        while self.take("<->"):
            lhs = ("<->", lhs, self.implies())
        return lhs

    def implies(self):
        lhs = self.disj()
        if self.take("->"):
            return ("->", lhs, self.implies())
        return lhs

    def disj(self):
        lhs = self.conj()
        while self.take("|"):
            lhs = ("|", lhs, self.conj())
        return lhs

    def conj(self):
        lhs = self.temporal()
        while self.take("&"):
            lhs = ("&", lhs, self.temporal())
        return lhs

    def temporal(self):
        lhs = self.unary()
        while self.peek() in TEMPORAL:
            op = self.toks[self.i]
            self.i += 1
            lhs = (op, lhs, self.unary())
        return lhs

    def unary(self):
        tok = self.peek()
        if tok in UNARY:
            self.i += 1
            return (UNARY[tok], self.unary())
        if self.take("("):
            inner = self.iff()
            if not self.take(")"):
                raise ValueError("unbalanced parenthesis")
            return inner
        if tok is None or not re.match(r"[A-Za-z_]", tok):
            raise ValueError(f"unexpected {tok!r}")
        self.i += 1
        return tok


def canonical(text: str):
    """A formula's parse tree, or its text with whitespace and parentheses
    removed where it does not parse."""
    try:
        p = _Parser(text)
        tree = p.iff()
        if p.peek() is None:
            return tree
    except ValueError:
        pass
    return re.sub(r"[\s()]", "", text)


def fields(req: dict) -> tuple:
    """(propositional fields, temporal fields) of one requirement."""
    timing = dict(req.get("timing", {}))
    if "stop" in timing:
        timing["stop"] = canonical(timing["stop"])
    prop = (canonical(req.get("condition", "true")),
            canonical(req.get("response", "true")))
    temp = (json.dumps(timing, sort_keys=True), req.get("condition-type"),
            json.dumps(req.get("scope"), sort_keys=True))
    return prop, temp


def align(orig: list, rep: list) -> list:
    """For each repair requirement, the original slot it stands in, or None
    for an extra one: an order-preserving match of least cost, where a match
    costs the number of fields that differ (0 to 2) and leaving a repair
    requirement unmatched costs 3."""
    fo, fr = [fields(r) for r in orig], [fields(r) for r in rep]

    def cost(i, j):
        return (fo[i][0] != fr[j][0]) + (fo[i][1] != fr[j][1])
    n, m = len(orig), len(rep)
    inf = float("inf")
    # best[i][j]: least cost of placing rep[j:] against orig[i:].
    best = [[inf] * (m + 1) for _ in range(n + 1)]
    for i in range(n + 1):
        best[i][m] = 0
    for j in range(m - 1, -1, -1):
        best[n][j] = 3 * (m - j)
    for i in range(n - 1, -1, -1):
        for j in range(m - 1, -1, -1):
            best[i][j] = min(best[i + 1][j],               # orig i removed
                             cost(i, j) + best[i + 1][j + 1],
                             3 + best[i][j + 1])           # rep j extra
    out, i, j = [], 0, 0
    while j < m:
        if i < n and best[i][j] == cost(i, j) + best[i + 1][j + 1]:
            out.append(i)
            i, j = i + 1, j + 1
        elif i < n and best[i][j] == best[i + 1][j]:
            i += 1
        else:
            out.append(None)
            j += 1
    return out


def cell(orig: dict, rep: dict) -> frozenset:
    out = set()
    for side, tag in (("assumptions", "A"), ("guarantees", "G")):
        o, r = orig.get(side, []), rep.get(side, [])
        match = align(o, r)
        held = {i for i in match if i is not None}
        out |= {(f"{tag}{i}", "removed") for i in range(len(o)) if i not in held}
        extra = 0
        for j, i in enumerate(match):
            if i is None:
                out.add((f"{tag}+{extra}", "assumption" if tag == "A" else "added"))
                extra += 1
                continue
            (po, to), (pr, tr) = fields(o[i]), fields(r[j])
            if (po, to) == (pr, tr):
                continue
            if tag == "A":
                kind = "assumption"
            else:
                kind = ("timing+propositional" if po != pr and to != tr
                        else "propositional" if po != pr else "timing")
            out.add((f"{tag}{i}", kind))
    return frozenset(out)


REPAIR = re.compile(r"^repair_(\d+)\.json$")


def run_cells(orig: dict, run: str) -> list:
    out = []
    for name in sorted(f for f in os.listdir(run) if REPAIR.match(f)):
        with open(os.path.join(run, name)) as handle:
            out.append(cell(orig, json.load(handle)))
    return out


def descriptor_count(orig: dict, run: str) -> int:
    return len(set(run_cells(orig, run)))


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    with open(argv[0]) as handle:
        orig = json.load(handle)
    for run in argv[1:]:
        cells = run_cells(orig, run)
        print(f"{run}\t{len(cells)} repairs\t{len(set(cells))} cells")
    return 0


if __name__ == "__main__":
    sys.exit(main())
