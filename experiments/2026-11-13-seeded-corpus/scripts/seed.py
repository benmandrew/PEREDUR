#!/usr/bin/env python3
"""Seed unrealisable mutants from the realisable bases.

Each mutant O is a base B strengthened by a compound seed of 2 to 4 primitive
edits on its live weakenable guarantees (and, where B has some, dropped
assumptions). The primitive edits model faults in requirements:

* ``tighten-within``  a deadline shortened: ``within n`` to ``within k``,
  ``k <= n - 2``, or to ``immediately``;
* ``bound-eventually`` an open deadline given a bound: ``eventually`` to
  ``within k`` or ``immediately``;
* ``lengthen-for``    a duration lengthened: ``for n`` to ``for m``,
  ``m >= n + 2``, or to ``always``;
* ``continual``       a trigger made continual (``upon c`` to ``whenever c``);
* ``widen-condition`` a condition conjunct dropped, or the condition set true;
* ``drop-guard``      a conjunct dropped from a response's antecedent
  (``p & q -> r`` to ``q -> r``);
* ``conjoin-output``  an output literal from elsewhere in B conjoined to a
  response;
* ``drop-disjunct``   a disjunct dropped from a response;
* ``conflict-output`` the negation of an output literal that another guarantee
  asserts conjoined to a response, so two requirements drive one output
  apart;
* ``drop-assumption`` an assumption removed.

Not every edit is a strengthening in every context (a trigger's rising edge
is not monotone in its condition), so no edit is trusted: ``compare`` must
prove O strictly stronger than B. The remaining filters, cheapest first: O is
unrealisable; every live guarantee of O is realisable alone, so the conflict
needs two or more requirements and no single edit made a requirement
impossible; and the conjunction of O's requirements is satisfiable.

Writes ``<out>/subjects/<id>/{spec.json,fixes/base.json,seed.json}`` and
``<out>/mutants.csv``.
"""

from __future__ import annotations

import argparse
import copy
import csv
import json
import random
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
BIN = REPO / "build-release"

# --------------------------------------------------------------------------
# Propositional formulas: atoms, true, false, !, &, |, ->, <->.

TOKEN = re.compile(r"\s*(<->|->|[()!&|]|[A-Za-z_][A-Za-z0-9_]*)")


@dataclass(frozen=True)
class F:
    op: str  # atom, not, and, or, imp, iff, true, false
    args: tuple = ()
    name: str = ""


def tokens(text: str) -> list[str]:
    pos, out = 0, []
    text = text.strip()
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            raise ValueError(f"bad token at {text[pos:]!r}")
        out.append(m.group(1))
        pos = m.end()
        while pos < len(text) and text[pos].isspace():
            pos += 1
    return out


def parse(text: str) -> F:
    toks = tokens(text)
    i = 0

    def peek() -> str | None:
        return toks[i] if i < len(toks) else None

    def take() -> str:
        nonlocal i
        i += 1
        return toks[i - 1]

    def iff() -> F:
        left = imp()
        while peek() == "<->":
            take()
            left = F("iff", (left, imp()))
        return left

    def imp() -> F:
        left = disj()
        if peek() == "->":
            take()
            return F("imp", (left, imp()))
        return left

    def disj() -> F:
        left = conj()
        while peek() == "|":
            take()
            left = F("or", (left, conj()))
        return left

    def conj() -> F:
        left = unary()
        while peek() == "&":
            take()
            left = F("and", (left, unary()))
        return left

    def unary() -> F:
        t = take()
        if t == "!":
            return F("not", (unary(),))
        if t == "(":
            inner = iff()
            if take() != ")":
                raise ValueError("unbalanced")
            return inner
        if t == "true":
            return F("true")
        if t == "false":
            return F("false")
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", t):
            return F("atom", name=t)
        raise ValueError(f"unexpected {t!r}")

    f = iff()
    if i != len(toks):
        raise ValueError("trailing tokens")
    return f


def show(f: F) -> str:
    if f.op == "atom":
        return f.name
    if f.op in ("true", "false"):
        return f.op
    if f.op == "not":
        return f"!({show(f.args[0])})"
    sym = {"and": "&", "or": "|", "imp": "->", "iff": "<->"}[f.op]
    return f"({show(f.args[0])}) {sym} ({show(f.args[1])})"


