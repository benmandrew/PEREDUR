"""Translate Spectra statements into Spot LTL over a TLSF subject's atoms.

Covers the fragment GLASS, JVTS-Repair and AMT13 print (G, GF, next, value
and set comparisons) and what the ten shared subject files add: `define`
macros, the past operators Y/PREV under a top-level G, and the two response
patterns `respondsTo` and `pRespondsToS`.

An enum of n > 2 values takes ceil(log2 n) atoms `lower(name)_i`, with value
index v setting bit i to (v >> i) & 1. A two-valued enum takes the single atom
`lower(name)`, true on its second value. A boolean keeps `lower(name)`.
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass, field

TOKEN = re.compile(r"\s*(<->|->|!=|:=|=|\(|\)|\{|\}|,|!|&|\||;|[A-Za-z_][A-Za-z0-9_.]*|\S)")
WORD_OPS = {"and": "&", "or": "|", "not": "!", "implies": "->", "iff": "<->"}
PAST = ("Y", "PREV")
# The response patterns of the subject files, as LTL. Both monitors accept
# exactly the words where every trigger is followed, now or later, by a
# response.
PATTERNS = {"respondsTo": 2, "pRespondsToS": 2}


class Untranslatable(Exception):
    pass


@dataclass
class Var:
    name: str
    values: list[str] | None  # None for boolean
    env: bool


@dataclass
class Encoding:
    vars: dict[str, Var]
    rename: dict[str, str] = field(default_factory=dict)

    def atom(self, name: str) -> str:
        return self.rename.get(name, name.lower())

    def bits(self, var: Var) -> int:
        assert var.values is not None
        return max(1, math.ceil(math.log2(len(var.values))))

    def atoms(self, name: str) -> list[str]:
        var = self.vars[name]
        if var.values is None or len(var.values) == 2:
            return [self.atom(name)]
        return [f"{self.atom(name)}_{i}" for i in range(self.bits(var))]

    def value(self, name: str, value: str) -> str:
        var = self.vars[name]
        if var.values is None:
            if value not in ("true", "false"):
                raise Untranslatable(f"boolean {name} compared with {value}")
            return self.atom(name) if value == "true" else f"!{self.atom(name)}"
        if value not in var.values:
            raise Untranslatable(f"{value} is not a value of {name}")
        index = var.values.index(value)
        if len(var.values) == 2:
            return self.atom(name) if index == 1 else f"!{self.atom(name)}"
        lits = []
        for i, a in enumerate(self.atoms(name)):
            lits.append(a if (index >> i) & 1 else f"!{a}")
        return "(" + " & ".join(lits) + ")"

    def domain(self, name: str) -> str | None:
        """The constraint that an enum's bits hold a declared value, or None
        when every bit pattern is one."""
        var = self.vars[name]
        if var.values is None or len(var.values) == 2 ** self.bits(var):
            return None
        return "(" + " | ".join(self.value(name, v) for v in var.values) + ")"


# AST nodes are tuples: ("leaf", ltl), ("not", a), ("and"/"or", [..]),
# ("imp"/"iff", a, b), ("next", a), ("prev", a), ("G", a), ("GF", a), ("F", a).


class Parser:
    def __init__(self, text: str, enc: Encoding, defines: dict[str, list[str]]):
        self.toks = [WORD_OPS.get(t, t) for t in TOKEN.findall(text) if t.strip()]
        self.i = 0
        self.enc = enc
        self.defines = defines

    def peek(self, k: int = 0) -> str | None:
        j = self.i + k
        return self.toks[j] if j < len(self.toks) else None

    def take(self, expect: str | None = None) -> str:
        t = self.peek()
        if t is None or (expect is not None and t != expect):
            raise Untranslatable(f"expected {expect!r}, got {t!r}")
        self.i += 1
        return t

    def parse(self):
        node = self.temporal()
        if self.peek() not in (None, ";"):
            raise Untranslatable(f"trailing tokens from {self.peek()!r}")
        return node

    # G and GF scope over the whole remaining expression.
    def temporal(self):
        t = self.peek()
        if t == "GF":
            self.take()
            return ("GF", self.temporal())
        if t == "G":
            self.take()
            return ("G", self.temporal())
        return self.iff()

    def iff(self):
        left = self.imp()
        while self.peek() == "<->":
            self.take()
            left = ("iff", left, self.imp())
        return left

    def imp(self):
        left = self.disj()
        if self.peek() == "->":
            self.take()
            return ("imp", left, self.imp())
        return left

    def disj(self):
        parts = [self.conj()]
        while self.peek() == "|":
            self.take()
            parts.append(self.conj())
        return parts[0] if len(parts) == 1 else ("or", parts)

    def conj(self):
        parts = [self.unary()]
        while self.peek() == "&":
            self.take()
            parts.append(self.unary())
        return parts[0] if len(parts) == 1 else ("and", parts)

    def unary(self):
        t = self.peek()
        if t == "!":
            self.take()
            return ("not", self.unary())
        if t in ("G", "GF"):
            return self.temporal()
        return self.primary()

    def call_args(self) -> list:
        self.take("(")
        args = [self.temporal()]
        while self.peek() == ",":
            self.take()
            args.append(self.temporal())
        self.take(")")
        return args

    def primary(self):
        t = self.peek()
        if t in ("next",) + PAST:
            self.take()
            self.take("(")
            # next(v)=X compares a variable's next value.
            if (
                self.peek() in self.enc.vars
                and self.peek(1) == ")"
                and self.peek(2) in ("=", "!=")
            ):
                name = self.take()
                self.take(")")
                return self.compare_tail(name, "next" if t == "next" else "prev")
            inner = self.temporal()
            self.take(")")
            return ("next" if t == "next" else "prev", inner)
        if t in PATTERNS:
            self.take()
            args = self.call_args()
            if len(args) != PATTERNS[t]:
                raise Untranslatable(f"{t} takes {PATTERNS[t]} arguments")
            return ("G", ("imp", args[0], ("F", args[1])))
        if t == "(":
            self.take()
            inner = self.temporal()
            self.take(")")
            return inner
        if t in ("true", "TRUE"):
            self.take()
            return ("leaf", "true")
        if t in ("false", "FALSE"):
            self.take()
            return ("leaf", "false")
        if t in self.defines:
            self.take()
            return Parser(" ".join(self.defines[t]), self.enc, self.defines).parse()
        if t is not None and t in self.enc.vars:
            self.take()
            if self.peek() in ("=", "!="):
                return self.compare_tail(t)
            if self.enc.vars[t].values is not None:
                raise Untranslatable(f"enum {t} used as a boolean")
            return ("leaf", self.enc.atom(t))
        raise Untranslatable(f"unexpected token {t!r}")

    def compare_tail(self, name: str, mod: str | None = None):
        """`name` (under `mod`, one of None, next, prev) compared with the
        operand that follows: a value, a parenthesised value, a set of
        values, or another variable, possibly under next or a past
        operator."""

        def wrap(node, m):
            return node if m is None else (m, node)

        op = self.take()
        if op not in ("=", "!="):
            raise Untranslatable(f"expected a comparison, got {op!r}")
        rhs_var, rhs_mod, values = None, None, None
        t = self.peek()
        if t == "{":
            # The tools print `v!={}` for a variable a repair leaves free.
            self.take()
            values = []
            if self.peek() != "}":
                values.append(self.take())
            while self.peek() == ",":
                self.take()
                values.append(self.take())
            self.take("}")
        elif t in ("next",) + PAST and self.peek(2) in self.enc.vars:
            rhs_mod = "next" if self.take() == "next" else "prev"
            self.take("(")
            rhs_var = self.take()
            self.take(")")
        elif t == "(" and self.peek(2) == ")":
            self.take()
            values = [self.take()]
            self.take(")")
        elif t in self.enc.vars:
            rhs_var = self.take()
        else:
            values = [self.take()]
        if values == []:
            pos = ("leaf", "false")
        elif values is not None:
            pos = ("leaf", "(" + " | ".join(self.enc.value(name, v) for v in values) + ")")
            pos = wrap(pos, mod)
        else:
            assert rhs_var is not None
            lv, rv = self.enc.vars[name].values, self.enc.vars[rhs_var].values
            if (lv is None) != (rv is None):
                raise Untranslatable("boolean compared with an enum")
            common = ["true", "false"] if lv is None else [v for v in lv if v in rv]
            pos = ("or", [
                ("and", [
                    wrap(("leaf", self.enc.value(name, v)), mod),
                    wrap(("leaf", self.enc.value(rhs_var, v)), rhs_mod),
                ])
                for v in common
            ])
        return pos if op == "=" else ("not", pos)


def past_depth(node) -> int:
    kind = node[0]
    if kind == "leaf":
        return 0
    if kind == "prev":
        return 1 + past_depth(node[1])
    if kind == "next":
        return max(0, past_depth(node[1]) - 1)
    children = node[1] if kind in ("and", "or") else node[1:]
    return max((past_depth(c) for c in children), default=0)


def emit(node, k: int = 0, exact: bool = True) -> str:
    """LTL for `node` read k steps after a reference point. With `exact`, the
    reference point is time 0, so a past operator reaching before it is
    false; otherwise reaching before it is an error."""
    kind = node[0]
    if kind == "leaf":
        return node[1] if k == 0 or node[1] in ("true", "false") else "X " * k + node[1]
    if kind == "not":
        return f"!({emit(node[1], k, exact)})"
    if kind in ("and", "or"):
        op = " & " if kind == "and" else " | "
        return "(" + op.join(emit(c, k, exact) for c in node[1]) + ")"
    if kind in ("imp", "iff"):
        op = "->" if kind == "imp" else "<->"
        return f"(({emit(node[1], k, exact)}) {op} ({emit(node[2], k, exact)}))"
    if kind == "next":
        return emit(node[1], k + 1, exact)
    if kind == "prev":
        if k == 0:
            if exact:
                return "false"
            raise Untranslatable("past operator reaches before the reference")
        return emit(node[1], k - 1, exact)
    if kind in ("G", "GF", "F"):
        inner = node[1]
        d = past_depth(inner)
        if kind != "G" and d:
            raise Untranslatable(f"past operator under {kind}")
        prefix = "X " * k
        if kind == "F":
            return f"{prefix}F ({emit(inner)})"
        if kind == "GF":
            return f"{prefix}G F ({emit(inner)})"
        if d == 0:
            return f"{prefix}G ({emit(inner)})"
        if k:
            raise Untranslatable("past operator under a delayed G")
        # G p with past depth d: the first d instants exactly, then the rest
        # read d steps late, so that every past reference lands inside the word.
        heads = [emit(inner, j, True) for j in range(d)]
        tail = f"G ({emit(inner, d, False)})"
        return "(" + " & ".join(heads + [tail]) + ")"
    raise AssertionError(kind)


def translate(expr: str, enc: Encoding, defines: dict[str, list[str]] | None = None) -> str:
    return emit(Parser(expr, enc, defines or {}).parse())


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"(//|--)[^\n]*", "", text)


DECL = re.compile(r"\b(env|sys)\s+(boolean|\{[^}]*\})\s+([A-Za-z_][A-Za-z0-9_]*)\s*;")
DEFINE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*:=\s*([^;]*);")
STATEMENT = re.compile(
    r"\b(asm|assumption|gar|guarantee)\b(?:\s+[A-Za-z_][A-Za-z0-9_]*\s*:)?(.*?);", re.S
)


@dataclass
class Spec:
    enc: Encoding
    defines: dict[str, list[str]]
    statements: list[tuple[str, str]]  # (asm|gar, body)


def parse_spec(text: str, rename: dict[str, str] | None = None) -> Spec:
    body = strip_comments(text)
    # Pattern bodies hold their own statements; drop them.
    body = re.sub(r"\bpattern\b[^{]*\{(?:[^{}]|\{[^{}]*\})*\}", "", body)
    body = re.sub(r"\bimport\b[^\n]*", "", body)
    vars_ = {}
    for kind, ty, name in DECL.findall(body):
        values = None if ty == "boolean" else [v.strip() for v in ty.strip("{}").split(",")]
        vars_[name] = Var(name, values, kind == "env")
    enc = Encoding(vars_, rename or {})
    defines = {
        name: [WORD_OPS.get(t, t) for t in TOKEN.findall(expr) if t.strip()]
        for name, expr in DEFINE.findall(body)
    }
    body = DEFINE.sub("", body)
    body = re.sub(r"\bdefine\b", "", body)
    stmts = []
    for kind, expr in STATEMENT.findall(body):
        stmts.append(("asm" if kind in ("asm", "assumption") else "gar", expr.strip()))
    return Spec(enc, defines, stmts)


def always(node) -> bool:
    """Whether a statement holds at every instant rather than at the first:
    it carries G, GF or a pattern anywhere in it."""
    kind = node[0]
    if kind in ("G", "GF"):
        return True
    if kind == "leaf":
        return False
    children = node[1] if kind in ("and", "or") else node[1:]
    return any(always(c) for c in children)


def statement_ltl(body: str, spec: Spec) -> tuple[str, bool]:
    """A statement's LTL, and whether it is an initial condition."""
    node = Parser(body, spec.enc, spec.defines).parse()
    return emit(node), not always(node)


def assumption_section(body: str, spec: Spec) -> tuple[str, str]:
    """Where one added assumption goes in a TLSF file, and what it says there.

    A safety assumption `G phi` with no past operator goes to REQUIRE as phi,
    which the lowering wraps in G again. One with no temporal operator at all
    is an initial condition and goes to INITIALLY. Everything else, GF and the
    rare G over a past operator, goes to ASSUMPTIONS verbatim; under the
    non-strict lowering a safety assumption there means what it means in
    REQUIRE.
    """
    node = Parser(body, spec.enc, spec.defines).parse()
    if node[0] == "G" and past_depth(node[1]) == 0:
        return "REQUIRE", emit(node[1])
    if not always(node):
        return "INITIALLY", emit(node)
    return "ASSUMPTIONS", emit(node)
