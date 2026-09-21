#!/usr/bin/env python3
"""Convert FRET requirement exports into a PEREDUR FRETISH specification.

A FRET export is the JSON file FRET writes for a project, holding
`requirements` (each with the formaliser's parsed `semantics`) and
`variables` (each with an `idType` of Input, Output or blank). Several exports
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
- A numeric comparison `a >= 0.9` becomes the boolean atom `a_ge_0p9`.
- Names become snake_case, since SPOT mishandles uppercase inputs.
- Durations are ticks: FRET discards the unit of `within 5 minutes`.
- Requirements identical after conversion are kept once.

An atom FRET labels exactly once keeps its label. An unlabelled atom, or one
labelled both ways across exports, becomes an output when it appears in a
response or a stop condition and an input otherwise. `--as-input`/`--as-output`
override both. Every requirement becomes a guarantee; FRET has no assumptions.

The summary on stderr lists every rule that fired, so a reviewer can check
each lossy step. `--ids` writes which FRET reqids each guarantee came from.
"""

import argparse
import collections
import json
import re
import sys
from pathlib import Path

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
LITERALS = {"true", "false"}
COMPARISON = re.compile(
    r"([A-Za-z_]\w*)\s*(>=|<=|!=|==|=|>|<)\s*([A-Za-z_]\w*|\d+(?:\.\d+)?)")
COMPARISON_NAMES = {">=": "ge", "<=": "le", ">": "gt", "<": "lt",
                    "=": "eq", "==": "eq", "!=": "ne"}
NXT = re.compile(r"Nxt\s*\((.*)\)")
UNSUPPORTED = re.compile(r"\b(xor|persisted|occurred|preBool|prevOcc|nextOcc|"
                         r"FTP|FTF|absReal|abs|max|min)\b|[+*/^]|-(?!>)")
SCOPES = {"null": None, "in": "In", "notin": "NotIn", "before": "Before",
          "after": "After", "onlyIn": "OnlyIn", "onlyBefore": "OnlyBefore",
          "onlyAfter": "OnlyAfter"}
TICKED = {"within": "WithinTicks", "for": "ForTicks", "after": "AfterTicks"}
PLAIN = {"immediately": "Immediately", "next": "NextTimepoint",
         "eventually": "Eventually", "null": "Eventually",
         "always": "Always", "never": "Never"}
STOPPED = {"until": "Until", "before": "Before"}


class FretImportError(Exception):
    pass


def snake(name):
    return re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", name).lower()


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


class Converter:
    def __init__(self):
        self.guarantees, self.reqids = [], []
        self.labels = collections.defaultdict(set)
        self.roles = collections.defaultdict(set)
        self.modes = set()
        self.original = {}
        self.notes = collections.defaultdict(list)

    def name(self, raw):
        new = snake(raw)
        prev = self.original.setdefault(new, raw)
        if prev != raw:
            raise FretImportError(f"{raw!r} and {prev!r} both become {new!r}")
        if new != raw:
            self.notes["renamed"].append(f"{raw} -> {new}")
        return new

    def formula(self, expr, reqid):
        bad = UNSUPPORTED.search(expr)
        if bad:
            raise FretImportError(f"{reqid}: unsupported operator "
                                  f"{bad.group(0)!r} in {expr!r}")

        def comparison(m):
            lhs, op, rhs = m.groups()
            rhs = rhs.replace(".", "p")
            atom = f"{lhs}_{COMPARISON_NAMES[op]}_{rhs}"
            self.notes["comparison"].append(f"{reqid}: {m.group(0)} -> "
                                            f"{snake(atom)}")
            return atom

        expr = COMPARISON.sub(comparison, expr)
        expr = re.sub(r"\bTRUE\b", "true", expr)
        expr = re.sub(r"\bFALSE\b", "false", expr)
        expr = re.sub(r"\s+", " ", expr).strip()
        expr = IDENT.sub(lambda m: m.group(0)
                         if m.group(0) in LITERALS or m.group(0) == "Nxt"
                         else self.name(m.group(0)), expr)
        return strip_parens(expr)

    def timing(self, s, reqid):
        t = s.get("timing") or "null"
        if t in PLAIN:
            if t == "null":
                self.notes["no timing, eventually"].append(reqid)
            return {"type": PLAIN[t]}
        if t in TICKED:
            return {"type": TICKED[t], "ticks": int(s["duration"])}
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

    def add(self, r, component):
        reqid = r.get("reqid", "?")
        s = r.get("semantics")
        if not s or "ft" not in s:
            raise FretImportError(f"{reqid}: FRET did not formalise it")
        if component and s.get("component_name") != component:
            return
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
        ins, outs = [], []
        for a in sorted(self.roles):
            lab = self.labels.get(a, set())
            if a in force_in or a in force_out:
                output = a in force_out
            elif len(lab) == 1:
                output = lab == {"Output"}
            else:
                output = "acted" in self.roles[a]
                why = "labelled both ways" if lab else "unlabelled"
                self.notes[f"{why}, by role"].append(
                    f"{a} -> {'output' if output else 'input'}")
            (outs if output else ins).append(a)
        return ins, outs


def load(paths, component, force_in, force_out):
    conv = Converter()
    exports = [json.loads(Path(p).read_text()) for p in paths]
    components = {r["semantics"].get("component_name")
                  for e in exports for r in e["requirements"]
                  if r.get("semantics")}
    if not component and len(components) > 1:
        raise FretImportError(f"several components {sorted(components)}; "
                           f"pick one with --component")
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
    ap.add_argument("--ids", help="also write guarantee index -> FRET reqids")
    ap.add_argument("--as-input", action="append", default=[], metavar="ATOM",
                    help="force ATOM to be an input (repeatable)")
    ap.add_argument("--as-output", action="append", default=[], metavar="ATOM",
                    help="force ATOM to be an output (repeatable)")
    args = ap.parse_args(argv)
    try:
        spec, conv = load(args.exports, args.component, args.as_input,
                          args.as_output)
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