def flatten(f: F, op: str) -> list[F]:
    if f.op == op:
        return flatten(f.args[0], op) + flatten(f.args[1], op)
    return [f]


def rebuild(parts: list[F], op: str) -> F:
    out = parts[0]
    for p in parts[1:]:
        out = F(op, (out, p))
    return out


def atoms(f: F) -> set[str]:
    if f.op == "atom":
        return {f.name}
    return set().union(*(atoms(a) for a in f.args)) if f.args else set()


# --------------------------------------------------------------------------
# Primitive edits. Each takes (spec, slot, rng) and returns a new requirement
# or None where it does not apply.


def t_tighten_within(spec, g, rng):
    t = g["timing"]
    if t.get("type") != "WithinTicks":
        return None
    n = t["ticks"]
    choices = [{"type": "WithinTicks", "ticks": k} for k in range(1, n - 1)]
    choices.append({"type": "Immediately"})
    return {**g, "timing": rng.choice(choices)}


def t_bound_eventually(spec, g, rng):
    if g["timing"].get("type") != "Eventually":
        return None
    k = rng.randint(0, 6)
    timing = {"type": "Immediately"} if k == 0 else {"type": "WithinTicks", "ticks": k}
    return {**g, "timing": timing}


def t_lengthen_for(spec, g, rng):
    t = g["timing"]
    if t.get("type") != "ForTicks":
        return None
    n = t["ticks"]
    choices = [{"type": "ForTicks", "ticks": m} for m in range(n + 2, 2 * n + 4)]
    choices.append({"type": "Always"})
    return {**g, "timing": rng.choice(choices)}


def t_continual(spec, g, rng):
    if g.get("condition-type") != "trigger" or g.get("condition", "true") == "true":
        return None
    return {**g, "condition-type": "continual"}


def t_widen_condition(spec, g, rng):
    c = g.get("condition", "true")
    if c == "true":
        return None
    f = parse(c)
    parts = flatten(f, "and")
    if len(parts) > 1:
        parts.pop(rng.randrange(len(parts)))
        return {**g, "condition": show(rebuild(parts, "and"))}
    if g.get("condition-type") == "continual":
        return {**g, "condition": "true"}
    return None


def t_drop_guard(spec, g, rng):
    f = parse(g["response"])
    if f.op != "imp":
        return None
    ante = flatten(f.args[0], "and")
    if len(ante) < 2:
        return None
    ante.pop(rng.randrange(len(ante)))
    return {**g, "response": show(F("imp", (rebuild(ante, "and"), f.args[1])))}


def t_conjoin_output(spec, g, rng):
    f = parse(g["response"])
    outs = sorted(set(spec["out_atoms"]) - atoms(f))
    if not outs:
        return None
    a = F("atom", name=rng.choice(outs))
    lit = a if rng.random() < 0.5 else F("not", (a,))
    if f.op == "imp":  # conjoin to the consequent, keeping the guard
        new = F("imp", (f.args[0], F("and", (f.args[1], lit))))
    else:
        new = F("and", (f, lit))
    return {**g, "response": show(new)}


def t_drop_disjunct(spec, g, rng):
    f = parse(g["response"])
    target = f.args[1] if f.op == "imp" else f
    parts = flatten(target, "or")
    if len(parts) < 2:
        return None
    parts.pop(rng.randrange(len(parts)))
    new_t = rebuild(parts, "or")
    new = F("imp", (f.args[0], new_t)) if f.op == "imp" else new_t
    return {**g, "response": show(new)}


def consequent_literals(f: F, outs: set[str]) -> list[F]:
    """Output literals asserted at the top of a response's consequent."""
    target = f.args[1] if f.op == "imp" else f
    lits = []
    for part in flatten(target, "and"):
        if part.op == "atom" and part.name in outs:
            lits.append(part)
        elif (part.op == "not" and part.args[0].op == "atom"
              and part.args[0].name in outs):
            lits.append(part)
    return lits


