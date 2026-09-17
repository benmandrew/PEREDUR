#!/usr/bin/env python3
"""Tests for aurus_adapt.py, the AuRUS-to-PEREDUR run-directory adapter.

No pytest dependency, matching test_experiment_paths.py: run it directly
(``python3 scripts/test_aurus_adapt.py``) and it exits non-zero on the first
failure.

The failure modes worth guarding are the silent ones, and all three are silent
in the same way -- the pass runs, writes curves, and the curves are wrong.

A misdated candidate moves a point on the only axis these curves are read on,
and nothing downstream can tell an arrival time apart from a fabricated one,
so the dating rule is checked against a run.log fixture of AuRUS's own shape
rather than against the adapter's idea of it. A run directory whose name does
not carry `_<spec>_seed<NN>` is skipped by score_campaign.queue_runs without a
word, since a directory with no seed suffix is how that queue recognises
something that is not a run -- so the name is checked through the two readers
that parse it rather than by eye. And a rewritten run directory that keeps a
previous adaptation's candidate files scores those files too: `maximal` and
`compare` are both given the whole directory, where the index names only what
this pass wrote.
"""

import argparse
import json
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import aurus_adapt as AD  # noqa: E402
import score_campaign as SC  # noqa: E402
import score_curves as SCV  # noqa: E402

FAMILY = "lily11"  # a real examples/ directory, which spec_from_dir_name needs


def check(got, want, msg):
    if got != want:
        print(f"FAIL: {msg}\n  got:  {got!r}\n  want: {want!r}")
        sys.exit(1)


def run_log(series):
    """AuRUS's own log shape: an iteration line, then its two time lines.

    (iteration, n_solutions, elapsed_s) per entry. The elapsed line is written
    in minutes and seconds, which is the second resolution every arrival time
    in this archive has.
    """
    lines = []
    for iteration, nsol, elapsed in series:
        lines.append(f"{iteration}\t0.89\tchromosome@ab67\t100\t{nsol}")
        lines.append("Iteration Time: 0 m  4 s")
        lines.append(f"Elapsed Time: {elapsed // 60} m  {elapsed % 60} s")
    return "\n".join(lines) + "\n"


def make_tree(root, repeats, series=None, n_files=None):
    """An AuRUS output tree: <spec>/repeat-NN/{run.log, spec<i>.tlsf}."""
    series = series or [(0, 2, 4), (1, 4, 8), (2, 4, 13), (3, 6, 30)]
    for seed in repeats:
        repeat = root / FAMILY / f"repeat-{seed:02d}"
        repeat.mkdir(parents=True)
        (repeat / "run.log").write_text(run_log(series))
        count = series[-1][1] if n_files is None else n_files
        for index in range(count):
            (repeat / f"spec{index}.tlsf").write_text(
                f"INFO {{ spec {index} }}")
    return root


def adapt(root, out, **overrides):
    args = argparse.Namespace(prefix="aurus", scheme="aurus", grading="aurus",
                              copy=False, force=False, specs=None, seeds=None,
                              dry_run=False)
    for key, value in overrides.items():
        setattr(args, key, value)
    seeds = AD.parse_seeds(args.seeds)
    walls = AD.read_wall_times(root)
    records = []
    for spec, seed, repeat in AD.discover(root, set(), seeds):
        records.append(AD.adapt_one(spec, seed, repeat, out, args,
                                    walls.get((spec, seed))))
    return records


def index_of(out, seed):
    path = (out / AD.run_dir_name("aurus", FAMILY, seed) /
            AD.ACCUMULATED_DIR / AD.INDEX_NAME)
    rows = [line.split("\t") for line in path.read_text().splitlines()]
    return rows[0], [(r[0], int(r[1]), float(r[2])) for r in rows[1:]]


# -- the dating rule ----------------------------------------------------------
# spec_i entered ga.solutions at the first iteration whose #Sol reaches i+1,
# and that iteration's elapsed time is the only arrival time the archive has.
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    root, out = make_tree(tmp / "aurus", [0]), tmp / "results"
    records = adapt(root, out)
    check(len(records), 1, "one repeat materialises one record")
    check(records[0]["status"], "written", "a repeat with files is written")
    check(records[0]["n_candidates"], 6, "every solution file is indexed")

    header, rows = index_of(out, 0)
    check(header, ["file", "generation", "elapsed_s"],
          "the index carries the accumulator's own header")
    # #Sol reaches 2 at iteration 0 (t=4), 4 at iteration 1 (t=8) and 6 at
    # iteration 3 (t=30), so spec0/1 date to 4 s, spec2/3 to 8 s and
    # spec4/5 to 30 s.
    check([r[0] for r in rows],
          ["spec0.tlsf", "spec1.tlsf", "spec2.tlsf", "spec3.tlsf",
           "spec4.tlsf", "spec5.tlsf"],
          "the index is in accumulation order, ties broken by file index")
    check([r[2] for r in rows], [4.0, 4.0, 8.0, 8.0, 30.0, 30.0],
          "each solution takes the elapsed time of the iteration "
          "that found it")
    check([r[1] for r in rows], [0, 0, 1, 1, 3, 3],
          "the generation column carries the iteration that found it")

    # The whole point of the tree: score_curves reads it with no adapter of its
    # own, and reads the same numbers back.
    args = argparse.Namespace(spec=None, ideals=str(tmp / "no-such-ideals"),
                              maximality=False, cuts=20, jobs=0,
                              compare_timeout=1, maximal_timeout=1,
                              deadline_s=0)
    curve = SCV.score_run(out / AD.run_dir_name("aurus", FAMILY, 0), args)
    solutions = [r for r in curve if r["metric"] == "solutions"]
    check(solutions[-1]["value"], 6, "the solutions curve ends at every one")
    check([r["elapsed_s"] for r in solutions][:3],
          ["4.000000", "8.000000", "30.000000"],
          "the curve steps at the three distinct arrival times")
    check(curve[0]["spec"], FAMILY, "the family comes back off the manifest")
    check(str(curve[0]["seed"]), "0", "the seed comes back off the manifest")
    check(curve[0]["selection_scheme"], "aurus",
          "the arm label reaches the curve rows")
    check(curve[0]["stopped_by"], "individuals",
          "AuRUS stops on its individual cap, which is what the rows say")

