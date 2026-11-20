#!/usr/bin/env python3
"""Convert Spectra specifications (SYNTECH17) into PEREDUR PSP specifications.

The input is a checkout of SpectraSynthesizer/spectra-specs. The importer reads
`SYNTECH17/all_realizable` and `SYNTECH17/all_unrealizable` (or the folders
given with `--folder`), and for each `.spectra` file builds an intermediate
specification: the atoms, and each assumption and guarantee as a PSP pattern
or a fixed raw-LTL entry. `--check` lowers each one to LTL and asks ltlsynt
whether it is realisable, so that the verdict can be compared with the folder
the file came from. The checkout is data: nothing in it is executed, and its
commit is read from `.git` without running git.

    python3 -I scripts/import_spectra.py CHECKOUT --check \\
        --manifest examples/psp-syntech/manifest.csv \\
        --psp-dir examples/psp-syntech/specs

The subset of Spectra read is what SYNTECH17 uses:

- `env`, `sys` and `aux` variables of type boolean, an enumeration, a bounded
  integer `Int(a..b)` or a `type` alias of one, with array dimensions. `env`
  variables are inputs; `sys`, `aux` and monitor variables are outputs.
- `define`s, which may be Boolean or integer valued, and `predicate`s. Both are
  expanded by name, so `next(m)` inside a predicate reads the argument's
  variable one step on.
- `asm`/`assumption` and `gar`/`guarantee`, each optionally named, as an
  initial constraint (no operator, or `ini`), a safety constraint (`G`, `alw`)
  or a justice constraint (`GF`, `alwEv`). Each becomes a fixed entry: the
  initial constraint at time 0, the safety one under `G`, the justice one under
  `G F`.
- `monitor`s: Spectra reads a monitor as neither an assumption nor a guarantee
  but as a value its constraints fix uniquely (user guide, 2021). The monitor
  is an output and its constraints become fixed guarantees, which is exact when
  they fix the value and leave the system no choice.
- `next(e)` and the past operator `PREV(e)` (also `Y`). A constraint holding
  PREV is shifted one step: `G phi` becomes `phi@0 & G(phi@1)`, where `phi@t`
  reads every variable `t` steps on and `PREV` at time 0 is false (Spectra's
  initial value of a PREV auxiliary).
- Arithmetic (`+ - * / mod`) and comparisons over bounded integers, evaluated
  by cases over the operands' values; a value outside a domain equals nothing,
  as in Spectra.
- `weight` declarations, which only matter to Spectra's quantitative
  synthesis, are dropped.
- Three of the 51 patterns of `SYNTECH17/patterns/DwyerPatterns.spectra`, the
  only three that SYNTECH17 calls. Each is a deterministic Buchi monitor in
  the library; its doc comment gives its LTL, and the monitor accepts exactly
  that LTL (checked with `autfilt --equivalent-to` when this importer was
  written):

  | Spectra call | PSP | LTL |
  |---|---|---|
  | `pRespondsToS(p, s)` | response, globally, P=p, S=s | `G(p -> F s)` |
  | `pHolds_afterQuntilR(p, q, r)` | universality, after Q until R, S=p | `G((q & !r) -> (p W r))` |
  | `pBecomesTrue_betweenQandR(p, q, r)` | existence, between Q and R, S=p | `G((q & !r & F r) -> (!r U (p & !r)))` |

  The name `pRespondsToS` reads backwards: s responds to p. Dwyer's single
  proposition is PSP's S in universality and existence, so `p` of the last two
  calls goes in S. The between monitor's doc comment gives the LTL
  `G((q & !r) -> (!r W (p & !r)))`, which is equivalent to the between row.
  A pattern call whose arguments use `next` is lowered to a fixed entry, since
  PSP's P, S, Q and R are propositional.

Unsupported, and the specification is excluded with the reason: `SINCE`,
`TRIGGERED`, `ONCE`, `HISTORICALLY`, `counter`, `trig`, `regexp`, local
`pattern` declarations, any other library pattern, `PREV` inside a pattern
argument, and any import other than the Dwyer library.

Enumerations and bounded integers are bit-blasted with a log encoding, the
code of a value being its index in the domain. When the domain size is not a
power of two, a fixed non-weakenable entry `G(<valid codes>)` restricts the
code: an assumption for an `env` variable and a guarantee otherwise. Atom
names are the variable's name in snake_case behind a prefix for its kind
(`e_` env, `s_` sys, `a_` aux, `m_` monitor), with `__i<k>` for array cell k
and `__b<j>` for code bit j. Every name is lowercase and starts with one of
those prefixes, so it passes the atom screen (`src/runner/atom_names.hpp`).

Patterns are weakenable; fixed entries never are. The validation lowers a
specification to `(A1 & ... & An) -> (G1 & ... & Gm)` and calls
`ltlsynt --realizability --decompose=no` with every input and output named.
`--decompose=no` avoids the Spot 2.15.1 bug (Spot issue #651) that answers
UNREALIZABLE for some realisable specifications with a `G F` assumption.
Spectra decides GR(1) under its own semantics, and the LTL implication can
differ from it in edge cases (the system may violate a guarantee once the
environment can no longer meet an assumption), so a specification whose
verdict differs from its folder is excluded, as is one that times out.
"""

import argparse
import concurrent.futures
import csv
import json
import math
import re
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_FOLDERS = ("SYNTECH17/all_realizable", "SYNTECH17/all_unrealizable")
# Spectra call -> (PSP pattern, scope, PSP field of each argument).
PATTERNS = {
    "pRespondsToS": ("response", "globally", ("p", "s")),
    "pHolds_afterQuntilR": ("universality", "afteruntil", ("s", "q", "r")),
    "pBecomesTrue_betweenQandR": ("existence", "between", ("s", "q", "r")),
}
EVENT_SCOPES = {"before", "after", "between", "afteruntil"}
LIBRARY_PATTERN_NAME = re.compile(r"^(pattern\d+|notP_globally|notP_beforeR|"
                                  r"pBecomesTrue_betweenQandR|"
                                  r"pHolds_afterQuntilR|pRespondsToS)$")
