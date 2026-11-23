#!/usr/bin/env python3
"""Tests for the pooled pass's scripts: compare_pairs.py,
fingerprint_members.py, pool_members.py, pooled_design.py and
pooled_reduce.py.

No pytest dependency: run it directly (``python scripts/test_pooled.py``). Like
test_campaign.py, it records every failure and reports them together at the
end, exiting non-zero once the whole run is done. Nothing here touches a lab
machine: the tools the scripts call are stubs.
"""

import csv
import io
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))


FAILURES: list = []


def fail(msg):
    """Record a failure and carry on, as test_campaign.py's fail does."""
    print(f"FAIL: {msg}")
    FAILURES.append(msg)


def check(got, want, msg):
    if got != want:
        fail(f"{msg}\n  got:  {got!r}\n  want: {want!r}")


def check_true(cond, msg):
    if not cond:
        fail(msg)


# ── compare_pairs.py ──────────────────────────────────────────────────────────
#
# The pairwise pass, against a stub compare that answers from the repair's
# file name, run as the tick runs it: a subprocess. The output format is the
# hand-written runner's, which analyses already read; the exit status is what
# the tick advances on, so a pass with any pair unanswered must not exit 0.

COMPARE_PAIRS_PY = Path(__file__).resolve().parent / "compare_pairs.py"
STUB_COMPARE = """#!/bin/sh
# --repairs DIR --ideals DIR --timeout S: answer from the repair's name.
name=$(ls "$2")
case "$name" in
  eq*) echo "$name : equivalent to ideal" ;;
  weak*) echo "$name : strictly weaker than ideal" ;;
  strong*) echo "$name : strictly stronger than ideal" ;;
  inc*) echo "$name : incomparable with ideal" ;;
  to*) echo "$name : timeout" ;;
  hang*) sleep 10 ;;
  *) echo "nothing useful" ;;
esac
echo "Summary : 1 repair(s)"
"""