# -- the run directory name ---------------------------------------------------
# Both readers of the name are checked, since a name only one of them parses
# yields a tree that scores nothing and says nothing.
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    root, out = make_tree(tmp / "aurus", [0, 1, 2]), tmp / "results"
    adapt(root, out)
    run_dir = out / AD.run_dir_name("aurus", FAMILY, 2)
    check(SCV.seed_from_dir_name(run_dir), "2", "score_curves reads the seed")
    check(SCV.spec_from_dir_name(run_dir), FAMILY,
          "score_curves reads the family off the name alone")
    check(SC.seed_of(run_dir), 2, "score_campaign reads the seed")
    check([d.name for d in SC.queue_runs(out, [0, 2])],
          [AD.run_dir_name("aurus", FAMILY, 0),
           AD.run_dir_name("aurus", FAMILY, 2)],
          "the scorer's queue picks up this host's seeds and no others")
    check(SC.index_lines(run_dir), 7,
          "the queue can size a run from its index")

# -- what is skipped, and counted ---------------------------------------------
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    root, out = tmp / "aurus", tmp / "results"
    make_tree(root, [0])
    # Killed at the cap: the log records six solutions, the JVM died before
    # writing any of them.
    killed = root / FAMILY / "repeat-01"
    killed.mkdir(parents=True)
    (killed / "run.log").write_text(
        run_log([(0, 2, 4), (1, 4, 8), (2, 4, 13), (3, 6, 30)]))
    # One solution more than the series ever reports, so nothing dates it.
    (root / FAMILY / "repeat-00" / "spec6.tlsf").write_text("INFO {}")

    records = {r["seed"]: r for r in adapt(root, out)}
    check(records[1]["status"], "lost-at-cap",
          "a repeat killed before the write is named, not materialised empty")
    check((out / AD.run_dir_name("aurus", FAMILY, 1)).exists(), False,
          "and it leaves no run directory to score as an empty run")
    check(records[0]["undated"], 1, "an undated solution is counted")
    check(records[0]["n_candidates"], 6, "and left out of the index")
    check((out / AD.run_dir_name("aurus", FAMILY, 0) / AD.ACCUMULATED_DIR /
           "spec6.tlsf").exists(), False,
          "an undated solution reaches neither the index nor the directory")

# -- resume, rebuild, and stale files -----------------------------------------
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    root, out = make_tree(tmp / "aurus", [0]), tmp / "results"
    adapt(root, out)
    accumulated = (out / AD.run_dir_name("aurus", FAMILY, 0) /
                   AD.ACCUMULATED_DIR)
    (accumulated / "stale.tlsf").write_text("INFO {}")

    check(adapt(root, out)[0]["status"], "present",
          "a run directory already written is left alone")
    check((accumulated / "stale.tlsf").exists(), True,
          "resume rewrites nothing, so it cannot clean up either")
    check(adapt(root, out, force=True)[0]["status"], "written",
          "--force rewrites it")
    check((accumulated / "stale.tlsf").exists(), False,
          "--force rebuilds the directory rather than writing over it")

# -- linking and copying ------------------------------------------------------
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    root, out = make_tree(tmp / "aurus", [0]), tmp / "results"
    adapt(root, out)
    linked = (out / AD.run_dir_name("aurus", FAMILY, 0) / AD.ACCUMULATED_DIR /
              "spec0.tlsf")
    check(linked.is_symlink(), True, "candidates are linked by default")
    check(linked.resolve(), (root / FAMILY / "repeat-00" / "spec0.tlsf")
          .resolve(), "and the link resolves to the AuRUS file itself")

    copied_out = tmp / "copied"
    adapt(root, copied_out, copy=True)
    copied = (copied_out / AD.run_dir_name("aurus", FAMILY, 0) /
              AD.ACCUMULATED_DIR / "spec0.tlsf")
    check(copied.is_symlink(), False, "--copy writes a real file")
    shutil.rmtree(root)
    check(copied.read_text(), "INFO { spec 0 }",
          "which survives the tree it came from going away")

# -- filters ------------------------------------------------------------------
check(AD.parse_seeds("0-2,5"), {0, 1, 2, 5}, "inclusive ranges, as declared")
check(AD.parse_seeds(None), None, "no filter means every repeat")
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    root, out = make_tree(tmp / "aurus", range(6)), tmp / "results"
    records = adapt(root, out, seeds="0-1,4")
    check(sorted(r["seed"] for r in records), [0, 1, 4],
          "a seed range takes a sample of repeats, the design k-of-30 needs")

# -- the summary --------------------------------------------------------------
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    root, out = make_tree(tmp / "aurus", [0, 1]), tmp / "results"
    args = argparse.Namespace(prefix="aurus", scheme="aurus", grading="aurus",
                              copy=False, seeds=None)
    summary = AD.summarise(adapt(root, out), args, root, out)
    check(summary["runs"], 2, "the summary counts the runs it wrote")
    check(summary["candidates"], 12, "and the candidates under them")
    check(summary["by_family"][FAMILY]["candidates"], 12,
          "per family, which is the unit the comparison is read on")
    json.dumps(summary)  # a summary that cannot be written is no record

print("ok: all aurus_adapt round-trips pass")
