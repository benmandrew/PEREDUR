#!/usr/bin/env python3
"""Check import_fret's domain constraints against Lift-Plus-Cruise.

The two lift-plus-cruise examples were transcribed by hand, with kias and
wind on a 10-knot threshold grid and lift_mode one-hot, and their
consistency written as non-weakenable requirements. This regenerates those
from the atoms alone and checks two things:

- each generated constraint is equivalent to the hand-written one, over
  every valuation of its atoms;
- with the hand-written constraints swapped for the generated ones, realize
  reproduces every realizability claim of the paper and its technical report.

A third arm with no constraints at all is printed for reference: it shows
which verdicts the constraints actually carry.

Needs a built `realize`: set PEREDUR_REALIZE, or it defaults to
build-release/realize. Run with ``python3 scripts/test_domain_constraints_lpc.py``.
"""

import copy
import itertools
import json
import os
import re
import subprocess
import sys
import tempfile
from fractions import Fraction
from pathlib import Path

import import_fret as I

ROOT = Path(__file__).resolve().parent.parent
REALIZE = os.environ.get("PEREDUR_REALIZE",
                         str(ROOT / "build-release" / "realize"))
LIFT_MODES = ["lm_tb", "lm_stb", "lm_swb", "lm_wb"]
TYPES = {"kias": "double", "kgs": "double", "wind": "double",
         "lift_mode": "integer"}


def fail(msg):
    print(f"FAIL: {msg}")
    sys.exit(1)


def predicates(atoms):
    """What each LPC atom compares, read off its name."""
    preds = {}
    for a in atoms:
        m = re.fullmatch(r"(kias|kgs|wind)_(ge|gt)_(m?)(\d+)", a)
        if m:
            sign = -1 if m.group(3) else 1
            preds[a] = (m.group(1), m.group(2), sign * Fraction(m.group(4)))
        elif a in LIFT_MODES:
            preds[a] = ("lift_mode", "eq", Fraction(LIFT_MODES.index(a)))
    return preds


def truth(formula, env):
    """Evaluate the `(a -> b)` / `!(a & b)` conjunctions both sides use."""
    py = re.sub(r"\((\w+) -> (\w+)\)", r"(not \1 or \2)", formula)
    py = py.replace("&", " and ").replace("|", " or ").replace("!", " not ")
    return eval(py, {}, dict(env))  # noqa: S307 -- names are spec atoms


def equivalent(a, b, atoms):
    return all(truth(a, dict(zip(atoms, v))) == truth(b, dict(zip(atoms, v)))
               for v in itertools.product((True, False), repeat=len(atoms)))


def fixed(spec):
    return [r for k in ("assumptions", "guarantees") for r in spec[k]
            if r.get("weakenable") is False]


def strip(spec):
    out = copy.deepcopy(spec)
    for k in ("assumptions", "guarantees"):
        out[k] = [r for r in out[k] if r.get("weakenable") is not False]
    return out


def generated(spec):
    out = strip(spec)
    comparisons = {a: (op, ("var", v), ("num", c)) for a, (v, op, c)
                   in predicates(spec["in_atoms"] + spec["out_atoms"]).items()}
    assumptions, guarantees, _ = I.domain_requirements(
        comparisons, TYPES, spec["in_atoms"], spec["out_atoms"])
    out["assumptions"] += assumptions
    out["guarantees"] += guarantees
    return out


def within(spec, ticks):
    out = copy.deepcopy(spec)
    [r] = [r for r in out["guarantees"] if r["timing"]["type"] == "WithinTicks"
           and r["response"] == "lm_tb"]
    r["timing"]["ticks"] = ticks
    return out


def without_kias_0(spec):
    out = copy.deepcopy(spec)
    out["guarantees"] = [r for r in out["guarantees"]
                         if (r["response"], r["timing"]["type"])
                         != ("kias_ge_0", "Always")]
    dropped = len(spec["guarantees"]) - len(out["guarantees"])
    if dropped != 1:
        fail(f"expected one KIAS_0 guarantee, found {dropped}")
    return out


