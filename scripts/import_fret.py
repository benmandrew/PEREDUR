#!/usr/bin/env python3
"""Convert FRET requirement exports into a PEREDUR FRETISH specification.

A FRET export is the JSON file FRET writes for a project, holding
`requirements` (each with the formaliser's parsed `semantics`) and
`variables` (each with an `idType` of Input, Output or blank). Older FRET
versions write a bare list of requirements with no variables. Several exports
of one system merge into one specification:

    python3 scripts/import_fret.py FRET_files/*.json -o examples/x/spec.json

The conversion reads FRET's parsed fields rather than its fulltext, and each
rule below matches the formula FRET itself emits (`semantics.ft`):

- `regular` conditions become trigger, `holding` (whenever) continual, and a
  missing condition a trigger on `true`, an obligation at time 0 only.
- A missing timing becomes Eventually, FRET's default (`!LAST U r`).
- Probability bounds are dropped, as FRET's LTL formula drops them.
- A top-level conjunct `Nxt(p)` of an immediately response moves into a
  second requirement with NextTimepoint timing.
- A numeric comparison `a >= 0.9` becomes the boolean atom `a_ge_0p9`. With
  `--atomise-arithmetic`, so does one over arithmetic or functions:
  `abs(x) < y + 1` becomes `abs_x_lt_y_plus_1`.
- `a = b` is iff when either side is boolean: a formula, a literal, or a name
  used bare somewhere in the exports. Otherwise it is a comparison atom.
- `=>` becomes `->`, and `TRUE`/`FALSE` become `true`/`false`.
- Names become snake_case, since SPOT mishandles uppercase inputs, and `%`
  becomes `_pct`: `measureO2%` is `measure_o2_pct`.
- A scope keeps its kind and mode. A formula mode such as `in (a | b)` gets
  a fresh output atom, `mode_a_or_b`, defined by a non-weakenable guarantee
  `G(mode_a_or_b <-> (a | b))`. The definition is exact: the system sees each
  step's inputs before it chooses its outputs.
- A mode that a requirement also mentions, such as one the controller sets,
  is declared as an atom too and takes that atom's side. A mode only scopes
  name is a pure mode, which PEREDUR reads as an input, unless an Output
  label or `--as-output` makes it an output.
- FRET keeps mode exclusivity outside the export. `--exclusive a,b,c` adds
  that at most one of them holds at each step: an assumption when they are
  inputs or pure modes, a guarantee when they are outputs.
- Durations are ticks: FRET discards the unit of `within 5 minutes`.
- Requirements identical after conversion are kept once.
- Comparison atoms are otherwise free, so a boolean spec could set
  `x_ge_5 & !x_ge_3`. For each group of comparisons sharing variables, one
  non-weakenable Always requirement admits exactly the truth vectors some
  value realises, integer-valued where FRET's `dataType` says so: an
  assumption when its atoms are inputs, a guarantee when outputs.
  `--no-domains` leaves them out. Comparisons sharing variables are
  constrained together; where one compares two names or carries arithmetic
  (`--atomise-arithmetic`), z3 decides which vectors some values realise,
  and a group it cannot decide is rejected.

FRET's parser silently drops the `= X` of `(a | b) = X`, so a name in the
fulltext's response that the parsed response lacks is rejected;
`--from-fulltext REQID` parses that requirement's response from its fulltext
instead. Names colliding after snake_case are rejected, unless they differ
only in case and `--merge-case` is given. A requirement with empty fulltext is
a placeholder and is skipped; any other one FRET did not formalise is rejected
unless named by `--skip`.

An atom FRET labels exactly once keeps its label, and a comparison atom takes
its variable's. A comparison over several variables takes their side and never
the role rule's: each needs one label, or `--as-input`/`--as-output` naming
it, and a comparison of an input with an output is rejected. An unlabelled
atom, or one labelled both ways across exports, becomes an output when it
appears in a response or a stop condition and an input otherwise.
`--as-input`/`--as-output` override both, and take shell-style patterns;
naming a variable also sides every comparison of it. Every requirement becomes
a guarantee; FRET has no assumptions.

The summary on stderr lists every rule that fired, so a reviewer can check
each lossy step. `--ids` writes which FRET reqids each guarantee came from.
"""

import argparse
import collections
import fnmatch
import itertools
import json
import math
import re
import sys
from fractions import Fraction
from pathlib import Path

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
TOKEN = re.compile(r"(\s*)(<->|<=>|->|=>|>=|<=|!=|==|[=<>!~&|()+\-*/^,]|"
                   r"\d+(?:\.\d+)?|[A-Za-z_][\w%]*)")
LITERALS = {"true", "false"}
SPELLINGS = {"=>": "->", "<=>": "<->"}
RELATIONS = {">=": "ge", "<=": "le", ">": "gt", "<": "lt",
             "=": "eq", "==": "eq", "!=": "ne"}
ARITH_NAMES = {"+": "plus", "-": "minus", "*": "times", "/": "div", "^": "pow"}
FUNCTIONS = {"abs", "absReal", "min", "max"}
NXT = re.compile(r"Nxt\s*\((.*)\)")
TEMPORAL = re.compile(r"\b(xor|persisted|occurred|preBool|prevOcc|nextOcc|"
                      r"FTP|FTF)\b")
ARITHMETIC = re.compile(r"\b(absReal|abs|max|min)\b|[+*/^]|-(?!>)")
SCOPES = {"null": None, "in": "In", "notin": "NotIn", "before": "Before",
          "after": "After", "onlyIn": "OnlyIn", "onlyBefore": "OnlyBefore",
          "onlyAfter": "OnlyAfter"}