cp_root = Path(tempfile.mkdtemp(prefix="compare-pairs-"))
try:
    stub_bin = cp_root / "compare"
    stub_bin.write_text(STUB_COMPARE)
    stub_bin.chmod(0o755)
    names = ["eq.json", "weak.json", "strong.json", "inc.json", "to.json",
             "hang.json", "junk.json"]
    for n in names + ["ideal.json"]:
        (cp_root / n).write_text("{}")
    pairs_csv = cp_root / "pairs.csv"
    pairs_csv.write_text("id,a_path,b_path\n" + "".join(
        f"p{i},{cp_root / n},{cp_root / 'ideal.json'}\n"
        for i, n in enumerate(names)))
    out_csv = cp_root / "out" / "o.csv"
    manifest = cp_root / "m" / "x.json"

    def run_pairs(*extra) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(COMPARE_PAIRS_PY), str(pairs_csv),
             str(out_csv), "--jobs", "3", "--wall-timeout", "1",
             "--compare", str(stub_bin), "--manifest", str(manifest), *extra],
            capture_output=True, text=True,
            env=dict(os.environ, COMPARE_PAIRS_TMP=str(cp_root / "tmp")))

    proc = run_pairs()
    check(proc.returncode, 0, f"a complete pass exits 0: {proc.stderr}")
    with open(out_csv, newline="") as handle:
        got = {r["id"]: r for r in csv.DictReader(handle)}
    check(out_csv.read_text().splitlines()[0], "id,relation,rc,secs",
          "the output keeps run.py's header")
    check({k: got[k]["relation"] for k in sorted(got)},
          {"p0": "equivalent", "p1": "weaker", "p2": "stronger",
           "p3": "incomparable", "p4": "undecided", "p5": "undecided",
           "p6": "error"},
          "relations map as run.py maps them, the wall timeout to undecided")
    check(got["p5"]["rc"], "124", "with timeout's own exit status recorded")
    check_true(proc.stdout.startswith("7 pairs to run, 0 done")
               and re.search(r"^done \d+s$", proc.stdout, re.M) is not None,
               f"and run.py's progress lines: {proc.stdout!r}")
    meta = json.loads(manifest.read_text())
    check((meta["kind"], meta["pairs_total"], meta["missing"]),
          ("compare", 7, 0), "the manifest records the planned and missing")
    check_true(not any((cp_root / "tmp").iterdir()),
               "and the symlink directories are removed")

    proc = run_pairs()
    check((proc.returncode, proc.stdout.splitlines()[0]),
          (0, "0 pairs to run, 7 done"), "a rerun skips every id present")
    check(len(out_csv.read_text().splitlines()), 8, "and appends nothing")

    # A pass missing an id -- a killed one, or a pair that raised -- is not
    # done. Simulated by a new id whose temporary directory cannot be made.
    with open(pairs_csv, "a") as handle:
        handle.write(f"p7,{cp_root / 'eq.json'},{cp_root / 'ideal.json'}\n")
    proc = subprocess.run(
        [sys.executable, str(COMPARE_PAIRS_PY), str(pairs_csv), str(out_csv),
         "--compare", str(stub_bin)], capture_output=True, text=True,
        env=dict(os.environ, COMPARE_PAIRS_TMP=str(cp_root / "nowhere" / "x")))
    check(proc.returncode, 0, "the tmp directory is created when absent")
    (cp_root / "ro").mkdir()
    (cp_root / "ro").chmod(0o500)
    with open(pairs_csv, "a") as handle:
        handle.write(f"p8,{cp_root / 'eq.json'},{cp_root / 'ideal.json'}\n")
    proc = subprocess.run(
        [sys.executable, str(COMPARE_PAIRS_PY), str(pairs_csv), str(out_csv),
         "--compare", str(stub_bin)], capture_output=True, text=True,
        env=dict(os.environ, COMPARE_PAIRS_TMP=str(cp_root / "ro")))
    if os.geteuid() != 0:
        check(proc.returncode, 1,
              f"a pass with an id still missing exits non-zero: {proc.stderr}")
        check_true("incomplete: 1 of 9" in proc.stderr, "and says how many")
    (cp_root / "ro").chmod(0o700)

    # A dirs column: COMPARE_DIRECTIONS reaches compare, the DIR line becomes
    # two columns, and a pair the outer timeout killed reads ? where it was
    # asked and 0 where the planner had already refuted the direction.
    dir_bin = cp_root / "compare-dirs"
    dir_bin.write_text("""#!/bin/sh
name=$(ls "$2")
case "$name" in
  hang*) sleep 10 ;;
esac
case "$COMPARE_DIRECTIONS" in
  fwd) echo "DIR\t$name\tideal.json\t1\t0" >&2; echo "$name : strictly stronger than ideal" ;;
  rev) echo "DIR\t$name\tideal.json\t0\t?" >&2; echo "$name : timeout" ;;
  *) echo "DIR\t$name\tideal.json\t1\t1" >&2; echo "$name : equivalent to ideal" ;;
esac
""")
    dir_bin.chmod(0o755)
    dpairs = cp_root / "dpairs.csv"
    dpairs.write_text("id,a_path,b_path,dirs\n"
                      f"d0,{cp_root / 'eq.json'},{cp_root / 'ideal.json'},both\n"
                      f"d1,{cp_root / 'eq.json'},{cp_root / 'ideal.json'},fwd\n"
                      f"d2,{cp_root / 'eq.json'},{cp_root / 'ideal.json'},rev\n"
                      f"d3,{cp_root / 'hang.json'},{cp_root / 'ideal.json'},fwd\n")
    dout = cp_root / "dout.csv"
    proc = subprocess.run(
        [sys.executable, str(COMPARE_PAIRS_PY), str(dpairs), str(dout),
         "--compare", str(dir_bin), "--wall-timeout", "1", "--jobs", "2"],
        capture_output=True, text=True,
        env=dict(os.environ, COMPARE_PAIRS_TMP=str(cp_root / "tmp")))
    check(proc.returncode, 0, f"a pass over a dirs column exits 0: {proc.stderr}")
    lines = dout.read_text().splitlines()
    check(lines[0], "id,relation,rc,secs,a_implies_b,b_implies_a",
          "the output gains the two direction columns")
    got = {r["id"]: (r["relation"], r["a_implies_b"], r["b_implies_a"])
           for r in csv.DictReader(io.StringIO(dout.read_text()))}
    check(got, {"d0": ("equivalent", "1", "1"), "d1": ("stronger", "1", "0"),
                "d2": ("undecided", "0", "?"), "d3": ("undecided", "?", "0")},
          "each direction is read off the DIR line, or ? where none came")
    dpairs.write_text("id,a_path,b_path,dirs\n"
                      f"x,{cp_root / 'eq.json'},{cp_root / 'ideal.json'},up\n")
    proc = subprocess.run(
        [sys.executable, str(COMPARE_PAIRS_PY), str(dpairs),
         str(cp_root / "x.csv"), "--compare", str(dir_bin)],
        capture_output=True, text=True)
    check((proc.returncode, "dirs must be one of" in proc.stderr), (2, True),
          "an unknown direction is refused before any call")
finally:
    shutil.rmtree(cp_root, ignore_errors=True)

# ── fingerprint_members.py ────────────────────────────────────────────────────
#
# The driver against a stub fpdraw and a stub draw script: what is checked is
# the bookkeeping (resume, progress, join) and the planner, whose pruning
# decides which pairs compare ever sees.

FINGERPRINT_MEMBERS_PY = Path(__file__).resolve().parent / "fingerprint_members.py"
STUB_FPDRAW = """#!/usr/bin/env python3
import json, os, sys
here = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here, "fpdraw-calls.txt"), "a") as h:
    h.write(sys.argv[1] + " " + os.environ.get("FPDRAW_UNGLUE", "-") + "\\n")
if sys.argv[1] == "ltl":
    print("signals\\ta ")
    for p in open(sys.argv[3]).read().split():
        print(p + "\\tA\\tG\\tG")
else:
    for p in open(sys.argv[3]).read().split():
        print(p + "\\t" + json.load(open(p))["fp"])
"""
STUB_DRAW = """#!/usr/bin/env python3
import os, sys
ltl, n, seed, out = sys.argv[1:5]
rows = open(ltl).read().splitlines()[1:]
open(out, "w").write("0 1 a=1\\n" * int(n))
open(out + ".meta", "w").write("".join(
    f"{i}\\t{rows[i % len(rows)].split()[0]}\\t0\\t"
    + os.environ.get("STUB_RESULT", "SAT") + "\\t1\\tNone\\n"
    for i in range(int(n))))
"""

