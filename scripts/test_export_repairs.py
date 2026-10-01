#!/usr/bin/env python3
"""Tests for export_repairs.py.

No pytest dependency: run it directly (``python3 scripts/test_export_repairs.py``)
and it exits non-zero on the first failure. The end-to-end test builds run
directories for `examples/rad-core-10`, so it needs no build.
"""

import json
import sys
import tempfile
from pathlib import Path

import export_repairs as E

ORIGINAL = json.loads((E.EXAMPLES / "rad-core-10" / "spec.json").read_text())
G1 = ORIGINAL["guarantees"][0]


def req(**fields):
    base = {"condition": "true", "condition-type": "trigger",
            "response": "r", "timing": {"type": "Eventually"}}
    return {**base, **fields}


def test_requirement_text_matches_the_cpp_printer():
    # Requirement::to_string drops a true trigger condition and keeps a true
    # continual one, since FRET reads the latter as unscoped otherwise.
    assert E.requirement_text(req()) == "C shall eventually satisfy r"
    assert (E.requirement_text(req(**{"condition-type": "continual"}))
            == "whenever true C shall eventually satisfy r")
    text = E.requirement_text(req(
        condition="a & b", scope={"type": "NotIn", "mode": "m"},
        timing={"type": "WithinTicks", "ticks": 3}, weakenable=False))
    assert text == ("except in m upon a & b C shall within 3 ticks "
                    "satisfy r  [locked]"), text
    assert (E.requirement_text(req(timing={"type": "Before", "stop": "s"}))
            == "C shall before s satisfy r")


def test_normalise_agrees_across_the_two_spellings():
    # The C++ printer brackets every operand; an imported example does not.
    cases = {"(ambiguity_present) | (risk_present)":
             "ambiguity_present | risk_present",
             "(!((a) | (b))) | (c)": "!(a | b) | c",
             "((a) & (b)) | (c)": "(a & b) | c",
             "(a) & ((b) | (c))": "a & (b | c)",
             "a -> b -> c": "a -> (b -> c)",
             "((a) & (b)) & (c)": "a & b & c",
             "!(wind_ge_m30)": "!wind_ge_m30",
             "X(a) U (b)": "X a U b"}
    for text, want in cases.items():
        assert E.normalise(text) == want, (text, E.normalise(text))
        assert E.normalise(want) == want, want


def test_align_pairs_kept_changed_removed_and_added():
    a, b, c, d = (req(response=x) for x in "abcd")
    got = [(kind, i, r and r["response"])
           for kind, i, r in E.align([a, b, c], [a, d, req(response="e")])]
    assert got == [("unchanged", 0, "a"), ("changed", 1, "d"),
                   ("changed", 2, "e")], got
    got = [(kind, i) for kind, i, _ in E.align([a, b, c], [a, c])]
    assert got == [("unchanged", 0), ("removed", 1), ("unchanged", 2)], got
    got = [(kind, i) for kind, i, _ in E.align([a], [a, d])]
    assert got == [("unchanged", 0), ("added", None)], got


def test_short_arms_keeps_the_varying_tokens():
    arms = ["sweep_O_directed_nsga2_log", "sweep_O_uniform_nsga2_log"]
    assert E.short_arms(arms) == {arms[0]: "directed", arms[1]: "uniform"}
    assert E.short_arms(["only"]) == {"only": "only"}


def test_core_labels_carry_parent_index_and_reqid():
    labels = E.label_requirements("rad-core-10", ORIGINAL)
    assert labels["guarantees"] == ["G1 (rad #10, S01_a)"], labels


def write_run(root: Path, arm: str, seed: int, repairs, finished=True):
    run = root / f"sweep_O_{arm}_nsga2-apportion_log_rad-core-10_seed{seed:02d}"
    (run / "accumulated").mkdir(parents=True)
    for n, guarantee in enumerate(repairs):
        body = {**ORIGINAL, "guarantees": [guarantee],
                "fitness": {"total": 0.5 + n / 10}}
        if finished:
            (run / f"repair_{n}.json").write_text(json.dumps(body))
        else:
            (run / "accumulated" / f"gen01_{n:04d}.json").write_text(
                json.dumps(body))
    if finished:
        (run / "run.json").write_text("{}")
    else:
        names = "".join(f"gen01_{n:04d}.json\n" for n in range(len(repairs)))
        (run / "accumulated" / "maximal.tsv").write_text("file\n" + names)


def test_end_to_end():
    weaker = {**G1, "timing": {"type": "Eventually"}}
    other = {**G1, "response": "!(confirm_task_request)"}
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        results, out = tmp / "results", tmp / "out"
        write_run(results, "directed", 0, [weaker, other])
        write_run(results, "uniform", 0, [weaker])
        # Killed before run.json: its maximal set comes from the accumulator.
        write_run(results, "uniform", 1, [other], finished=False)
        write_run(results, "directed", 1, [])
        sys.argv = ["export_repairs.py", str(results), "--out", str(out),
                    "--subjects", "rad-core-10"]
        E.main()

        bundle = out / "rad-core-10"
        files = sorted(p.name for p in (bundle / "repairs").iterdir())
        assert files == ["r0001.json", "r0002.json"], files
        md = (bundle / "repairs.md").read_text()
        assert "2 distinct repairs, found by 3 of 4 runs" in md, md
        # `other` has the higher fitness (0.6), so it comes first.
        assert md.index("changed (response)") < md.index("changed (timing)")
        rows = (bundle / "repairs.csv").read_text().splitlines()
        assert rows[0] == ("repair,runs,runs_directed,runs_uniform,seeds,"
                           "best_fitness,changed,removed,added"), rows[0]
        assert rows[1].startswith("r0001,2,1,1,0 1,0.600000,1,0,0"), rows[1]
        r1 = json.loads((bundle / "repairs" / "r0001.json").read_text())
        assert "fitness" not in r1, r1
        assert [f["censored"] for f in r1["found_by"]] == [False, True], r1
        core = (bundle / "core.md").read_text()
        assert "FRET S01_a: Robot shall before" in core, core
        assert "| rad-core-10 | 3 of 4 | 2 |" in \
            (out / "README.md").read_text()


def main():
    tests = [v for k, v in globals().items() if k.startswith("test_")]
    for test in tests:
        test()
        print(f"ok  {test.__name__}")
    print(f"{len(tests)} passed")


if __name__ == "__main__":
    main()