TICKED = {"within": "WithinTicks", "for": "ForTicks", "after": "AfterTicks"}
PLAIN = {"immediately": "Immediately", "next": "NextTimepoint",
         "eventually": "Eventually", "null": "Eventually",
         "always": "Always", "never": "Never"}
STOPPED = {"until": "Until", "before": "Before"}
FULLTEXT_WORDS = {"if", "then", "IF", "THEN", "TRUE", "FALSE"} | LITERALS


class FretImportError(Exception):
    pass


def snake(name):
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_",
                  name).lower().replace("%", "_pct")


def split_top(expr, op):
    """Split `expr` on `op` at parenthesis depth zero."""
    parts, depth, start = [], 0, 0
    for i, c in enumerate(expr):
        depth += (c == "(") - (c == ")")
        if depth == 0 and c == op:
            parts.append(expr[start:i].strip())
            start = i + 1
    parts.append(expr[start:].strip())
    return parts


def strip_parens(expr):
    while expr.startswith("(") and expr.endswith(")"):
        depth = 0
        for i, c in enumerate(expr):
            depth += (c == "(") - (c == ")")
            if depth == 0 and i < len(expr) - 1:
                return expr
        expr = expr[1:-1].strip()
    return expr


class Node:
    """A parsed subexpression spanning tokens [lo, hi)."""

    def __init__(self, kind, lo, hi, kids=(), text=None):
        self.kind, self.lo, self.hi = kind, lo, hi
        self.kids, self.text = list(kids), text


class Parser:
    """FRET's expression syntax, loosest first: iff, implication (right
    associative), or, and, not, relation, sum, product, power, negation."""

    def __init__(self, expr, reqid):
        self.expr, self.reqid, self.toks, pos = expr, reqid, [], 0
        while pos < len(expr):
            m = TOKEN.match(expr, pos)
            if not m:
                if expr[pos:].strip():
                    self.fail(f"cannot read {expr[pos:]!r}")
                break
            self.toks.append((m.group(2), bool(m.group(1))))
            pos = m.end()
        self.i = 0

    def fail(self, why):
        raise FretImportError(f"{self.reqid}: {why} in {self.expr!r}")

    def peek(self):
        return self.toks[self.i][0] if self.i < len(self.toks) else None

    def take(self, *want):
        if self.peek() not in want:
            self.fail(f"expected {' or '.join(want)} at token {self.i}")
        self.i += 1

    def parse(self):
        node = self.iff()
        if self.peek() is not None:
            self.fail(f"unexpected {self.peek()!r}")
        return node

    def binary(self, kind, ops, sub, right=False):
        lo, node = self.i, sub()
        while self.peek() in ops:
            self.i += 1
            rhs = self.binary(kind, ops, sub, right) if right else sub()
            node = Node(kind, lo, self.i, [node, rhs])
            if right:
                break
        return node

    def iff(self):
        return self.binary("iff", {"<->", "<=>"}, self.imp)

    def imp(self):
        return self.binary("imp", {"->", "=>"}, self.disj, right=True)

    def disj(self):
        return self.binary("or", {"|"}, self.conj)

    def conj(self):
        return self.binary("and", {"&"}, self.neg)

    def neg(self):
        if self.peek() in ("!", "~"):
            lo = self.i
            self.i += 1
            return self.finish(Node("not", lo, 0, [self.neg()]))
        return self.relation()

    def finish(self, node):
        node.hi = self.i
        return node

    def relation(self):
        lo, node = self.i, self.sum()
        if self.peek() in RELATIONS:
            op = self.peek()
            self.i += 1
            node = self.finish(Node("rel", lo, 0, [node, self.sum()], op))
        return node

    def sum(self):
        return self.binary("arith", {"+", "-"}, self.product)

    def product(self):
        return self.binary("arith", {"*", "/"}, self.power)

    def power(self):
        return self.binary("arith", {"^"}, self.unary)

    def unary(self):
        if self.peek() == "-":
            lo = self.i
            self.i += 1
            return self.finish(Node("minus", lo, 0, [self.unary()]))
        return self.primary()

    def primary(self):
        lo, tok = self.i, self.peek()
        if tok == "(":
            self.i += 1
            inner = self.iff()
            self.take(")")
            return self.finish(Node("paren", lo, 0, [inner]))
        if tok is None or not (tok[0].isalnum() or tok[0] == "_"):
            self.fail(f"unexpected {tok!r}")
        self.i += 1
        if tok[0].isdigit():
            return Node("num", lo, self.i, text=tok)
        if self.peek() == "(":
            self.i += 1
            args = [] if self.peek() == ")" else [self.iff()]
            while self.peek() == ",":
                self.i += 1
                args.append(self.iff())
            self.take(")")
            return self.finish(Node("func", lo, 0, args, tok))
        if tok.lower() in LITERALS:
            return Node("lit", lo, self.i, text=tok)
        return Node("ident", lo, self.i, text=tok)


def unwrap(node):
    while node.kind == "paren":
        node = node.kids[0]
    return node


def is_bool(node):
    """Whether `node` is boolean whatever its names turn out to be."""
    if node.kind == "paren":
        return is_bool(node.kids[0])
    if node.kind == "func":
        return node.text == "Nxt"
    return node.kind in {"lit", "not", "and", "or", "imp", "iff", "rel"}