PREFIX = {"env": "e_", "sys": "s_", "aux": "a_", "monitor": "m_"}
CONSTRAINT_KINDS = {"G": "safety", "alw": "safety", "always": "safety",
                    "GF": "justice", "alwEv": "justice",
                    "alwaysEventually": "justice", "ini": "ini",
                    "initially": "ini"}
UNSUPPORTED_KEYWORDS = {"counter", "trig", "regexp", "pattern"}
PAST_UNSUPPORTED = {"SINCE": "SINCE", "S": "SINCE", "TRIGGERED": "TRIGGERED",
                    "T": "TRIGGERED"}
PAST_UNARY_UNSUPPORTED = {"ONCE": "ONCE", "O": "ONCE",
                          "HISTORICALLY": "HISTORICALLY", "H": "HISTORICALLY"}
MAX_PREV_DEPTH = 4


class SpectraError(Exception):
    """A file this importer cannot read."""


class Unsupported(SpectraError):
    """A file using a Spectra construct this importer does not lower."""


# ---------------------------------------------------------------- lexing


def strip_comments(text):
    """Remove `/* */`, `//` and `--` comments, keeping line breaks."""
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group().count("\n"),
                  text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r"--[^\n]*", "", text)


TOKEN = re.compile(r"\s*(?:(?P<num>\d+)|(?P<id>[A-Za-z_][A-Za-z0-9_]*)|"
                   r"(?P<str>\"[^\"]*\")|"
                   r"(?P<op><->|->|!=|<=|>=|:=|\.\.|[=<>!&|+\-*/%()\[\]{},;:]))")


def tokenize(text):
    tokens, pos, text = [], 0, strip_comments(text)
    while True:
        m = TOKEN.match(text, pos)
        if not m:
            rest = text[pos:].strip()
            if not rest:
                return tokens
            raise SpectraError(f"cannot read {rest[:20]!r}")
        pos = m.end()
        kind = m.lastgroup
        tokens.append((kind, m.group(kind)))


# ---------------------------------------------------------------- parsing
#
# Expression nodes are tuples: ("num", n), ("bool", b), ("id", name),
# ("index", base, idx), ("call", name, args), ("not", e), ("neg", e),
# ("next", e), ("prev", e), ("bin", op, a, b), and ("past", op, ...) for
# the past operators that are not lowered.


class Parser:
    def __init__(self, tokens):
        self.toks = tokens
        self.i = 0

    def peek(self, k=0):
        j = self.i + k
        return self.toks[j] if j < len(self.toks) else (None, None)

    def at(self, value, k=0):
        return self.peek(k)[1] == value

    def take(self, value=None, kind=None):
        tok = self.peek()
        if tok[0] is None:
            raise SpectraError("unexpected end of file")
        if (value is not None and tok[1] != value) or \
                (kind is not None and tok[0] != kind):
            raise SpectraError(f"expected {value or kind}, found {tok[1]!r}")
        self.i += 1
        return tok[1]

    def accept(self, value):
        if self.at(value):
            self.i += 1
            return True
        return False

    # Precedence, lowest first, as in Spectra's grammar: -> (right
    # associative), <->, | and xor, &, relations, mod, + -, * /, the binary
    # past operators, then the unary operators.
    def expr(self):
        left = self.iff()
        if self.accept("->") or self.accept("implies"):
            return ("bin", "->", left, self.expr())
        return left

    def iff(self):
        left = self.disj()
        while self.accept("<->") or self.accept("iff"):
            left = ("bin", "<->", left, self.disj())
        return left

    def disj(self):
        left = self.conj()
        while True:
            if self.accept("|") or self.accept("or"):
                left = ("bin", "|", left, self.conj())
            elif self.accept("xor"):
                left = ("bin", "xor", left, self.conj())
            else:
                return left

    def conj(self):
        left = self.rel()
        while self.accept("&") or self.accept("and"):
            left = ("bin", "&", left, self.rel())
        return left

    def rel(self):
        left = self.remainder()
        for op in ("=", "!=", "<=", ">=", "<", ">"):
            if self.accept(op):
                return ("bin", op, left, self.remainder())
        return left

    def remainder(self):
        left = self.additive()
        if self.accept("mod") or self.accept("%"):
            return ("bin", "mod", left, self.additive())
        return left

    def additive(self):
        left = self.mult()
        while self.at("+") or self.at("-"):
            op = self.take()
            left = ("bin", op, left, self.mult())
        return left

    def mult(self):
        left = self.past_binary()
        while self.at("*") or self.at("/"):
            op = self.take()
            left = ("bin", op, left, self.past_binary())
        return left

    def past_binary(self):
        left = self.unary()
        while self.peek()[1] in PAST_UNSUPPORTED and self.peek()[0] == "id":
            op = PAST_UNSUPPORTED[self.take()]
            left = ("past", op, left, self.unary())
        return left

    def unary(self):
        if self.accept("!") or self.accept("not"):
            return ("not", self.unary())
        if self.accept("-"):
            return ("neg", self.unary())
        if self.at("PREV") or self.at("Y"):
            self.take()
            return ("prev", self.unary())
        if self.peek()[1] in PAST_UNARY_UNSUPPORTED:
            return ("past", PAST_UNARY_UNSUPPORTED[self.take()], self.unary())
        return self.primary()

    def primary(self):
        kind, value = self.peek()
        if kind == "num":
            self.take()
            return ("num", int(value))
        if self.accept("("):
            e = self.expr()
            self.take(")")
            return e
        if kind != "id":
            raise SpectraError(f"unexpected {value!r} in an expression")
        self.take()
        if value in ("TRUE", "true"):
            return ("bool", True)
        if value in ("FALSE", "false"):
            return ("bool", False)
        if value == "next":
            self.take("(")
            e = self.expr()
            self.take(")")
            return ("next", e)
        if value == "regexp":
            raise Unsupported("regexp")
        if self.accept("("):
            args = []
            if not self.at(")"):
                args.append(self.expr())
                while self.accept(","):
                    args.append(self.expr())
            self.take(")")
            return ("call", value, tuple(args))
        node = ("id", value)
        while self.accept("["):
            node = ("index", node, self.expr())
            self.take("]")
        return node

    # Types: boolean, Int(a..b), {A, B}, or an alias; then [n] dimensions.
    def type_expr(self):
        if self.accept("boolean"):
            base = ("bool",)
        elif self.accept("Int"):
            self.take("(")
            lo = self.signed_int()
            self.take("..")
            hi = self.signed_int()
            self.take(")")
            base = ("int", lo, hi)
        elif self.accept("{"):
            values = [self.take(kind="id")]
            while self.accept(","):
                values.append(self.take(kind="id"))
            self.take("}")
            base = ("enum", tuple(values))
        else:
            base = ("alias", self.take(kind="id"))
        dims = []
        while self.accept("["):
            dims.append(int(self.take(kind="num")))
            self.take("]")
        return base, tuple(dims)

    def signed_int(self):
        neg = self.accept("-")
        n = int(self.take(kind="num"))
        return -n if neg else n


