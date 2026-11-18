#!/usr/bin/env python3
"""Tests for score_campaign.py, the scorer a `kind = "score"` phase runs.

No pytest dependency: run it directly (``python
scripts/test_score_campaign.py``). Like test_campaign.py, it records every
failure and reports them together at the end, exiting non-zero once the whole
run is done. Nothing here touches a lab machine: the tools the scripts call are
stubs.
"""

import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
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


def git(root: Path, *args: str) -> str:
    """git against a fixture, with the ambient config that could break it off.

    Identity and signing are pinned rather than inherited: a machine whose
    global config sets `commit.gpgsign` cannot commit here without a key, and
    the fixture's whole purpose is to behave the same on every box.
    """
    proc = subprocess.run(
        ["git", "-C", str(root), "-c", "user.email=t@t", "-c", "user.name=t",
         "-c", "commit.gpgsign=false", "-c", "gpg.format=openpgp", *args],
        capture_output=True, text=True)
    if proc.returncode != 0:
        # Fatal, unlike an assertion: the fixture is unusable from here on.
        fail(f"git {' '.join(args)}: {proc.stderr.strip()}")
        raise SystemExit(1)
    return proc.stdout.strip()


# ── score_campaign.py ─────────────────────────────────────────────────────────
#
# The scorer itself, against a fake results tree, a stub score_curves.py and
# two stub binaries, run as the tick runs it: a subprocess with the overrides
# in its environment. What matters is the bookkeeping around the scorer --
# which runs are queued and in what order, what a failure leaves behind, what
# a rerun repeats -- since the pass costs hundreds of worker-hours and a resume
# that re-scores a finished run, or skips a failed one, is invisible until the
# bill arrives.

SCORE_CAMPAIGN_PY = Path(__file__).resolve().parent / "score_campaign.py"

STUB_CURVES = '''#!/usr/bin/env python3
"""Stands in for score_curves.py: writes a curve, or fails as its run says."""
import os, sys, time
out = sys.argv[sys.argv.index("--out") + 1]
run = os.path.basename(sys.argv[-1])
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "calls.txt"), "a") as handle:
    handle.write(" ".join(sys.argv[1:]) + "\\n")
if "fail" in run:
    sys.exit(3)
if "slow" in run:
    # A grandchild standing in for maximal, whose pid the test checks after
    # the wall cap fires: the cap has to kill the scorer's whole session.
    import subprocess
    child = subprocess.Popen([sys.executable, "-c",
                              "import time; time.sleep(60)"])
    with open(out + ".grandchild", "w") as handle:
        handle.write(str(child.pid))
    time.sleep(10)
if "empty" in run:
    open(out, "w").close()
    sys.exit(0)
with open(out, "w") as handle:
    handle.write("spec,seed,metric,value\\n" + run + ",0,solutions,1\\n")
print("scored", run)
'''

HEAD_SHA = "f" * 40


def fake_binary(path: Path, commit: str = HEAD_SHA, dirty: str = "0") -> None:
    path.write_text(f"#!/bin/sh\necho commit={commit}\necho commit_short="
                    f"{commit[:7]}\necho dirty={dirty}\n")
    path.chmod(0o755)