class Converter:
    def __init__(self, atomise=False, from_fulltext=(), skip=(),
                 merge_case=False):
        self.guarantees, self.reqids = [], []
        self.labels = collections.defaultdict(set)
        self.roles = collections.defaultdict(set)
        self.modes = set()
        # Each formula mode's fresh atom, mapped to the formula it names.
        self.defined = {}
        self.original = {}
        self.notes = collections.defaultdict(list)
        self.atomise, self.merge_case = atomise, merge_case
        self.from_fulltext, self.skip = set(from_fulltext), set(skip)
        # Names used as booleans somewhere, and name pairs compared by `=`,
        # which are iff when either side is in the set.
        self.boolean, self.equated = set(), []
        # Each comparison atom as (op, lhs, rhs) over term trees, and each
        # variable's FRET dataType.
        self.comparisons, self.types = {}, {}

    def name(self, raw):
        new = snake(raw)
        prev = self.original.setdefault(new, raw)
        if prev != raw and self.merge_case and prev.lower() == raw.lower():
            self.notes["case variants merged"].append(f"{raw} = {prev}")
        elif prev != raw:
            raise FretImportError(f"{raw!r} and {prev!r} both become {new!r}")
        if new != raw:
            self.notes["renamed"].append(f"{raw} -> {new}")
        return new

    def parse(self, expr, reqid):
        bad = TEMPORAL.search(expr) or (not self.atomise
                                        and ARITHMETIC.search(expr))
        if bad:
            raise FretImportError(f"{reqid}: unsupported operator "
                                  f"{bad.group(0)!r} in {expr!r}")
        p = Parser(expr, reqid)
        tree = p.parse()
        self.scan(tree, p)
        return tree, p

    def scan(self, node, p):
        """Check `node` in boolean context, recording boolean names."""
        if node.kind == "ident":
            self.boolean.add(node.text)
        elif node.kind == "rel":
            a, b = map(unwrap, node.kids)
            if node.text in ("=", "==", "!=") and (is_bool(a) or is_bool(b)):
                self.scan(a, p)
                self.scan(b, p)
            elif node.text in ("=", "==", "!=") and \
                    a.kind == b.kind == "ident":
                self.equated.append((a.text, b.text))
            elif is_bool(a) or is_bool(b):
                p.fail(f"ordering {node.text!r} on a boolean")
        elif node.kind == "func" and node.text != "Nxt":
            p.fail(f"unsupported function {node.text!r}")
        elif node.kind in ("num", "arith", "minus"):
            p.fail("arithmetic outside a comparison")
        else:
            for k in node.kids:
                self.scan(k, p)

    def settle(self):
        """Close the boolean names over `=`: `a = b` with `a` boolean makes
        `b` boolean too."""
        grew = True
        while grew:
            grew = False
            for a, b in self.equated:
                if (a in self.boolean) != (b in self.boolean):
                    self.boolean |= {a, b}
                    grew = True

    def iff(self, node):
        a, b = map(unwrap, node.kids)
        if node.text in ("=", "==", "!="):
            if is_bool(a) or is_bool(b):
                return True
            if a.kind == b.kind == "ident":
                return a.text in self.boolean or b.text in self.boolean
        return False

    def emit(self, node, p, reqid):
        if node.kind == "ident":
            return self.name(node.text)
        if node.kind == "lit":
            return node.text.lower()
        if node.kind == "rel" and self.iff(node):
            sides = [self.emit(unwrap(k), p, reqid) for k in node.kids]
            sides = [x if IDENT.fullmatch(x) else f"({x})" for x in sides]
            lits = [s for s in sides if s in LITERALS]
            if lits:
                other = sides[1] if sides[0] in LITERALS else sides[0]
                positive = (lits[0] == "true") == (node.text != "!=")
                self.notes["boolean equality"].append(
                    f"{reqid}: {self.source(node, p)}")
                return other if positive else f"!({other})"
            self.notes["boolean equality"].append(
                f"{reqid}: {self.source(node, p)}")
            out = f"({sides[0]} <-> {sides[1]})"
            return f"!{out}" if node.text == "!=" else out
        if node.kind == "rel":
            words = []
            for tok, _ in p.toks[node.lo:node.hi]:
                if tok in RELATIONS:
                    words.append(RELATIONS[tok])
                elif tok in ARITH_NAMES:
                    words.append(ARITH_NAMES[tok])
                elif tok not in ("(", ")", ","):
                    words.append(tok.replace(".", "p"))
            atom = self.name("_".join(words))
            self.notes["comparison"].append(
                f"{reqid}: {self.source(node, p)} -> {atom}")
            a, b = (self.term(unwrap(k), p) for k in node.kids)
            self.comparisons[atom] = (RELATIONS[node.text], a, b)
            return atom
        return self.splice(node, p, reqid)

    def term(self, node, p):
        """`node`, a side of a comparison, as a term tree: `("var", name)`,
        `("num", value)`, `("neg", t)`, `(op, t, u)` for `+ - * / ^`, or
        `(func, t, ...)` for abs, absReal, min and max."""
        node = unwrap(node)
        if node.kind == "ident":
            return ("var", snake(node.text))
        if node.kind == "num":
            return ("num", Fraction(node.text))
        if node.kind == "minus":
            return ("neg", self.term(node.kids[0], p))
        if node.kind == "arith":
            op = p.toks[node.kids[0].hi][0]
            return (op, *(self.term(k, p) for k in node.kids))
        if node.kind == "func" and node.text in FUNCTIONS:
            return (node.text, *(self.term(k, p) for k in node.kids))
        p.fail(f"cannot compare {node.kind} {self.source(node, p)!r}")

    def splice(self, node, p, reqid):
        """Re-emit `node`'s own tokens as written, its children converted."""
        out, i, kids = [], node.lo, iter(node.kids)
        kid = next(kids, None)
        while i < node.hi:
            tok, space = p.toks[i]
            if kid is not None and i == kid.lo:
                tok, i = self.emit(kid, p, reqid), kid.hi
                kid = next(kids, None)
            else:
                tok, i = SPELLINGS.get(tok, tok), i + 1
            out.append((" " if space and out else "") + tok)
        return "".join(out)

    def source(self, node, p):
        return "".join((" " if space and i else "") + tok for i, (tok, space)
                       in enumerate(p.toks[node.lo:node.hi]))

    def formula(self, expr, reqid):
        tree, p = self.parse(expr, reqid)
        return strip_parens(self.emit(tree, p, reqid))

    def timing(self, s, reqid):
        t = s.get("timing") or "null"
        if t in PLAIN:
            if t == "null":
                self.notes["no timing, eventually"].append(reqid)
            return {"type": PLAIN[t]}
        if t in TICKED:
            # Older FRET versions store the duration as a one-element list.
            d = s["duration"]
            d = d[0] if isinstance(d, list) and len(d) == 1 else d
            return {"type": TICKED[t], "ticks": int(d)}
        if t in STOPPED:
            return {"type": STOPPED[t],
                    "stop": self.formula(s["stop_condition"], reqid)}
        raise FretImportError(f"{reqid}: unsupported timing {t!r}")

    def scope(self, s, reqid):
        scope = s.get("scope") or {"type": "null"}
        kind = scope.get("type", "null")
        if kind not in SCOPES:
            raise FretImportError(f"{reqid}: unsupported scope {kind!r}")
        if scope.get("exclusive") or scope.get("required"):
            raise FretImportError(f"{reqid}: strict or required scopes are not "
                                  f"supported: {scope}")
        if SCOPES[kind] is None:
            return None
        mode = self.formula(s["scope_mode"], reqid)
        if not IDENT.fullmatch(mode):
            mode = self.define(mode, reqid)
        self.modes.add(mode)
        return {"type": SCOPES[kind], "mode": mode}

    def define(self, formula, reqid):
        """A fresh output atom naming the formula mode `formula`.

        Its definition `G(name <-> formula)` is exact as a guarantee: the
        system sees each step's inputs before it chooses its outputs, so it
        can always match the formula, and can never do otherwise.
        """
        words = re.sub(r"\s+", "_", formula.replace("|", " or ")
                       .replace("&", " and ").replace("!", " not ")
                       .replace("(", " ").replace(")", " ")).strip("_")
        name = f"mode_{words}"
        if self.defined.get(name, formula) != formula or name in self.original:
            raise FretImportError(f"{reqid}: formula mode {formula!r} would "
                                  f"be named {name!r}, which is taken")
        self.defined[name] = formula
        for a in set(IDENT.findall(formula)) - LITERALS:
            self.roles[a].add("read")
        self.notes["formula mode"].append(f"{reqid}: {formula} -> {name}")
        return name

    def fields(self, r, component):
        """The FRET fields of `r` to convert, or None to leave `r` out."""
        reqid = r.get("reqid", "?").strip()
        s = r.get("semantics")
        formalised = s and "ft" in s and "post_condition" in s
        if not formalised and not r.get("fulltext", "").strip():
            self.notes["empty, skipped"].append(reqid)
            return None
        if reqid in self.skip:
            self.notes["skipped by --skip"].append(reqid)
            return None
        if not formalised:
            raise FretImportError(f"{reqid}: FRET did not formalise it")
        if component and s.get("component_name") != component:
            return None
        s = dict(s)
        if reqid in self.from_fulltext:
            m = re.search(r"\bsatisfy\b(.*)", r.get("fulltext", ""), re.S)
            if not m:
                raise FretImportError(f"{reqid}: no response in its fulltext")
            s["post_condition"] = m.group(1).strip().rstrip(".")
            self.notes["response from fulltext"].append(reqid)
        else:
            m = re.search(r"\bsatisfy\b(.*)", r.get("fulltext", ""), re.S)
            lost = (set(IDENT.findall(m.group(1))) if m else set()) \
                - set(IDENT.findall(s["post_condition"])) - FULLTEXT_WORDS
            if lost:
                raise FretImportError(
                    f"{reqid}: FRET's parsed response lacks {sorted(lost)} "
                    f"from the fulltext; see --from-fulltext")
        return reqid, s

    def prescan(self, r, component):
        got = self.fields(r, component)
        if got:
            reqid, s = got
            for key in ("regular_condition", "post_condition",
                        "stop_condition", "scope_mode"):
                if s.get(key):
                    self.parse(s[key], reqid)

    def add(self, r, component):
        got = self.fields(r, component)
        if not got:
            return
        reqid, s = got
        kind = s.get("condition") or "null"
        if kind == "regular":
            cond, ctype = self.formula(s["regular_condition"], reqid), "trigger"
        elif kind == "holding":
            cond, ctype = self.formula(s["regular_condition"], reqid), "continual"
        elif kind == "null":
            cond, ctype = "true", "trigger"
        else:
            raise FretImportError(f"{reqid}: unsupported condition {kind!r}")
        if s.get("probability") not in (None, "null", "none"):
            self.notes["probability dropped"].append(reqid)
        timing = self.timing(s, reqid)
        scope = self.scope(s, reqid)
        response = self.formula(s["post_condition"], reqid)

        parts = [(response, timing)]
        conjuncts = split_top(response, "&")
        nexts = [NXT.fullmatch(c) for c in conjuncts]
        if any(nexts):
            if timing["type"] != "Immediately":
                raise FretImportError(f"{reqid}: Nxt under {timing['type']}")
            now = [c for c, m in zip(conjuncts, nexts) if not m]
            later = [strip_parens(m.group(1).strip()) for m in nexts if m]
            parts = [(" & ".join(later), {"type": "NextTimepoint"})]
            if now:
                parts.insert(0, (" & ".join(now), timing))
            self.notes["Nxt split"].append(reqid)
        for resp, tim in parts:
            if "Nxt" in resp:
                raise FretImportError(f"{reqid}: nested Nxt in {response!r}")
            self.record(reqid, cond, ctype, resp, tim, scope)

    def record(self, reqid, cond, ctype, resp, tim, scope):
        req = {"condition": cond, "condition-type": ctype,
               "response": resp, "timing": tim}
        if scope:
            req["scope"] = scope
        acting = set(IDENT.findall(resp)) | set(IDENT.findall(tim.get("stop", "")))
        for a in acting - LITERALS:
            self.roles[a].add("acted")
        for a in set(IDENT.findall(cond)) - LITERALS - acting:
            self.roles[a].add("read")
        if req in self.guarantees:
            self.reqids[self.guarantees.index(req)].append(reqid)
            self.notes["duplicate merged"].append(reqid)
            return
        self.guarantees.append(req)
        self.reqids.append([reqid])

    def label(self, v):
        if v.get("dataType"):
            self.types[snake(v["variable_name"])] = v["dataType"]
        if v.get("idType"):
            self.labels[snake(v["variable_name"])].add(v["idType"])

    def partition(self, force_in, force_out):
        taken = set(self.defined) & set(self.original)
        if taken:
            raise FretImportError(f"formula mode names {sorted(taken)} are "
                                  f"also FRET names")
        def forced(a, patterns):
            return any(fnmatch.fnmatchcase(a, pat) for pat in patterns)

        def side(v):
            """Whether variable `v` is an output, from --as-input,
            --as-output or a single FRET label; None when neither says."""
            if forced(v, force_in) or forced(v, force_out):
                return forced(v, force_out)
            lab = self.labels.get(v, set())
            return lab == {"Output"} if len(lab) == 1 else None

        ins, outs = [], []
        for a in sorted(self.roles):
            names = variables(self.comparisons[a]) \
                if a in self.comparisons else set()
            if len(names) > 1:
                (outs if self.shared_side(a, names, side, forced, force_in,
                                          force_out) else ins).append(a)
                continue
            # A comparison atom takes the side of the variable it compares.
            var = next(iter(names), a)
            lab = self.labels.get(var, set())
            if forced(a, force_in) or forced(a, force_out):
                output = forced(a, force_out)
            elif forced(var, force_in) or forced(var, force_out):
                output = forced(var, force_out)
            elif len(lab) == 1:
                output = lab == {"Output"}
            else:
                output = "acted" in self.roles[a]
                why = "labelled both ways" if lab else "unlabelled"
                self.notes[f"{why}, by role"].append(
                    f"{a} -> {'output' if output else 'input'}")
            (outs if output else ins).append(a)
        # A mode no requirement mentions stays a pure mode, which PEREDUR
        # already reads as an input, unless a label or a flag makes it an
        # output. Declaring it an input atom as well would change nothing
        # but let mutation draw it into conditions and responses.
        for m in sorted(self.modes - set(self.roles) - set(self.defined)):
            if side(m):
                outs.append(m)
                self.notes["mode made an output by label or flag"].append(m)
        return ins, sorted(outs + list(self.defined))

    def shared_side(self, atom, names, side, forced, force_in, force_out):
        """The side of a comparison over several variables, which only its
        variables can give: the environment and the system each choose
        their own values, so a comparison across the two has no exact
        side, and the role rule would guess one."""
        sides = {v: side(v) for v in sorted(names)}
        unknown = [v for v, out in sides.items() if out is None]
        if unknown:
            raise FretImportError(
                f"{atom} compares {unknown}, with no single Input/Output "
                f"label; name the side with --as-input or --as-output")
        if len(set(sides.values())) > 1:
            raise FretImportError(
                f"{atom} compares inputs "
                f"{[v for v, out in sides.items() if not out]} with outputs "
                f"{[v for v, out in sides.items() if out]}; neither side "
                f"of it is exact")
        output = next(iter(sides.values()))
        if forced(atom, force_out if not output else force_in):
            raise FretImportError(
                f"{atom} is forced to the other side from its variables")
        return output