def parse(text):
    """The declarations and constraints of a Spectra file, as a dict."""
    p = Parser(tokenize(text))
    out = {"imports": [], "types": {}, "vars": [], "defines": {},
           "predicates": {}, "monitors": [], "constraints": [],
           "dropped": []}

    def constraint_body():
        kind_tok = p.peek()[1]
        if kind_tok == "trig":
            raise Unsupported("trig")
        if kind_tok == "G" and p.at("F", 1):
            p.i += 2
            kind = "justice"
        elif kind_tok in CONSTRAINT_KINDS and p.peek()[0] == "id":
            p.take()
            kind = CONSTRAINT_KINDS[kind_tok]
        else:
            kind = "ini"
        e = p.expr()
        p.take(";")
        return kind, e

    while p.peek()[0] is not None:
        kw = p.take()
        if kw == "import":
            out["imports"].append(p.take(kind="str").strip('"'))
        elif kw in ("module", "spec"):
            out["name"] = p.take(kind="id")
            p.accept(";")
        elif kw == "type":
            name = p.take(kind="id")
            p.take("=")
            out["types"][name] = p.type_expr()
            p.accept(";")
        elif kw in ("env", "sys", "aux"):
            p.accept("keep")
            typ = p.type_expr()
            name = p.take(kind="id")
            p.take(";")
            out["vars"].append((kw, name, typ))
        elif kw == "define":
            while p.peek()[0] == "id" and p.at(":=", 1):
                name = p.take()
                p.take(":=")
                out["defines"][name] = p.expr()
                p.take(";")
        elif kw == "predicate":
            name = p.take(kind="id")
            p.take("(")
            params = []
            while not p.at(")"):
                typ = p.type_expr()
                params.append((p.take(kind="id"), typ))
                p.accept(",")
            p.take(")")
            if p.accept("{"):
                body = p.expr()
                p.accept(";")
                p.take("}")
            else:
                p.take(":")
                body = p.expr()
                p.take(";")
            out["predicates"][name] = (tuple(n for n, _ in params), body)
        elif kw == "monitor":
            typ = p.type_expr()
            name = p.take(kind="id")
            p.take("{")
            cons = []
            while not p.accept("}"):
                cons.append(constraint_body())
            out["monitors"].append((name, typ, cons))
        elif kw == "weight":
            if p.peek()[0] == "id" and p.at(":", 1):
                p.i += 2
            p.accept("-")
            p.take(kind="num")
            p.expr()
            p.take(";")
            out["dropped"].append("weight")
        elif kw in ("asm", "assumption", "gar", "guarantee"):
            side = "assumption" if kw in ("asm", "assumption") \
                else "guarantee"
            name = None
            if p.peek()[0] == "id" and p.at(":", 1):
                name = p.take()
                p.take(":")
            kind, e = constraint_body()
            out["constraints"].append((side, name, kind, e))
        elif kw in UNSUPPORTED_KEYWORDS:
            raise Unsupported(kw)
        else:
            raise SpectraError(f"unexpected {kw!r} at the top level")
    return out


# ---------------------------------------------------------------- formulas
#
# Propositional formulas are hash-consed: an id indexes FORMULAS, whose
# entries are ("T",), ("F",), ("ap", name, k) for the atom name read k steps
# on (k nested X), ("not", a), ("and", ids), ("or", ids), ("iff", a, b) and
# ("imp", a, b).


class FormulaId(int):
    """A formula id, kept apart from integer constants."""