fm_root = Path(tempfile.mkdtemp(prefix="fingerprint-members-"))
try:
    import gzip as _gzip
    fpdraw = fm_root / "fpdraw"
    fpdraw.write_text(STUB_FPDRAW)
    fpdraw.chmod(0o755)
    draw = fm_root / "draw.py"
    draw.write_text(STUB_DRAW)
    members = fm_root / "members"
    members.mkdir()
    # fsm: 0 holds {0,1}, 1 holds {0}, 2 holds {0,1}, 3 holds {2}. Open
    # directions: 1 => 0, 0 <=> 2, 1 => 2; node 3 meets nobody.
    nodes = [("03", [["directed", 0, "a.json"], ["uniform", 1, "b.json"]]),
             ("01", [["directed", 2, "c.json"]]),
             ("03", [["uniform", 3, "d.json"]]),
             ("04", [["uniform", 4, "e.json"]])]
    with _gzip.open(members / "fsm.jsonl.gz", "wt") as handle:
        for i, (fp, mem) in enumerate(nodes):
            handle.write(json.dumps({"node": i, "members": mem,
                                     "spec": {"fp": fp, "i": i}}) + "\n")
    with _gzip.open(members / "rad-core-1.jsonl.gz", "wt") as handle:
        handle.write(json.dumps({"node": 0, "members": [["directed", 0, "x"]],
                                 "spec": {"fp": "01"}}) + "\n")
    subjects = fm_root / "subjects-h.txt"
    subjects.write_text("fsm\nrad-core-1\n")
    work, out = fm_root / "work", fm_root / "out"
    joined = fm_root / "pairs-h.csv"
    manifest = fm_root / "m.json"

    def run_fm(**env) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(FINGERPRINT_MEMBERS_PY), str(subjects),
             str(members), str(work), str(out), "--pairs", str(joined),
             "--words", "5", "--workers", "1", "--fpdraw", str(fpdraw),
             "--draw-script", str(draw), "--manifest", str(manifest)],
            capture_output=True, text=True, env=dict(os.environ, **env))

    proc = run_fm(STUB_RESULT="ERROR")
    check(proc.returncode, 1, f"a draw black answered ERROR on fails: "
                              f"{proc.stdout}")
    check_true("ERROR on 5 of 5" in proc.stdout and not joined.exists(),
               "naming the count, and joining nothing")
    check_true(not (out / "fsm" / "words").exists(),
               "and leaving no words behind to resume from")

    proc = run_fm()
    check(proc.returncode, 0, f"a clean pass exits 0: {proc.stdout} {proc.stderr}")
    plan = list(csv.DictReader(open(work / "fsm" / "pairs.csv", newline="")))
    check([(r["id"], r["dirs"]) for r in plan],
          [("fsm:0:1", "rev"), ("fsm:0:2", "both"), ("fsm:1:2", "fwd")],
          "a pair is planned in each direction no word refutes, none else")
    check(plan[0]["a_path"], str(work / "fsm" / "nodes" / "00000.json"),
          "with the node files written under the work directory")
    check(len((work / "fsm" / "members.list").read_text().split()), 5,
          "a node is drawn once per member that holds it")
    check(json.loads((work / "fsm" / "nodes" / "00002.json").read_text()),
          {"fp": "03", "i": 2}, "the node file is the pooled spec")
    calls = (fm_root / "fpdraw-calls.txt").read_text().split("\n")
    check([c for c in calls if c.startswith("eval")], ["eval 0", "eval 1"],
          "FPDRAW_UNGLUE is set on rad-core-* alone")
    check(len(joined.read_text().splitlines()), 4,
          "the joined pair list holds every subject's rows under one header")
    done_rows = list(csv.DictReader(open(out / "done-subjects-h.csv")))
    check([(r["subject"], r["pairs"]) for r in done_rows],
          [("fsm", "3"), ("rad-core-1", "0")],
          "each finished subject appends one progress row")
    meta = json.loads(manifest.read_text())
    check((meta["kind"], meta["planned"], meta["missing"], meta["out"]),
          ("fingerprint", 2, 0, str(out / "done-subjects-h.csv")),
          "the manifest names the progress file status counts")

    (fm_root / "fpdraw-calls.txt").unlink()
    proc = run_fm()
    check((proc.returncode, (fm_root / "fpdraw-calls.txt").exists()),
          (0, False), "a rerun skips every finished subject")
    check(len(list(csv.DictReader(open(out / "done-subjects-h.csv")))), 2,
          "and appends no progress row")
    (work / "fsm" / "pairs.csv").unlink()
    proc = run_fm()
    check((proc.returncode, (fm_root / "fpdraw-calls.txt").exists(),
           (work / "fsm" / "pairs.csv").exists()),
          (0, False, True), "a subject missing only its plan is re-planned "
                            "without re-drawing or re-evaluating")
finally:
    shutil.rmtree(fm_root, ignore_errors=True)

# ── pooled design: pool_members.py, planning, pooled_reduce.py ────────────────
#
# One design read three ways. Arms x/full, x/base and x/only, contrasts full
# and only against base. Each subject's runs hold, at seed 0:
#   full: n0 = {fp 01, i 0}
#   base: n1 = {fp 03, i 1}
#   only: n1 again (one node, two arms), n2 = {fp 01, i 3}, n3 = {fp 07, i 4}
# Open directions: 0 => 1, 2 => 1, 1 => 3, 0 => 3 and 2 => 3, 0 <=> 2. The
# design drops 0-2 and 0-3, since full and only share no node and no contrast.

