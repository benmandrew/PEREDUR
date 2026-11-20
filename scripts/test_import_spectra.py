#!/usr/bin/env python3
"""Tests for import_spectra.py over small inline Spectra snippets.

No pytest dependency: run it directly (``python3 scripts/test_import_spectra.py``)
and it exits non-zero on a failure. pytest also collects the ``test_``
functions. Nothing here reads the spectra-specs checkout or runs ltlsynt; the
one Spot check is skipped when ``ltlfilt`` is not on the path.
"""

import itertools
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import import_spectra as I  # noqa: E402

HEADER = """import "../patterns/DwyerPatterns.spectra"
module Snippet
"""


def spec(body):
    return I.import_spec(HEADER + body, "snippet.spectra", "c0ffee")


def entries(s, side):
    return [e for e in s[side] if e.get("kind") != "domain"]


def raises(body, cls, text):
    try:
        spec(body)
    except cls as exc:
        assert text in str(exc), f"{text!r} not in {exc!r}"
        return
    raise AssertionError(f"no {cls.__name__} for {body!r}")


def lowering(body):
    return I.Lowering(I.parse(HEADER + body))


def evaluate(low, expr):
    node = I.Parser(I.tokenize(expr)).expr()
    return low.eval(node, 0, {})


def truth(f, a, env):
    """The value of formula a under env, a map from atom to bool."""
    node = f.nodes[a]
    op = node[0]
    if op == "T":
        return True
    if op == "F":
        return False
    if op == "ap":
        assert node[2] == 0, "a temporal atom in a propositional check"
        return env[node[1]]
    if op == "not":
        return not truth(f, node[1], env)
    if op == "and":
        return all(truth(f, b, env) for b in node[1])
    if op == "or":
        return any(truth(f, b, env) for b in node[1])
    if op == "iff":
        return truth(f, node[1], env) == truth(f, node[2], env)
    return (not truth(f, node[1], env)) or truth(f, node[2], env)


def assignments(low, names):
    """Every value of the named integer variables, with the atom env that
    encodes it."""
    domains = []
    for name in names:
        var = low.vars[name]
        atoms, values = var.cells[()]
        domains.append([(v, {a: bool((code >> j) & 1)
                             for j, a in enumerate(atoms)})
                        for code, v in enumerate(values)])
    for combo in itertools.product(*domains):
        env = {}
        for _, bits in combo:
            env.update(bits)
        yield [v for v, _ in combo], env


def test_comments_are_stripped():
    text = "a /* b\n c */ d // e\n f -- g\n h"
    assert I.strip_comments(text).split() == ["a", "d", "f", "h"]
    assert I.strip_comments("x /*\n\n*/ y").count("\n") == 2


def test_declarations_and_kinds():
    s = spec("""
type Mode = {IDLE, RUN, STOP};
env boolean req;
env Int(0..5) level;
sys Mode mode;
sys boolean[2] grant;
aux boolean busy;
define on := mode = RUN;
asm named: G (req -> next(!req));
asm req = false;
gar GF on;
gar alwEv busy;
gar G grant[0] -> on;
weight w: -3 next(on);
""")
    assert s["in_atoms"] == ["e_req", "e_level__b0", "e_level__b1",
                             "e_level__b2"]
    assert s["out_atoms"] == ["s_mode__b0", "s_mode__b1", "s_grant__i0",
                              "s_grant__i1", "a_busy"]
    doms = [e for e in s["assumptions"] if e.get("kind") == "domain"]
    assert [e["name"] for e in doms] == ["domain:level"]
    assert doms[0]["fixed"] == "G(!e_level__b2 | !e_level__b1)", doms
    sys_doms = [e for e in s["guarantees"] if e.get("kind") == "domain"]
    assert [e["name"] for e in sys_doms] == ["domain:mode"]
    a = entries(s, "assumptions")
    assert [(e["name"], e["kind"]) for e in a] == [("named", "safety"),
                                                   (None, "ini")]
    assert a[0]["fixed"] == "G(e_req -> !X(e_req))"
    assert a[1]["fixed"] == "!e_req"
    g = entries(s, "guarantees")
    assert [e["kind"] for e in g] == ["justice", "justice", "safety"]
    assert g[0]["fixed"] == "G(F(s_mode__b0 & !s_mode__b1))", g[0]
    assert g[1]["fixed"] == "G(F(a_busy))"
    assert g[2]["fixed"] == "G(s_grant__i0 -> s_mode__b0 & !s_mode__b1)"
    assert all(not e["weakenable"] for e in s["assumptions"] +
               s["guarantees"])
    assert s["commit"] == "c0ffee" and s["source"] == "snippet.spectra"