def t_conflict_output(spec, g, rng):
    """Conjoin the negation of an output literal another guarantee asserts."""
    outs = set(spec["out_atoms"])
    others = [h for h in spec["guarantees"]
              if h is not g and not h.get("removed")]
    lits = []
    for h in others:
        try:
            lits += consequent_literals(parse(h["response"]), outs)
        except ValueError:
            pass
    if not lits:
        return None
    lit = rng.choice(lits)
    neg = lit.args[0] if lit.op == "not" else F("not", (lit,))
    f = parse(g["response"])
    if f.op == "imp":
        new = F("imp", (f.args[0], F("and", (f.args[1], neg))))
    else:
        new = F("and", (f, neg))
    return {**g, "response": show(new)}


def t_conflict_now(spec, g, rng):
    """conflict-output, with the edited requirement's deadline made immediate,
    so the two obligations fall on one step."""
    new = t_conflict_output(spec, g, rng)
    if new is None:
        return None
    return {**new, "timing": {"type": "Immediately"}}


GUARANTEE_EDITS = {
    "tighten-within": t_tighten_within,
    "bound-eventually": t_bound_eventually,
    "lengthen-for": t_lengthen_for,
    "continual": t_continual,
    "widen-condition": t_widen_condition,
    "drop-guard": t_drop_guard,
    "conjoin-output": t_conjoin_output,
    "drop-disjunct": t_drop_disjunct,
    "conflict-output": t_conflict_output,
}
# Used only where --extra-edits names it: the second pass over the systems
# whose first pass gave no unrealisable mutant.
EXTRA_EDITS = {"conflict-now": t_conflict_now}


# --------------------------------------------------------------------------
# Sideways edits (2026-11-18-seeded-corpus-sideways). Each models an error that
# in general neither strengthens nor weakens the requirement; compare decides
# the compound mutant's relation to B. Used only where --sideways is given.


def atom_paths(f: F, path: tuple = ()) -> list[tuple]:
    """Paths (child indices from the root) of every atom occurrence."""
    if f.op == "atom":
        return [path]
    return [q for k, a in enumerate(f.args) for q in atom_paths(a, path + (k,))]


def node_at(f: F, path: tuple) -> F:
    for k in path:
        f = f.args[k]
    return f


def replace_at(f: F, path: tuple, new: F) -> F:
    if not path:
        return new
    args = list(f.args)
    args[path[0]] = replace_at(args[path[0]], path[1:], new)
    return F(f.op, tuple(args), f.name)


def formula_fields(g) -> list[str]:
    return [k for k in ("condition", "response") if g.get(k, "true") != "true"]


def t_swap_signal(spec, g, rng):
    """One atom replaced by another atom of the same kind that occurs in B."""
    fields = formula_fields(g)
    if not fields:
        return None
    key = rng.choice(fields)
    f = parse(g[key])
    paths = atom_paths(f)
    if not paths:
        return None
    path = rng.choice(paths)
    name = node_at(f, path).name
    ins, outs = set(spec["in_atoms"]), set(spec["out_atoms"])
    pool = sorted((ins if name in ins else outs if name in outs else set()) - {name})
    if not pool:
        return None
    new = replace_at(f, path, F("atom", name=rng.choice(pool)))
    return {**g, key: show(new)}


def t_flip_literal(spec, g, rng):
    """One literal negated, or its negation removed."""
    fields = formula_fields(g)
    if not fields:
        return None
    key = rng.choice(fields)
    f = parse(g[key])
    paths = atom_paths(f)
    if not paths:
        return None
    path = rng.choice(paths)
    atom = node_at(f, path)
    if path and node_at(f, path[:-1]).op == "not":
        new = replace_at(f, path[:-1], atom)
    else:
        new = replace_at(f, path, F("not", (atom,)))
    return {**g, key: show(new)}


def t_reverse_implication(spec, g, rng):
    """A response p -> q written as q -> p."""
    f = parse(g["response"])
    if f.op != "imp":
        return None
    return {**g, "response": show(F("imp", (f.args[1], f.args[0])))}


SIDEWAYS_EDITS = {
    "swap-signal": t_swap_signal,
    "flip-literal": t_flip_literal,
    "reverse-implication": t_reverse_implication,
}