def load(path):
    return json.loads((ROOT / "examples" / path).read_text())


mini = load("lift-plus-cruise-mini/spec.json")
mini_fix = load("lift-plus-cruise-mini/fixes/paper-reach-hover-11.json")
full = load("lift-plus-cruise-full/spec.json")
wind_20 = load("lift-plus-cruise-full/fixes/paper-wind-20.json")

# Each generated constraint matches the hand-written one on its variable.
for spec, name in ((mini, "mini"), (full, "full")):
    preds = predicates(spec["in_atoms"] + spec["out_atoms"])
    ours = generated(spec)
    for hand in fixed(spec):
        atoms = sorted(set(re.findall(r"\w+", hand["response"])))
        variables = {preds[a][0] for a in atoms}
        if len(variables) != 1:
            fail(f"{name}: hand constraint spans {variables}")
        [var] = variables
        side = "assumptions" if atoms[0] in spec["in_atoms"] else "guarantees"
        mine = [r for r in fixed(ours) if r in ours[side]
                and {preds[a][0] for a in re.findall(r"\w+", r["response"])}
                == {var}]
        if len(mine) != 1:
            fail(f"{name}: {len(mine)} generated {side} for {var}")
        if not equivalent(hand["response"], mine[0]["response"], atoms):
            fail(f"{name}: {var} differs\n  hand: {hand['response']}\n"
                 f"  ours: {mine[0]['response']}")
        print(f"ok: {name} {var} ({len(atoms)} atoms, {side[:-1]}) is "
              f"equivalent to the hand-written constraint"
              f"{', and textually equal' if hand['response'] == mine[0]['response'] else ''}")
    extra = [r["response"] for r in fixed(ours)
             if not any(preds[a][0] in {preds[b][0] for h in fixed(spec)
                                        for b in re.findall(r"\w+", h["response"])}
                        for a in re.findall(r"\w+", r["response"]))]
    for e in extra:
        print(f"   {name} also gains: {e}")

# The paper's claims (REFSQ 2023 section 4.2, NASA report section 4.3).
CASES = [
    ("mini, REACH_HOVER 10", mini, "UNREALIZABLE"),
    ("mini, REACH_HOVER 11", mini_fix, "REALIZABLE"),
    ("full, |wind| <= 30, 16 ticks", full, "UNREALIZABLE"),
    ("full, |wind| <= 30, 16 ticks, no KIAS_0", without_kias_0(full),
     "REALIZABLE"),
    ("full, |wind| <= 20, 16 ticks", wind_20, "REALIZABLE"),
    ("full, |wind| <= 20, 13 ticks", within(wind_20, 13), "REALIZABLE"),
    ("full, |wind| <= 20, 12 ticks", within(wind_20, 12), "UNREALIZABLE"),
]
ARMS = [("hand", lambda s: s), ("generated", generated), ("none", strip)]

with tempfile.TemporaryDirectory() as d:
    paths = []
    for i, (_, spec, _) in enumerate(CASES):
        for arm, make in ARMS:
            p = Path(d) / f"{i}-{arm}.json"
            p.write_text(json.dumps(make(spec), indent=2))
            paths.append(str(p))
    run = subprocess.run([REALIZE, *paths], capture_output=True, text=True,
                         timeout=1800)
    if run.returncode != 0:
        fail(f"realize exited {run.returncode}: {run.stderr.strip()}")
    verdict = dict(line.rsplit(": ", 1) for line in run.stdout.splitlines())

bad = []
print(f"\n{'case':44} {'paper':13} {'hand':13} {'generated':13} none")
for i, (name, _, want) in enumerate(CASES):
    got = {arm: verdict[str(Path(d) / f"{i}-{arm}.json")] for arm, _ in ARMS}
    print(f"{name:44} {want:13} {got['hand']:13} {got['generated']:13} "
          f"{got['none']}")
    bad += [f"{name}: {arm} is {got[arm]}, paper says {want}"
            for arm in ("hand", "generated") if got[arm] != want]
if bad:
    fail("\n  ".join(bad))
print("\nok: generated constraints reproduce every Lift-Plus-Cruise claim")