def test_atom_names_pass_the_screen():
    s = spec("""
env boolean Fail;
env boolean gate;
env Int(0..2) XPos;
sys boolean Go;
gar G (Fail | gate | XPos = 1 | Go);
""")
    unsafe = re.compile(r"^[FGX][A-Za-z_]")
    for a in s["in_atoms"] + s["out_atoms"]:
        assert not unsafe.match(a), a
    for a in s["in_atoms"]:
        assert a == a.lower(), a
    assert "e_fail" in s["in_atoms"] and "e_xpos__b1" in s["in_atoms"]


def test_patterns_map_to_psp_fields():
    s = spec("""
env boolean a;
env boolean b;
sys boolean c;
gar resp: pRespondsToS(a, c);
gar pHolds_afterQuntilR(c, a, b);
asm pBecomesTrue_betweenQandR(b, a, c);
""")
    g = s["guarantees"]
    assert g[0] == {"pattern": "response", "scope": "globally", "p": "e_a",
                    "s": "s_c", "name": "resp", "weakenable": True}, g[0]
    assert g[1] == {"pattern": "universality", "scope": "afteruntil",
                    "s": "s_c", "q": "e_a", "r": "e_b", "name": None,
                    "weakenable": True}, g[1]
    a = s["assumptions"][0]
    assert a == {"pattern": "existence", "scope": "between", "s": "e_b",
                 "q": "e_a", "r": "s_c", "name": None,
                 "weakenable": True}, a
    counts = I.slot_counts(s)
    assert counts["guarantees_weakenable"] == 2
    assert counts["guarantees_event_scoped"] == 1
    assert counts["assumptions_event_scoped"] == 1
    assert I.entry_ltl(g[1]) == "G(((e_a) & !(e_b)) -> ((s_c) W (e_b)))"


def test_a_pattern_under_g_is_the_pattern():
    s = spec("""
env boolean a;
sys boolean c;
gar G (pRespondsToS(a, c));
""")
    assert s["guarantees"][0]["pattern"] == "response"


def test_a_temporal_pattern_argument_is_fixed():
    s = spec("""
env boolean a;
sys boolean c;
gar pRespondsToS(a, next(c));
""")
    e = s["guarantees"][0]
    assert e["fixed"] == "G((e_a) -> F(X(s_c)))", e
    assert not e["weakenable"]
    assert e["kind"] == "pattern-temporal-argument"


def test_prev_is_shifted_one_step():
    s = spec("""
env boolean a;
sys boolean b;
gar G (PREV(a) -> b);
gar G (PREV(!a) | b);
asm GF PREV(a);
gar PREV(a) | b;
""")
    g = entries(s, "guarantees")
    # PREV is false at time 0, so the first step of the first constraint is
    # vacuous and of the second it is `b`.
    assert g[0]["fixed"] == "G(e_a -> X(s_b))", g[0]
    assert g[1]["fixed"] == "(s_b) & (G(!e_a | X(s_b)))", g[1]
    assert g[2]["fixed"] == "s_b", g[2]
    assert s["assumptions"][0]["fixed"] == "G(F(e_a))"


