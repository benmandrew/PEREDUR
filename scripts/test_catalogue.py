#!/usr/bin/env python3
"""Tests for catalogue.py. No pytest; run it directly."""

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import catalogue  # noqa: E402

FAILURES = []


def expect(cond, msg):
    if not cond:
        FAILURES.append(msg)


PAPERS = {"ext": {"title": "Ext", "repo": "~/ext"}}


def a_set(**kw):
    s = {"name": "s", "paths": ["results-s"], "paper": "ext",
         "status": "cited", "purpose": "p"}
    s.update(kw)
    return s


def test_committed_catalogue_is_valid():
    papers, sets = catalogue.load()
    errors, _ = catalogue.validate(papers, sets)
    expect(not errors, f"committed catalogue: {errors}")
    rendered = catalogue.render(papers, sets) + "\n"
    expect(catalogue.RENDERED.read_text() == rendered,
           "CATALOGUE.md is stale; run scripts/catalogue.py render")


def test_validate_rejects_bad_fields():
    errors, _ = catalogue.validate(PAPERS, [a_set(status="maybe")])
    expect(any("status" in e for e in errors), "an unknown status is rejected")
    errors, _ = catalogue.validate(PAPERS, [a_set(status="superseded")])
    expect(any("superseded_by" in e for e in errors),
           "superseded needs superseded_by")
    errors, _ = catalogue.validate(PAPERS, [a_set(status="superseded", superseded_by="x")])
    expect(any("'x' is not a set" in e for e in errors),
           "superseded_by must name a set")
    errors, _ = catalogue.validate(PAPERS, [a_set(), a_set(name="t")])
    expect(any("claimed by" in e for e in errors), "a path has one owner")
    errors, _ = catalogue.validate({}, [a_set()])
    expect(any("no [paper] table" in e for e in errors), "a paper needs a table")


def test_check_finds_uncatalogued_and_missing():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "results-s").mkdir()
        (root / "results-stray.csv").write_text("")
        (root / "2026-01-01-campaign").mkdir()
        errors = catalogue.check(PAPERS, [a_set(), a_set(name="u", paths=["results-gone"])], root)
        expect(any("results-stray.csv" in e and "not catalogued" in e for e in errors),
               f"a stray entry is reported: {errors}")
        expect(any("2026-01-01-campaign" in e for e in errors),
               "an uncatalogued campaign directory is reported")
        expect(any("results-gone" in e and "missing" in e for e in errors),
               "a catalogued path that is absent is reported")
        errors = catalogue.check(PAPERS, [a_set(), a_set(name="u", paths=["results-gone"], absent_ok=True),
                                          a_set(name="c", paths=["2026-01-01-campaign", "results-stray.csv"])], root)
        expect(not errors, f"absent_ok and full coverage pass: {errors}")


def test_check_skips_data_in_a_clean_clone():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "2026-01-01-campaign").mkdir()
        errors = catalogue.check(PAPERS, [a_set(), a_set(name="c", paths=["2026-01-01-campaign"])], root)
        expect(not errors, f"a clone with only campaign directories passes: {errors}")


def test_check_reads_a_local_location():
    with tempfile.TemporaryDirectory() as tmp:
        root, other = Path(tmp) / "here", Path(tmp) / "other"
        root.mkdir()
        other.mkdir()
        (other / "results-x").mkdir()
        sets = [a_set(name="x", location=str(other), paths=["results-x", "results-y"])]
        errors = catalogue.check(PAPERS, sets, root)
        expect(any("results-y missing" in e for e in errors),
               f"a missing path in another checkout is reported: {errors}")
        sets = [a_set(name="x", location="av2: ~/far", paths=["results-y"])]
        expect(not catalogue.check(PAPERS, sets, root), "a host location is not read")


if __name__ == "__main__":
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
    for f in FAILURES:
        print("FAIL:", f)
    print(f"{len(tests)} tests, {len(FAILURES)} failure(s)")
    sys.exit(1 if FAILURES else 0)
