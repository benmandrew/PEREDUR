#!/usr/bin/env python3
"""Tests for the separation half of score_curves.py.

No pytest dependency, matching test_aurus_adapt.py: run it directly
(``python3 scripts/test_score_curves.py``) and it exits non-zero on the first
failure.

Two failure modes are silent. A recount from recorded membership that read the
sidecar out of order, or kept a member the maximality stage would have, writes
a plausible count that is simply another number. And a change to the net that
moved the `hamming` count would shift every archived separation figure with no
error anywhere, so the default is pinned against the threshold form the
archived passes ran.

The last test runs the `fingerprint` binary and is skipped where there is no
release build.
"""

import argparse
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import score_curves as SCV  # noqa: E402

FAMILY = "lily02"  # a real examples/ directory with several fixes


def check(got, want, msg):
    if got != want:
        print(f"FAIL: {msg}\n  got:  {got!r}\n  want: {want!r}")
        sys.exit(1)


def test_hamming_is_the_archived_threshold_form():
    prints = [0b0000, 0b0001, 0b0011, 0b0111, 0b1111, 0b1110]
    for epsilon in (0.0, 0.05, 0.25, 0.5):
        check(SCV.net_count(prints, epsilon, 4, "hamming"),
              SCV.separated_count(prints, int(epsilon * 4)),
              f"hamming at {epsilon} is the threshold net the archived "
              f"passes ran")


def test_union_normalises_by_the_pair():
    # One disagreement out of two satisfied words: distance 0.5.
    check(SCV.net_count([0b01, 0b11], 0.4, 64, "union"), 2,
          "a pair at 0.5 is apart at 0.4")
    check(SCV.net_count([0b01, 0b11], 0.5, 64, "union"), 1,
          "and not at 0.5, the comparison being strict")
    check(SCV.net_count([0, 0], 0.0, 64, "union"), 1,
          "two candidates satisfying no word are at distance zero")


def test_union_survives_sparse_fingerprints():
    # Disjoint single words out of 64: hamming reads 2/64, close; union reads
    # 1.0, as far apart as two candidates can be.
    prints = [1 << 0, 1 << 1]
    check(SCV.net_count(prints, 0.05, 64, "hamming"), 1,
          "hamming merges two disjoint sparse candidates")
    check(SCV.net_count(prints, 0.05, 64, "union"), 2,
          "union keeps them apart")


def test_members_round_trip_in_admission_order():
    root = Path(tempfile.mkdtemp(prefix="score-curves-"))
    try:
        members = [(1.5, "b.tlsf"), (1.5, "a.tlsf"), (3.0, "c.tlsf"),
                   (3.0, "a.tlsf")]
        out = root / "run_seed00.csv.part"
        SCV.write_sidecars(out, {"members": members, "fingerprints": {},
                                 "n_words": 8})
        path = SCV.sidecar_paths(root / "run_seed00.csv")[0]
        check(SCV.read_members(path), members,
              "a sidecar reads back in the order it was written")
        check(SCV.read_members(root / "absent.members.tsv"), None,
              "a missing sidecar is None, not an empty antichain")
    finally:
        shutil.rmtree(root, ignore_errors=True)


def test_members_rows_net_each_cut():
    members = [(1.0, "a"), (1.0, "b"), (2.0, "a"), (2.0, "c"), (2.0, "z")]
    prints = {"a": 0b0001, "b": 0b0011, "c": 0b1110}
    rows = SCV.members_rows({"spec": "s"}, members, prints, [0.3], 4,
                            "hamming")
    check([(r["metric"], r["elapsed_s"], r["value"]) for r in rows],
          [("eps_maximal_solutions_0.3", "1.000000", 1),
           ("eps_maximal_solutions_0.3", "2.000000", 2)],
          "one row a cut; b is a's neighbour, c is not, and z has no "
          "fingerprint")


