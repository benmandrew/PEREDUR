#!/usr/bin/env python3
"""Tests for score_curves.py's reading of a run directory.

No pytest dependency: run it directly (``python scripts/test_score_curves.py``)
and it exits non-zero on the first failure. `compare` and `maximal` are stubbed,
so no binary runs. What is guarded is that a climbed repair written before the
accumulator held climb results still reaches the curves, at the run's end.
"""

import json
import sys
import tempfile
from argparse import Namespace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import score_curves as S  # noqa: E402


def check(got, want, msg):
    if got != want:
        print(f"FAIL: {msg}\n  got:  {got!r}\n  want: {want!r}")
        sys.exit(1)


def spec(response):
    return {"assumptions": [], "guarantees": [{"response": response}]}


def make_run(root: Path, climb: bool) -> Path:
    """A run with one accumulated file and two repairs, one of them climbed."""
    run = root / ("climb" if climb else "plain")
    (run / "accumulated").mkdir(parents=True)
    (run / "run.json").write_text(json.dumps({
        "wall_s": 10.0, "generations_run": 3,
        "config": {"filters": {"climb_output": climb}}}))
    (run / "accumulated" / "gen01_0000.json").write_text(
        json.dumps(spec("a"), indent=2))
    (run / "accumulated" / "index.tsv").write_text(
        "file\tgeneration\telapsed_s\ngen01_0000.json\t1\t2.500000\n")
    (run / "repair_0.json").write_text(
        json.dumps({**spec("a"), "fitness": {"total": 0.9}}))
    (run / "repair_1.json").write_text(
        json.dumps({**spec("b"), "fitness": {"total": 0.8}}))
    # A run directory holds no other repair_ file on the FRETISH path, but a
    # name that is not repair_<N>.json must not be read as one.
    (run / "repair_notes.json").write_text("{}")
    return run


with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    climb_run = make_run(root, climb=True)
    plain_run = make_run(root, climb=False)

    index = S.read_index(climb_run)
    check(S.climbed_repairs(climb_run, S.read_manifest(climb_run), index),
          [("repair_1.json", 4, 10.0)],
          "a repair no accumulated file holds joins at wall_s, generation 4")
    check(S.climbed_repairs(plain_run, S.read_manifest(plain_run), index), [],
          "a run without the climb adds nothing")
    check(S.climbed_repairs(climb_run, {"config": {"filters": {
        "climb_output": True}}}, index), [],
          "a run with no wall_s cannot place its climbed repairs")

    # The stubs stand in for `compare` and `maximal`: every file implies an
    # ideal, and every file survives.
    calls = {"compare": [], "maximal": []}

    def fake_compare(repairs_dir, ideals_dir, timeout_s):
        calls["compare"].append(repairs_dir)
        return {path.name: "strictly stronger"
                for path in repairs_dir.glob("*.json")
                if path.name != "run.json"}

    def fake_maximal(files, jobs, timeout_s):
        calls["maximal"].append(list(files))
        return {path.name for path in files}

    S.compare_relations = fake_compare
    S.maximal_over = fake_maximal
    ideals = root / "ideals"
    ideals.mkdir()
    args = Namespace(spec="fsm", ideals=str(ideals), compare_timeout=1,
                     maximality=True, cuts=20, jobs=0, maximal_timeout=1)
    rows = S.score_run(climb_run, args)

    def final(metric):
        return [row for row in rows if row["metric"] == metric][-1]

    check(final("solutions")["value"], 2,
          "the climbed repair counts as a solution")
    check(final("ideal_solutions")["value"], 2,
          "the climbed repair's relation to the ideals is read")
    check(final("ideal_solutions")["elapsed_s"], "10.000000",
          "the climbed repair counts from the run's end")
    check(final("maximal_ideal_solutions")["value"], 2,
          "the climbed repair reaches the maximal set")
    check(calls["compare"], [climb_run / "accumulated", climb_run],
          "compare reads the accumulated files and then the repairs")
    check(calls["maximal"][-1][-1], climb_run / "repair_1.json",
          "maximal reads the climbed repair from the run directory")

print("ok: score_curves reads climbed repairs")
