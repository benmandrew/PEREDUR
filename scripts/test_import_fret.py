#!/usr/bin/env python3
"""Tests for import_fret.py over a synthetic FRET export.

No pytest dependency: run it directly (``python3 scripts/test_import_fret.py``)
and it exits non-zero on the first failure. Each requirement below exercises
one conversion rule, with its `semantics` shaped as FRET's formaliser writes
them.
"""

import itertools
import json
import random
import sys
import tempfile
from fractions import Fraction
from pathlib import Path

import import_fret as I  # noqa: E402


def req(reqid, **sem):
    sem.setdefault("scope", {"type": "null"})
    sem.setdefault("condition", "null")
    sem.setdefault("probability", "none")
    sem.setdefault("component_name", "Robot")
    sem.setdefault("ft", "stub")
    return {"reqid": reqid, "semantics": sem}


def var(name, id_type):
    return {"variable_name": name, "idType": id_type}


EXPORT = {
    "requirements": [
        req("TRIG", condition="regular", regular_condition="(startButton)",
            timing="immediately", post_condition="(valve & !alarm)"),
        req("HOLD", condition="holding", regular_condition="(level >= 0.9)",
            timing="within", duration="5", post_condition="(drain)"),
        req("NULLCOND", timing="before", stop_condition="(move)",
            post_condition="(checked)"),
        req("NOTIME", condition="regular", regular_condition="(startButton)",
            timing="null", probability="bound", post_condition="(valve)"),
        req("NXT", condition="regular", regular_condition="(fault)",
            timing="immediately",
            post_condition="(alarm & Nxt ( valve ))"),
        req("SCOPED", scope={"type": "in"}, scope_mode="dressingMode",
            timing="always", post_condition="(TRUE -> drain)"),
        req("DUP", condition="regular", regular_condition="(startButton)",
            timing="immediately", post_condition="(valve & !alarm)"),
    ],
    "variables": [
        var("startButton", "Input"),
        var("valve", "Output"),
        var("alarm", ""),
        var("drain", "Output"),
        var("drain", "Input"),
    ],
}


def check(got, want, msg):
    if got != want:
        print(f"FAIL: {msg}\n  got:  {got!r}\n  want: {want!r}")
        sys.exit(1)


def load(export, **kw):
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "export.json"
        path.write_text(json.dumps(export))
        return I.load([path], kw.get("component"), kw.get("as_input", []),
                      kw.get("as_output", []), kw.get("all_components", False),
                      kw.get("atomise", False), kw.get("from_fulltext", ()),
                      kw.get("skip", ()), kw.get("merge_case", False),
                      kw.get("domains", True))


spec, conv = load(EXPORT)
g = spec["guarantees"]
check(len(g), 7, "six requirements, one split by Nxt, one duplicate merged")
check(conv.reqids[0], ["TRIG", "DUP"], "duplicate records both reqids")
check(g[0], {"condition": "start_button", "condition-type": "trigger",
             "response": "valve & !alarm", "timing": {"type": "Immediately"}},
      "regular condition is a trigger, names become snake_case")
check(g[1], {"condition": "level_ge_0p9", "condition-type": "continual",
             "response": "drain",
             "timing": {"type": "WithinTicks", "ticks": 5}},
      "holding is continual, a comparison becomes an atom")
check(g[2], {"condition": "true", "condition-type": "trigger",
             "response": "checked",
             "timing": {"type": "Before", "stop": "move"}},
      "a missing condition is a trigger on true")
check(g[3]["timing"], {"type": "Eventually"}, "a missing timing is eventually")
check(conv.notes["probability dropped"], ["NOTIME"], "probability is reported")
check([(x["response"], x["timing"]["type"]) for x in g[4:6]],
      [("alarm", "Immediately"), ("valve", "NextTimepoint")],
      "Nxt moves into its own NextTimepoint requirement")
check(g[6], {"condition": "true", "condition-type": "trigger",
             "response": "true -> drain", "timing": {"type": "Always"},
             "scope": {"type": "In", "mode": "dressing_mode"}},
      "an in scope keeps its mode, TRUE is lowercased")
check(spec["modes"], ["dressing_mode"], "modes are declared")
check(spec["in_atoms"], ["fault", "level_ge_0p9", "start_button"],
      "FRET's Input label and read-only atoms are inputs")
check(spec["out_atoms"], ["alarm", "checked", "drain", "move", "valve"],
      "labelled outputs, unlabelled responses and stops, and a response "
      "labelled both ways are outputs")

spec, _ = load(EXPORT, as_input=["drain"])
check("drain" in spec["in_atoms"], True, "--as-input overrides the role")