EDITS: dict = {}
ADMIT = {"strictly stronger than"}


def editable(spec: dict) -> list[int]:
    return [i for i, g in enumerate(spec["guarantees"])
            if not g.get("removed") and g.get("weakenable", True) is not False]


def seed_once(base: dict, rng: random.Random, n_edits: int):
    spec = copy.deepcopy(base)
    log = []
    tries = 0
    while len(log) < n_edits and tries < 50:
        tries += 1
        if spec.get("assumptions") and rng.random() < 0.1:
            i = rng.randrange(len(spec["assumptions"]))
            del spec["assumptions"][i]
            log.append({"edit": "drop-assumption", "slot": i})
            continue
        slots = editable(spec)
        if not slots:
            break
        i = rng.choice(slots)
        name = rng.choice(sorted(EDITS))
        try:
            new = EDITS[name](spec, spec["guarantees"][i], rng)
        except ValueError:
            new = None
        if new is None or new == spec["guarantees"][i]:
            continue
        spec["guarantees"][i] = new
        log.append({"edit": name, "slot": i})
    if len(log) < n_edits:
        return None
    return spec, log


# --------------------------------------------------------------------------
# Filters.


def run(cmd: list[str], timeout: int, stdin: str | None = None):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, input=stdin,
                              timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        return None


def realize(path: Path, timeout: int) -> str:
    out = run([str(BIN / "realize"), str(path)], timeout)
    if out is None:
        return "TIMEOUT"
    words = out.stdout.split()
    return words[-1] if out.returncode == 0 and words else "ERROR"


def relation(work: Path, mutant: Path, base: Path, timeout: int) -> str:
    r, i = work / "r", work / "i"
    for d in (r, i):
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
    shutil.copy(mutant, r / "o.json")
    shutil.copy(base, i / "b.json")
    out = run([str(BIN / "compare"), "--repairs", str(r), "--ideals", str(i),
               "--timeout", "60"], timeout)
    if out is None:
        return "timeout"
    # compare names the ideal after every relation but "incomparable".
    m = re.search(r"o\.json\s*:\s*(?:(incomparable)\s*$|(.+?) b\.json)",
                  out.stdout, re.M)
    if not m:
        return "error"
    return (m.group(1) or m.group(2)).strip()


def satisfiable(path: Path, timeout: int) -> bool | None:
    out = run([str(BIN / "ltl"), str(path)], timeout)
    if out is None or out.returncode != 0:
        return None
    parts = [ln.split("LTL:", 1)[1].strip() for ln in out.stdout.splitlines()
             if "LTL:" in ln]
    if not parts:
        return None
    phi = " & ".join(f"({p})" for p in parts)
    sat = run(["ltlfilt", "--satisfiable", "-F", "-"], timeout, stdin=phi + "\n")
    if sat is None:
        return None
    return bool(sat.stdout.strip())


def singles_realisable(work: Path, spec: dict, timeout: int) -> bool:
    for j in editable(spec):
        s = copy.deepcopy(spec)
        s["guarantees"] = [spec["guarantees"][j]]
        p = work / f"single{j}.json"
        p.write_text(json.dumps(s))
        if realize(p, timeout) != "REALIZABLE":
            return False
    return True


def check(task):
    """Run every filter on one candidate; returns a result row."""
    cid, base_id, base_path, spec, log, root, timeout = task
    work = root / "work" / cid
    work.mkdir(parents=True, exist_ok=True)
    mp = work / "spec.json"
    mp.write_text(json.dumps(spec, indent=2) + "\n")
    row = {"id": cid, "base": base_id, "n_edits": len(log),
           "edits": "+".join(e["edit"] for e in log), "stage": ""}
    rel = relation(work, mp, base_path, timeout)
    row["relation"] = rel
    if rel not in ADMIT:
        row["stage"] = "relation"
        return row, None
    v = realize(mp, timeout)
    row["realize"] = v
    if v != "UNREALIZABLE":
        row["stage"] = "realize"
        return row, None
    if not singles_realisable(work, spec, timeout):
        row["stage"] = "singles"
        return row, None
    sat = satisfiable(mp, timeout)
    row["satisfiable"] = sat
    if not sat:
        row["stage"] = "satisfiable"
        return row, None
    row["stage"] = "kept"
    return row, (spec, log)