HOLDS = {"lt": lambda x, c: x < c, "le": lambda x, c: x <= c,
         "gt": lambda x, c: x > c, "ge": lambda x, c: x >= c,
         "eq": lambda x, c: x == c, "ne": lambda x, c: x != c}
FLIPPED = {"lt": "gt", "le": "ge", "gt": "lt", "ge": "le", "eq": "eq",
           "ne": "ne"}
INTEGER_TYPES = {"integer", "unsigned integer"}
MAX_GROUP = 16


def samples(consts, integer, unsigned):
    """One value inside every region the constants cut the line into.

    Each predicate `x op c` changes truth only at its `c`, so these values
    realise every truth vector the predicates can take together.
    """
    cs = sorted(set(consts))
    if integer:
        pts = set()
        for c in cs:
            lo, hi = math.floor(c), math.ceil(c)
            pts |= {lo - 1, lo, hi, hi + 1}
    else:
        pts = set(cs) | {cs[0] - 1, cs[-1] + 1}
        pts |= {(a + b) / 2 for a, b in zip(cs, cs[1:])}
    if unsigned:
        # The region holding 0 may have only negative samples so far.
        pts = {p for p in pts | {0} if p >= 0}
    return pts


def satisfies(vector, clause):
    return any(vector[i] == pol for i, pol in clause)