def rejects(requirement, fragment, msg):
    try:
        load({"requirements": [requirement], "variables": []})
    except I.FretImportError as e:
        check(fragment in str(e), True, f"{msg}: {e}")
        return
    print(f"FAIL: {msg}: accepted")
    sys.exit(1)


rejects(req("X", timing="always", post_condition="(a xor b)"), "xor",
        "xor has no spelling in the grammar")
rejects(req("X", timing="always", post_condition="(a + b > 2)"), "'+'",
        "arithmetic is rejected")
rejects(req("X", timing="finally", post_condition="(a)"), "finally",
        "a timing with no counterpart is rejected")
rejects(req("X", timing="next", post_condition="(a & Nxt(b))"), "Nxt",
        "Nxt under a timing other than immediately is rejected")
rejects(req("X", timing="always", post_condition="(fooBar & foo_bar)"),
        "both become", "names colliding after snake_case are rejected")
check(load({"requirements": [req("A", timing="always", post_condition="(p)"),
                             req("B", timing="always", post_condition="(q)",
                                 component_name="Human")],
            "variables": []}, component="Human")[0]["out_atoms"], ["q"],
      "--component keeps one component")
try:
    load({"requirements": [req("A", timing="always", post_condition="(p)"),
                           req("B", timing="always", post_condition="(q)",
                               component_name="Human")], "variables": []})
    print("FAIL: several components accepted without --component")
    sys.exit(1)
except I.FretImportError:
    pass


def responses(requirements, **kw):
    spec, conv = load(requirements, **kw)
    return [g["response"] for g in spec["guarantees"]], spec, conv


# Older FRET writes a bare list of requirements, with list durations.
got, spec, _ = responses([
    req("EQ", timing="always", post_condition="((a | b) = x)"),
    req("BOOL", timing="always", post_condition="(u = v & w)"),
    req("NUM", timing="always", post_condition="(s = t)"),
    req("LIT", timing="always", post_condition="(ridgeOn = TRUE)"),
    req("IMP", timing="always", post_condition="(IMUFail => ( Stop ))"),
    req("LIST", timing="for", duration=["3"], post_condition="(u)"),
], as_output=["s", "t"])
check(got, ["(a | b) <-> x", "(u <-> v) & w", "s_eq_t", "ridge_on",
            "imu_fail -> ( stop )", "u"],
      "= is iff beside a formula or a bare name, else an atom; => is ->; "
      "acronyms split")
check(spec["guarantees"][5]["timing"], {"type": "ForTicks", "ticks": 3},
      "a one-element list duration is read")

got, _, _ = responses({"requirements": [
    req("ARITH", timing="always", post_condition="(abs(x) < y + 1)")],
    "variables": []}, atomise=True, as_output=["x", "y"])
check(got, ["abs_x_lt_y_plus_1"],
      "--atomise-arithmetic turns an arithmetic comparison into an atom")
rejects(req("X", timing="always", post_condition="(abs(x) => y)"), "abs",
        "a function outside a comparison is rejected")

lossy = dict(req("DROP", timing="always", post_condition="((a | b))"),
             fulltext="R shall always satisfy (a | b) = x")
rejects(lossy, "lacks ['x']", "a name FRET's parse dropped is rejected")
got, _, conv = responses([lossy], from_fulltext=["DROP"])
check(got, ["(a | b) <-> x"], "--from-fulltext reads the response itself")

placeholder = {"reqid": "EMPTY", "fulltext": "", "semantics": {}}
prose = {"reqid": "PROSE", "fulltext": "The system shall be good.",
         "semantics": {"type": "nasa"}}
_, _, conv = responses([placeholder, req("A", timing="always",
                                         post_condition="(p)")])
check(conv.notes["empty, skipped"], ["EMPTY"], "a placeholder is skipped")
rejects(prose, "did not formalise", "an unformalised requirement is rejected")
_, _, conv = responses([prose], skip=["PROSE"])
check(conv.notes["skipped by --skip"], ["PROSE"], "--skip leaves one out")

cased = [req("A", timing="always", post_condition="(explain)"),
         req("B", timing="always", post_condition="(Explain)")]
try:
    load(cased)
    print("FAIL: case variants accepted without --merge-case")
    sys.exit(1)
except I.FretImportError:
    pass
got, _, _ = responses(cased, merge_case=True)
check(got, ["explain"], "--merge-case reads case variants as one atom")

spec, _ = load([req("A", timing="always",
                    post_condition="(uiBypass0 & uiBypass1)"),
                req("B", timing="always", post_condition="(q)",
                    component_name="Human")],
               all_components=True, as_input=["ui_bypass*"])