def main() -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--bases-csv", type=Path, required=True)
    ap.add_argument("--per-base", type=int, default=40,
                    help="candidates drawn per base")
    ap.add_argument("--min-guarantees", type=int, default=3)
    ap.add_argument("--max-guarantees", type=int, default=20)
    ap.add_argument("--exclude-clusters", default="")
    ap.add_argument("--only-clusters", default="")
    ap.add_argument("--edits", default="2,3,4",
                    help="compound seed sizes to draw from")
    ap.add_argument("--extra-edits", default="",
                    help="comma list of EXTRA_EDITS to add to the mix")
    ap.add_argument("--sideways", action="store_true",
                    help="add SIDEWAYS_EDITS to the mix and admit mutants "
                         "incomparable with the base")
    ap.add_argument("--tag", default="",
                    help="suffix for mutant ids and the CSV, for a second pass")
    ap.add_argument("--seed", type=int, default=20261113)
    ap.add_argument("--jobs", type=int, default=12)
    ap.add_argument("--timeout", type=int, default=300)
    args = ap.parse_args()

    global EDITS, ADMIT
    EDITS = dict(GUARANTEE_EDITS)
    if args.sideways:
        EDITS.update(SIDEWAYS_EDITS)
        ADMIT = {"strictly stronger than", "incomparable"}
    for name in filter(None, args.extra_edits.split(",")):
        EDITS[name] = EXTRA_EDITS[name]
    sizes = [int(x) for x in args.edits.split(",")]
    only = set(filter(None, args.only_clusters.split(",")))
    excluded = set(args.exclude_clusters.split(",")) if args.exclude_clusters else set()
    bases = [r for r in csv.DictReader(args.bases_csv.open())
             if r["kept"] == "1" and r["cluster"] not in excluded
             and args.min_guarantees <= int(r["n_guarantees"]) <= args.max_guarantees
             and (not only or r["cluster"] in only)]
    bases_dir = args.bases_csv.parent / "bases"
    rng = random.Random(args.seed)
    tasks, seen = [], set()
    for b in bases:
        base_path = bases_dir / f"{b['id']}.json"
        base = json.loads(base_path.read_text())
        seen.add(json.dumps(base, sort_keys=True))
        drawn = 0
        for _ in range(args.per_base * 20):
            if drawn >= args.per_base:
                break
            res = seed_once(base, rng, rng.choice(sizes))
            if res is None:
                continue
            spec, log = res
            key = json.dumps(spec, sort_keys=True)
            if key in seen:
                continue
            seen.add(key)
            cid = f"{b['id']}__m{args.tag}{drawn:02d}"
            tasks.append((cid, b["id"], base_path, spec, log,
                          args.out, args.timeout))
            drawn += 1
        print(f"{b['id']}: {drawn} candidates", file=sys.stderr)

    rows = []
    subjects = args.out / "subjects"
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for row, kept in pool.map(check, tasks):
            b = next(x for x in bases if x["id"] == row["base"])
            row["cluster"] = b["cluster"]
            rows.append(row)
            print(f"{row['id']}: {row['stage']} ({row['edits']})", file=sys.stderr)
            if kept:
                spec, log = kept
                d = subjects / row["id"]
                (d / "fixes").mkdir(parents=True, exist_ok=True)
                (d / "spec.json").write_text(json.dumps(spec, indent=2) + "\n")
                shutil.copy(bases_dir / f"{row['base']}.json", d / "fixes" / "base.json")
                (d / "seed.json").write_text(json.dumps(
                    {"base": row["base"], "cluster": b["cluster"], "edits": log},
                    indent=2) + "\n")

    fields = ["id", "base", "cluster", "n_edits", "edits", "relation",
              "realize", "satisfiable", "stage"]
    name = f"mutants{'-' + args.tag if args.tag else ''}.csv"
    with (args.out / name).open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    kept_n = sum(r["stage"] == "kept" for r in rows)
    print(f"{kept_n} of {len(rows)} candidates kept", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