def entails(clauses, target):
    """Whether the unit and binary `clauses` entail `target`.

    Adding the negation of `target` and propagating along the implication
    graph finds a conflict exactly when it is entailed, since `clauses` are
    satisfiable and whatever propagation leaves untouched is a subset of
    them.
    """
    edges = collections.defaultdict(set)
    for c in clauses:
        if len(c) == 2:
            (a, pa), (b, pb) = c
            edges[(a, not pa)].add((b, pb))
            edges[(b, not pb)].add((a, pa))
    seen = set()
    todo = [(i, not pol) for i, pol in target] + \
        [c[0] for c in clauses if len(c) == 1]
    while todo:
        lit = todo.pop()
        if lit not in seen:
            seen.add(lit)
            todo.extend(edges[lit])
    return any((i, not pol) in seen for i, pol in seen)


def variables(comparison):
    """The variable names in a comparison or term."""
    if comparison[0] == "var":
        return {comparison[1]}
    return set().union(set(), *(variables(t) for t in comparison[1:]
                               if isinstance(t, tuple)))


def against_constant(comparison):
    """A comparison of one variable with a number as `(variable, op, c)`,
    else None."""
    def fold(t):
        return ("num", -t[1][1]) if t[0] == "neg" and t[1][0] == "num" else t
    op, a, b = comparison[0], fold(comparison[1]), fold(comparison[2])
    if a[0] == "num":
        a, b, op = b, a, FLIPPED[op]
    if a[0] == "var" and b[0] == "num":
        return a[1], op, b[1]
    return None