check((spec["in_atoms"], spec["out_atoms"]),
      (["ui_bypass0", "ui_bypass1"], ["q"]),
      "--all-components keeps every component; --as-input takes patterns")

spec, conv = load({"requirements": [
    req("LO", timing="always", post_condition="(speed >= 10 -> fast)"),
    req("HI", timing="always", post_condition="(speed >= 20 -> faster)"),
    req("NEG", timing="always", post_condition="(-5 < speed -> moving)"),
    req("MODE", timing="always",
        post_condition="((mode = 0 -> idle) & (mode = 1 -> busy))"),
    req("GUST", condition="regular", regular_condition="(gust > 3)",
        timing="immediately", post_condition="(slow)"),
    req("CALM", condition="regular", regular_condition="(gust <= 1)",
        timing="immediately", post_condition="(fast)"),
    req("LEVEL", timing="always",
        post_condition="((level > 2 -> fast) & (level > 4 -> faster))"),
    req("PAIR", timing="always", post_condition="(a < b -> fast)")],
    "variables": [var("speed", "Input"), var("mode", "Input"),
                  var("fast", "Output"), var("faster", "Output"),
                  var("moving", "Output"), var("idle", "Output"),
                  var("busy", "Output"), var("slow", "Output"),
                  var("level", "Output"), var("a", "Output"),
                  var("b", "Output"),
                  dict(var("gust", "Input"), dataType="integer"),
                  dict(var("mode", "Input"), dataType="integer")]},
    atomise=True)
domain = {r["response"].split("_")[0].lstrip("(!"): r["response"]
          for r in spec["assumptions"] + spec["guarantees"]
          if r.get("weakenable") is False}
check(domain["speed"],
      "(speed_ge_10 -> minus_5_lt_speed) & (speed_ge_20 -> speed_ge_10)",
      "an input variable's comparisons are ordered by an assumption, with a "
      "flipped comparison and a negative constant read right")
check([domain["gust"], domain["mode"]],
      ["!(gust_gt_3 & gust_le_1)", "!(mode_eq_0 & mode_eq_1)"],
      "integer comparisons that no value satisfies together are excluded")
check(domain["level"], "(level_gt_4 -> level_gt_2)",
      "an output variable's comparisons are ordered by a guarantee")
check("mode_eq_0" in spec["in_atoms"], True,
      "a comparison atom takes its variable's label, not its role")
check(spec["guarantees"][-1]["weakenable"], False,
      "domain constraints are not weakenable")
check("a" in domain, False,
      "a lone comparison of two names allows both values, so adds nothing")
check(conv.reqids[-1], ["domain"], "--ids marks a domain constraint")
spec, _ = load({"requirements": [
    req("LO", timing="always", post_condition="(speed >= 10 -> fast)"),
    req("HI", timing="always", post_condition="(speed >= 20 -> faster)")],
    "variables": []}, domains=False)
check(spec["assumptions"], [], "--no-domains adds nothing")
rejects(req("X", condition="regular", regular_condition="(v > 1)",
            timing="immediately", post_condition="(v > 2)"),
        "both inputs", "a variable compared on both sides is rejected")


def oracle(preds, integer, unsigned):
    """The truth vectors the predicates take over a grid fine enough to
    land inside every region their quarter-integer constants cut."""
    step = Fraction(1) if integer else Fraction(1, 8)
    xs = [Fraction(n) * step for n in range(int(-12 / step), int(12 / step))]
    return {tuple(I.HOLDS[op](x, c) for op, c in preds)
            for x in xs if not unsigned or x >= 0}


rng = random.Random(0)
for trial in range(400):
    integer = rng.random() < 0.5
    unsigned = integer and rng.random() < 0.3
    preds = [(rng.choice(list(I.HOLDS)), Fraction(rng.randint(-24, 24), 4))
             for _ in range(rng.randint(1, 6))]
    clauses = I.domain_clauses(preds, integer, unsigned)
    models = {m for m in itertools.product((True, False), repeat=len(preds))
              if all(I.satisfies(m, c) for c in clauses)}
    check(models, oracle(preds, integer, unsigned),
          f"trial {trial}: clauses admit exactly the realisable vectors of "
          f"{preds}, integer={integer}, unsigned={unsigned}")

var_a, var_b = ("var", "a"), ("var", "b")
for trial in range(150):
    integer = rng.random() < 0.5
    unsigned = integer and rng.random() < 0.3
    preds = [(rng.choice(list(I.HOLDS)), Fraction(rng.randint(-24, 24), 4))
             for _ in range(rng.randint(1, 5))]
    types = {"a": "unsigned integer" if unsigned else
             "integer" if integer else "double"}
    check(I.smt_vectors([(op, var_a, ("num", c)) for op, c in preds], types),
          I.sampled_vectors(preds, integer, unsigned),
          f"trial {trial}: z3 and sampling agree on {preds}, {types}")