def test_monitors_are_outputs_with_fixed_guarantees():
    s = spec("""
env boolean sound;
monitor boolean clapped {
  !clapped;
  G sound <-> next(clapped) = !clapped;
}
""")
    assert s["out_atoms"] == ["m_clapped"]
    assert [e["fixed"] for e in s["guarantees"]] == [
        "!m_clapped", "G(e_sound <-> (X(m_clapped) <-> !m_clapped))"]


def test_predicates_expand_by_name():
    s = spec("""
sys Int(0..3) m;
sys boolean up;
predicate step(boolean cond, Int(0..3) x, Int(0..2) d):
  cond <-> (next(x) = x + d - 1);
predicate stay(boolean cond) { cond -> next(m) = m }
gar G step(up, m, 2);
gar G stay(!up);
""")
    low = lowering("""
sys Int(0..3) m;
sys boolean up;
""")
    assert len(entries(s, "guarantees")) == 2
    assert "X(s_m__b0)" in s["guarantees"][0]["fixed"]
    assert low.vars["m"].cells[()][0] == ["s_m__b0", "s_m__b1"]


def test_integer_comparisons_match_python():
    low = lowering("""
env Int(0..5) x;
env Int(0..5) y;
env Int(2..4) z;
define k := 3;
""")
    f = low.f
    exprs = {
        "x < y": lambda x, y, z: x < y,
        "x >= y": lambda x, y, z: x >= y,
        "x = y": lambda x, y, z: x == y,
        "x != 3": lambda x, y, z: x != 3,
        "x <= k": lambda x, y, z: x <= 3,
        "x + 1 = y": lambda x, y, z: x + 1 == y,
        "y - x = 1": lambda x, y, z: y - x == 1,
        "-1 <= y - x & y - x <= 1": lambda x, y, z: abs(y - x) <= 1,
        "z > x": lambda x, y, z: z > x,
        "z = x - 1": lambda x, y, z: z == x - 1,
        "x * 2 = z + 2": lambda x, y, z: x * 2 == z + 2,
        "!(x < z | x > z)": lambda x, y, z: x == z,
    }
    for expr, expect in exprs.items():
        a = evaluate(low, expr)
        for (x, y, z), env in assignments(low, ["x", "y", "z"]):
            assert truth(f, a, env) == expect(x, y, z), (expr, x, y, z)


def test_enumerations_and_variable_indices():
    low = lowering("""
type Color = {RED, GREEN, BLUE};
env Color c;
env Color d;
env Int(0..2) i;
env boolean[3] lit;
""")
    f = low.f
    colors = ["RED", "GREEN", "BLUE"]
    for ci, di, ii in itertools.product(range(3), range(3), range(3)):
        env = {}
        for name, code in (("c", ci), ("d", di), ("i", ii)):
            atoms = low.vars[name].cells[()][0]
            env.update({a: bool((code >> j) & 1)
                        for j, a in enumerate(atoms)})
        lit = [True, False, True]
        env.update({f"e_lit__i{j}": lit[j] for j in range(3)})
        assert truth(f, evaluate(low, "c = d"), env) == (ci == di)
        assert truth(f, evaluate(low, "c != GREEN"), env) == \
            (colors[ci] != "GREEN")
        assert truth(f, evaluate(low, "lit[i]"), env) == lit[ii]
        # Enumerations are ordered by declaration, not by name.
        assert truth(f, evaluate(low, "c <= d"), env) == (ci <= di)
        assert truth(f, evaluate(low, "c > BLUE"), env) is False
        assert truth(f, evaluate(low, "GREEN < c"), env) == (ci == 2)
        assert truth(f, evaluate(low, "lit[2] & !lit[1]"), env)


def test_precedence_follows_spectra():
    low = lowering("""
env boolean a;
env boolean b;
env boolean c;
env Int(0..3) x;
""")
    f = low.f
    imp_iff = evaluate(low, "a -> b <-> c")
    for va, vb, vc in itertools.product([False, True], repeat=3):
        env = {"e_a": va, "e_b": vb, "e_c": vc}
        assert truth(f, imp_iff, env) == ((not va) or (vb == vc))
    assert f.show(evaluate(low, "a | b & c")) == "e_a | e_b & e_c"
    assert f.show(evaluate(low, "(a | b) & c")) == "(e_a | e_b) & e_c"
    assert f.show(evaluate(low, "a -> b -> c")) == "e_a -> (e_b -> e_c)"


