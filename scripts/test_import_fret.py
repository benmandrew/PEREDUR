#!/usr/bin/env python3
"""Tests for import_fret.py over a synthetic FRET export.

No pytest dependency: run it directly (``python3 scripts/test_import_fret.py``)
and it exits non-zero on the first failure. Each requirement below exercises
one conversion rule, with its `semantics` shaped as FRET's formaliser writes
them.
"""

import json
import sys
import tempfile
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
                      kw.get("as_output", []))


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

print("ok: all FRET import checks pass")