class Formulas:
    def __init__(self):
        self.nodes, self.index = [], {}
        self.T = self._make(("T",))
        self.F = self._make(("F",))

    def _make(self, key):
        found = self.index.get(key)
        if found is None:
            found = FormulaId(len(self.nodes))
            self.nodes.append(key)
            self.index[key] = found
        return found

    def ap(self, name, k=0):
        return self._make(("ap", name, k))

    def neg(self, a):
        node = self.nodes[a]
        if a == self.T:
            return self.F
        if a == self.F:
            return self.T
        if node[0] == "not":
            return node[1]
        return self._make(("not", a))

    def _nary(self, op, items):
        unit, zero = (self.T, self.F) if op == "and" else (self.F, self.T)
        flat, seen = [], set()
        for a in items:
            parts = self.nodes[a][1] if self.nodes[a][0] == op else (a,)
            for b in parts:
                if b == zero:
                    return zero
                if b == unit or b in seen:
                    continue
                seen.add(b)
                flat.append(b)
        for b in flat:
            if self.neg(b) in seen:
                return zero
        if not flat:
            return unit
        if len(flat) == 1:
            return flat[0]
        return self._make((op, tuple(flat)))

    def conj(self, items):
        return self._nary("and", items)

    def disj(self, items):
        return self._nary("or", items)

    def iff(self, a, b):
        if a == b:
            return self.T
        if a == self.T:
            return b
        if b == self.T:
            return a
        if a == self.F:
            return self.neg(b)
        if b == self.F:
            return self.neg(a)
        if self.neg(a) == b:
            return self.F
        return self._make(("iff", a, b))

    def imp(self, a, b):
        if a == self.F or b == self.T or a == b:
            return self.T
        if a == self.T:
            return b
        if b == self.F:
            return self.neg(a)
        return self._make(("imp", a, b))

    def is_temporal(self, a, memo=None):
        memo = {} if memo is None else memo
        if a in memo:
            return memo[a]
        node = self.nodes[a]
        if node[0] == "ap":
            out = node[2] > 0
        elif node[0] in ("T", "F"):
            out = False
        elif node[0] in ("and", "or"):
            out = any(self.is_temporal(b, memo) for b in node[1])
        else:
            out = any(self.is_temporal(b, memo) for b in node[1:])
        memo[a] = out
        return out

    def atoms(self, a, out=None, memo=None):
        out = set() if out is None else out
        memo = set() if memo is None else memo
        if a in memo:
            return out
        memo.add(a)
        node = self.nodes[a]
        if node[0] == "ap":
            out.add(node[1])
        elif node[0] in ("and", "or"):
            for b in node[1]:
                self.atoms(b, out, memo)
        elif node[0] in ("not", "iff", "imp"):
            for b in node[1:]:
                self.atoms(b, out, memo)
        return out

    # Spot syntax. Binding strength: ! > & > | > -> and <->, so a child
    # is parenthesised when it binds no tighter than its parent.
    RANK = {"T": 9, "F": 9, "ap": 9, "not": 8, "and": 6, "or": 5,
            "imp": 3, "iff": 3}

    def show(self, a):
        node = self.nodes[a]
        op = node[0]
        if op == "T":
            return "true"
        if op == "F":
            return "false"
        if op == "ap":
            return "X(" * node[2] + node[1] + ")" * node[2]

        def child(b, strict=True):
            text = self.show(b)
            r = self.RANK[self.nodes[b][0]]
            mine = self.RANK[op]
            return f"({text})" if r < mine or (strict and r == mine and
                                                op in ("imp", "iff")) \
                else text
        if op == "not":
            return "!" + child(node[1])
        if op == "and":
            return " & ".join(child(b) for b in node[1])
        if op == "or":
            return " | ".join(child(b) for b in node[1])
        sym = "->" if op == "imp" else "<->"
        return f"{child(node[1])} {sym} {child(node[2])}"


# ---------------------------------------------------------------- semantics


class EnumLit(str):
    """A bare enumeration value, before it meets the variable it is
    compared with."""


class Var:
    def __init__(self, kind, name, base, dims, atom_base):
        self.kind, self.name, self.base, self.dims = kind, name, base, dims
        self.cells = {}  # index tuple -> (atom names, domain values)
        values = domain_values(base)
        nbits = max(1, math.ceil(math.log2(len(values)))) \
            if base[0] != "bool" else 0
        for idx in cell_indices(dims):
            name_ = atom_base + "".join(f"__i{i}" for i in idx)
            if base[0] == "bool":
                self.cells[idx] = ([name_], None)
            else:
                self.cells[idx] = ([f"{name_}__b{j}" for j in range(nbits)],
                                   values)


def domain_values(base):
    if base[0] == "bool":
        return (False, True)
    if base[0] == "int":
        return tuple(range(base[1], base[2] + 1))
    return tuple(EnumLit(v) for v in base[1])


def cell_indices(dims):
    if not dims:
        return [()]
    return [(i,) + rest for i in range(dims[0]) for rest in
            cell_indices(dims[1:])]


def snake(name):
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name).lower()