SCRIPTS = Path(__file__).resolve().parent
POOL_MEMBERS_PY = SCRIPTS / "pool_members.py"
POOLED_REDUCE_PY = SCRIPTS / "pooled_reduce.py"
sys.path.insert(0, str(SCRIPTS))
import pooled_design as PD  # noqa: E402
import pooled_reduce as PR  # noqa: E402

pd_root = Path(tempfile.mkdtemp(prefix="pooled-design-"))
try:
    import gzip as _gzip
    design = pd_root / "design.toml"

    def write_design(seeds: str, extra: str = "") -> None:
        design.write_text(f"""
draws = 7
[[groups]]
results = "res"
run_dir = "{{level}}_{{scheme}}_{{subject}}_seed{{seed:02d}}"
seeds = "{seeds}"
subjects = ["fsm", "big"]
sampled = ["big"]
draws = 10
contrasts = [
  {{ name = "full", a = "x/full", b = "x/base" }},
  {{ name = "only-a", a = "x/only", b = "x/base", family = "only" }},
]
{extra}""")

    write_design("0-1")
    rec = PD.load_design(str(design))["subjects"]
    check((rec["fsm"]["arms"], rec["fsm"]["sampled"], rec["big"]["sampled"],
           rec["big"]["draws"], rec["fsm"]["seeds"],
           [c["family"] for c in rec["fsm"]["contrasts"]]),
          (["x/full", "x/base", "x/only"], False, True, 10, [0, 1],
           ["full", "only"]),
          "a design gives each subject its arms in contrast order, its "
          "estimator, draws and seeds, and a contrast its own family by default")
    check((PD.partners(rec["fsm"], "x/base"), PD.partners(rec["fsm"], "x/only")),
          (["x/full", "x/only"], ["x/base"]),
          "an arm's partners are the arms a contrast pairs it with")
    check(PD.parse_seeds("0-2,15-16", "s"), [0, 1, 2, 15, 16],
          "seeds take ranges and lists")
    for body, expect_in, why in (
        ({"groups": [{"results": "r", "run_dir": "{x}", "seeds": "0",
                      "subjects": ["s"], "contrasts": [
                          {"name": "c", "a": "s/a", "b": "s/b"}]}]},
         "run_dir may use only", "a run_dir naming an unknown field"),
        ({"groups": [{"results": "r", "run_dir": "d", "seeds": "0",
                      "subjects": ["s"], "sampled": ["t"], "contrasts": [
                          {"name": "c", "a": "s/a", "b": "s/b"}]}]},
         "sampled names t", "a sampled subject outside subjects"),
        ({"groups": [{"results": "r", "run_dir": "d", "seeds": "0",
                      "subjects": ["s"], "contrasts": [
                          {"name": "c", "a": "base", "b": "s/b"}]}]},
         "is not <scheme>/<level>", "an arm without its scheme"),
        ({"groups": [{"results": "r", "run_dir": "d", "seeds": "0-1,1",
                      "subjects": ["s"], "contrasts": [
                          {"name": "c", "a": "s/a", "b": "s/b"}]}]},
         "repeats a seed", "a repeated seed"),
        ({"groups": [{"results": "r", "run_dir": "d", "seeds": "0",
                      "subjects": ["s"], "contrasts": [
                          {"name": "c", "a": "s/a", "b": "s/a"}]}]},
         "the same arm", "a contrast of an arm with itself"),
    ):
        try:
            PD.parse_design(body)
            got = "accepted"
        except PD.DesignError as exc:
            got = str(exc)
        check_true(expect_in in got, f"{why} must be refused ({got!r})")
    src = PD.parse_design({"groups": [{
        "results": "r", "run_dir": "d", "seeds": "0-3", "members": "output",
        "subjects": ["s"],
        "contrasts": [{"name": "c", "a": "s/a", "b": "s/b"}],
        "sources": {"s/b": {"results": "r2", "seeds": "0-1,5"}}}]})["subjects"]["s"]
    check((src["sources"]["s/a"], src["sources"]["s/b"], src["seeds"]),
          ({"results": "r", "run_dir": "d", "seeds": [0, 1, 2, 3],
            "members": "output"},
           {"results": "r2", "run_dir": "d", "seeds": [0, 1, 5],
            "members": "output"}, [0, 1, 2, 3, 5]),
          "a sources entry replaces only the keys it names, for its arm alone, "
          "and the subject's seeds are the union over its arms")
    for sources, expect_in, why in (
        ({"s/z": {"results": "r2"}}, "which no contrast names",
         "a source for an arm no contrast names"),
        ({"s/b": {"subjects": ["s"]}}, "unknown key(s) subjects",
         "a source key that is not results, run_dir, seeds or members"),
    ):
        try:
            PD.parse_design({"groups": [{
                "results": "r", "run_dir": "d", "seeds": "0", "subjects": ["s"],
                "contrasts": [{"name": "c", "a": "s/a", "b": "s/b"}],
                "sources": sources}]})
            got = "accepted"
        except PD.DesignError as exc:
            got = str(exc)
        check_true(expect_in in got, f"{why} must be refused ({got!r})")
    check(PD.draw_order("s", "x/a", 3, [5, 1, 9, 2]),
          PD.draw_order("s", "x/a", 3, [2, 9, 1, 5]),
          "a draw order depends on the node set, not the order it is given in")

    def put_run(subject: str, arm: str, seed: int, specs) -> None:
        scheme, level = arm.split("/")
        run = pd_root / "res" / f"{level}_{scheme}_{subject}_seed{seed:02d}"
        (run / "accumulated").mkdir(parents=True)
        (run / "run.json").write_text("{}")
        if specs is None:
            return
        names = []
        for k, spec in enumerate(specs):
            names.append(f"gen01_{k:04d}.json")
            # Key order differs from run to run; identity is on sorted JSON.
            body = dict(reversed(list(spec.items()))) if k % 2 else spec
            (run / "accumulated" / names[-1]).write_text(json.dumps(body))
        (run / "accumulated" / "maximal.tsv").write_text(
            "file\n" + "".join(f"{n}\n" for n in names))
        (run / "repair_0.json").write_text(json.dumps(dict(specs[0], fitness=1)))

    for subject in ("fsm", "big"):
        put_run(subject, "x/full", 0, [{"fp": "01", "i": 0}])
        put_run(subject, "x/base", 0, [{"fp": "03", "i": 1}])
        put_run(subject, "x/only", 0, [{"i": 1, "fp": "03"},
                                       {"fp": "01", "i": 3},
                                       {"fp": "07", "i": 4}])
    pmembers = pd_root / "members"

    def run_pool(*extra) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(POOL_MEMBERS_PY), str(design), str(pmembers),
             "--root", str(pd_root), *extra],
            capture_output=True, text=True)

    proc = run_pool()
    check((proc.returncode,
           "fsm: 3 run director(ies) missing or unfinished" in proc.stderr,
           (pmembers / "fsm.jsonl.gz").exists()), (1, True, False),
          f"a pool missing a seed's runs is refused: {proc.stderr}")
    for subject in ("fsm", "big"):
        for arm in ("x/full", "x/base", "x/only"):
            put_run(subject, arm, 1, None)
    proc = run_pool()
    check(proc.returncode, 0, f"a complete pool exits 0: {proc.stderr}")
    with _gzip.open(pmembers / "fsm.jsonl.gz", "rt") as handle:
        pooled = [json.loads(line) for line in handle]
    check([(n["node"], n["arms"], n["spec"]["i"]) for n in pooled],
          [(0, ["x/full"], 0), (1, ["x/base", "x/only"], 1),
           (2, ["x/only"], 3), (3, ["x/only"], 4)],
          "identical sorted JSON is one node, numbered in design-arm order")
    check(pooled[1]["members"],
          [["x/base", 0, "accumulated/gen01_0000.json"],
           ["x/only", 0, "accumulated/gen01_0000.json"]],
          "and a node lists every member holding it")
    index = list(csv.DictReader(open(pmembers / "index.csv", newline="")))
    check([(r["subject"], r["arm"], r["runs"], r["runs_empty"], r["nodes"])
           for r in index if r["subject"] == "fsm"],
          [("fsm", "x/base", "2", "1", "1"), ("fsm", "x/full", "2", "1", "1"),
           ("fsm", "x/only", "2", "1", "3")],
          "the index counts runs, empty runs and nodes per arm")
    first = (pmembers / "fsm.jsonl.gz").read_bytes()
    run_pool("--subjects", "fsm")
    check(((pmembers / "fsm.jsonl.gz").read_bytes() == first,
           len(list(csv.DictReader(open(pmembers / "index.csv"))))),
          (True, 6), "a re-pool gives the same bytes and keeps other "
                     "subjects' index rows")
    write_design("0-1", "members = \"output\"")
    # members = "output" is a group key; the extra line lands in the group.
    run_pool("--subjects", "fsm")
    with _gzip.open(pmembers / "fsm.jsonl.gz", "rt") as handle:
        outs = [json.loads(line) for line in handle]
    check([(n["spec"], n["members"][0][2]) for n in outs],
          [({"fp": "01", "i": 0}, "repair_0.json"),
           ({"fp": "03", "i": 1}, "repair_0.json")],
          "members = \"output\" pools repair files without their fitness")
    # x/only read from another results tree at seed 0 alone.
    shutil.copytree(pd_root / "res" / "only_x_fsm_seed00",
                    pd_root / "res2" / "only_x_fsm_seed00")
    write_design("0-1", '[groups.sources]\n'
                        '"x/only" = { results = "res2", seeds = "0" }')
    proc = run_pool("--subjects", "fsm")
    with _gzip.open(pmembers / "fsm.jsonl.gz", "rt") as handle:
        srcd = [json.loads(line) for line in handle]
    check((proc.returncode, [n["node"] for n in srcd] == [n["node"] for n in pooled],
           [r["runs"] for r in csv.DictReader(open(pmembers / "index.csv"))
            if r["subject"] == "fsm" and r["arm"] == "x/only"]),
          (0, True, ["1"]), f"an arm with a sources entry is pooled from its "
                            f"own tree and seeds: {proc.stderr}")
    write_design("0-1")
    run_pool()

    # A host's pool phase: fsm is in the committed pool and copied, big is
    # pooled from results.
    committed, host_out = pd_root / "committed", pd_root / "host-members"
    committed.mkdir()
    shutil.copy(pmembers / "fsm.jsonl.gz", committed / "fsm.jsonl.gz")
    with open(pmembers / "index.csv", newline="") as handle:
        crow = [r for r in csv.DictReader(handle) if r["subject"] == "fsm"]
    with open(committed / "index.csv", "w", newline="") as handle:
        w = csv.DictWriter(handle, list(crow[0]))
        w.writeheader()
        w.writerows(crow)
    host_subjects = pd_root / "subjects-h.txt"
    host_subjects.write_text("fsm\nbig\n")
    # The committed copy has a seed the results lack, so a copy is visible.
    shutil.rmtree(pd_root / "res" / "full_x_fsm_seed01")
    host_manifest = pd_root / "pool.json"

    def run_host_pool() -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(POOL_MEMBERS_PY), str(design), str(host_out),
             "--root", str(pd_root), "--subjects-file", str(host_subjects),
             "--copy-from", str(committed), "--manifest", str(host_manifest)],
            capture_output=True, text=True)

    proc = run_host_pool()
    check(proc.returncode, 0, f"a host pool copies and pools: {proc.stderr}")
    check(((host_out / "fsm.jsonl.gz").read_bytes()
           == (committed / "fsm.jsonl.gz").read_bytes(),
           (host_out / "big.jsonl.gz").read_bytes()
           == (pmembers / "big.jsonl.gz").read_bytes()), (True, True),
          "the committed subject is copied byte for byte, the other pooled")
    hrows = list(csv.DictReader(open(host_out / "index.csv", newline="")))
    check(([r for r in hrows if r["subject"] == "fsm"] == crow,
           len([r for r in hrows if r["subject"] == "big"])), (True, 3),
          "and the copied subject's index rows are carried over unchanged")
    done = list(csv.DictReader(open(host_out / "done-subjects-h.csv")))
    hman = json.loads(host_manifest.read_text())
    check(([(r["subject"], r["source"]) for r in done], hman["kind"],
           hman["planned"], hman["out"], hman["missing"]),
          ([("fsm", "copied"), ("big", "pooled")], "pool", 2,
           str(host_out / "done-subjects-h.csv"), 0),
          "one progress row per subject, which the manifest names for status")
    (pd_root / "res" / "only_x_big_seed01" / "run.json").unlink()
    proc = run_host_pool()
    check((proc.returncode, "big: 1 run director(ies) missing or unfinished"
           in proc.stderr), (1, True),
          f"a run without run.json fails its subject: {proc.stderr}")
    check([r["subject"] for r in csv.DictReader(
        open(host_out / "done-subjects-h.csv"))], ["fsm"],
          "and the progress file holds only what this pass finished")
    (pd_root / "res" / "only_x_big_seed01" / "run.json").write_text("{}")
    with _gzip.open(committed / "fsm.jsonl.gz", "wt") as handle:
        handle.write("{}\n")
    proc = run_host_pool()
    check((proc.returncode, "does not match the checksum" in proc.stderr),
          (1, True), "a committed pool its index does not describe is refused")
    shutil.copy(pmembers / "fsm.jsonl.gz", committed / "fsm.jsonl.gz")
    proc = subprocess.run(
        [sys.executable, str(POOL_MEMBERS_PY), str(design), str(committed),
         "--subjects-file", str(host_subjects), "--copy-from", str(committed)],
        capture_output=True, text=True)
    check((proc.returncode, "one directory" in proc.stderr), (2, True),
          "and so is a pool written over the committed one")
    put_run("fsm", "x/full", 1, None)

    pwork, pout = pd_root / "work", pd_root / "out"
    psubjects = pd_root / "subjects.txt"
    psubjects.write_text("fsm\nbig\n")
    fpdraw = pd_root / "fpdraw"
    fpdraw.write_text(STUB_FPDRAW)
    fpdraw.chmod(0o755)
    draw = pd_root / "draw.py"
    draw.write_text(STUB_DRAW)

    def run_planner(*extra) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(FINGERPRINT_MEMBERS_PY), str(psubjects),
             str(pmembers), str(pwork), str(pout), "--words", "5",
             "--workers", "1", "--fpdraw", str(fpdraw), "--draw-script",
             str(draw), *extra], capture_output=True, text=True)

    def planned(subject: str) -> list:
        return sorted((r["id"], r["dirs"]) for r in csv.DictReader(
            open(pwork / subject / "pairs.csv", newline="")))

    proc = run_planner()
    check(proc.returncode, 0, f"the planner runs on a pool: {proc.stderr}")
    check(len(planned("fsm")), 6,
          "without a design every pair with an open direction is planned")
    shutil.rmtree(pwork)
    shutil.rmtree(pout)
    proc = run_planner("--design", str(design))
    check(proc.returncode, 0, f"a design narrows the plan: {proc.stderr}")
    want = [("fsm:0:1", "fwd"), ("fsm:1:2", "rev"), ("fsm:1:3", "fwd"),
            ("fsm:2:3", "fwd")]
    check(planned("fsm"), want,
          "an exhaustive subject keeps pairs sharing an arm or a contrast")
    check(planned("big"), [(i.replace("fsm", "big"), d) for i, d in want],
          "a sampled subject drawing every node keeps the pairs that can "
          "decide a draw")
    orders = {arm: [int(x) for x in (pout / "big" / f"order-x__{arm}.txt")
                    .read_text().split()] for arm in ("full", "base", "only")}
    check({arm: sorted(v) for arm, v in orders.items()},
          {"full": [0], "base": [1], "only": [1, 2, 3]},
          "the draw order of each arm is written in full")
    check_true(not (pout / "fsm" / "order-x__only.txt").exists(),
               "and an exhaustive subject writes none")
    big = PD.load_design(str(design))["subjects"]["big"]
    one_draw = dict(big, draws=1)
    bits = [0x01, 0x03, 0x01, 0x07]
    narrow = {(x, y) for x, y, _ in PD.sampled_plan(
        one_draw, pooled, bits, {"x/full": [0], "x/base": [1],
                                 "x/only": [2, 3, 1]})}
    check(narrow, {(0, 1), (1, 2)},
          "one draw an arm keeps only the drawn nodes' pairs")

    # Verdicts: each planned direction holds, except 1 => 3 is undecided.
    collected = pd_root / "collected"
    hfp = collected / "h" / "fp"
    hfp.mkdir(parents=True)
    for subject in ("fsm", "big"):
        shutil.copytree(pout / subject, hfp / subject)

    def write_verdicts(skip=None) -> None:
        with open(collected / "h" / "subsumption-h.csv", "w", newline="") as h:
            w = csv.writer(h)
            w.writerow(["id", "relation", "rc", "secs", "a_implies_b",
                        "b_implies_a"])
            for subject in ("fsm", "big"):
                for pid, dirs in want:
                    pid = pid.replace("fsm", subject)
                    if pid == skip:
                        continue
                    fwd = "?" if pid.endswith(":1:3") else (
                        "1" if dirs in ("fwd", "both") else "0")
                    rev = "1" if dirs in ("rev", "both") else "0"
                    w.writerow([pid, "x", 0, 2, fwd, rev])

    rout = pd_root / "reduced"

    def run_reduce() -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(POOLED_REDUCE_PY), str(design), str(pmembers),
             str(collected), str(rout)], capture_output=True, text=True)

    write_verdicts(skip="fsm:2:3")
    proc = run_reduce()
    check_true(proc.returncode != 0 and "1 planned pair(s) have no row"
               in proc.stderr,
               f"a reduction missing a planned row is refused: {proc.stderr}")
    write_verdicts()
    proc = run_reduce()
    check(proc.returncode, 0, f"a complete reduction exits 0: {proc.stderr}")
    rows = {(r["subject"], r["contrast"]): r for r in csv.DictReader(
        open(rout / "strength" / "contrasts.csv", newline=""))}
    fsm_full, fsm_only = rows[("fsm", "full")], rows[("fsm", "only-a")]
    check((fsm_full["a_front"], fsm_full["b_front"],
           fsm_full["net_imp"], fsm_full["net_non"]),
          ("1", "1", "1.0", "1.0"),
          "full's lone node implies base's: a is the stronger, net +1")
    check_true("net" not in fsm_full and "sub_a" not in fsm_full,
               "the dropped-pair reading writes no shares, being non's")
    check((fsm_only["a_front"], fsm_only["net_non"], fsm_only["queries"],
           fsm_only["call_s_sum"]),
          ("1", "1.0", "4", "8"),
          "only's frontier is n2 alone, which implies base's n1")
    big_full, big_only = rows[("big", "full")], rows[("big", "only-a")]
    check((big_full["plan"], big_full["net_non"], big_only["net_non"],
           big_only["frontier_draws_a"], big_only["undecided"],
           big_full["undecided"]),
          ("sampled", "1.0", "1.0", "1", "2", "0"),
          "the sampled estimator agrees when every node is drawn, and counts "
          "the undecided direction a draw met")
    summary_text = (rout / "strength" / "summary.txt").read_text()
    check_true("Holm (only)" in summary_text and "Holm (full)" in summary_text,
               "the summary corrects within each contrast family")
    check_true((rout / "ball-radii" / "report-k1.txt").exists(),
               "and the ball-radii report is written")

    check(PR.wilcoxon_p([1, 2, 3, 4, 5]), 0.0625,
          "the exact signed-rank p of five positive differences is 2/32")
    check(round(PR.wilcoxon_p([1, -1, 2, 2]), 4), round(2 * 3 / 16, 4),
          "and ties take their average rank")
    check({k: round(v, 6) for k, v in
           PR.holm({"a": 0.01, "b": 0.04, "c": 0.03}).items()},
          {"a": 0.03, "c": 0.06, "b": 0.06},
          "Holm's adjustment is monotone in the sorted p")
    check(round(PR.jaccard(0b0110, 0b0011), 4), round(2 / 3, 4),
          "Jaccard distance is the symmetric difference over the union")

    # a's two members sit 0.5 apart, b's 1.0 apart. b's 0b0111 is within
    # a's radius of 0b0011 (1/3), b's 0b1000 is 1.0 from both; b's radius
    # of 1.0 holds every point that shares a word with its member, which is
    # all of a.
    hand = [0b0011, 0b0001, 0b0111, 0b1000]
    got = PR.pooled_draw(PR.Dist([0, 1, 2, 3], hand), [0, 1], [2, 3], [1])
    check(got, {1: (0.5, 1.0)},
          "pooled containment: half of b in a's region, all of a in b's")
    # Every pair shares no word, so every radius is 1.0; no ball holds a
    # point it shares nothing with (a ball of radius 1.0 once held all).
    apart = [0b0001, 0b0010, 0b0100, 0b1000]
    got = PR.pooled_draw(PR.Dist([0, 1, 2, 3], apart), [0, 1], [2, 3], [1])
    check(got, {1: (0.0, 0.0)},
          "a point sharing no word lies in no ball, even of radius 1.0")
    check_true(PR.inside(0.5, 0.5) and not PR.inside(1.0, 1.0),
               "a ball holds its boundary except at distance 1.0")
    rng = random.Random(7)
    same = True
    for _ in range(50):
        prints = [rng.getrandbits(48) | 1 for _ in range(20)]
        ga = rng.sample(range(20), 6)
        gb = rng.sample(range(20), 6)
        dist = PR.Dist(sorted(set(ga) | set(gb)), prints)
        for k in (1, 3):
            x, y = PR.pooled_draw(dist, ga, gb, [k])[k]
            unit = PR.unit_row({"d": ga, "u": gb}, prints, k, None)
            same &= (round(x, 4) == unit[3 + PR.PAIRS.index("du")]
                     and round(y, 4) == unit[3 + PR.PAIRS.index("ud")])
    check_true(same, "pooled containment agrees with the per-run unit_row")
    # Arm b repeats arm a run for run (one RNG stream, a feature that never
    # fires): with seed-disjoint halves the contrast reads as the control,
    # not as full overlap.
    twin_bits = [rng.getrandbits(48) | 1 for _ in range(16)]
    twin = [{"node": i, "arms": ["x/a", "x/b"],
             "members": [["x/a", i % 8, "f"], ["x/b", i % 8, "f"]]}
            for i in range(16)]
    trows = PR.pooled_task(("s", {"name": "c", "a": "x/a", "b": "x/b"},
                            twin, twin_bits, [1], 1, "0"))
    check((trows[0]["b_in_a"], trows[0]["a_in_b"]),
          (trows[0]["ctrl_a"], trows[0]["ctrl_b"]),
          "an arm identical to base run for run reads as base's own control")
    check_true(trows[0]["b_in_a"] < 1.0,
               "and the shared seeds do not count as overlap")
    # The seed-half reach control. n0 is held at seeds 0 and 1, so in both
    # halves; n1 at seed 0 alone, n2 at seed 1 alone, and n1 strictly implies
    # n2. The halves' joint front is {n0, n1}: the even half holds both, the
    # odd half n0 alone.
    import joint_measures as JM  # noqa: E402
    hnodes = [{"node": 0, "arms": ["x/a"],
               "members": [["x/a", 0, "f"], ["x/a", 1, "f"]]},
              {"node": 1, "arms": ["x/a"], "members": [["x/a", 0, "f"]]},
              {"node": 2, "arms": ["x/a"], "members": [["x/a", 1, "f"]]}]
    hV = {(1, 2): "1", (2, 1): "0", (0, 1): "0", (1, 0): "0",
          (0, 2): "0", (2, 0): "0"}
    hbits = [0b0011, 0b0100, 0b1100]
    got = JM.half_control(hnodes, hbits, hV, "x/a", {"sampled": False}, None, 4)
    check(got, {"even": 1.0, "odd": 0.5, "control": 0.75, "joint_front": 2},
          "each half's reach is on the joint front of the two halves, and the "
          "control is their mean")
    got = JM.half_control(hnodes, hbits, hV, "x/a",
                          {"sampled": True, "draws": 3}, {"x/a": [2, 1, 0]}, 4)
    check(got, {"even": 1.0, "odd": 0.5, "control": 0.75, "joint_front": 2},
          "the sampled estimator agrees when every node is drawn")
    lone = [dict(n, members=[["x/a", 0, "f"]]) for n in hnodes]
    check(JM.half_control(lone, hbits, hV, "x/a", {"sampled": False}, None,
                          4)["control"], None,
          "an arm with no odd seed has no control")
    jout = pd_root / "joint"
    proc = subprocess.run(
        [sys.executable, str(SCRIPTS / "joint_measures.py"), str(design),
         str(pmembers), str(collected), str(jout), "--half-control"],
        capture_output=True, text=True)
    check(proc.returncode, 0, f"joint_measures.py --half-control exits 0: "
                              f"{proc.stderr}")
    crow = list(csv.DictReader(open(jout / "half-control.csv", newline="")))
    check((len(crow), {r["control"] for r in crow}), (6, {""}),
          "a row per subject and arm, blank where an arm's odd seed is empty")
    check_true("reach control, odd against even seeds" in
               (jout / "summary.txt").read_text(),
               "and the summary reports the control")
    proc = subprocess.run(
        [sys.executable, str(SCRIPTS / "joint_measures.py"), str(design),
         str(pmembers), str(collected), str(jout), "--half-control",
         "--share-only"], capture_output=True, text=True)
    check(proc.returncode, 2, "--half-control with --share-only is refused")
    proc = subprocess.run(
        [sys.executable, str(POOLED_REDUCE_PY), str(design), str(pmembers),
         str(collected), str(rout), "--pooled-only", "--pooled-draws", "2"],
        capture_output=True, text=True)
    check(proc.returncode, 0, f"a pooled-only reduction exits 0: {proc.stderr}")
    prow = list(csv.DictReader(open(rout / "ball-radii-pooled" / "pooled.csv",
                                    newline="")))
    check_true(prow and all(r["b_in_a"] == "" for r in prow)
               and (rout / "ball-radii-pooled" / "report-k1.txt").exists(),
               "a pool no larger than K leaves its pooled cells blank")
finally:
    shutil.rmtree(pd_root, ignore_errors=True)


if FAILURES:
    print(f"\n{len(FAILURES)} pooled-pass test(s) failed.")
    sys.exit(1)
print("All pooled-pass tests passed.")