def sampled_vectors(preds, integer=False, unsigned=False):
    """The truth vectors of `preds`, each `(op, c)` on one variable, over
    every value it can take: exact, and needs no solver."""
    return {tuple(HOLDS[op](x, c) for op, c in preds)
            for x in samples([c for _, c in preds], integer, unsigned)}


def smt_vectors(comparisons, types):
    """The truth vectors of `comparisons` that some values of their
    variables realise, enumerated by z3 one blocked vector at a time."""
    try:
        import z3
    except ImportError:
        raise FretImportError(
            "comparisons over several variables or arithmetic need z3's "
            "Python bindings (z3-solver, in `nix develop`); --no-domains "
            "skips them") from None
    names = sorted(set().union(*(variables(c) for c in comparisons)))
    zvars = {v: z3.Int(v) if types.get(v) in INTEGER_TYPES else z3.Real(v)
             for v in names}

    def real(t):
        return z3.ToReal(t) if z3.is_int(t) else t

    def term(t):
        head, args = t[0], [term(u) for u in t[1:] if isinstance(u, tuple)]
        if head == "var":
            return zvars[t[1]]
        if head == "num":
            return z3.RealVal(str(t[1]))
        if head == "neg":
            return -args[0]
        if head in ("abs", "absReal"):
            return z3.If(args[0] >= 0, args[0], -args[0])
        if head in ("min", "max"):
            pick = args[0] <= args[1] if head == "min" else args[0] >= args[1]
            return z3.If(pick, args[0], args[1])
        if head == "/":
            return real(args[0]) / real(args[1])
        return {"+": lambda a, b: a + b, "-": lambda a, b: a - b,
                "*": lambda a, b: a * b, "^": lambda a, b: a ** b}[head](*args)

    solver = z3.Solver()
    solver.set("timeout", 10000)
    for v in names:
        if types.get(v) == "unsigned integer":
            solver.add(zvars[v] >= 0)
    flags = [z3.Bool(f"cmp!{i}") for i in range(len(comparisons))]
    ops = {"lt": lambda a, b: a < b, "le": lambda a, b: a <= b,
           "gt": lambda a, b: a > b, "ge": lambda a, b: a >= b,
           "eq": lambda a, b: a == b, "ne": lambda a, b: a != b}
    for flag, (op, a, b) in zip(flags, comparisons):
        solver.add(flag == ops[op](term(a), term(b)))
    vectors = set()
    while True:
        verdict = solver.check()
        if verdict == z3.unknown:
            raise FretImportError(
                f"z3 cannot decide comparisons over {names}: "
                f"{solver.reason_unknown()}; --no-domains skips them")
        if verdict == z3.unsat:
            return vectors
        model = solver.model()
        vector = tuple(z3.is_true(model.eval(f, model_completion=True))
                       for f in flags)
        vectors.add(vector)
        solver.add(z3.Or([f != val for f, val in zip(flags, vector)]))