class Lowering:
    """Evaluates parsed expressions to formulas over bit-blasted atoms."""

    def __init__(self, parsed):
        self.f = Formulas()
        self.parsed = parsed
        self.types = parsed["types"]
        self.defines = parsed["defines"]
        self.predicates = parsed["predicates"]
        self.vars = {}
        self.enum_values = set()
        self.prev_underflow = False
        self.define_cache = {}
        self.expanding = set()
        decls = list(parsed["vars"]) + [("monitor", n, t)
                                        for n, t, _ in parsed["monitors"]]
        seen_atoms = {}
        for kind, name, (base, dims) in decls:
            base, dims = self.resolve(base, dims)
            if base[0] == "enum":
                self.enum_values.update(base[1])
            if name in self.vars:
                raise SpectraError(f"variable {name} declared twice")
            var = Var(kind, name, base, dims, PREFIX[kind] + snake(name))
            for atoms, _ in var.cells.values():
                for atom in atoms:
                    if atom in seen_atoms:
                        raise SpectraError(f"atom {atom} names both "
                                           f"{seen_atoms[atom]} and {name}")
                    seen_atoms[atom] = name
            self.vars[name] = var
        for base, _ in self.types.values():
            if base[0] == "enum":
                self.enum_values.update(base[1])

    def resolve(self, base, dims):
        seen = set()
        while base[0] == "alias":
            if base[1] not in self.types or base[1] in seen:
                raise SpectraError(f"unknown type {base[1]}")
            seen.add(base[1])
            inner, inner_dims = self.types[base[1]]
            base, dims = inner, inner_dims + dims
        return base, dims

    # A value is a formula id (Boolean), a Python int, an EnumLit, or a
    # dict from domain values to formula ids (one case per value, the cases
    # disjoint), and a list for an array.

    def cell_value(self, var, idx, k):
        atoms, values = var.cells[idx]
        if values is None:
            return self.f.ap(atoms[0], k)
        bits = [self.f.ap(a, k) for a in atoms]
        if isinstance(values[0], EnumLit):
            return {v: code_eq(self.f, bits, code)
                    for code, v in enumerate(values)}
        return BV(bits, values[0], len(values))

    def domain_constraint(self, var):
        """The formula restricting every cell's code to the domain, or None
        when every code is a value."""
        parts = []
        for idx, (atoms, values) in var.cells.items():
            if values is None or len(values) == 2 ** len(atoms):
                continue
            bits = [self.f.ap(a) for a in atoms]
            parts.append(code_lt(self.f, bits, len(values)))
        return self.f.conj(parts) if parts else None

    def eval(self, node, k, env):
        op = node[0]
        if op == "num":
            return node[1]
        if op == "bool":
            return self.f.T if node[1] else self.f.F
        if op == "id":
            return self.lookup(node[1], k, env)
        if op == "index":
            return self.index(node, k, env)
        if op == "call":
            return self.call(node, k, env)
        if op == "not":
            return self.f.neg(self.boolean(self.eval(node[1], k, env)))
        if op == "neg":
            return self.arith("-", 0, self.eval(node[1], k, env))
        if op == "next":
            return self.eval(node[1], k + 1, env)
        if op == "prev":
            if k == 0:
                self.prev_underflow = True
                return self.f.F
            return self.boolean(self.eval(node[1], k - 1, env))
        if op == "past":
            raise Unsupported(node[1])
        if op == "bin":
            return self.binary(node[1], self.eval(node[2], k, env),
                               self.eval(node[3], k, env))
        raise SpectraError(f"cannot evaluate {op}")

    def lookup(self, name, k, env):
        if name in env:
            arg, arg_env = env[name]
            return self.eval(arg, k, arg_env)
        if name in self.defines:
            key = (name, k)
            if key not in self.define_cache:
                if name in self.expanding:
                    raise SpectraError(f"define {name} is recursive")
                self.expanding.add(name)
                try:
                    self.define_cache[key] = self.eval(self.defines[name], k,
                                                       {})
                finally:
                    self.expanding.discard(name)
            return self.define_cache[key]
        if name in self.vars:
            var = self.vars[name]
            if var.dims:
                return [self.cell_value(var, idx, k) for idx in
                        cell_indices(var.dims)] if len(var.dims) == 1 \
                    else ArrayRef(self, var, (), k)
            return self.cell_value(var, (), k)
        if name in self.enum_values:
            return EnumLit(name)
        if name in PATTERNS or LIBRARY_PATTERN_NAME.match(name):
            raise SpectraError(f"pattern {name} inside an expression")
        raise SpectraError(f"unknown name {name}")

    def index(self, node, k, env):
        base = self.eval(node[1], k, env)
        idx = self.eval(node[2], k, env)
        if isinstance(base, ArrayRef):
            return base.at(idx)
        if not isinstance(base, list):
            raise SpectraError("indexing something that is not an array")
        return select(self, base, idx)

    def call(self, node, k, env):
        name, args = node[1], node[2]
        if name not in self.predicates:
            if name in PATTERNS or LIBRARY_PATTERN_NAME.match(name):
                raise SpectraError(f"pattern {name} inside an expression")
            raise SpectraError(f"unknown predicate {name}")
        params, body = self.predicates[name]
        if len(params) != len(args):
            raise SpectraError(f"{name} takes {len(params)} arguments")
        inner = {p: (a, env) for p, a in zip(params, args)}
        return self.boolean(self.eval(body, k, inner))

    def boolean(self, v):
        if not is_formula(v):
            raise SpectraError("a value that is not Boolean where a "
                               "Boolean is needed")
        return v

    def cases(self, v, limit=None):
        """A value as disjoint cases, one per value it can take."""
        if isinstance(v, dict):
            return v
        if isinstance(v, EnumLit) or is_const(v):
            return {v: self.f.T}
        if isinstance(v, BV):
            if limit is not None and v.n > limit:
                raise Unsupported("arithmetic over domains too large to "
                                  "enumerate")
            return {v.off + i: code_eq(self.f, v.bits, i)
                    for i in range(v.n)}
        raise SpectraError("a Boolean where a value is needed")

    def compare(self, op, a, b):
        """a op b over integers or enumerations, through the bit-vector
        comparators where one side is a variable."""
        f = self.f
        if isinstance(b, BV) and not isinstance(a, BV):
            a, b, op = b, a, FLIP[op]
        if isinstance(a, BV):
            if isinstance(b, BV) and b.off == a.off:
                return bv_compare(f, op, a.bits, b.bits)
            cb = self.cases(b, CASE_LIMIT)
            return f.disj([f.conj([g, const_compare(f, op, a, v)])
                           for v, g in cb.items()])
        ca, cb = self.cases(a), self.cases(b)
        if len(ca) * len(cb) > CASE_LIMIT ** 2:
            raise Unsupported("a comparison over domains too large to "
                              "enumerate")
        return f.disj([f.conj([ca[x], cb[y]]) for x in ca for y in cb
                       if TESTS[op](x, y)])

    def binary(self, op, a, b):
        f = self.f
        if op in ("&", "|", "->", "<->", "xor"):
            a, b = self.boolean(a), self.boolean(b)
            if op == "&":
                return f.conj([a, b])
            if op == "|":
                return f.disj([a, b])
            if op == "->":
                return f.imp(a, b)
            if op == "<->":
                return f.iff(a, b)
            return f.neg(f.iff(a, b))
        if op in ("=", "!=") and is_formula(a) and is_formula(b):
            eq = f.iff(a, b)
            return eq if op == "=" else f.neg(eq)
        if is_formula(a) or is_formula(b):
            raise SpectraError(f"a Boolean operand of {op}")
        if op in TESTS:
            return self.compare(op, a, b)
        return self.arith(op, a, b)

    def arith(self, op, a, b):
        if is_const(a) and is_const(b):
            return int_op(op, a, b)
        if op in ("+", "-") and isinstance(a, BV) and is_const(b):
            return BV(a.bits, a.off + (b if op == "+" else -b), a.n)
        if op == "+" and isinstance(b, BV) and is_const(a):
            return BV(b.bits, b.off + a, b.n)
        ca, cb = self.cases(a, CASE_LIMIT), self.cases(b, CASE_LIMIT)
        groups = {}
        for x, fx in ca.items():
            for y, fy in cb.items():
                v = int_op(op, x, y)
                if v is not None:
                    groups.setdefault(v, []).append(self.f.conj([fx, fy]))
        return {v: self.f.disj(fs) for v, fs in groups.items()}