def two_variable_oracle(comparisons, integer):
    step = Fraction(1) if integer else Fraction(1, 4)
    grid = [Fraction(n) * step for n in range(int(-16 / step), int(16 / step))]

    def value(t, env):
        if t[0] == "var":
            return env[t[1]]
        if t[0] == "num":
            return t[1]
        return {"+": lambda x, y: x + y, "-": lambda x, y: x - y}[t[0]](
            value(t[1], env), value(t[2], env))
    return {tuple(I.HOLDS[op](value(x, env), value(y, env))
                  for op, x, y in comparisons)
            for a in grid for b in grid for env in [{"a": a, "b": b}]}


SIDES = [var_a, var_b, ("+", var_a, var_b), ("-", var_a, var_b)]
for trial in range(60):
    integer = rng.random() < 0.5
    comparisons = [(rng.choice(list(I.HOLDS)), rng.choice(SIDES),
                    rng.choice([("num", Fraction(rng.randint(-4, 4))),
                                var_b]))
                   for _ in range(rng.randint(2, 4))]
    kind = "integer" if integer else "double"
    got = I.smt_vectors(comparisons, {"a": kind, "b": kind})
    check(got, two_variable_oracle(comparisons, integer),
          f"trial {trial}: z3 finds exactly the realisable vectors of "
          f"{comparisons}, {kind}")
    clauses = I.exact_clauses(got, len(comparisons))
    check({m for m in itertools.product((True, False),
                                        repeat=len(comparisons))
           if all(I.satisfies(m, c) for c in clauses)}, got,
          f"trial {trial}: clauses admit exactly z3's vectors")

spec, conv = load({"requirements": [
    req("AB", timing="always", post_condition="(a < b -> p)"),
    req("BC", timing="always", post_condition="(b < c -> q)"),
    req("AC", timing="always", post_condition="(a < c -> r)"),
    req("SUM", timing="always",
        post_condition="(x + y > 10 -> p & (x > 5 -> q) & (y > 5 -> r))")],
    "variables": [var(v, "Input") for v in "abcxy"]
    + [var(v, "Output") for v in "pqr"]
    + [dict(var(v, "Input"), dataType="integer") for v in "xy"]},
    atomise=True)
check([r["response"] for r in spec["assumptions"]],
      ["(a_lt_b & b_lt_c -> a_lt_c) & (a_lt_c -> a_lt_b | b_lt_c)",
       "(x_gt_5 & y_gt_5 -> x_plus_y_gt_10) & "
       "(x_plus_y_gt_10 -> x_gt_5 | y_gt_5)"],
      "z3 constrains comparisons across variables and over arithmetic")
check(sorted(conv.notes["domain constraint"]),
      ["a, b, c (z3): 2 clause(s)", "x, y (z3): 2 clause(s)"],
      "the summary names the solver")


def rejects_with(export, fragment, msg, **kw):
    try:
        load(export, **kw)
    except I.FretImportError as e:
        check(fragment in str(e), True, f"{msg}: {e}")
        return
    print(f"FAIL: {msg}: accepted")
    sys.exit(1)


across = [req("A", condition="regular", regular_condition="(p)",
              timing="immediately", post_condition="(a < b & !(b > 5))")]
rejects_with({"requirements": across,
              "variables": [var("a", "Input"), var("b", "Output")]},
             "compares inputs ['a'] with outputs ['b']",
             "a comparison of an input with an output is rejected")
rejects_with({"requirements": across, "variables": [var("b", "Output")]},
             "compares ['a'], with no single Input/Output label",
             "a comparison over an unlabelled variable is rejected")
rejects_with({"requirements": across,
              "variables": [var("a", "Input"), var("a", "Output"),
                            var("b", "Output")]},
             "compares ['a'], with no single Input/Output label",
             "a variable labelled both ways is not guessed")
spec, _ = load({"requirements": across, "variables": [var("b", "Output")]},
               as_output=["a"])
check("a_lt_b" in spec["out_atoms"], True,
      "--as-output on a variable gives its comparisons their side")
spec, _ = load({"requirements": across, "variables": []},
               as_input=["a", "b"])
check({"a_lt_b", "b_gt_5"} <= set(spec["in_atoms"]), True,
      "comparisons take their variables' side, not the role rule's")
rejects_with({"requirements": across, "variables": [var("b", "Output")]},
             "forced to the other side", "an atom forced against its "
             "variables is rejected", as_output=["a"], as_input=["a_lt_b"])

print("ok: all FRET import checks pass")