def exact_clauses(real, k, span=lambda c: 0):
    """Clauses over `k` atoms whose models are exactly the vectors `real`.

    A clause is a tuple of `(index, polarity)` literals. Binary clauses are
    tried first and cover orderings and exclusions; a wider clause is added
    only where they admit a vector no value realises, as with `x >= 1`,
    `x <= 1` and `x != 1` together. Redundant binary clauses are then
    dropped, largest `span` first.
    """
    lits = [(i, pol) for i in range(k) for pol in (True, False)]
    clauses = [(lit,) for lit in lits
               if all(satisfies(v, (lit,)) for v in real)]
    units = {c[0][0] for c in clauses}
    clauses += [(a, b) for n, a in enumerate(lits) for b in lits[n + 1:]
                if a[0] != b[0] and a[0] not in units and b[0] not in units
                and all(satisfies(v, (a, b)) for v in real)]
    while True:
        extra = next((m for m in itertools.product((True, False), repeat=k)
                      if m not in real
                      and all(satisfies(m, c) for c in clauses)), None)
        if extra is None:
            break
        # Shrink the extra vector's literals to a set no value realises.
        core = list(range(k))
        for i in list(core):
            rest = [j for j in core if j != i]
            if not any(all(v[j] == extra[j] for j in rest) for v in real):
                core = rest
        clauses.append(tuple((j, not extra[j]) for j in core))
    for c in sorted([c for c in clauses if len(c) == 2], key=span,
                    reverse=True):
        rest = [d for d in clauses if d is not c and len(d) <= 2]
        if entails(rest, c):
            clauses.remove(c)
    return clauses


def domain_clauses(preds, integer=False, unsigned=False):
    """`exact_clauses` for `preds`, each `(op, c)` on one variable. Dropping
    the widest spans first leaves the chain of neighbouring thresholds."""
    def span(c):
        return max(preds[i][1] for i, _ in c) - min(preds[i][1] for i, _ in c)
    return exact_clauses(sampled_vectors(preds, integer, unsigned),
                         len(preds), span)


def render(clause, atoms):
    pos = [atoms[i] for i, pol in clause if pol]
    neg = [atoms[i] for i, pol in clause if not pol]
    if len(clause) == 1:
        return pos[0] if pos else f"!{neg[0]}"
    if not pos:
        return f"!({' & '.join(neg)})"
    if not neg:
        return f"({' | '.join(pos)})"
    return f"({' & '.join(neg)} -> {' | '.join(pos)})"


def groups(comparisons):
    """The comparison atoms, split into groups that share no variable."""
    parent = {}

    def root(v):
        while parent.setdefault(v, v) != v:
            v = parent[v]
        return v
    for c in comparisons.values():
        vs = sorted(variables(c))
        for v in vs[1:]:
            parent[root(v)] = root(vs[0])
    by_root = collections.defaultdict(list)
    for atom, c in sorted(comparisons.items()):
        by_root[root(min(variables(c)))].append(atom)
    return sorted(by_root.values())


def domain_requirements(comparisons, types, ins, outs):
    """One non-weakenable Always requirement per group of comparison atoms
    sharing variables, as `(assumptions, guarantees, notes)`.

    `comparisons` maps an atom to `(op, lhs, rhs)` over term trees, and
    `types` a variable to its FRET `dataType`. A group of comparisons of
    one variable with numbers is solved by sampling; any other group by z3.
    A group whose atoms are all inputs constrains the environment, one whose
    atoms are all outputs the system; one with both has no side that is
    exact, so it is rejected.
    """
    ins, outs = set(ins), set(outs)
    assumptions, guarantees, notes = [], [], []
    for atoms in groups(comparisons):
        names = sorted(set().union(*(variables(comparisons[a])
                                     for a in atoms)))
        var = ", ".join(names)
        sides = {a in ins for a in atoms}
        if len(sides) > 1:
            raise FretImportError(
                f"comparisons on {var} are both inputs "
                f"{sorted(a for a in atoms if a in ins)} and outputs "
                f"{sorted(a for a in atoms if a in outs)}")
        if len(atoms) > MAX_GROUP:
            raise FretImportError(f"{len(atoms)} comparisons on {var}; the "
                                  f"exactness check stops at {MAX_GROUP}")
        plain = [against_constant(comparisons[a]) for a in atoms]
        if all(plain) and len(names) == 1:
            dtype = types.get(var)
            clauses = domain_clauses([(op, c) for _, op, c in plain],
                                     dtype in INTEGER_TYPES,
                                     dtype == "unsigned integer")
            how = dtype or "untyped, read as real"
        else:
            clauses = exact_clauses(
                smt_vectors([comparisons[a] for a in atoms], types),
                len(atoms))
            how = "z3"
        if not clauses:
            continue
        req = always(" & ".join(render(c, atoms) for c in clauses))
        (assumptions if sides == {True} else guarantees).append(req)
        notes.append(f"{var} ({how}): {len(clauses)} clause(s)")
    return assumptions, guarantees, notes


def always(response):
    return {"condition": "true", "condition-type": "continual",
            "response": response, "timing": {"type": "Always"},
            "weakenable": False}


def exclusion(names, ins, outs, modes):
    """`G` at most one of `names`, as `(requirement, is_assumption)`.

    A pure mode is an environment signal, so it counts as an input.
    """
    unknown = [n for n in names if n not in ins and n not in outs
               and n not in modes]
    if unknown:
        raise FretImportError(f"--exclusive names {unknown}, which no "
                              f"requirement uses")
    sides = {n in outs for n in names}
    if len(sides) > 1:
        raise FretImportError(
            f"--exclusive mixes inputs "
            f"{[n for n in names if n not in outs]} with outputs "
            f"{[n for n in names if n in outs]}")
    response = " & ".join(f"!({a} & {b})"
                          for a, b in itertools.combinations(names, 2))
    return always(response), sides == {False}