CASE_LIMIT = 256
TESTS = {"=": lambda x, y: x == y, "!=": lambda x, y: x != y,
         "<": lambda x, y: x < y, "<=": lambda x, y: x <= y,
         ">": lambda x, y: x > y, ">=": lambda x, y: x >= y}
FLIP = {"=": "=", "!=": "!=", "<": ">", "<=": ">=", ">": "<", ">=": "<="}


class BV:
    """A bounded integer: off plus the unsigned code in bits (least
    significant first), the code ranging over 0..n-1."""

    def __init__(self, bits, off, n):
        self.bits, self.off, self.n = bits, off, n


def code_eq(f, bits, code):
    """The code in bits equals code."""
    if not 0 <= code < 2 ** len(bits):
        return f.F
    return f.conj([b if (code >> j) & 1 else f.neg(b)
                   for j, b in enumerate(bits)])


def code_lt(f, bits, bound):
    """The code in bits is below bound, compared from the top bit down."""
    if bound <= 0:
        return f.F
    if bound >= 2 ** len(bits):
        return f.T
    out = f.F
    for j, b in enumerate(bits):
        # out: the code's low j bits are below bound's low j bits.
        out = f.disj([f.neg(b), out]) if (bound >> j) & 1 else \
            f.conj([f.neg(b), out])
    return out


def const_compare(f, op, bv, value):
    """bv op value, value an integer."""
    if isinstance(value, EnumLit):
        raise SpectraError("an integer compared with an enumeration value")
    k = value - bv.off
    if op == "=":
        return code_eq(f, bv.bits, k)
    if op == "!=":
        return f.neg(code_eq(f, bv.bits, k))
    if op == "<":
        return code_lt(f, bv.bits, k)
    if op == "<=":
        return code_lt(f, bv.bits, k + 1)
    if op == ">":
        return f.neg(code_lt(f, bv.bits, k + 1))
    return f.neg(code_lt(f, bv.bits, k))


def bv_compare(f, op, a, b):
    """a op b for two codes, padded with false to one width."""
    width = max(len(a), len(b))
    a = list(a) + [f.F] * (width - len(a))
    b = list(b) + [f.F] * (width - len(b))
    if op in ("=", "!="):
        eq = f.conj([f.iff(x, y) for x, y in zip(a, b)])
        return eq if op == "=" else f.neg(eq)
    if op in (">", ">="):
        a, b, op = b, a, FLIP[op]
    out = f.F if op == "<" else f.T
    for x, y in zip(a, b):
        # out: a's low bits are below (or at most) b's low bits.
        out = f.disj([f.conj([f.neg(x), y]),
                      f.conj([f.iff(x, y), out])])
    return out


class ArrayRef:
    """A multi-dimensional array, indexed one dimension at a time."""

    def __init__(self, low, var, prefix, k):
        self.low, self.var, self.prefix, self.k = low, var, prefix, k

    def at(self, idx):
        depth = len(self.prefix) + 1
        size = self.var.dims[len(self.prefix)]
        if depth == len(self.var.dims):
            cells = [self.low.cell_value(self.var, self.prefix + (i,), self.k)
                     for i in range(size)]
        else:
            cells = [ArrayRef(self.low, self.var, self.prefix + (i,), self.k)
                     for i in range(size)]
        return select(self.low, cells, idx)


def select(low, cells, idx):
    if is_const(idx):
        if not 0 <= idx < len(cells):
            raise SpectraError(f"index {idx} out of bounds")
        return cells[idx]
    cases = low.cases(idx, CASE_LIMIT)
    picks = [(cases[i], cells[i]) for i in range(len(cells)) if i in cases]
    f = low.f
    if any(isinstance(c, BV) for _, c in picks):
        picks = [(g, low.cases(c, CASE_LIMIT)) for g, c in picks]
    if all(is_formula(c) for _, c in picks):
        return f.disj([f.conj([g, c]) for g, c in picks])
    if all(isinstance(c, dict) for _, c in picks):
        out = {}
        for g, c in picks:
            for v, fv in c.items():
                out.setdefault(v, []).append(f.conj([g, fv]))
        return {v: f.disj(fs) for v, fs in out.items()}
    raise SpectraError("an array of arrays indexed by a variable")


def is_formula(v):
    return type(v) is FormulaId


def is_const(v):
    return type(v) is int


def int_op(op, x, y):
    if isinstance(x, EnumLit) or isinstance(y, EnumLit):
        raise SpectraError("arithmetic on an enumeration value")
    if op == "+":
        return x + y
    if op == "-":
        return x - y
    if op == "*":
        return x * y
    if op == "/":
        return None if y == 0 else x // y
    if op == "mod":
        return None if y == 0 else x % y
    raise SpectraError(f"unknown operator {op}")


# ---------------------------------------------------------------- import


def pattern_ltl(pattern, fields):
    """The LTL of one of the three library patterns, in Spot syntax."""
    g = {k: f"({v})" for k, v in fields.items()}
    if pattern == "response":
        return f"G({g['p']} -> F{g['s']})"
    if pattern == "universality":
        return f"G(({g['q']} & !{g['r']}) -> ({g['s']} W {g['r']}))"
    if pattern == "existence":
        return (f"G(({g['q']} & !{g['r']} & F{g['r']}) -> "
                f"(!{g['r']} U ({g['s']} & !{g['r']})))")
    raise ValueError(pattern)