def test_recount_from_members_end_to_end():
    if not SCV.FINGERPRINT_BIN.is_file():
        print(f"skip: no fingerprint binary at {SCV.FINGERPRINT_BIN}")
        return
    root = Path(tempfile.mkdtemp(prefix="score-curves-"))
    try:
        run_dir = root / f"run_{FAMILY}_seed00"
        accumulated = run_dir / SCV.ACCUMULATED_DIR
        accumulated.mkdir(parents=True)
        fixes = sorted((SCV.EXAMPLES_DIR / FAMILY / "fixes").glob("*.tlsf"))
        names = []
        lines = ["file\tgeneration\telapsed_s"]
        for number, fix in enumerate(fixes):
            name = f"gen01_{number:04d}.tlsf"
            shutil.copy(fix, accumulated / name)
            names.append(name)
            lines.append(f"{name}\t1\t{number + 1}.0")
        (accumulated / SCV.INDEX_NAME).write_text("\n".join(lines) + "\n")
        membership = root / "maximal-pass"
        membership.mkdir()
        recorded = [(2.0, names[0]), (2.0, names[1])] + \
            [(9.0, name) for name in names]
        SCV.write_sidecars(membership / f"{run_dir.name}.csv",
                           {"members": recorded})

        args = argparse.Namespace(
            spec=FAMILY, ideals=None, skip_ideals=True, maximality=False,
            epsilon=[0.0], fingerprint_words=512, fingerprint_seed=0,
            fingerprint_max_prefix=8, fingerprint_max_cycle=8,
            fingerprint_distance="union", members_from=str(membership),
            cuts=5, jobs=1, maximal_timeout=1, compare_timeout=1)
        sidecars: dict = {}
        rows = SCV.score_run(run_dir, args, None, sidecars)
        maximal = [r for r in rows
                   if r["metric"] == "eps_maximal_solutions_0"]
        check([r["elapsed_s"] for r in maximal], ["2.000000", "9.000000"],
              "a row at each recorded cut and no other")
        prints = sidecars["fingerprints"]
        check(sorted(prints), names, "every candidate was fingerprinted")
        check(maximal[-1]["value"], len(set(prints.values())),
              "at epsilon 0 the last cut keeps one candidate per distinct "
              "fingerprint")
        check(sidecars["members"], recorded,
              "the recorded membership is carried into the new sidecar")
        check(any(r["metric"] == "eps_solutions_0" for r in rows), True,
              "the all-candidate curve is still written")
    finally:
        shutil.rmtree(root, ignore_errors=True)


def test_prefilter_flags_reach_maximal():
    absent = argparse.Namespace(prefilter_words=None,
                                prefilter_max_prefix=None,
                                prefilter_max_cycle=None)
    check(SCV.prefilter_flags(absent), [],
          "no prefilter flag leaves maximal at its own default")
    check(SCV.prefilter_flags(argparse.Namespace()), [],
          "and an args object predating the flags passes none")
    root = Path(tempfile.mkdtemp(prefix="score-curves-prefilter-"))
    saved = SCV.MAXIMAL_BIN
    try:
        argv_file = root / "argv.txt"
        stub = root / "maximal"
        stub.write_text(f"#!/bin/sh\necho \"$@\" > {argv_file}\n"
                        "printf 'elapsed_s\\tfile\\tevent\\tx\\n"
                        "0.5\\ta.tlsf\\tadmit\\t-\\n'\n")
        stub.chmod(0o755)
        SCV.MAXIMAL_BIN = stub
        flags = SCV.prefilter_flags(argparse.Namespace(
            prefilter_words=4096, prefilter_max_prefix=None,
            prefilter_max_cycle=None))
        log = SCV.antichain_walk(root, 2, 30, flags)
        check(argv_file.read_text().split(),
              ["--curve", str(root / SCV.INDEX_NAME), "--jobs", "2",
               "--prefilter-words", "4096"],
              "--prefilter-words reaches maximal --curve")
        check(log, [(0.5, "a.tlsf", "admit")], "and the log still parses")
    finally:
        SCV.MAXIMAL_BIN = saved
        shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    for test in (test_hamming_is_the_archived_threshold_form,
                 test_union_normalises_by_the_pair,
                 test_union_survives_sparse_fingerprints,
                 test_members_round_trip_in_admission_order,
                 test_members_rows_net_each_cut,
                 test_recount_from_members_end_to_end,
                 test_prefilter_flags_reach_maximal):
        test()
    print("All score_curves.py tests passed.")