score_root = Path(tempfile.mkdtemp(prefix="campaign-score-"))
try:
    bins = score_root / "bin"
    bins.mkdir()
    for name in ("maximal", "compare"):
        fake_binary(bins / name)
    stub_dir = score_root / "stub"
    stub_dir.mkdir()
    stub_curves = stub_dir / "stub_curves.py"
    stub_curves.write_text(STUB_CURVES)
    curve_calls = stub_dir / "calls.txt"
    results = score_root / "results-x"
    # Five runs on this host's seeds, one on another host's, and one
    # directory that is not a run at all. Index sizes chosen so the
    # smallest-first order differs from the alphabetical one.
    for run, lines in (("b_seed1", 4), ("a_seed2", 2), ("c_seed3", 0),
                       ("fail_seed1", 1), ("empty_seed2", 1),
                       ("other_seed9", 0), ("accumulated", 0)):
        (results / run).mkdir(parents=True)
        if lines:
            (results / run / "accumulated").mkdir()
            (results / run / "accumulated" / "index.tsv").write_text(
                "header\n" + "row\n" * (lines - 1))
    (results / "notes.txt").write_text("not a run\n")
    out = score_root / "curves-x"

    # The gate compares against the HEAD of the checkout the scorer lives in,
    # which is this one, so the fake binaries report that -- the position a
    # freshly staged host is in.
    real_head = git(SCORE_CAMPAIGN_PY.parent.parent, "rev-parse", "HEAD")
    for name in ("maximal", "compare"):
        fake_binary(bins / name, real_head)

    def scorer(*extra: str, env_extra: dict | None = None,
               seeds=("1", "2", "3")) -> subprocess.CompletedProcess:
        env = dict(os.environ, PEREDUR_BIN_DIR=str(bins),
                   PEREDUR_SCORE_CURVES_CMD=f"{sys.executable} {stub_curves}",
                   **(env_extra or {}))
        return subprocess.run(
            [sys.executable, str(SCORE_CAMPAIGN_PY), "--results", str(results),
             "--out", str(out), "--seeds", *seeds, "--cores", "1",
             "--deadline-s", "1", "--wall-cap-s", "2", *extra],
            cwd=str(score_root), env=env, capture_output=True, text=True)

    proc = scorer("--dry-run", "--workers", "1")
    check(proc.returncode, 0, f"a dry run exits 0: {proc.stderr}")
    check_true("5 run(s)" in proc.stdout and "5 to score" in proc.stdout,
               f"and counts this host's runs alone: {proc.stdout}")
    check_true("other_seed9" not in proc.stdout,
               "another host's seed is not queued here")
    check_true("accumulated" not in proc.stdout.split("queue:")[1]
               and "notes.txt" not in proc.stdout,
               "nor a directory or file that is not a run")
    check_true("--maximality --cuts 20 --jobs 1 --deadline-s 1 "
               "--maximal-timeout 900 --compare-timeout 600 --out "
               "<out>/<run>.csv.part <run-dir>" in proc.stdout,
               f"the command template is printed: {proc.stdout}")
    check_true(not out.exists() and not curve_calls.exists(),
               "and a dry run writes nothing and scores nothing")

    # The gate: a binary from another commit refuses, --allow-stale-binary
    # lets it through and is recorded.
    fake_binary(bins / "maximal", "a" * 40)
    proc = scorer("--workers", "1", "--dry-run")
    check(proc.returncode, 0, "a dry run reports a stale binary and exits 0")
    proc = scorer("--workers", "1")
    check(proc.returncode, 1, "a stale maximal refuses to score")
    check_true("STALE BINARY" in proc.stdout and "maximal" in proc.stdout
               and "Refusing" in proc.stderr,
               f"naming the binary: {proc.stdout} {proc.stderr}")
    check_true(not out.exists(), "before writing anything")
    fake_binary(bins / "maximal", real_head)
    fake_binary(bins / "compare", real_head, dirty="1")
    proc = scorer("--workers", "1")
    check(proc.returncode, 1, "and a compare built dirty refuses too")
    check_true("compare" in proc.stdout and "modified working tree"
               in proc.stdout, "saying which and why")
    fake_binary(bins / "compare", real_head)

    # A run with everything in it, one worker so the order is observable.
    proc = scorer("--workers", "1")
    check(proc.returncode, 1, "a pass with failures exits 1")
    calls = [ln.split()[-1].rsplit("/", 1)[-1]
             for ln in curve_calls.read_text().splitlines()]
    check(calls, ["c_seed3", "empty_seed2", "fail_seed1", "a_seed2", "b_seed1"],
          "smallest index first, ties on the name")
    check(sorted(p.name for p in out.glob("*.csv")),
          ["a_seed2.csv", "b_seed1.csv", "c_seed3.csv"],
          "the runs that scored have their curve in place")
    check(list(out.glob("*.part")), [], "and no .part file is left behind")
    check_true("empty_seed2" in (out / "failures.txt").read_text()
               and "3 " in (out / "failures.txt").read_text(),
               "an empty output is a failure, as is a non-zero exit")
    failures = {ln.split()[1].rsplit("/", 1)[-1]: ln.split()[0]
                for ln in (out / "failures.txt").read_text().splitlines()}
    check(failures, {"fail_seed1": "3", "empty_seed2": "0"},
          "failures.txt carries the exit status and the run")
    timings = [ln.split() for ln in (out / "timings.txt").read_text()
               .splitlines()]
    check([t[3] for t in timings], calls,
          "timings.txt has one line per attempt, in order")
    check([t[0] for t in timings], ["0", "0", "0", "1", "3"],
          "carrying the accumulated count, the index less its header")
    check({t[4] for t in timings}, {"900/600/1"},
          "and the budgets the run was scored under")
    check_true(all(t[2] in ("0", "3") for t in timings),
               "with the exit status per line")
    check_true("scored c_seed3" in (out / "warnings.log").read_text(),
               "the scorers' output lands in warnings.log")

    manifests = list(out.glob("score-manifest-*.json"))
    check(len(manifests), 1, "one manifest, named for the host")
    manifest = json.loads(manifests[0].read_text())
    check(manifest["counts"],
          {"queued": 5, "already_scored": 0, "scored": 3, "failed": 2},
          "the manifest counts the pass")
    check(manifest["seeds"], [1, 2, 3], "and records the seeds")
    check((manifest["workers"], manifest["cores"], manifest["cuts"],
           manifest["maximal_timeout"], manifest["compare_timeout"],
           manifest["deadline_s"], manifest["wall_cap_s"]),
          (1, 1, 20, 900, 600, 1, 2), "every budget")
    check(manifest["binaries"]["maximal"]["commit"], real_head,
          "both scoring binaries' commits")
    check(manifest["binaries"]["compare"]["dirty"], "0", "and their dirty flag")
    check(manifest["git"]["head"], real_head, "the checkout's head")
    check(manifest["allow_stale_binary"], False,
          "whether the gate was overridden")
    check_true(manifest["finished"] is not None and manifest["started"],
               "and both timestamps")
    check_true(manifest["python_version"].startswith("3."),
               "and the interpreter")
    check_true(manifest["invocation"].endswith(
        "--out <out>/<run>.csv.part <run-dir>"), "and the invocation template")
    check(manifest["pinned"], shutil.which("taskset") is not None,
          "and whether the workers were pinned")

    # Resume: the failures are retried, the curves are not.
    curve_calls.unlink()
    proc = scorer("--workers", "2")
    check(proc.returncode, 1, "still failing, so still 1")
    calls = sorted(ln.split()[-1].rsplit("/", 1)[-1]
                   for ln in curve_calls.read_text().splitlines())
    check(calls, ["empty_seed2", "fail_seed1"],
          "a rerun scores only the runs with no curve")
    manifest = json.loads(manifests[0].read_text())
    check(manifest["counts"],
          {"queued": 5, "already_scored": 3, "scored": 0, "failed": 2},
          "and the manifest says which were already there")
    check(len((out / "timings.txt").read_text().splitlines()), 7,
          "timings.txt keeps every attempt")

    # Every run has a curve: exit 0, so a tick moves the entry on.
    (results / "fail_seed1").rename(results / "fixed_seed1")
    (results / "empty_seed2").rename(results / "full_seed2")
    (out / "failures.txt").write_text("")
    proc = scorer("--workers", "2")
    check(proc.returncode, 0, f"a complete pass exits 0: {proc.stdout}")
    check_true("5/5 curves present" in proc.stdout, "saying so")
    check(json.loads(manifests[0].read_text())["counts"]["scored"], 2,
          "with the two new curves counted")

    # The outer wall cap: a scorer that overruns is killed and recorded as
    # coreutils timeout records it, and the run has no curve.
    (results / "slow_seed3").mkdir()
    proc = scorer("--workers", "1", seeds=("3",))
    check(proc.returncode, 1, "a run killed by the wall cap fails the pass")
    check_true("124 " in (out / "failures.txt").read_text()
               and "slow_seed3" in (out / "failures.txt").read_text(),
               f"and lands in failures.txt as exit 124: "
               f"{(out / 'failures.txt').read_text()!r}")
    check_true(not (out / "slow_seed3.csv").exists()
               and not (out / "slow_seed3.csv.part").exists(),
               "leaving neither a curve nor a .part")

    def grandchild_dead() -> bool:
        """Whether the stand-in maximal the slow scorer forked was killed
        with it. Polled briefly: the kill is asynchronous to the wait."""
        pid = int((out / "slow_seed3.csv.part.grandchild").read_text())
        for _ in range(50):
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                return True
            time.sleep(0.1)
        os.kill(pid, signal.SIGKILL)
        return False

    check_true(grandchild_dead(),
               "and the scorer's own child died with it: the cap kills the "
               "whole session, not score_curves.py alone")

    # Unpinned where taskset is absent: an empty PATH has no taskset and no
    # timeout, and the manifest says so rather than pretending.
    proc = scorer("--workers", "1", seeds=("2",),
                  env_extra={"PATH": str(score_root / "nowhere")})
    check(proc.returncode, 0, f"an unpinned pass still scores: {proc.stderr}")
    manifest = json.loads(manifests[0].read_text())
    check(manifest["pinned"], False, "and records that it was unpinned")
    check_true("no taskset" in manifest["pinning"], "in words")

    # The fallback cap, with no coreutils timeout to kill the group, has to
    # reach the grandchild too.
    (out / "slow_seed3.csv.part.grandchild").unlink()
    proc = scorer("--workers", "1", seeds=("3",),
                  env_extra={"PATH": str(score_root / "nowhere")})
    check(proc.returncode, 1, "the fallback cap still fails the slow run")
    check_true(grandchild_dead(),
               "and kills the scorer's child as coreutils timeout would")
    check_true("124 slow" in " ".join(
        ln.split()[2] + " " + ln.split()[3]
        for ln in (out / "timings.txt").read_text().splitlines()),
        "recording exit 124 as the coreutils cap does")

    # Nothing to score is never a finished pass: a tick would mark the phase
    # done over nothing and status would read 0 of 0.
    proc = scorer("--workers", "1", seeds=("42",))
    check(proc.returncode, 2, "a queue with no run on this host's seeds is "
                              "an error")
    check_true("nothing to score" in proc.stderr and "42" in proc.stderr
               and str(results) in proc.stderr,
               f"naming the directory and the seeds: {proc.stderr!r}")
    proc = scorer("--workers", "1", "--dry-run", seeds=("42",))
    check(proc.returncode, 2, "and so is a dry run of one")

    # An oversized pool is not slow but fast: taskset -c on a core the host
    # does not have exits 1 at once, and the queue drains into failures.txt.
    cpus = os.cpu_count()
    if cpus is not None:
        failures_before = (out / "failures.txt").read_text()
        (out / "b_seed1.csv").unlink()
        proc = scorer("--workers", str(cpus + 1), "--cores", "1",
                      seeds=("1",))
        check(proc.returncode, 2, "workers x cores past the host's CPUs is "
                                  "refused at startup")
        check_true(str(cpus) in proc.stderr and str(cpus + 1) in proc.stderr,
                   f"naming both counts: {proc.stderr!r}")
        check((out / "failures.txt").read_text(), failures_before,
              "before any run is attempted")

    for bad, why in ((["--workers", "0"], "zero workers"),
                     (["--cuts", "-1"], "negative cuts"),
                     (["--wall-cap-s", "0"], "a zero wall cap")):
        proc = scorer(*bad)
        check(proc.returncode, 2, f"{why} is an argument error")
    proc = subprocess.run(
        [sys.executable, str(SCORE_CAMPAIGN_PY), "--results",
         str(score_root / "absent"), "--out", str(out), "--seeds", "1"],
        capture_output=True, text=True)
    check_true(proc.returncode != 0 and "no results directory" in proc.stderr,
               "a missing results directory is refused by name")
finally:
    shutil.rmtree(score_root, ignore_errors=True)


if FAILURES:
    print(f"\n{len(FAILURES)} score_campaign.py test(s) failed.")
    sys.exit(1)
print("All score_campaign.py tests passed.")