def lower_constraint(low, kind, expr):
    """The LTL of an initial, safety or justice constraint, with PREV
    shifted away."""
    f = low.f
    if kind == "ini":
        return f.show(low.boolean(low.eval(expr, 0, {})))
    for depth in range(MAX_PREV_DEPTH + 1):
        low.prev_underflow = False
        body = low.boolean(low.eval(expr, depth, {}))
        if not low.prev_underflow:
            break
    else:
        raise Unsupported(f"PREV nested deeper than {MAX_PREV_DEPTH}")
    if kind == "justice":
        return f"G(F({f.show(body)}))" if body != f.T else "true"
    prefix = [low.boolean(low.eval(expr, t, {})) for t in range(depth)]
    parts = [f.show(p) for p in prefix if p != f.T]
    if body != f.T:
        parts.append(f"G({f.show(body)})")
    return " & ".join(f"({p})" for p in parts) if len(parts) > 1 else \
        (parts[0] if parts else "true")


def import_spec(text, source="", commit=""):
    """The intermediate specification for one Spectra file."""
    parsed = parse(text)
    for imp in parsed["imports"]:
        if not imp.endswith("DwyerPatterns.spectra"):
            raise Unsupported(f"import {imp}")
    low = Lowering(parsed)
    f = low.f
    spec = {"source": source, "commit": commit, "in_atoms": [],
            "out_atoms": [], "assumptions": [], "guarantees": []}
    for var in low.vars.values():
        side = "in_atoms" if var.kind == "env" else "out_atoms"
        for atoms, _ in var.cells.values():
            spec[side].extend(atoms)
        dom = low.domain_constraint(var)
        if dom is not None:
            entry = {"fixed": f"G({f.show(dom)})", "name": f"domain:{var.name}",
                     "kind": "domain", "weakenable": False}
            spec["assumptions" if var.kind == "env" else "guarantees"] \
                .append(entry)
    for name, _, cons in parsed["monitors"]:
        for kind, expr in cons:
            if kind == "justice":
                raise SpectraError(f"justice in monitor {name}")
            spec["guarantees"].append({
                "fixed": lower_constraint(low, kind, expr),
                "name": f"monitor:{name}", "kind": "monitor",
                "weakenable": False})
    for side, name, kind, expr in parsed["constraints"]:
        target = spec["assumptions" if side == "assumption" else "guarantees"]
        # `G p(...)` occurs on pattern calls in SYNTECH17 and Spectra
        # accepts it; G of a G-formula is the formula itself.
        if kind in ("ini", "safety") and expr[0] == "call" and \
                (expr[1] in PATTERNS or LIBRARY_PATTERN_NAME.match(expr[1])):
            target.append(import_pattern(low, name, expr))
            continue
        ltl = lower_constraint(low, kind, expr)
        if ltl == "true":
            continue
        target.append({"fixed": ltl, "name": name, "kind": kind,
                       "weakenable": False})
    return spec


def import_pattern(low, name, expr):
    call, args = expr[1], expr[2]
    if call not in PATTERNS:
        raise Unsupported(f"library pattern {call}")
    pattern, scope, fields = PATTERNS[call]
    if len(args) != len(fields):
        raise SpectraError(f"{call} takes {len(fields)} arguments")
    f = low.f
    values = {}
    low.prev_underflow = False
    temporal = False
    for field, arg in zip(fields, args):
        v = low.boolean(low.eval(arg, 0, {}))
        if low.prev_underflow:
            raise Unsupported("PREV inside a pattern argument")
        temporal = temporal or f.is_temporal(v)
        values[field] = f.show(v)
    if temporal:
        return {"fixed": pattern_ltl(pattern, values), "name": name,
                "kind": "pattern-temporal-argument", "source_pattern": call,
                "weakenable": False}
    entry = {"pattern": pattern, "scope": scope}
    entry.update(values)
    entry.update({"name": name, "weakenable": True})
    return entry


def entry_ltl(entry):
    if "fixed" in entry:
        return entry["fixed"]
    return pattern_ltl(entry["pattern"],
                       {k: entry[k] for k in "psqr" if k in entry})


def spec_ltl(spec):
    """The specification as one LTL formula: assumptions imply guarantees."""
    g = " & ".join(f"({entry_ltl(e)})" for e in spec["guarantees"]) or "true"
    if not spec["assumptions"]:
        return g
    a = " & ".join(f"({entry_ltl(e)})" for e in spec["assumptions"])
    return f"({a}) -> ({g})"


def to_psp(spec):
    """The `.psp` document (schemas/psp-specification.schema.json)."""
    def req(e):
        if "fixed" in e:
            return {"fixed": e["fixed"]}
        out = {"pattern": e["pattern"], "scope": e["scope"]}
        out.update({k: e[k] for k in "psqr" if k in e})
        if not e.get("weakenable", True):
            out["weakenable"] = False
        return out
    return {"assumptions": [req(e) for e in spec["assumptions"]],
            "guarantees": [req(e) for e in spec["guarantees"]],
            "in_atoms": spec["in_atoms"], "out_atoms": spec["out_atoms"]}


def slot_counts(spec):
    """Weakenable, fixed and event-delimited slots, per side and in all."""
    out = {}
    for side in ("assumptions", "guarantees"):
        es = spec[side]
        out[f"{side}_weakenable"] = sum(1 for e in es if e["weakenable"])
        out[f"{side}_fixed"] = sum(1 for e in es if not e["weakenable"])
        out[f"{side}_event_scoped"] = sum(
            1 for e in es if e.get("scope") in EVENT_SCOPES)
    return out


# ---------------------------------------------------------------- checking