def load(paths, component, force_in, force_out, all_components=False,
         atomise=False, from_fulltext=(), skip=(), merge_case=False,
         domains=True, exclusive=()):
    conv = Converter(atomise, from_fulltext, skip, merge_case)
    exports = []
    for p in paths:
        e = json.loads(Path(p).read_text())
        exports.append({"requirements": e} if isinstance(e, list) else e)
    components = {r["semantics"].get("component_name")
                  for e in exports for r in e["requirements"]
                  if (r.get("semantics") or {}).get("post_condition")}
    if not component and not all_components and len(components) > 1:
        raise FretImportError(f"several components {sorted(components)}; "
                              f"pick one with --component or pass "
                              f"--all-components")
    for e in exports:
        for r in e["requirements"]:
            conv.prescan(r, component)
    conv.settle()
    conv.notes.clear()
    for e in exports:
        for v in e.get("variables", []):
            conv.label(v)
        for r in e["requirements"]:
            conv.add(r, component)
    ins, outs = conv.partition({snake(a) for a in force_in},
                               {snake(a) for a in force_out})
    spec = {"assumptions": [], "guarantees": conv.guarantees,
            "in_atoms": ins, "out_atoms": outs}
    for name, formula in sorted(conv.defined.items()):
        spec["guarantees"].append(always(f"{name} <-> ({formula})"))
        conv.reqids.append(["mode definition"])
    for group in exclusive:
        names = sorted({snake(n.strip()) for n in group.split(",")
                        if n.strip()})
        if len(names) < 2:
            raise FretImportError(f"--exclusive {group!r} names fewer than "
                                  f"two signals")
        requirement, assumed = exclusion(names, set(ins), set(outs),
                                         conv.modes)
        if assumed:
            spec["assumptions"].append(requirement)
        else:
            spec["guarantees"].append(requirement)
            conv.reqids.append(["exclusive"])
        conv.notes["exclusive"].append(
            f"{', '.join(names)} ({'assumption' if assumed else 'guarantee'})")
    if domains:
        assumptions, guarantees, notes = domain_requirements(
            conv.comparisons, conv.types, ins, outs)
        spec["assumptions"] += assumptions
        spec["guarantees"] += guarantees
        conv.reqids += [["domain"] for _ in guarantees]
        if notes:
            conv.notes["domain constraint"] += notes
    if conv.modes:
        spec["modes"] = sorted(conv.modes)
    return spec, conv


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Convert FRET requirement exports into a PEREDUR "
                    "FRETISH specification.")
    ap.add_argument("exports", nargs="+", help="FRET project export(s)")
    ap.add_argument("-o", "--output", required=True, help="spec.json to write")
    ap.add_argument("--component", help="keep only this FRET component")
    ap.add_argument("--all-components", action="store_true",
                    help="keep every component in one specification")
    ap.add_argument("--atomise-arithmetic", action="store_true",
                    help="turn a comparison over arithmetic or functions "
                         "into an atom instead of rejecting it")
    ap.add_argument("--no-domains", action="store_true",
                    help="leave comparison atoms unconstrained instead of "
                         "adding what each variable's values allow")
    ap.add_argument("--from-fulltext", action="append", default=[],
                    metavar="REQID",
                    help="parse REQID's response from its fulltext, for when "
                         "FRET's parse dropped part of it (repeatable)")
    ap.add_argument("--skip", action="append", default=[], metavar="REQID",
                    help="leave REQID out (repeatable)")
    ap.add_argument("--merge-case", action="store_true",
                    help="read names that differ only in case as one atom "
                         "instead of rejecting them")
    ap.add_argument("--exclusive", action="append", default=[],
                    metavar="A,B,...",
                    help="add that at most one of these signals or modes "
                         "holds at each step, which FRET keeps outside its "
                         "export (repeatable)")
    ap.add_argument("--ids", help="also write guarantee index -> FRET reqids")
    ap.add_argument("--as-input", action="append", default=[], metavar="ATOM",
                    help="force ATOM, a name or shell pattern, to be an input "
                         "(repeatable)")
    ap.add_argument("--as-output", action="append", default=[], metavar="ATOM",
                    help="force ATOM, a name or shell pattern, to be an "
                         "output (repeatable)")
    args = ap.parse_args(argv)
    try:
        spec, conv = load(args.exports, args.component, args.as_input,
                          args.as_output, args.all_components,
                          args.atomise_arithmetic, args.from_fulltext,
                          args.skip, args.merge_case, not args.no_domains,
                          args.exclusive)
    except FretImportError as e:
        print(f"import_fret: {e}", file=sys.stderr)
        return 1
    Path(args.output).write_text(json.dumps(spec, indent=2) + "\n")
    if args.ids:
        Path(args.ids).write_text(json.dumps(
            [{"guarantee": i, "reqids": r} for i, r in enumerate(conv.reqids)],
            indent=2) + "\n")
    print(f"{len(spec['guarantees'])} guarantees, {len(spec['in_atoms'])} "
          f"inputs, {len(spec['out_atoms'])} outputs, "
          f"{len(spec.get('modes', []))} modes", file=sys.stderr)
    for rule, items in sorted(conv.notes.items()):
        items = sorted(set(items))
        print(f"  {rule} ({len(items)}): {', '.join(items)}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
