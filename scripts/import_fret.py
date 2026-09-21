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
- Names become snake_case, since SPOT mishandles uppercase inputs.
- Durations are ticks: FRET discards the unit of `within 5 minutes`.
- Requirements identical after conversion are kept once.

FRET's parser silently drops the `= X` of `(a | b) = X`, so a name in the
fulltext's response that the parsed response lacks is rejected;
`--from-fulltext REQID` parses that requirement's response from its fulltext
instead. Names colliding after snake_case are rejected, unless they differ
only in case and `--merge-case` is given. A requirement with empty fulltext is
a placeholder and is skipped; any other one FRET did not formalise is rejected
unless named by `--skip`.

An atom FRET labels exactly once keeps its label. An unlabelled atom, or one
labelled both ways across exports, becomes an output when it appears in a
response or a stop condition and an input otherwise. `--as-input`/`--as-output`
override both, and take shell-style patterns. Every requirement becomes a
guarantee; FRET has no assumptions.

The summary on stderr lists every rule that fired, so a reviewer can check
each lossy step. `--ids` writes which FRET reqids each guarantee came from.
"""

import argparse
import collections
import fnmatch
import json
import re
import sys
from pathlib import Path

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
TOKEN = re.compile(r"(\s*)(<->|<=>|->|=>|>=|<=|!=|==|[=<>!~&|()+\-*/^,]|"
                   r"\d+(?:\.\d+)?|[A-Za-z_]\w*)")
LITERALS = {"true", "false"}
SPELLINGS = {"=>": "->", "<=>": "<->"}
RELATIONS = {">=": "ge", "<=": "le", ">": "gt", "<": "lt",
             "=": "eq", "==": "eq", "!=": "ne"}
ARITH_NAMES = {"+": "plus", "-": "minus", "*": "times", "/": "div", "^": "pow"}
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
                  name).lower()


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
        self.original = {}
        self.notes = collections.defaultdict(list)
        self.atomise, self.merge_case = atomise, merge_case
        self.from_fulltext, self.skip = set(from_fulltext), set(skip)
        # Names used as booleans somewhere, and name pairs compared by `=`,
        # which are iff when either side is in the set.
        self.boolean, self.equated = set(), []

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
            return atom
        return self.splice(node, p, reqid)

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
        mode = self.name(s["scope_mode"])
        self.modes.add(mode)
        return {"type": SCOPES[kind], "mode": mode}

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
                        "stop_condition"):
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
        if v.get("idType"):
            self.labels[snake(v["variable_name"])].add(v["idType"])

    def partition(self, force_in, force_out):
        clash = self.modes & set(self.roles)
        if clash:
            raise FretImportError(f"modes also used as atoms: {sorted(clash)}")

        def forced(a, patterns):
            return any(fnmatch.fnmatchcase(a, pat) for pat in patterns)

        ins, outs = [], []
        for a in sorted(self.roles):
            lab = self.labels.get(a, set())
            if forced(a, force_in) or forced(a, force_out):
                output = forced(a, force_out)
            elif len(lab) == 1:
                output = lab == {"Output"}
            else:
                output = "acted" in self.roles[a]
                why = "labelled both ways" if lab else "unlabelled"
                self.notes[f"{why}, by role"].append(
                    f"{a} -> {'output' if output else 'input'}")
            (outs if output else ins).append(a)
        return ins, outs


def load(paths, component, force_in, force_out, all_components=False,
         atomise=False, from_fulltext=(), skip=(), merge_case=False):
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
    ap.add_argument("--from-fulltext", action="append", default=[],
                    metavar="REQID",
                    help="parse REQID's response from its fulltext, for when "
                         "FRET's parse dropped part of it (repeatable)")
    ap.add_argument("--skip", action="append", default=[], metavar="REQID",
                    help="leave REQID out (repeatable)")
    ap.add_argument("--merge-case", action="store_true",
                    help="read names that differ only in case as one atom "
                         "instead of rejecting them")
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
                          args.skip, args.merge_case)
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