def realizable(spec, ltlsynt, timeout):
    """True, False, or the string 'timeout' or 'error: ...'."""
    cmd = [ltlsynt, "--realizability", "--decompose=no", "-F", "-"]
    if spec["in_atoms"]:
        cmd.append("--ins=" + ",".join(spec["in_atoms"]))
    if spec["out_atoms"]:
        cmd.append("--outs=" + ",".join(spec["out_atoms"]))
    try:
        res = subprocess.run(cmd, input=spec_ltl(spec), capture_output=True,
                             text=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        return "timeout"
    first = res.stdout.strip().splitlines()[:1]
    if first == ["REALIZABLE"]:
        return True
    if first == ["UNREALIZABLE"]:
        return False
    return "error: " + (res.stderr.strip().splitlines() or ["?"])[0][:120]


def read_commit(checkout):
    """The checked-out commit, read from `.git` without running git."""
    git = Path(checkout) / ".git"
    head = (git / "HEAD").read_text().strip()
    if not head.startswith("ref: "):
        return head
    ref = head[5:]
    loose = git / ref
    if loose.is_file():
        return loose.read_text().strip()
    packed = git / "packed-refs"
    if packed.is_file():
        for line in packed.read_text().splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[1] == ref:
                return parts[0]
    raise SpectraError(f"cannot resolve {ref} in {git}")


MANIFEST_FIELDS = [
    "spec", "folder", "expected", "status", "reason", "verdict",
    "verdict_match", "ltlsynt_seconds", "in_atoms", "out_atoms",
    "assumptions_weakenable", "assumptions_fixed",
    "assumptions_event_scoped", "guarantees_weakenable", "guarantees_fixed",
    "guarantees_event_scoped", "weakenable", "event_scoped",
    "eval_candidate"]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("checkout", help="spectra-specs checkout")
    ap.add_argument("--folder", action="append",
                    help="folder under the checkout (default: SYNTECH17 "
                         "all_realizable and all_unrealizable)")
    ap.add_argument("--commit", help="commit to record (default: read .git)")
    ap.add_argument("--intermediate-dir", type=Path,
                    help="write each intermediate specification here")
    ap.add_argument("--psp-dir", type=Path,
                    help="write each imported specification's .psp here")
    ap.add_argument("--manifest", type=Path, help="write the CSV report")
    ap.add_argument("--check", action="store_true",
                    help="check each verdict with ltlsynt")
    ap.add_argument("--ltlsynt", default="ltlsynt")
    ap.add_argument("--timeout", type=float, default=300.0)
    ap.add_argument("--jobs", type=int, default=4)
    args = ap.parse_args(argv)

    checkout = Path(args.checkout)
    commit = args.commit or read_commit(checkout)
    rows, specs = [], {}
    for folder in args.folder or DEFAULT_FOLDERS:
        expected = "unrealizable" if "unrealizable" in folder \
            else "realizable"
        for path in sorted((checkout / folder).glob("*.spectra")):
            stem = path.stem
            row = {"spec": stem, "folder": folder, "expected": expected,
                   "status": "imported", "reason": ""}
            try:
                spec = import_spec(path.read_text(errors="replace"),
                                   f"{folder}/{path.name}", commit)
            except Unsupported as exc:
                row.update(status="excluded", reason=f"unsupported: {exc}")
            except SpectraError as exc:
                row.update(status="excluded", reason=f"parse: {exc}")
            else:
                specs[stem] = spec
                counts = slot_counts(spec)
                row.update(counts)
                row.update(in_atoms=len(spec["in_atoms"]),
                           out_atoms=len(spec["out_atoms"]),
                           weakenable=counts["assumptions_weakenable"] +
                           counts["guarantees_weakenable"],
                           event_scoped=counts["assumptions_event_scoped"] +
                           counts["guarantees_event_scoped"])
            rows.append(row)

    if args.check:
        def job(stem):
            start = time.monotonic()
            verdict = realizable(specs[stem], args.ltlsynt, args.timeout)
            return stem, verdict, time.monotonic() - start
        by_stem = {r["spec"]: r for r in rows}
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            for stem, verdict, secs in pool.map(job, sorted(specs)):
                row = by_stem[stem]
                row["ltlsynt_seconds"] = f"{secs:.1f}"
                if verdict is True or verdict is False:
                    row["verdict"] = "realizable" if verdict \
                        else "unrealizable"
                    row["verdict_match"] = row["verdict"] == row["expected"]
                    if not row["verdict_match"]:
                        row.update(status="excluded",
                                   reason="verdict differs from Spectra")
                else:
                    row.update(status="excluded", reason=f"ltlsynt {verdict}",
                               verdict=verdict.split(":")[0])
                print(f"{stem}: {row.get('verdict')} ({secs:.1f} s)",
                      file=sys.stderr, flush=True)

    for row in rows:
        if row["status"] == "imported" and "guarantees_weakenable" in row:
            row["eval_candidate"] = (row["expected"] == "unrealizable" and
                                     row["guarantees_weakenable"] >= 2)

    for stem, spec in specs.items():
        if args.intermediate_dir:
            args.intermediate_dir.mkdir(parents=True, exist_ok=True)
            (args.intermediate_dir / f"{stem}.json").write_text(
                json.dumps(spec, indent=1) + "\n")
        row = next(r for r in rows if r["spec"] == stem)
        if args.psp_dir and row["status"] == "imported":
            args.psp_dir.mkdir(parents=True, exist_ok=True)
            (args.psp_dir / f"{stem}.psp").write_text(
                json.dumps(to_psp(spec), indent=1) + "\n")

    if args.manifest:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        with args.manifest.open("w", newline="") as fh:
            w = csv.DictWriter(fh, MANIFEST_FIELDS, lineterminator="\n")
            w.writeheader()
            for row in rows:
                w.writerow({k: row.get(k, "") for k in MANIFEST_FIELDS})
    imported = sum(1 for r in rows if r["status"] == "imported")
    print(f"commit {commit}: {imported} imported, {len(rows) - imported} "
          f"excluded", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