def test_unsupported_constructs_are_named():
    raises("env boolean a;\nenv boolean b;\ngar G (a SINCE b);",
           I.Unsupported, "SINCE")
    raises("env boolean a;\ncounter n(0..3) { n = 0; inc: a; }",
           I.Unsupported, "counter")
    raises("env boolean a;\ngar pattern05(a, a);", I.Unsupported,
           "library pattern pattern05")
    raises("env boolean a;\nsys boolean c;\ngar pRespondsToS(PREV(a), c);",
           I.Unsupported, "PREV inside a pattern argument")
    raises("env boolean a;\ngar G ONCE(a);", I.Unsupported, "ONCE")
    raises("env boolean a;\ngar G b;", I.SpectraError, "unknown name b")
    try:
        I.import_spec('import "Other.spectra"\nmodule M\nenv boolean a;')
    except I.Unsupported as exc:
        assert "Other.spectra" in str(exc)
    else:
        raise AssertionError("another import is accepted")


def test_psp_document_and_spec_ltl():
    s = spec("""
env boolean a;
env Int(0..2) x;
sys boolean c;
asm G (a -> x = 1);
gar pRespondsToS(a, c);
gar G (c -> !a);
""")
    doc = I.to_psp(s)
    assert set(doc) == {"assumptions", "guarantees", "in_atoms", "out_atoms"}
    assert doc["assumptions"] == [
        {"fixed": "G(!e_x__b1 | !e_x__b0)"},
        {"fixed": "G(e_a -> e_x__b0 & !e_x__b1)"}]
    assert doc["guarantees"] == [
        {"pattern": "response", "scope": "globally", "p": "e_a", "s": "s_c"},
        {"fixed": "G(s_c -> !e_a)"}]
    assert I.spec_ltl(s) == (
        "((G(!e_x__b1 | !e_x__b0)) & (G(e_a -> e_x__b0 & !e_x__b1))) -> "
        "((G((e_a) -> F(s_c))) & (G(s_c -> !e_a)))")


def test_spot_parses_the_lowering():
    ltlfilt = shutil.which("ltlfilt")
    if ltlfilt is None:
        return
    s = spec("""
env boolean a;
env Int(0..4) x;
sys boolean c;
asm G (a -> next(x) > x);
asm GF x = 4;
gar pHolds_afterQuntilR(c, a, x = 0);
gar pBecomesTrue_betweenQandR(c, a, next(a));
gar G (PREV(c) -> !c);
""")
    res = subprocess.run([ltlfilt, "-F", "-", "--stats=%x"],
                         input=I.spec_ltl(s), capture_output=True, text=True,
                         check=False)
    assert res.returncode == 0, res.stderr


def test_commit_is_read_without_git():
    with tempfile.TemporaryDirectory() as tmp:
        git = Path(tmp) / ".git"
        (git / "refs" / "heads").mkdir(parents=True)
        (git / "HEAD").write_text("ref: refs/heads/master\n")
        (git / "packed-refs").write_text(
            "# pack-refs\nabc123 refs/heads/master\n")
        assert I.read_commit(tmp) == "abc123"
        (git / "refs" / "heads" / "master").write_text("def456\n")
        assert I.read_commit(tmp) == "def456"
        (git / "HEAD").write_text("0123abcd\n")
        assert I.read_commit(tmp) == "0123abcd"


if __name__ == "__main__":
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failures = 0
    for t in tests:
        try:
            t()
        except Exception as exc:  # noqa: BLE001
            failures += 1
            print(f"FAIL: {t.__name__}: {exc!r}")
    print(f"{len(tests)} tests, {failures} failure(s)")
    sys.exit(1 if failures else 0)
