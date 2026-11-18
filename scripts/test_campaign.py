#!/usr/bin/env python3
"""Tests for campaign.py: parsing, process detection, collect, stage and queue.

No pytest dependency, matching test_experiment_paths.py: run it directly
(``python scripts/test_campaign.py``). Unlike that suite, this one records
every failure and reports them together at the end, exiting non-zero once the
whole run is done — see ``fail`` for why. Read the closing summary rather than
the first FAIL line. Nothing here touches a lab machine — the remote protocol is exercised
against captured marker output, and ``collect`` is exercised against two
throwaway checkouts under a temporary directory, so the merge, the natural key
and the verification all run for real without a 29GB transfer.

The failure modes worth guarding are the silent ones. Process detection keyed
on the whole command line would match this tool's own ssh command and report a
finished campaign as running; a plan line read positionally would start
reporting the aliasing count as the row total; and a collect that loses rows or
duplicates keys looks exactly like a collect that worked. Two hosts declaring
overlapping seed ranges is the same shape of failure one level up: both run the
overlap, the merge keeps one row per key, and the campaign costs more and
yields less than it says while every table reads normal.

The stage and queue tests run against throwaway git checkouts under a temporary
directory, with PEREDUR_RUNNER_CMD pointed at a stub that records its arguments
instead of running a campaign. No lab machine is touched and no run is launched.
"""

import argparse
import contextlib
import csv
import importlib
import io
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from dataclasses import replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import campaign as C  # noqa: E402
import merge_experiments as merge  # noqa: E402

# The script under test, as a path rather than through the module's __file__,
# which is typed as optional and is not a path the moment a module is built
# some other way.
CAMPAIGN_PY = Path(__file__).resolve().parent / "campaign.py"


FAILURES: list = []


def fail(msg):
    """Record a failure and carry on to the rest of the suite.

    Exiting on the first one hid whatever came after it, which is how an
    ambient-state dependency in the stage fixtures went unnoticed on a busy
    machine: 79 of the 227 assertions here sat behind the one that failed.
    """
    print(f"FAIL: {msg}")
    FAILURES.append(msg)


def check(got, want, msg):
    if got != want:
        fail(f"{msg}\n  got:  {got!r}\n  want: {want!r}")


def check_true(cond, msg):
    if not cond:
        fail(msg)


# ── The runner's Plan line ────────────────────────────────────────────────────

PLAN = "400 result rows (0 via aliasing), 400 already done; 0 runs to execute"

check(C.parse_plan_line(PLAN),
      {"rows_planned": 400, "rows_done_plan": 400, "runs_left": 0},
      "plan line as run_experiments.py prints it")

check(C.parse_plan_line(
    "7600 result rows (1520 via aliasing), 42 already done; 7558 runs to execute"),
    {"rows_planned": 7600, "rows_done_plan": 42, "runs_left": 7558},
    "aliasing count must not be read as the row total")

# Each number is read from the phrase naming it, so a clause inserted ahead of
# one does not reassign the others.
check(C.parse_plan_line(
    "Plan: 12 skipped, 100 result rows (0 via aliasing), 5 already done; "
    "95 runs to execute")["rows_planned"],
    100, "an added leading clause must not shift rows_planned")

check(C.parse_plan_line("nothing resembling a plan"), {},
      "a non-plan line yields no counts rather than a wrong one")


# ── Inventory and detail parsing ──────────────────────────────────────────────

MANIFEST = """{
  "profile": "fretish-order-pairs",
  "hostname": "av3",
  "started": "2026-08-11T12:53:14+0100",
  "git": {"branch": "feat/fretish-order-pairs", "describe": "x",
          "head": "b093374f0a0d8e83d0a9fe3412e6935bffc5b078"},
  "binaries": {"peredur": {"commit_short": "b093374", "dirty": "0"}},
  "sweep": {"sweeps": ["R"], "specs": ["amba"], "seeds": [20, 21], "jobs": 1,
            "results_csv": "experiments/results-fretish-order-pairs.csv",
            "results_dir": "experiments/results-fretish-order-pairs"}
}"""

INVENTORY = f"""##PS
zsh -zsh
python3 python3 scripts/run_experiments.py --profile fretish-order-pairs --jobs 1
##HOST
av3
1786460000
##GIT
feat/fretish-order-pairs
b093374
0
##MANIFESTS
##FILE experiments/results-fretish-order-pairs-manifest-av3.json
{MANIFEST}
##ENDFILE
##END
"""

inv = C.parse_inventory(INVENTORY)
check(inv["hostname"], "av3", "hostname section")
check(inv["epoch"], 1786460000, "host clock sampled in the same call")
check(inv["branch"], "feat/fretish-order-pairs", "checkout branch")
check(inv["head"], "b093374", "checkout head")
check(inv["dirty"], False, "clean checkout")
check(len(inv["manifests"]), 1, "one manifest parsed")
check(inv["error"], None, "no error on a good inventory")

check(C.parse_inventory("##ERR not a directory: /nope")["error"],
      "not a directory: /nope", "a missing checkout reports rather than raises")

# A manifest that is not valid JSON is skipped, not fatal: a launch killed
# mid-write leaves one, and it must not take the whole poll down.
broken = C.parse_inventory("##MANIFESTS\n##FILE x\n{not json\n##ENDFILE\n##END\n")
check(broken["manifests"], [], "an unparseable manifest is skipped")

campaigns = C.campaigns_from_manifests(inv["manifests"])
check(len(campaigns), 1, "one campaign per manifest")
c = campaigns[0]
check(c["profile"], "fretish-order-pairs", "profile from the manifest")
check(c["branch"], "feat/fretish-order-pairs", "launch branch from the manifest")
check(c["binary_commit"], "b093374", "binary commit from the manifest")
check(c["results_csv"], "experiments/results-fretish-order-pairs.csv", "csv path")

legacy = C.campaigns_from_manifests([{
    **inv["manifests"][0],
    "binaries": {"counter": {"commit_short": "0129e6c", "dirty": "1"}}}])
check(legacy[0]["binary_commit"], "0129e6c",
      "a manifest from before the rename keys the engine as counter")
check(legacy[0]["dirty_binary"], True, "and its dirty flag reads the same key")

check(C.plan_args(c),
      "--profile fretish-order-pairs --dry-run --sweeps R --specs amba --seeds 20 21",
      "plan args replay the manifest's own selection")

no_sweeps = dict(c, sweeps=None)
check_true("--sweeps" not in C.plan_args(no_sweeps),
           "a manifest recording every sweep must not pin one")

DETAIL = """##CAMPAIGN fretish-order-pairs
##CSVROWS 401
##CSVMTIME 1786455660
##LOGMTIME 1786455673.4823
##RUNDIRS 400
##PLAN 400 result rows (0 via aliasing), 400 already done; 0 runs to execute
##CAMPAIGN empty-one
##CSVROWS 1
##END
"""
detail = C.parse_detail(DETAIL)
check(detail["fretish-order-pairs"]["rows_done"], 400, "done rows from the plan")
check(detail["fretish-order-pairs"]["rows_planned"], 400, "planned rows from the plan")
check(detail["fretish-order-pairs"]["csv_rows"], 400, "wc -l counts the header too")
check(detail["fretish-order-pairs"]["log_mtime"], 1786455673, "fractional mtime")
check_true("rows_from_csv" not in detail["fretish-order-pairs"],
           "a campaign with a plan is not on the fallback")
check(detail["empty-one"]["rows_done"], 0, "a header-only CSV has zero rows")
check_true("rows_planned" not in detail["empty-one"],
           "a profile the checkout no longer defines reports no plan")
check(detail["empty-one"]["rows_from_csv"], True,
      "and falls back to the file's length, marked as such")

# The CSV's length is not this launch's progress. Profiles may share a CSV, and
# a top-up relaunch rewrites the manifest to a couple of seeds while the file
# keeps every earlier row. Reading the file's length as done would report 400/40 here and, worse,
# call a campaign that started minutes ago finished.
TOPUP = """##CAMPAIGN replicate
##CSVROWS 401
##PLAN 40 result rows (0 via aliasing), 0 already done; 40 runs to execute
##END
"""
topup = C.parse_detail(TOPUP)["replicate"]
check(topup["rows_done"], 0, "a top-up reads its own share, not the whole file")
check(topup["rows_planned"], 40, "against its own plan")
shared = dict(topup, profile="replicate", branch="b", started=None)
C.annotate(shared, {"epoch": 1786460000, "processes": []})
check(shared["state"], "stalled",
      "a just-relaunched campaign must not be reported done")


# ── Process detection ─────────────────────────────────────────────────────────

# The last line is this tool's own ssh command as the remote ps sees it: its
# text names run_experiments.py, and matching on the command line rather than
# on comm would report an idle machine as running.
PS = [
    "zsh -zsh",
    "sshd sshd: benandrew@notty",
    "zsh zsh -c cd /home/benandrew/projects/counter && python3 "
    "scripts/run_experiments.py --profile fretish-order-pairs --dry-run",
    "python3 python3 scripts/run_experiments.py --profile aurus-h2h --jobs 4",
    "peredur /home/benandrew/projects/counter/build-release/peredur --input x",
]
procs = C.live_processes(PS)
check([p["comm"] for p in procs], ["python3", "peredur"],
      "only the runner and the engine, never the shell that names them")
check([p["comm"] for p in C.live_processes([
    "counter /home/benandrew/projects/counter/build-release/counter --input x"])],
    ["counter"], "an engine binary built before the rename is still an engine")
check(procs[0]["profile"], "aurus-h2h", "the runner's profile is read off its args")

check(C.live_processes([
    "python3 python3 scripts/run_experiments.py --profile fretish-order-pairs --dry-run"]),
    [], "a --dry-run runner is a status probe, not a campaign")

check(C.profile_of_args("python3 scripts/run_experiments.py --jobs 4"), None,
      "an unflagged runner names no profile, the flag having no default")
check(C.profile_of_args("python3 x.py --profile=aurus-h2h"), "aurus-h2h", "--profile=VALUE")


# ── The remote scripts must survive the lab's login shell ─────────────────────

# The lab shell is zsh, whose default NOMATCH aborts the script where a glob
# matches nothing — so on a host with no top-level manifest a globbed sweep
# takes every later section with it, silently. Run the real inventory script
# under zsh against a checkout that has none and require the closing marker.
empty = Path(tempfile.mkdtemp(prefix="campaign-noglob-"))
try:
    (empty / "experiments").mkdir()
    (empty / ".git").mkdir()
    for shell in ("sh", "zsh"):
        if shutil.which(shell) is None:
            continue
        proc = subprocess.run([shell, "-c", C.inventory_script(str(empty))],
                              capture_output=True, text=True, timeout=60)
        check_true("##END" in proc.stdout,
                   f"the inventory script must reach ##END under {shell} "
                   f"with no manifest present")
        check_true("##MANIFESTS" in proc.stdout
                   and "##SCOREMANIFESTS" in proc.stdout,
                   f"and still emit every section under {shell}")
finally:
    shutil.rmtree(empty, ignore_errors=True)


# ── A timed-out probe keeps what arrived ──────────────────────────────────────

# Sections stream out in order, so a probe cut short still holds every record
# that got its turn. Discarding them flips a finished campaign to "stalled"
# purely because the poll was slow.
slow = "printf '##CAMPAIGN p\\n##CSVROWS 401\\n'; sleep 30"
partial, error = C.run_shell(C.LOCAL, slow, timeout=2)
check_true(C.is_timeout(error), "the slow probe did time out")
check(C.parse_detail(partial or "")["p"]["csv_rows"], 400,
      "and its completed records are still parsed")


# ── Derived state ─────────────────────────────────────────────────────────────

def annotated(rows_done, rows_planned, log_mtime, procs, epoch=1786460000,
              started="2026-08-11T12:53:14+0100"):
    campaign = {"profile": "p", "branch": "b", "started": started,
                "rows_done": rows_done, "rows_planned": rows_planned,
                "log_mtime": log_mtime}
    C.annotate(campaign, {"epoch": epoch, "processes": procs})
    return campaign


done = annotated(400, 400, 1786455673, [])
check(done["state"], "done", "complete and idle reads as done")
check(round(done["pct"]), 100, "percent complete")
check(done["eta_s"], None, "a finished campaign has no ETA")

stalled = annotated(797, 800, 1786455673, [])
check(stalled["state"], "stalled", "incomplete with no live process is stalled")
check(int(stalled["pct"]), 99, "797/800 must not read as 100%")

running = annotated(200, 400, 1786459000,
                    [{"comm": "python3", "profile": "p"}])
check(running["state"], "running", "a runner on this profile means running")
check_true(running["eta_s"] is not None, "a running campaign gets an ETA")
# Half the rows in the elapsed time implies roughly that much again.
check(running["eta_s"], running["elapsed_s"], "ETA extrapolates the row rate")

# Staleness is differenced against the host's own clock, sampled in the same
# call, because av3 runs minutes away from av2 whenever NTP is off.
check(annotated(1, 2, 1786459940, [])["stale_s"], 60,
      "staleness uses the host clock, not this machine's")

other = annotated(1, 2, 1786459940, [{"comm": "python3", "profile": "other"}])
check(other["state"], "stalled", "a runner on another profile is not this one")

# An unattributable process must match no campaign. One unrelated `peredur` on
# av2 otherwise reported all six of its archived campaigns as running, while
# idle av3 reported the same six from the same data as stalled -- the same
# campaign in two states, decided by a process belonging to neither.
engine = annotated(1, 2, 1786459940, [{"comm": "peredur", "profile": None}])
check(engine["state"], "stalled",
      "a process that names no profile must claim no campaign")
check(engine["running"], False, "and must not read as running")

both_hosts = [annotated(1, 2, 1786459940, procs) for procs in
              ([], [{"comm": "peredur", "profile": None}])]
check(both_hosts[0]["state"], both_hosts[1]["state"],
      "an idle host and a host busy on other work must agree about the same "
      "archived campaign")

# A matched process is not evidence on its own. Nine days without a run.log is
# far past any single run's timeout, and "running" beside that is the reading
# that stops someone investigating a dead run.
nine_days = 1786460000 - 9 * 86400
stuck = annotated(1, 2, nine_days, [{"comm": "python3", "profile": "p"}])
check(stuck["state"], "stuck",
      "a live runner over a long-dead log must not read as running")
check(stuck["eta_s"], None, "and must not offer an ETA")
check_true(stuck["running"], "though the process is still recorded as matched")

host = {"host": "av2", "reachable": True, "hostname": "av2", "branch": "b",
        "head": "abc1234", "dirty": False, "hidden": 0,
        "processes": [{"comm": "python3", "profile": "p"}],
        "campaigns": [dict(stuck, profile="p", branch="b",
                           binary_commit="abc1234")]}
check_true(any("producing nothing" in n for n in C.status_notes([host])),
           "and says so distinctly rather than collapsing to stalled")

# The boundary is the harness's own bound on one run's silence, so a campaign
# inside it is still running on the same evidence.
fresh = annotated(1, 2, 1786460000 - C.STALE_RUN_S + 60,
                  [{"comm": "python3", "profile": "p"}])
check(fresh["state"], "running", "a log inside the threshold still corroborates")
just_past = annotated(1, 2, 1786460000 - C.STALE_RUN_S - 60,
                      [{"comm": "python3", "profile": "p"}])
check(just_past["state"], "stuck", "and one outside it does not")

# No log at all is a campaign whose first run directory does not exist yet.
no_log = annotated(0, 400, None, [{"comm": "python3", "profile": "p"}])
check(no_log["state"], "running",
      "a just-launched campaign with no run.log yet is not stuck")

check(C.human_duration(None), "-", "unknown duration")
check(C.human_duration(45), "45s", "seconds")
check(C.human_duration(3900), "1h05m", "hours and minutes")
check(C.human_duration(300000), "3d11h", "days and hours")
check(C.human_bytes(0), "0B", "zero bytes")
check(C.human_bytes(3774873), "3.6MB", "megabytes")


# ── The status table ──────────────────────────────────────────────────────────

unreachable = {"host": "av2", "reachable": False,
               "error": "ssh: connect to host av2 port 22: No route to host",
               "campaigns": [], "processes": []}
rows = C.status_rows([unreachable])
check(len(rows), 1, "an unreachable host costs one row, not the run")
check_true(rows[0][1].startswith("(unreachable: ssh: connect"),
           "and says why")
check_true(len(rows[0][1]) <= 60,
           "a paragraph-long ssh error must not widen the whole column")

timed_out = dict(unreachable, error="timed out after 45s")
check_true(C.status_rows([timed_out])[0][1].startswith("(no answer:"),
           "a deadline is reported as a deadline, not as a dead host")
check(C.is_timeout("timed out after 45s"), True, "timeout recognised")
check(C.is_timeout("ssh: no route to host"), False, "an ssh failure is not one")
check(C.is_timeout(None), False, "no error is not a timeout")

fallback = {"host": "av2", "reachable": True, "hostname": "av2",
            "branch": "b", "head": "abc1234", "dirty": False, "hidden": 0,
            "processes": [],
            "campaigns": [dict(topup, rows_done=401, rows_planned=None,
                               rows_from_csv=True, profile="replicate",
                               branch="b", binary_commit="abc1234",
                               state="stalled", pct=None)]}
check(C.status_rows([fallback])[0][2], "~401/?",
      "a whole-CSV count is marked in the table, never passed off as progress")
check_true(any("whole CSV" in n for n in C.status_notes([fallback])),
           "and explained in a note")

check(C.checkout_rows([unreachable])[0][5], "unreachable",
      "the checkout table degrades too")

live = {"host": "av3", "reachable": True, "hostname": "av3",
        "branch": "feat/x", "head": "abc1234", "dirty": True, "hidden": 0,
        "processes": [{"comm": "peredur", "profile": None},
                      {"comm": "peredur", "profile": None}],
        "campaigns": [dict(done, branch="feat/other", binary_commit="b093374",
                           dirty_binary=True)]}
check(C.checkout_rows([live])[0],
      ["av3", "av3", "feat/x", "abc1234", "dirty", "peredur"],
      "the checkout table names the branch a resumed run would use")
check(C.status_rows([live])[0][7], "feat/other!",
      "a campaign launched on a branch the checkout has left is flagged")
check(C.status_rows([live])[0][8], "b093374*",
      "a campaign launched off a dirty binary is flagged")
check_true(any("dirty" in n for n in C.status_notes([live])),
           "and the flag is explained in a note")


# ── A score phase in the status table ─────────────────────────────────────────
#
# The scoring pass runs for days and must not read as `stalled` while it is
# running, nor as `running` once it has stopped moving. Its manifest sits
# inside its output directory, its progress is a count of curves in that
# directory, and the process that claims it is score_campaign.py naming that
# directory rather than a runner naming a profile.

SCORE_MANIFEST = """{
  "kind": "score",
  "hostname": "av3",
  "started": "2026-08-11T10:00:00+0100",
  "finished": null,
  "results": "experiments/results-rematch",
  "out": "experiments/curves-rematch",
  "seeds": [15, 16],
  "git": {"branch": "campaign/aurus-rematch", "head": "abc1234def"},
  "binaries": {"maximal": {"commit_short": "abc1234", "dirty": "0"},
               "compare": {"commit_short": "abc1234", "dirty": "0"}},
  "allow_stale_binary": false,
  "counts": {"queued": 30, "already_scored": 0, "scored": 0, "failed": 0}
}"""

SCORE_INVENTORY = f"""##PS
zsh -zsh
python3 python3 scripts/score_campaign.py --results experiments/results-rematch --out experiments/curves-rematch --seeds 15 16
maximal build-release/maximal --x
##HOST
av3
1786460000
##GIT
campaign/aurus-rematch
abc1234
0
##MANIFESTS
##SCOREMANIFESTS
##SFILE experiments/curves-rematch/score-manifest-av3.json
{SCORE_MANIFEST}
##ENDSFILE
##END
"""

sinv = C.parse_inventory(SCORE_INVENTORY)
check(len(sinv["score_manifests"]), 1, "a score manifest is parsed")
check(sinv["score_manifests"][0]["file"],
      "experiments/curves-rematch/score-manifest-av3.json",
      "with the path it was read from")
check(sinv["manifests"], [], "and is not mistaken for a run manifest")
check(C.parse_inventory("##SCOREMANIFESTS\n##SFILE x\n{no\n##ENDSFILE\n##END\n")
      ["score_manifests"], [], "an unparseable score manifest is skipped")

sprocs = C.live_processes(sinv["ps"])
check([p["comm"] for p in sprocs], ["python3", "maximal"],
      "the scorer and maximal are live processes")
check((sprocs[0].get("kind"), sprocs[0].get("out"), sprocs[0]["profile"]),
      ("score", "experiments/curves-rematch", None),
      "the scorer is read as a scorer naming its output directory, and no "
      "profile")
check(C.live_processes(["python3 python3 scripts/score_campaign.py --out x "
                        "--dry-run"]), [],
      "a --dry-run scorer is a probe, not a pass")

srec = C.campaigns_from_score_manifests(sinv["score_manifests"])
check(len(srec), 1, "one record per score manifest")
s = srec[0]
check((s["kind"], s["profile"], s["label"], s["out"]),
      ("score", "curves-rematch", "score:curves-rematch",
       "experiments/curves-rematch"),
      "keyed on the output directory's name, labelled as a score phase")
check((s["branch"], s["binary_commit"], s["rows_planned"]),
      ("campaign/aurus-rematch", "abc1234", 30),
      "carrying the branch, maximal's commit and the queued count")
check(s["dirty_binary"], False, "a clean gate is not flagged")
check(C.campaigns_from_score_manifests(
    [dict(json.loads(SCORE_MANIFEST), allow_stale_binary=True)])[0]
    ["dirty_binary"], True, "but an overridden gate is, like a dirty binary")

sdetail = C.detail_script("/r", srec)
check_true("##CAMPAIGN curves-rematch" in sdetail
           and "experiments/curves-rematch -maxdepth 1 -type f -name '*.csv'"
           in sdetail and "OUTMTIME" in sdetail,
           f"the detail probe counts curves and the newest file: {sdetail}")
check_true("run_experiments.py" not in sdetail,
           "and asks the runner for no plan on a score phase")
sparsed = C.parse_detail("##CAMPAIGN curves-rematch\n##CSVS 12\n"
                         "##OUTMTIME 1786459940.5\n##END\n")["curves-rematch"]
check((sparsed["rows_done"], sparsed["log_mtime"]), (12, 1786459940),
      "curves written is the done count, and the newest file the staleness")
check_true("rows_from_csv" not in sparsed,
           "which is not a whole-CSV fallback and is not marked as one")


def score_annotated(csvs, mtime, procs, finished=None, epoch=1786460000):
    record = dict(s, rows_done=csvs, log_mtime=mtime, finished=finished)
    C.annotate(record, {"epoch": epoch, "processes": procs})
    return record


scorer_proc = {"comm": "python3", "profile": None, "kind": "score",
               "out": "experiments/curves-rematch"}
check(score_annotated(12, 1786459940, [scorer_proc])["state"], "running",
      "a scorer naming this output directory means running")
check(score_annotated(12, 1786459940, [scorer_proc, {"comm": "maximal",
                                                      "profile": None}])
      ["state"], "running", "with or without its maximal beside it")
check(score_annotated(12, 1786459940, [dict(scorer_proc, out="/abs/path/to/"
                                             "curves-rematch")])["state"],
      "running", "matched by name, so an absolute --out still claims it")
check(score_annotated(12, 1786459940, [dict(scorer_proc, out="experiments/"
                                             "curves-other")])["state"],
      "stalled", "a scorer on another directory is not this pass")
check(score_annotated(12, 1786459940,
                      [{"comm": "python3", "profile": "curves-rematch"}])
      ["state"], "stalled",
      "and a runner is never a scorer, whatever its profile is called")
check(score_annotated(12, 1786459940, [])["state"], "stalled",
      "no scorer and 12 of 30 is stalled")
check(score_annotated(30, 1786459940, [])["state"], "done",
      "every queued run with a curve is done")
check(score_annotated(12, 1786460000 - C.STALE_RUN_S - 60, [scorer_proc])
      ["state"], "stuck", "a scorer over a directory nothing has touched for "
                           "longer than one run's scoring is stuck")
check_true(score_annotated(12, 1786459940, [scorer_proc])["eta_s"] is not None,
           "and a running pass gets an ETA off its curve rate")

shost = {"host": "av3", "reachable": True, "hostname": "av3",
         "branch": "campaign/aurus-rematch", "head": "abc1234", "dirty": False,
         "hidden": 0, "processes": [scorer_proc],
         "campaigns": [score_annotated(12, 1786459940, [scorer_proc])]}
srow = C.status_rows([shost])[0]
check(srow[1], "score:curves-rematch", "the row is labelled as a score phase")
check(srow[2], "12/30", "curves written against runs queued")
check((srow[3], srow[4], srow[7], srow[8]), ("40%", "running",
                                               "campaign/aurus-rematch",
                                               "abc1234"),
      "with its percentage, state, branch and maximal's commit")
check_true(any("scorer live on --out experiments/curves-rematch" in n
               for n in C.status_notes([shost])),
           "and the scorer is noted as live")
stuck_host = dict(shost, campaigns=[score_annotated(
    12, 1786460000 - C.STALE_RUN_S - 60, [scorer_proc])])
check_true(any("scorer is alive" in n and "producing nothing" in n
               for n in C.status_notes([stuck_host])),
           "a stuck pass is explained in the scorer's own words")
stale_host = dict(shost, campaigns=[dict(score_annotated(
    12, 1786459940, [scorer_proc]), dirty_binary=True)])
check(C.status_rows([stale_host])[0][8], "abc1234*",
      "a pass that overrode the freshness gate is flagged on BINARY")
check(C.status_rows([dict(shost, campaigns=[score_annotated(
    None, None, [scorer_proc])])])[0][2], "?/30",
      "a pass whose output directory is not there yet has no curve count")


# ── status for a compare phase ────────────────────────────────────────────────
#
# compare_pairs.py writes its manifest under one fixed directory, and the
# detail probe counts rows in the output CSV it names; the planned count is the
# pair list's length from the manifest.

COMPARE_MANIFEST = """{
  "kind": "compare",
  "hostname": "av1",
  "started": "2026-10-06T10:00:00+0100",
  "finished": null,
  "pairs": "experiments/pairs-av1.csv",
  "out": "experiments/out-av1.csv",
  "pairs_total": 50,
  "git": {"branch": "campaign/pooled-rq2", "head": "abc1234def"},
  "binaries": {"compare": {"commit_short": "abc1234", "dirty": "0"}}
}"""

COMPARE_INVENTORY = f"""##PS
python3 python3 scripts/compare_pairs.py experiments/pairs-av1.csv experiments/out-av1.csv --jobs 28 --manifest experiments/compare-manifests/rq2.subsumption.json
##HOST
av1
1786460000
##GIT
campaign/pooled-rq2
abc1234
0
##MANIFESTS
##SCOREMANIFESTS
##COMPAREMANIFESTS
##CFILE experiments/compare-manifests/rq2.subsumption.json
{COMPARE_MANIFEST}
##ENDCFILE
##END
"""

check_true("experiments/compare-manifests -maxdepth 1" in
           C.inventory_script("/r") and "*.json" in C.inventory_script("/r"),
           "the inventory sweeps the compare manifest directory with find")
cinv = C.parse_inventory(COMPARE_INVENTORY)
check(len(cinv["compare_manifests"]), 1, "a compare manifest is parsed")
check((cinv["manifests"], cinv["score_manifests"]), ([], []),
      "and is mistaken for neither other kind")
cprocs = C.live_processes(cinv["ps"])
check((cprocs[0].get("kind"), cprocs[0].get("manifest")),
      ("compare", "experiments/compare-manifests/rq2.subsumption.json"),
      "compare_pairs.py is read as a comparer naming its manifest")
crec = C.campaigns_from_compare_manifests(cinv["compare_manifests"])
cr = crec[0]
check((cr["kind"], cr["label"], cr["out"], cr["rows_planned"],
       cr["binary_commit"]),
      ("compare", "compare:rq2.subsumption", "experiments/out-av1.csv", 50,
       "abc1234"),
      "one record per manifest, planned from the pair list's length")
cdetail = C.detail_script("/r", crec)
check_true("##CAMPAIGN compare:rq2.subsumption" in cdetail
           and "wc -l < experiments/out-av1.csv" in cdetail
           and "run_experiments.py" not in cdetail,
           f"the detail probe counts the output's rows: {cdetail}")
cparsed = C.parse_detail("##CAMPAIGN compare:rq2.subsumption\n##CSVROWS 21\n"
                         "##OUTMTIME 1786459940\n##END\n")[
    "compare:rq2.subsumption"]
check((cparsed["rows_done"], cparsed.get("rows_from_csv")), (20, None),
      "the header is not a row, and the count is not marked as a fallback")
cann = dict(cr, **cparsed)
C.annotate(cann, {"epoch": 1786460000, "processes": cprocs})
check(cann["state"], "running", "the comparer naming the manifest claims it")
crow = C.status_rows([{"host": "av1", "reachable": True,
                       "branch": "campaign/pooled-rq2", "processes": cprocs,
                       "campaigns": [cann]}])[0]
check(crow[1:5], ["compare:rq2.subsumption", "20/50", "40%", "running"],
      "and status reports rows done against pairs planned")
cdone = dict(cr, rows_done=50, log_mtime=1786459940)
C.annotate(cdone, {"epoch": 1786460000, "processes": []})
check(cdone["state"], "done", "every pair with a row is done")
check_true(any(kind == "busy" for kind, _ in C.stage_refusals(
    C.HostProbe(branch="b", head="h", binary={"commit": "h", "dirty": "0"},
                processes=cprocs), "b")),
           "and a live comparer blocks a stage")

# A pool pass reports as the other unseeded kinds do: subjects finished, from
# its progress CSV, against the subject list's length in its manifest.
pprocs = C.live_processes([
    "python3 python3 scripts/pool_members.py d.toml experiments/pm "
    "--subjects-file s-av2.txt --manifest experiments/compare-manifests/"
    "pooled.pool.json"])
check((pprocs[0].get("kind"), pprocs[0].get("manifest")),
      ("pool", "experiments/compare-manifests/pooled.pool.json"),
      "pool_members.py is read as a pool pass naming its manifest")
prec = C.campaigns_from_compare_manifests([{
    "kind": "pool", "file": "experiments/compare-manifests/pooled.pool.json",
    "hostname": "av2", "started": "2026-11-09T10:00:00+0000",
    "out": "experiments/pm/done-s-av2.csv", "planned": 9,
    "git": {"branch": "b", "head": "abc1234def"}}])[0]
check((prec["kind"], prec["label"], prec["out"], prec["rows_planned"]),
      ("pool", "pool:pooled.pool", "experiments/pm/done-s-av2.csv", 9),
      "a pool manifest is one record, planned from its subject count")
pann = dict(prec, rows_done=4, log_mtime=1786459940)
C.annotate(pann, {"epoch": 1786460000, "processes": pprocs})
check(pann["state"], "running", "and the pool pass naming it claims it")


# ── collect: merge and verification against local fixtures ────────────────────

CSV_HEADER = ["sweep", "level_name", "selection", "weakening", "metric",
              "repair_mode", "spec", "seed", "commit", "dirty", "wall_time_s"]


def write_csv(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_HEADER)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def row(seed, spec="amba", selection="nsga2-truncate", commit="aaaaaaa"):
    return {"sweep": "R", "level_name": "elit0.1", "selection": selection,
            "weakening": "wkon", "metric": "direct", "repair_mode": "mono",
            "spec": spec, "seed": seed, "commit": commit, "dirty": "0",
            "wall_time_s": "1.0"}


def make_checkout(root: Path, rows: list[dict], seeds: list[int]) -> None:
    write_csv(root / "experiments" / "results-fixture.csv", rows)
    for seed in seeds:
        run = root / "experiments" / "results-fixture" / f"run_seed{seed:02d}"
        run.mkdir(parents=True, exist_ok=True)
        (run / "run.log").write_text(f"seed {seed}\n")


def collect_args(**kwargs) -> argparse.Namespace:
    base = {"profile": "fixture", "host": None, "dry_run": False,
            "no_results": False, "csv": "results-fixture.csv",
            "results_dir": "results-fixture"}
    base.update(kwargs)
    return argparse.Namespace(**base)


tmp = Path(tempfile.mkdtemp(prefix="campaign-test-"))
real_merge_root = merge.REPO_ROOT
try:
    hosts = {"h1": tmp / "h1", "h2": tmp / "h2"}
    make_checkout(hosts["h1"], [row(s) for s in (0, 1)], [0, 1])
    make_checkout(hosts["h2"], [row(s) for s in (2, 3)], [2, 3])
    dest = tmp / "dest"
    (dest / "experiments").mkdir(parents=True)

    C.REPO_ROOT = dest
    C.HOSTS = ("h1", "h2")
    C.source_root = lambda host: str(hosts[host])
    # pull_source resolves the per-run destination against merge's own repo
    # root, which is the real checkout. Both have to point at the fixture or
    # the test writes result trees into the working tree.
    merge.REPO_ROOT = dest

    merged = dest / "experiments" / "results-fixture.csv"
    check(C.cmd_collect(collect_args()), 0, "a clean two-host collect verifies")
    check(len(merge.read_rows(merged)[1]), 4, "both halves land in the merge")
    check_true((dest / "experiments" / "results-fixture" /
                "run_seed03" / "run.log").exists(),
               "the per-run trees come across too")

    # Idempotent: re-collecting an already merged campaign overlaps on every
    # key, which the union arithmetic must read as "already here" rather than
    # as a duplicate.
    check(C.cmd_collect(collect_args()), 0, "re-collecting still verifies")
    check(len(merge.read_rows(merged)[1]), 4, "and adds no rows")

    # A host whose CSV records the same runs under a different binary must not
    # split the key: `commit` is not a key field, and a collect that made it
    # one would double every row.
    check_true("commit" not in merge.KEY_FIELDS,
               "commit must never join the natural key")
    write_csv(hosts["h2"] / "experiments" / "results-fixture.csv",
              [row(s, commit="bbbbbbb") for s in (2, 3)])
    check(C.cmd_collect(collect_args()), 0, "a rebuilt binary re-collects clean")
    check(len(merge.read_rows(merged)[1]), 4, "and still yields four rows")

    # The old selection spelling names the same cell, so it must join rather
    # than merge into a second row.
    write_csv(hosts["h2"] / "experiments" / "results-fixture.csv",
              [row(s, selection="nsga2") for s in (2, 3)])
    check(C.cmd_collect(collect_args()), 0, "a retired scheme name re-collects")
    check(len(merge.read_rows(merged)[1]), 4, "and does not split the key")

    # A host asked for that contributes nothing is the silent-loss case the
    # union check cannot see: computed over the survivors alone it agrees with
    # itself perfectly and would print OK over half a campaign. Both ways a
    # host can drop out are covered -- a transfer that raises, and one that
    # returns no CSV.
    for label, sabotage in (
        ("rsync fails", lambda: shutil.rmtree(hosts["h2"])),
        ("no CSV", lambda: (hosts["h2"] / "experiments"
                            / "results-fixture.csv").unlink()),
    ):
        make_checkout(hosts["h2"], [row(s) for s in (2, 3)], [2, 3])
        sabotage()
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = C.cmd_collect(collect_args())
        printed = buffer.getvalue()
        check(code, 1, f"a collect losing a host must exit non-zero ({label})")
        check_true("OK: every source row" not in printed,
                   f"and must not print the OK line ({label})")
        check_true("INCOMPLETE" in printed and "h2" in printed,
                   f"and must name the host it lost ({label})")

    # What did arrive is still merged, because the merge is keyed and
    # idempotent: a later run against the recovered host completes the file.
    check(len(merge.read_rows(merged)[1]), 4, "the surviving rows are kept")

    make_checkout(hosts["h2"], [row(s) for s in (2, 3)], [2, 3])
    check(C.cmd_collect(collect_args()), 0,
          "and the recovered host collects clean afterwards")

    # Verification has to be able to fail: a merged file holding one key twice
    # is exactly what a broken merge produces, and it must say so.
    doubled = dest / "experiments" / "doubled.csv"
    write_csv(doubled, [row(0), row(0)])
    check(C.verify_merge(doubled, set(), {}), False,
          "a duplicated key must fail verification")

    lossy = dest / "experiments" / "lossy.csv"
    write_csv(lossy, [row(0)])
    check(C.verify_merge(lossy, {merge.key_of(row(9))}, {}), False,
          "a row present before the merge and absent after must fail")

    # ── collect --curves ──────────────────────────────────────────────────────
    #
    # A score phase's output is one CSV per run under <out>/ on each host,
    # beside the timings, failures and warnings files and the host's score
    # manifest. Each host's directory comes across whole, and the per-run
    # CSVs are joined into one file by run name: disjoint seeds give disjoint
    # run names, so a run on both hosts is a split violation, and a host that
    # answers nothing is the same silent loss the results merge guards.
    CURVE_HEADER = "spec,seed,metric,elapsed_s,value,censored"

    def curve(host: str, run: str, rows: int = 2, value: int = 1) -> Path:
        path = hosts[host] / "experiments" / "curves-fixture" / f"{run}.csv"
        path.parent.mkdir(parents=True, exist_ok=True)
        spec, seed = run.rsplit("_seed", 1)
        path.write_text(CURVE_HEADER + "\n" + "".join(
            f"{spec},{seed},solutions,{i * 10},{value},0\n"
            for i in range(rows)))
        return path

    def side_files(host: str, failed: int = 0) -> None:
        out = hosts[host] / "experiments" / "curves-fixture"
        (out / "timings.txt").write_text("3 10 0 amba_seed0 900/600/4500\n")
        (out / "failures.txt").write_text("3 x/fail_seed0\n" * failed)
        (out / "warnings.log").write_text("scored amba_seed0\n")
        (out / f"score-manifest-{host}.json").write_text(
            json.dumps({"kind": "score", "counts": {"queued": 2}}))

    for run in ("amba_seed0", "amba_seed1"):
        curve("h1", run)
    for run in ("amba_seed2", "amba_seed3"):
        curve("h2", run, rows=3)
    side_files("h1")
    side_files("h2", failed=1)
    merged_curves = dest / "experiments" / "curves-fixture.csv"

    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_collect(collect_args(curves="curves-fixture",
                                          dry_run=True))
    check(code, 0, f"a curves dry run verifies: {buffer.getvalue()}")
    check_true(not merged_curves.exists()
               and not (dest / "experiments" / "curves-fixture").exists(),
               "and transfers nothing and writes nothing")
    check_true("WOULD TRANSFER" in buffer.getvalue(),
               "while reporting what it would move")

    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_collect(collect_args(curves="curves-fixture"))
    printed = buffer.getvalue()
    check(code, 0, f"a clean two-host curves collect verifies: {printed}")
    check(C.count_csv_rows(merged_curves), 10,
          "every per-run row lands in the joined file, header once")
    check(merged_curves.read_text().count(CURVE_HEADER), 1,
          "with the header written exactly once")
    check(merged_curves.read_text().splitlines()[1].split(","),
          ["amba", "0", "solutions", "0", "1", "0"],
          "and the runs in name order")
    for host in ("h1", "h2"):
        pulled = dest / "experiments" / "curves-fixture" / host
        check(sorted(p.name for p in pulled.iterdir()),
              sorted([f"amba_seed{s}.csv" for s in
                      ((0, 1) if host == "h1" else (2, 3))]
                     + ["timings.txt", "failures.txt", "warnings.log",
                        f"score-manifest-{host}.json"]),
              f"{host}'s whole directory is kept under its own name")
    check_true("OK: every curve is present exactly once" in printed,
               "and the verification passes")
    check_true("h2: 2 curves, 1 failed attempt(s)" in printed,
               "noting each host's failures.txt")

    check(C.cmd_collect(collect_args(curves="curves-fixture")), 0,
          "re-collecting still verifies")
    check(C.count_csv_rows(merged_curves), 10, "and adds no rows")

    # A run scored on both hosts is a seed split violation. Byte-identical
    # copies are one curve and pass; differing ones are reported.
    curve("h2", "amba_seed1")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_collect(collect_args(curves="curves-fixture"))
    check(code, 0, "an identical curve on both hosts is one curve")
    check_true("overlap h1/h2: 1 run(s)" in buffer.getvalue(),
               "and the overlap is reported")
    check(C.count_csv_rows(merged_curves), 10, "written once")
    # A different size as well as a different value: rsync's quick check is
    # size and mtime, and the two writes land inside one second.
    curve("h2", "amba_seed1", rows=3, value=7)
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_collect(collect_args(curves="curves-fixture"))
    check(code, 1, "two differing curves for one run fail the collect")
    check_true("MISMATCH" in buffer.getvalue()
               and "amba_seed1" in buffer.getvalue(),
               "naming the run")
    check(C.count_csv_rows(merged_curves), 10,
          "without touching the merged file")
    (hosts["h2"] / "experiments" / "curves-fixture" / "amba_seed1.csv").unlink()
    (dest / "experiments" / "curves-fixture" / "h2" / "amba_seed1.csv").unlink()

    # A curve from another vintage of score_curves.py carries a different
    # header and must not be concatenated under this one's columns.
    odd = curve("h2", "amba_seed3")
    odd.write_text("spec,seed,other\namba,3,1\n")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_collect(collect_args(curves="curves-fixture"))
    check(code, 1, "a curve with another header fails the collect")
    check_true("2 different headers" in buffer.getvalue()
               and "spec,seed,other" in buffer.getvalue()
               and "amba_seed3" in buffer.getvalue(),
               f"naming both headers and a curve carrying the odd one: "
               f"{buffer.getvalue()!r}")
    check(C.count_csv_rows(merged_curves), 10,
          "and the merged file from the last good collect is left standing")
    curve("h2", "amba_seed3", rows=3)

    # A host that contributes nothing is INCOMPLETE, whether the transfer
    # failed or the directory holds no curve.
    for label, sabotage in (
        ("rsync fails", lambda: shutil.rmtree(
            hosts["h2"] / "experiments" / "curves-fixture")),
        ("no curves", lambda: [p.unlink() for p in (
            hosts["h2"] / "experiments" / "curves-fixture").glob("*.csv")]),
    ):
        shutil.rmtree(dest / "experiments" / "curves-fixture" / "h2",
                      ignore_errors=True)
        sabotage()
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = C.cmd_collect(collect_args(curves="curves-fixture"))
        check(code, 1, f"a curves collect losing a host exits non-zero "
                       f"({label})")
        check_true("INCOMPLETE" in buffer.getvalue()
                   and "h2" in buffer.getvalue(),
                   f"and names the host it lost ({label})")
        check_true("OK: every curve" not in buffer.getvalue(),
                   f"and does not print the OK line ({label})")
        for run in ("amba_seed2", "amba_seed3"):
            curve("h2", run, rows=3)
        side_files("h2")
    check(C.count_csv_rows(merged_curves), 4,
          "what did arrive was still joined")
    check(C.cmd_collect(collect_args(curves="curves-fixture")), 0,
          "and the recovered host collects clean afterwards")
    check(C.count_csv_rows(merged_curves), 10, "restoring every row")

    # ── collect --outputs ─────────────────────────────────────────────────────
    #
    # A fingerprint pass and the compare pass after it leave a tree, not one
    # CSV per run: each host's directory comes across whole and nothing is
    # joined. A host with nothing there is INCOMPLETE.
    for h in ("h1", "h2"):
        tree = hosts[h] / "experiments" / "analysis-fixture" / "fp" / f"s-{h}"
        tree.mkdir(parents=True)
        (tree / "fp.tsv").write_text("x\t0f\n")
        (hosts[h] / "experiments" / "analysis-fixture"
         / f"subsumption-{h}.csv").write_text("id,relation\n")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_collect(collect_args(outputs="analysis-fixture"))
    check(code, 0, f"an outputs collect from two hosts exits 0: "
                   f"{buffer.getvalue()}")
    got_out = dest / "experiments" / "analysis-fixture"
    check_true((got_out / "h1" / "fp" / "s-h1" / "fp.tsv").is_file()
               and (got_out / "h2" / "subsumption-h2.csv").is_file(),
               "each host's tree lands under <out>/<host>/")
    check_true(not (dest / "experiments" / "analysis-fixture.csv").exists(),
               "and nothing is joined")
    shutil.rmtree(hosts["h2"] / "experiments" / "analysis-fixture")
    shutil.rmtree(got_out / "h2")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_collect(collect_args(outputs="analysis-fixture"))
    check((code, "INCOMPLETE" in buffer.getvalue() and "h2" in buffer.getvalue()),
          (1, True), "a host with no outputs makes the collect INCOMPLETE")
finally:
    merge.REPO_ROOT = real_merge_root
    shutil.rmtree(tmp, ignore_errors=True)

# ── TOML ──────────────────────────────────────────────────────────────────────

importlib.reload(C)

DECLARATION = """
# The campaign this file declares.
name = "fretish-order-pairs"
branch = "feat/fretish-order-pairs"
profile = "fretish-order-pairs"
hosts = { av2 = "0-99", av3 = "100-199" }
phases = [ { profile = "fretish-order-pairs", jobs = 4 } ]
"""

HEADER_FORM = """
name = "two-phase"
branch = "feat/two-phase"
hosts = { av2 = "0-9" }

[[phases]]
name = "warm"
profile = "aurus-h2h"
jobs = 2
specs = ["amba", "lily02"]

[[phases]]
profile = "aurus-h2h"
jobs = 4
"""

check(C.parse_toml(DECLARATION)["hosts"], {"av2": "0-99", "av3": "100-199"},
      "an inline table of host seed ranges")
check(C.parse_toml(HEADER_FORM)["phases"][0]["specs"], ["amba", "lily02"],
      "[[phases]] headers are the same thing as the inline array")

roundtrip = {"campaign": "x", "seeds": "0-99", "phase": 2, "state": "failed",
             "log": ['said "no"', "line\ttwo"], "flag": False}
check(C.parse_toml(C.dump_toml(roundtrip)), roundtrip,
      "an entry survives being written and read back")

for bad, why in (
    ('a = { b = 1,\n c = 2 }', "a newline inside an inline table"),
    ('a = 1\na = 2', "a duplicate key"),
    ('a = "unterminated', "an unterminated string"),
    ("[t]\n[t]", "a duplicate table"),
):
    try:
        C.parse_toml(bad)
        check_true(False, f"{why} must be rejected")
    except C.TomlError:
        pass


# ── The campaign declaration ──────────────────────────────────────────────────

def write_declaration(root: Path, name: str, text: str) -> Path:
    path = root / "experiments" / name / "campaign.toml"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def declaration_error(root: Path, name: str, text: str) -> str:
    write_declaration(root, name, text)
    try:
        C.load_campaign(name, root)
    except C.CampaignError as exc:
        return str(exc)
    return ""


decl_root = Path(tempfile.mkdtemp(prefix="campaign-decl-"))
try:
    # The same shape as DECLARATION above, on a profile this checkout defines:
    # fretish-order-pairs's own profile lives on its branch, which is the normal case
    # and is why load_campaign checks against the runner rather than a list.
    write_declaration(decl_root, "probe", DECLARATION
                      .replace("fretish-order-pairs", "probe")
                      .replace('profile = "probe"', 'profile = "aurus-h2h"'))
    campaign = C.load_campaign("probe", decl_root)
    check(campaign["branch"], "feat/probe", "branch from the file")
    check(campaign["hosts"]["av2"], list(range(100)), "a range expands")
    check(len(campaign["hosts"]["av3"]), 100, "and so does the other host's")
    check(campaign["phases"][0]["profile"], "aurus-h2h", "the phase profile")
    check(campaign["build"], C.DEFAULT_BUILD_CMD, "the build command defaults")

    # `configs` is optional and has no default: there is no command that is
    # right for every campaign, and gen_configs.py run with the wrong flags is
    # worse than not run at all. The check that follows it is not optional,
    # which is the half that covers the declarations written before the key.
    check(campaign["configs"], None, "and configs defaults to no command")
    check(campaign["config_dirs"],
          [C.profile_configs_dir("aurus-h2h")],
          "with the phase's own configs directory derived for the check")
    check_true(not Path(campaign["config_dirs"][0]).is_absolute(),
               "relative, since the absolute form names this machine's home "
               "directory and the check runs on somebody else's")

    write_declaration(decl_root, "gen", DECLARATION
                      .replace("fretish-order-pairs", "gen")
                      .replace('profile = "gen"', 'profile = "aurus-h2h"')
                      + '\nconfigs = "python3 scripts/gen_configs.py --x"\n')
    check(C.load_campaign("gen", decl_root)["configs"],
          "python3 scripts/gen_configs.py --x",
          "a declared configs command is carried through")

    for bad, why in (('configs = ""', "an empty configs command"),
                     ("configs = 3", "a configs key that is not a string"),
                     ("configs = true", "a configs key that is a flag")):
        got = declaration_error(decl_root, "badconfigs", f"""
name = "badconfigs"
branch = "feat/x"
hosts = {{ av2 = "0-1" }}
phases = [ {{ profile = "fretish-order-pairs", jobs = 1 }} ]
{bad}
""")
        check_true("configs must be a non-empty string" in got,
                   f"{why} must be refused ({got!r})")

    # ── confine: a CPU set and a memory cap on a run phase ───────────────────
    #
    # A campaign sharing a host with other users' jobs. The unconfined launch
    # must stay byte for byte what it was, since every archived campaign's
    # queue log records it.
    def confined(confine: str, phase_extra: str = "") -> dict:
        write_declaration(decl_root, "confined", f"""
name = "confined"
branch = "feat/x"
hosts = {{ av2 = "0-1", av3 = "2-3" }}
{confine}
phases = [ {{ profile = "fretish-order-pairs", jobs = 2 {phase_extra}}} ]
""")
        return C.load_campaign("confined", decl_root)["phases"][0]

    base_cmd = C.phase_command(confined(""), [0, 1], "av2")
    plain = confined("")
    check(C.confine_prefix(plain, "av2", "u"), [],
          "no confine key gives no prefix")
    check(C.phase_command(plain, [0, 1], "av2",
                          prefix=C.confine_prefix(plain, "av2", "u")),
          base_cmd, "and the launch command is unchanged")
    check(base_cmd, " ".join([C.RUNNER_CMD, "--profile", "fretish-order-pairs",
                              "--jobs", "2", "--seeds", "0", "1"]),
          "which is the runner and its arguments alone")
    unit = C.confine_unit({"file": "007-confined.toml"}, 1)
    check(unit, "peredur-007-confined-p1", "the scope is named by entry and phase")
    only_cpus = confined('confine = { av2 = { cpus = "0-15" } }')
    check(C.confine_prefix(only_cpus, "av2", unit), ["taskset", "-c", "0-15"],
          "cpus alone is taskset alone, with no scope")
    check(C.confine_prefix(only_cpus, "av3", unit), [],
          "and confines only the host it names")
    only_mem = confined('confine = { av2 = { memory_max = "24G" } }')
    check(C.confine_prefix(only_mem, "av2", unit),
          ["systemd-run", "--user", "--scope", "-q", "--collect", "--unit",
           unit, "-p", "MemoryMax=24G", "-p", "MemorySwapMax=0"],
          "memory_max alone is a scope with no swap, and no taskset")
    both = confined('confine = { av2 = { cpus = "0-15", memory_max = "24G" } }')
    check(C.phase_command(both, [0, 1], "av2",
                          prefix=C.confine_prefix(both, "av2", unit)),
          "systemd-run --user --scope -q --collect --unit "
          "peredur-007-confined-p1 -p MemoryMax=24G -p MemorySwapMax=0 "
          "taskset -c 0-15 " + base_cmd,
          "both keys wrap the scope around taskset around the runner")
    over = confined('confine = { av2 = { cpus = "0-15", memory_max = "24G" } }',
                    ', confine = { av2 = { cpus = "0-7" }, av3 = {} }')
    check((C.confine_prefix(over, "av2", unit), C.confine_prefix(over, "av3", unit)),
          (["taskset", "-c", "0-7"], []),
          "a phase's table replaces the campaign's entry for each host it names")
    check(C.confine_prefix(confined('confine = { av2 = { cpus = "0-3,8,10-11" } }'),
                           "av2", unit)[-1], "0-3,8,10-11",
          "a comma-separated CPU list is passed through as written")
    check(C.confine_prefix(confined('confine = { av2 = { memory_max = "50%" } }'),
                           "av2", unit)[-3], "MemoryMax=50%",
          "a percentage cap is accepted")

    for bad, want in (
            ('{ av2 = { cpus = "0-15:2" } }', "not a CPU or a LOW-HIGH range"),
            ('{ av2 = { cpus = "0 - 15" } }', "not a CPU or a LOW-HIGH range"),
            ('{ av2 = { cpus = "" } }', "cpus must be a non-empty string"),
            ('{ av2 = { cpus = 15 } }', "cpus must be a non-empty string"),
            ('{ av2 = { cpus = "15-0" } }', "counts backwards"),
            ('{ av2 = { cpus = "0-7,4-9" } }', "names a CPU twice"),
            ('{ av2 = { memory_max = "24GB" } }', "memory_max must be a string"),
            ('{ av2 = { memory_max = "1.5G" } }', "memory_max must be a string"),
            ('{ av2 = { memory_max = 24 } }', "memory_max must be a string"),
            ('{ av2 = { memory_max = "0G" } }', "is zero"),
            ('{ av2 = { memory_max = "150%" } }', "not a percentage"),
            ('{ av2 = { allowed_cpus = "0-15" } }', "unknown key(s) allowed_cpus"),
            ('{ av1 = { cpus = "0-15" } }', "not declared in the campaign's hosts"),
            ('{ av2 = "0-15" }', "must be a table with"),
            ('"0-15"', "must be a table keyed by host")):
        got = declaration_error(decl_root, "confined", f"""
name = "confined"
branch = "feat/x"
hosts = {{ av2 = "0-1" }}
confine = {bad}
phases = [ {{ profile = "fretish-order-pairs", jobs = 1 }} ]
""")
        check_true(want in got, f"confine = {bad} must be refused with "
                                f"{want!r} ({got!r})")
    got = declaration_error(decl_root, "confined", """
name = "confined"
branch = "feat/x"
hosts = { av2 = "0-1" }
phases = [ { profile = "fretish-order-pairs", jobs = 1 },
           { kind = "score", profile = "fretish-order-pairs", confine = { av2 = { cpus = "0-3" } } } ]
""")
    check_true("unknown key(s) confine on a score phase" in got,
               f"a score phase, which pins its own workers, refuses confine "
               f"({got!r})")

    # Every profile the phases name, not only the campaign-level default: the
    # second phase's configs directory is the one nothing else would check.
    write_declaration(decl_root, "twoprofiles", """
name = "twoprofiles"
branch = "feat/x"
hosts = { av2 = "0-1" }
phases = [ { profile = "fretish-order-pairs", jobs = 1 }, { profile = "aurus-h2h", jobs = 1 } ]
""")
    two = C.load_campaign("twoprofiles", decl_root)
    check(two["config_dirs"],
          sorted({C.profile_configs_dir("fretish-order-pairs"), C.profile_configs_dir("aurus-h2h")}),
          "a campaign straddling two profiles checks both configs directories")

    # The error worth the most noise. Overlapping ranges cost twice the machine
    # time and yield one row per key, so the campaign is smaller and slower
    # than it says while every table reads normal.
    overlap = declaration_error(decl_root, "overlap", """
name = "overlap"
branch = "feat/x"
hosts = { av2 = "0-99", av3 = "90-199" }
phases = [ { profile = "fretish-order-pairs", jobs = 1 } ]
""")
    check_true("share 10 seed(s)" in overlap and "av2" in overlap
               and "av3" in overlap,
               f"overlapping seed ranges must be refused by name: {overlap!r}")
    check_true("90" in overlap, "and must name a shared seed")

    for text, expect_in, why in (
        ('hosts = { av2 = "0-" }', "not a seed", "an open-ended range"),
        ('hosts = { av2 = "9-0" }', "backwards", "a range counting backwards"),
        ('hosts = { av2 = "amba" }', "not a seed", "a non-numeric range"),
        ('hosts = { av2 = "" }', "declare no seeds",
         "an empty range under a seeded phase"),
        ('hosts = { av2 = "0-2,1-3" }', "repeats", "a range repeating itself"),
        ('hosts = { nowhere = "0-1" }', "unknown host", "an unknown host"),
    ):
        got = declaration_error(decl_root, "malformed", f"""
name = "malformed"
branch = "feat/x"
{text}
phases = [ {{ profile = "fretish-order-pairs", jobs = 1 }} ]
""")
        check_true(expect_in in got, f"{why} must be refused ({got!r})")

    unknown = declaration_error(decl_root, "unknown-profile", """
name = "unknown-profile"
branch = "feat/x"
hosts = { av2 = "0-1" }
phases = [ { profile = "no-such-profile", jobs = 1 } ]
""")
    check_true("defines no profile" in unknown,
               "a profile the runner does not define must be refused")
    check_true("aurus-h2h" in unknown, "and the known ones listed")

    # campaign.toml names a profile; it never redefines one. Eighteen archived
    # campaigns vendor their own copy of run_experiments.py, so a second copy
    # of that table here would be the nineteenth to keep in step.
    check(C.known_profiles(), set(__import__("run_experiments").PROFILES),
          "the profile list comes from the runner, not from a copy")

    for text, expect_in, why in (
        ('name = "wrong"\nbranch = "b"\nhosts = { av2 = "0" }\n'
         'phases = [ { profile = "fretish-order-pairs" } ]', "directory is",
         "a name that disagrees with its directory"),
        ('name = "decl"\nbranch = "b"\nhosts = { av2 = "0" }\n'
         'phases = []', "non-empty array", "no phases"),
        ('name = "decl"\nbranch = "b"\nhosts = { }\n'
         'phases = [ { profile = "fretish-order-pairs" } ]', "non-empty table", "no hosts"),
        ('name = "decl"\nbranch = "b"\nseeds = "0-9"\nhosts = { av2 = "0" }\n'
         'phases = [ { profile = "fretish-order-pairs" } ]', "unknown key",
         "a key nobody reads"),
        ('name = "decl"\nbranch = "b"\nhosts = { av2 = "0" }\n'
         'phases = [ { profile = "fretish-order-pairs", jobs = 0 } ]', "positive integer",
         "a job count of zero"),
    ):
        got = declaration_error(decl_root, "decl", text)
        check_true(expect_in in got, f"{why} must be refused ({got!r})")

    check_true("name = None" in declaration_error(decl_root, "absent", ""),
               "an empty file is not a valid declaration")
    missing = ""
    try:
        C.load_campaign("never-declared", decl_root)
    except C.CampaignError as exc:
        missing = str(exc)
    check_true("no campaign declaration" in missing,
               "and a campaign with no declaration at all names the path")

    # ── a compare phase ───────────────────────────────────────────────────────
    #
    # Not seed-split: each host runs its own pair list, named by a path with
    # {host} in it. The campaign-level table may give a host "" for that, and
    # av1 is a host like the other two.
    write_declaration(decl_root, "pairs", """
name = "pairs"
branch = "campaign/pooled-rq2"
hosts = { av1 = "", av2 = "", av3 = "" }

[[phases]]
name = "subsumption"
kind = "compare"
pairs = "experiments/pairs/pairs-{host}.csv"
out = "/abs/out-{host}.csv"
jobs = 28
""")
    pairs = C.load_campaign("pairs", decl_root)
    check(pairs["hosts"], {"av1": [], "av2": [], "av3": []},
          "av1 is accepted, and \"\" declares a host with no seeds")
    cmp_phase = pairs["phases"][0]
    check((cmp_phase["kind"], cmp_phase["name"], cmp_phase["jobs"],
           cmp_phase["black_timeout"], cmp_phase["wall_timeout"],
           cmp_phase["vmem_kb"], cmp_phase["hosts"]),
          ("compare", "subsumption", 28, 300, 700, 8000000, None),
          "a compare phase parses, its budgets defaulting to compare_pairs.py's")
    check(pairs["config_dirs"], [], "and reads no configs directory")
    check(pairs["results_dirs"], {"av1": [], "av2": [], "av3": []},
          "nor any results directory")
    check(C.phase_args(cmp_phase, [], "av1"),
          ["experiments/pairs/pairs-av1.csv", "/abs/out-av1.csv",
           "--jobs", "28", "--black-timeout", "300", "--wall-timeout", "700",
           "--vmem-kb", "8000000",
           "--manifest", "experiments/compare-manifests/pairs.subsumption.json"],
          "{host} is expanded per host, and every budget is stated")
    check(C.phase_command(cmp_phase, [], "av3"),
          C.COMPARE_PAIRS_CMD + " experiments/pairs/pairs-av3.csv "
          "/abs/out-av3.csv --jobs 28 --black-timeout 300 --wall-timeout 700 "
          "--vmem-kb 8000000 --manifest "
          "experiments/compare-manifests/pairs.subsumption.json",
          "and its command is compare_pairs.py's")
    check(C.phase_seeds(cmp_phase, "av1", [0, 1]), [],
          "a compare phase takes no seeds")
    check_true(all(C.phase_runs_on(cmp_phase, h, []) for h in C.HOSTS),
               "and runs on every declared host when it narrows none")
    av1_entry = C.new_entry(pairs, "av1", 3, "a" * 40)
    check((av1_entry["seeds"], av1_entry["phase_seeds"]), ("", [""]),
          "an entry for it freezes no seeds")

    write_declaration(decl_root, "narrowpairs", """
name = "narrowpairs"
branch = "b"
hosts = { av1 = "", av2 = "0-9" }
phases = [ { kind = "compare", pairs = "p.csv", out = "o.csv", hosts = ["av1"] },
           { profile = "fretish-order-pairs", jobs = 1, hosts = { av2 = "0-9" } } ]
""")
    narrow = C.load_campaign("narrowpairs", decl_root)
    check((C.phase_runs_on(narrow["phases"][0], "av1", []),
           C.phase_runs_on(narrow["phases"][0], "av2", list(range(10)))),
          (True, False), "a compare phase's hosts array narrows the host list")
    check(narrow["phases"][0]["name"], "compare", "and the name defaults")

    for body, expect_in, why in (
        ('pairs = "p.csv", out = "o.csv", bogus = 1', "unknown key(s) bogus",
         "an unknown key"),
        ('pairs = "p.csv", out = "o.csv", profile = "full"',
         "unknown key(s) profile", "a profile"),
        ('pairs = "p.csv", out = "o.csv", sweeps = ["A"], specs = ["x"]',
         "unknown key(s) specs, sweeps", "selection keys"),
        ('out = "o.csv"', "needs pairs", "a missing pair list"),
        ('pairs = "p.csv"', "needs out", "a missing output"),
        ('pairs = "p-{seed}.csv", out = "o.csv"', "only {host}",
         "a placeholder other than {host}"),
        ('pairs = "p.csv", out = "o.csv", jobs = 0', "positive integer",
         "a job count of zero"),
        ('pairs = "p.csv", out = "o.csv", wall_timeout = true',
         "positive integer", "a flag for a budget"),
        ('pairs = "p.csv", out = "o.csv", hosts = { av1 = "0-1" }',
         "array of host names", "a seed table for hosts"),
        ('pairs = "p.csv", out = "o.csv", hosts = ["av3"]',
         "not declared at the campaign level", "a host the campaign lacks"),
    ):
        got = declaration_error(decl_root, "badpairs", f"""
name = "badpairs"
branch = "b"
hosts = {{ av1 = "", av2 = "" }}
phases = [ {{ kind = "compare", {body} }} ]
""")
        check_true(expect_in in got, f"{why} must be refused ({got!r})")

    # ── a fingerprint phase ───────────────────────────────────────────────────
    #
    # Not seed-split either: four paths and an optional joined pair list, all
    # expanding {host}, and budgets defaulting to fingerprint_members.py's.
    write_declaration(decl_root, "fprint", """
name = "fprint"
branch = "campaign/fp"
hosts = { av1 = "", av2 = "", av3 = "" }

[[phases]]
name = "draw"
kind = "fingerprint"
subjects = "experiments/fprint/subjects-{host}.txt"
members = "experiments/fprint/members"
work = "experiments/fp-work"
out = "experiments/fp-out/fp"
pairs = "experiments/fp-work/pairs-{host}.csv"
workers = 4

[[phases]]
name = "subsumption"
kind = "compare"
pairs = "experiments/fp-work/pairs-{host}.csv"
out = "experiments/fp-out/subsumption-{host}.csv"
""")
    fprint = C.load_campaign("fprint", decl_root)
    fp_phase = fprint["phases"][0]
    check((fp_phase["kind"], fp_phase["name"], fp_phase["words"],
           fp_phase["seed"], fp_phase["workers"], fp_phase["black_timeout"],
           fp_phase["hosts"]),
          ("fingerprint", "draw", 32768, 3, 4, 30, None),
          "a fingerprint phase parses, its budgets defaulting to "
          "fingerprint_members.py's")
    check((fprint["config_dirs"], fprint["results_dirs"]["av1"]), ([], []),
          "and reads no configs or results directory")
    check(C.phase_args(fp_phase, [], "av2"),
          ["experiments/fprint/subjects-av2.txt", "experiments/fprint/members",
           "experiments/fp-work", "experiments/fp-out/fp",
           "--pairs", "experiments/fp-work/pairs-av2.csv",
           "--words", "32768", "--seed", "3", "--workers", "4",
           "--black-timeout", "30",
           "--manifest", "experiments/compare-manifests/fprint.draw.json"],
          "{host} is expanded per host, and every budget is stated")
    check_true(C.phase_command(fp_phase, [], "av1").startswith(
        C.FINGERPRINT_CMD + " experiments/fprint/subjects-av1.txt "),
               "and its command is fingerprint_members.py's")
    check((C.phase_seeds(fp_phase, "av1", [0]),
           all(C.phase_runs_on(fp_phase, h, []) for h in C.HOSTS)),
          ([], True), "it takes no seeds and runs on every declared host")
    check(C.new_entry(fprint, "av3", 4, "b" * 40)["phase_seeds"], ["", ""],
          "an entry for the chain freezes no seeds for either phase")

    write_declaration(decl_root, "fpnopairs", """
name = "fpnopairs"
branch = "b"
hosts = { av1 = "", av2 = "" }
phases = [ { kind = "fingerprint", subjects = "s.txt", members = "m", work = "w", out = "o", hosts = ["av2"], seed = 0 } ]
""")
    nop = C.load_campaign("fpnopairs", decl_root)["phases"][0]
    check((nop["pairs"], nop["seed"], C.phase_runs_on(nop, "av1", []),
           "--pairs" in C.phase_args(nop, [], "av2")),
          (None, 0, False, False),
          "pairs is optional, seed may be 0, and hosts narrows the list")

    write_declaration(decl_root, "fpdesign", """
name = "fpdesign"
branch = "b"
hosts = { av1 = "", av2 = "" }
phases = [ { kind = "fingerprint", subjects = "s-{host}.txt", members = "m", work = "w", out = "o", design = "d/design.toml" } ]
""")
    dph = C.load_campaign("fpdesign", decl_root)["phases"][0]
    dargs = C.phase_args(dph, [], "av1")
    check(dargs[dargs.index("--design") + 1], "d/design.toml",
          "a declared design is handed to fingerprint_members.py")
    write_declaration(decl_root, "pooling", """
    name = "pooling"
    branch = "b"
    hosts = { av1 = "", av2 = "" }

    [[phases]]
    kind = "pool"
    design = "x/design.toml"
    subjects = "x/subjects-{host}.txt"
    copy_from = "x/members"
    out = "pm"

    [[phases]]
    kind = "pool"
    name = "fresh"
    design = "x/design.toml"
    subjects = "x/s.txt"
    out = "pm2"
    hosts = ["av2"]
    """)
    pooling = C.load_campaign("pooling", decl_root)
    pool_ph, fresh = pooling["phases"]
    check((pool_ph["kind"], pool_ph["name"], pool_ph["hosts"],
           fresh["copy_from"], fresh["hosts"]),
          ("pool", "pool", None, None, ["av2"]),
          "a pool phase parses, copy_from is optional and hosts narrows it")
    check(C.phase_args(pool_ph, [], "av1"),
          ["x/design.toml", "pm", "--subjects-file", "x/subjects-av1.txt",
           "--copy-from", "x/members",
           "--manifest", "experiments/compare-manifests/pooling.pool.json"],
          "its arguments expand {host}, and no --allow-missing is ever passed")
    check_true(C.phase_command(pool_ph, [], "av1").startswith(C.POOL_CMD + " "),
               "and its command is pool_members.py's")
    check((C.phase_seeds(pool_ph, "av1", [0]), C.phase_runs_on(fresh, "av1", [])),
          ([], False), "it takes no seeds, and runs only where hosts says")
    for body, expect_in, why in (
        ('design = "d", subjects = "s", out = "m", copy_from = "m/"',
         "copy_from and out name one directory", "writing into the committed pool"),
        ('design = "d", out = "m"', "needs subjects", "a missing subject list"),
        ('design = "d", subjects = "s", out = "m", words = 3',
         "unknown key(s) words", "a fingerprint budget"),
    ):
        write_declaration(decl_root, "poolbad", f"""
    name = "poolbad"
    branch = "b"
    hosts = {{ av1 = "" }}
    phases = [ {{ kind = "pool", {body} }} ]
    """)
        try:
            C.load_campaign("poolbad", decl_root)
            got = "accepted"
        except C.CampaignError as exc:
            got = str(exc)
        check_true(expect_in in got, f"{why} must be refused ({got!r})")
    pool_root = Path(tempfile.mkdtemp(prefix="pool-inputs-"))
    try:
        (pool_root / "x").mkdir()
        check_true("no design at" in (C.pool_inputs_missing(
            pool_root, pool_ph, "av1") or ""),
                   "a pool phase without its design is refused before an attempt")
        (pool_root / "x" / "design.toml").write_text("draws = 1\n")
        check_true("no subject list at" in (C.pool_inputs_missing(
            pool_root, pool_ph, "av1") or "")
                   and "subjects-av1.txt" in C.pool_inputs_missing(
                       pool_root, pool_ph, "av1"),
                   "as is one without the host's subject list")
        (pool_root / "x" / "subjects-av1.txt").write_text("fsm\n")
        check_true("no committed pool at" in (C.pool_inputs_missing(
            pool_root, pool_ph, "av1") or ""),
                   "and one whose committed pool is missing")
        (pool_root / "x" / "members").mkdir()
        check((C.pool_inputs_missing(pool_root, pool_ph, "av1"),
               C.phase_inputs_missing(pool_root, pool_ph, "av1")), (None, None),
              "which its inputs in place satisfy")
    finally:
        shutil.rmtree(pool_root, ignore_errors=True)

    write_declaration(decl_root, "scorespecs", """
    name = "scorespecs"
    branch = "b"
    profile = "aurus-h2h"
    hosts = { av1 = "", av2 = "" }
    phases = [
      { kind = "score", specs = ["arbiter"], hosts = { av1 = "0-2" } },
      { kind = "score", name = "all", hosts = { av2 = "0-2" } },
    ]
    """)
    narrowed, whole = C.load_campaign("scorespecs", decl_root)["phases"]
    nargs = C.phase_args(narrowed, [0, 1, 2], "av1")
    check((nargs[nargs.index("--specs") + 1], "--specs" in C.phase_args(
        whole, [0], "av2")), ("arbiter", False),
          "a score phase's specs reach the scorer, and only when declared")
    write_declaration(decl_root, "scorespecsbad", """
    name = "scorespecsbad"
    branch = "b"
    profile = "aurus-h2h"
    hosts = { av1 = "0-2" }
    phases = [ { kind = "score", specs = [] } ]
    """)
    try:
        C.load_campaign("scorespecsbad", decl_root)
        got = "accepted"
    except C.CampaignError as exc:
        got = str(exc)
    check_true("specs must be a non-empty array" in got,
               f"an empty score specs list is refused ({got!r})")
    import score_campaign as SC  # noqa: E402
    sc_root = Path(tempfile.mkdtemp(prefix="score-specs-"))
    try:
        for name in ("sweep_O_base_x_wkoff_log_rad-core-1-18_seed00",
                     "sweep_O_base_x_wkoff_log_rad-core-17-18_seed00",
                     "sweep_O_base_x_wkoff_log_rad-core-1-18_seed01",
                     "sweep_O_base_x_wkoff_log_rad-core-10_seed00"):
            (sc_root / name).mkdir()
        check(sorted(r.name for r in SC.queue_runs(sc_root, [0, 1],
                                                   ["rad-core-1-18"])),
              ["sweep_O_base_x_wkoff_log_rad-core-1-18_seed00",
               "sweep_O_base_x_wkoff_log_rad-core-1-18_seed01"],
              "score_campaign.py --specs keeps only that spec's run directories, "
              "and rad-core-1-18 does not match rad-core-17-18")
        check(len(SC.queue_runs(sc_root, [0])), 3,
              "and without it every spec at the host's seeds is queued")
    finally:
        shutil.rmtree(sc_root, ignore_errors=True)

    design_root = Path(tempfile.mkdtemp(prefix="fpdesign-"))
    try:
        (design_root / "s-av1.txt").write_text("fsm\n")
        (design_root / "m").mkdir()
        check_true("no design at" in (C.fingerprint_inputs_missing(
            design_root, dph, "av1") or ""),
                   "and a missing design file is refused before an attempt")
        (design_root / "d").mkdir()
        (design_root / "d" / "design.toml").write_text("draws = 1\n")
        check(C.fingerprint_inputs_missing(design_root, dph, "av1"), None,
              "which a present one satisfies")
    finally:
        shutil.rmtree(design_root, ignore_errors=True)

    for body, expect_in, why in (
        ('members = "m", work = "w", out = "o"', "needs subjects",
         "a missing subject list"),
        ('subjects = "s", members = "m", out = "o"', "needs work",
         "a missing work directory"),
        ('subjects = "s", members = "m", work = "w", out = "o", jobs = 2',
         "unknown key(s) jobs", "a compare budget"),
        ('subjects = "s", members = "m", work = "w", out = "o", profile = "x"',
         "unknown key(s) profile", "a profile"),
        ('subjects = "s-{seed}", members = "m", work = "w", out = "o"',
         "only {host}", "a placeholder other than {host}"),
        ('subjects = "s", members = "m", work = "w", out = "o", words = 0',
         "at least 1", "zero words"),
        ('subjects = "s", members = "m", work = "w", out = "o", seed = -1',
         "at least 0", "a negative seed"),
        ('subjects = "s", members = "m", work = "w", out = "o", '
         'hosts = { av1 = "0" }', "array of host names", "a seed table"),
    ):
        got = declaration_error(decl_root, "badfp", f"""
name = "badfp"
branch = "b"
hosts = {{ av1 = "", av2 = "" }}
phases = [ {{ kind = "fingerprint", {body} }} ]
""")
        check_true(expect_in in got, f"{why} must be refused ({got!r})")

    # A phase may narrow the split. Two paths whose sample sizes come from
    # separate power calculations cannot share one range: the wider one
    # multiplies the narrower phase's row count without saying so.
    write_declaration(decl_root, "narrowed", """
name = "narrowed"
branch = "feat/x"
hosts = { av2 = "0-34", av3 = "35-69" }

[[phases]]
name = "wide"
profile = "fretish-order-pairs"
jobs = 4

[[phases]]
name = "narrow"
profile = "aurus-h2h"
jobs = 1
hosts = { av2 = "0-12", av3 = "13-24" }

[[phases]]
name = "av2-only"
profile = "aurus-h2h"
jobs = 1
hosts = { av2 = "0-4" }
""")
    narrowed = C.load_campaign("narrowed", decl_root)
    wide, narrow, only = narrowed["phases"]
    check(wide["hosts"], None, "a phase that declares no split carries none")
    check(C.phase_seeds(wide, "av2", narrowed["hosts"]["av2"]),
          list(range(35)), "and falls back to the campaign's range")
    check(C.phase_seeds(narrow, "av2", narrowed["hosts"]["av2"]),
          list(range(13)), "a narrowed phase takes its own range")
    check(C.phase_seeds(narrow, "av3", narrowed["hosts"]["av3"]),
          list(range(13, 25)), "on each host independently")
    # An absent host is a deliberate narrowing, not a gap to fill in: falling
    # back would hand it the seeds the override exists to withhold.
    check(C.phase_seeds(only, "av3", narrowed["hosts"]["av3"]), [],
          "a phase that omits a host runs nowhere on it")

    phase_overlap = declaration_error(decl_root, "phase-overlap", """
name = "phase-overlap"
branch = "feat/x"
hosts = { av2 = "0-34", av3 = "35-69" }
phases = [ { profile = "fretish-order-pairs", hosts = { av2 = "0-12", av3 = "10-24" } } ]
""")
    check_true("share 3 seed(s)" in phase_overlap,
               f"a phase's split is held to the overlap rule too: "
               f"{phase_overlap!r}")
    check_true("phases[0]" in phase_overlap, "and the phase is named")

    # `local` is a known host, so this clears the known-host check and can
    # only be caught by the subset rule.
    added = declaration_error(decl_root, "phase-adds-host", """
name = "phase-adds-host"
branch = "feat/x"
hosts = { av2 = "0-34" }
phases = [ { profile = "fretish-order-pairs", hosts = { av2 = "0-9", local = "10-19" } } ]
""")
    check_true("not declared at the campaign level" in added and "local" in added,
               f"a phase narrows the split; it cannot add a host: {added!r}")

    phase_bad = declaration_error(decl_root, "phase-bad-range", """
name = "phase-bad-range"
branch = "feat/x"
hosts = { av2 = "0-34" }
phases = [ { profile = "fretish-order-pairs", hosts = { av2 = "9-0" } } ]
""")
    check_true("backwards" in phase_bad,
               f"and a malformed phase range is refused: {phase_bad!r}")

    # ── score phases ──────────────────────────────────────────────────────────
    #
    # A second phase kind: the offline maximality pass two campaigns needed
    # after their search, hand-rolled both times. Every declaration written
    # before the key existed has to load exactly as it did, so `kind` absent
    # means `run` and the run-phase records above carry that spelled out.
    check(narrowed["phases"][0]["kind"], "run",
          "a phase that names no kind is a run phase")

    write_declaration(decl_root, "scored", """
name = "scored"
branch = "feat/scored"
profile = "aurus-h2h"
hosts = { av2 = "0-9", av3 = "10-19" }

[[phases]]
jobs = 4

[[phases]]
kind = "score"

[[phases]]
kind = "score"
name = "old"
results = "experiments/results-old"
out = "experiments/curves-again"
workers = 2
cores = 8
cuts = 5
maximal_timeout = 60
compare_timeout = 30
deadline_s = 100
hosts = { av2 = "0-4" }
""")
    scored = C.load_campaign("scored", decl_root)
    run, score, old = scored["phases"]
    check(run["kind"], "run", "the first phase is the search")
    check(score["kind"], "score", "the second is a score phase")
    check(score["results"], C.profile_results_dir("aurus-h2h"),
          "which scores the campaign profile's results directory by default")
    check(score["results"], "experiments/results-aurus-h2h",
          "relative to the checkout, as the configs directory is")
    check(score["out"], "experiments/curves-aurus-h2h",
          "and writes to curves-<stem> beside it")
    check(score["name"], "curves-aurus-h2h", "named after its output by default")
    check({k: score[k] for k in C.SCORE_BUDGET_KEYS},
          {"workers": 8, "cores": 4, "cuts": 20, "maximal_timeout": 900,
           "compare_timeout": 600, "deadline_s": 4500, "wall_cap_s": 5400},
          "every budget defaults to the scorer's own value, and the wall cap "
          "sits 900s past the deadline")
    check(C.score_defaults(),
          __import__("score_campaign").DEFAULTS,
          "the defaults come from score_campaign.py, not from a copy")
    check((old["results"], old["out"], old["name"], old["profile"]),
          ("experiments/results-old", "experiments/curves-again", "old",
           "aurus-h2h"),
          "an explicit results and out are carried through")
    check({k: old[k] for k in C.SCORE_BUDGET_KEYS},
          {"workers": 2, "cores": 8, "cuts": 5, "maximal_timeout": 60,
           "compare_timeout": 30, "deadline_s": 100, "wall_cap_s": 1000},
          "and so is every budget it states")
    check(old["hosts"], {"av2": list(range(5))},
          "a score phase narrows the split exactly as a run phase does")
    check(scored["config_dirs"], [C.profile_configs_dir("aurus-h2h")],
          "score phases add no configs directory to the stage check")
    check(scored["results_dirs"],
          {"av2": ["experiments/results-old"], "av3": []},
          "and the stage check wants only the results directory no earlier "
          "run phase of this campaign writes, on the hosts the phase runs on")
    check(C.curves_dir_for("experiments/results"), "experiments/curves",
          "the bare `results` directory scores into `curves`")
    check(C.curves_dir_for("results-x"), "curves-x",
          "and a bare name stays bare")

    write_declaration(decl_root, "dironly", """
name = "dironly"
branch = "feat/x"
hosts = { av2 = "0-1" }
phases = [ { kind = "score", results = "experiments/results-rematch" } ]
""")
    dironly = C.load_campaign("dironly", decl_root)
    check(dironly["phases"][0]["profile"], None,
          "a score phase naming a directory needs no profile at all")
    check(dironly["phases"][0]["out"], "experiments/curves-rematch",
          "results-rematch scores into curves-rematch")
    check(dironly["config_dirs"], [],
          "and a campaign of score phases alone checks no configs directory")
    check(dironly["results_dirs"], {"av2": ["experiments/results-rematch"]},
          "but does check the results directory it reads")

    for text, expect_in, why in (
        ('phases = [ { kind = "score" } ]', "neither",
         "a score phase with no results and no profile"),
        ('phases = [ { kind = "score", profile = "nope" } ]',
         "defines no profile", "a score phase on an unknown profile"),
        ('phases = [ { kind = "score", results = "x", jobs = 2 } ]',
         "unknown key(s) jobs on a score phase", "jobs on a score phase"),
        ('phases = [ { kind = "score", results = "x", sweeps = ["R"] } ]',
         "unknown key(s) sweeps on a score phase", "sweeps on a score phase"),
        ('phases = [ { kind = "score", results = "x", specs = "a" } ]',
         "specs must be a non-empty array", "a bare string of specs on a "
         "score phase"),
        ('phases = [ { profile = "fretish-order-pairs", workers = 2 } ]',
         "unknown key(s) workers on a run phase", "a budget on a run phase"),
        ('phases = [ { kind = "grade", profile = "fretish-order-pairs" } ]',
         "kind must be one of", "an unknown kind"),
        ('phases = [ { kind = "score", results = "x", workers = 0 } ]',
         "workers must be a positive integer", "zero workers"),
        ('phases = [ { kind = "score", results = "x", cores = true } ]',
         "cores must be a positive integer", "a boolean core count"),
        ('phases = [ { kind = "score", results = "x", deadline_s = "1" } ]',
         "deadline_s must be a positive integer", "a string deadline"),
        ('phases = [ { kind = "score", results = "" } ]',
         "results must be a non-empty string", "an empty results"),
        ('phases = [ { kind = "score", results = "x", out = 3 } ]',
         "out must be a non-empty string", "a numeric out"),
    ):
        got = declaration_error(decl_root, "badscore", f"""
name = "badscore"
branch = "feat/x"
hosts = {{ av2 = "0-1" }}
{text}
""")
        check_true(expect_in in got, f"{why} must be refused ({got!r})")
finally:
    shutil.rmtree(decl_root, ignore_errors=True)

check(C.parse_seed_range("0-9,20", "x"), list(range(10)) + [20],
      "a comma-separated range")
check(C.format_seed_range([0, 1, 2, 5, 9, 10]), "0-2,5,9-10",
      "and the inverse collapses runs")
check(C.format_seed_range(C.parse_seed_range("100-199", "x")), "100-199",
      "a range survives the round trip a queue entry puts it through")

phase = {"name": "p", "profile": "aurus-h2h", "jobs": 4, "sweeps": ["R"],
         "specs": None}
check(C.phase_args(phase, [0, 1, 2]),
      ["--profile", "aurus-h2h", "--jobs", "4", "--sweeps", "R",
       "--seeds", "0", "1", "2"],
      "a phase becomes runner arguments, seeds last")
check_true(C.phase_command(phase, [0, 1]).startswith(C.RUNNER_CMD + " "),
           "and a run phase's command is the runner's")

score_phase = {"name": "curves", "kind": "score", "profile": None,
               "results": "experiments/results-x", "out": "experiments/curves-x",
               "workers": 3, "cores": 2, "cuts": 5, "maximal_timeout": 60,
               "compare_timeout": 30, "deadline_s": 100, "wall_cap_s": 1000}
check(C.phase_args(score_phase, [0, 1]),
      ["--results", "experiments/results-x", "--out", "experiments/curves-x",
       "--workers", "3", "--cores", "2", "--cuts", "5",
       "--maximal-timeout", "60", "--compare-timeout", "30",
       "--deadline-s", "100", "--wall-cap-s", "1000", "--seeds", "0", "1"],
      "a score phase becomes scorer arguments, every budget stated, seeds last")
check(C.phase_command(score_phase, [0, 1]),
      C.SCORER_CMD + " --results experiments/results-x --out "
      "experiments/curves-x --workers 3 --cores 2 --cuts 5 "
      "--maximal-timeout 60 --compare-timeout 30 --deadline-s 100 "
      "--wall-cap-s 1000 --seeds 0 1",
      "and its command is the scorer's, not the runner's")
check(C.phase_launcher({"profile": "aurus-h2h"}), C.RUNNER_CMD,
      "a phase record with no kind at all launches the runner")

# The freeze has to reach the overrides. Freezing the campaign range alone
# would pin the phases that do not narrow it and leave every phase that does
# free to move under rows already written against the old split.
frozen_campaign = {
    "name": "frozen", "hosts": {"av2": list(range(35))},
    "phases": [
        {"name": "wide", "profile": "aurus-h2h", "jobs": 4, "sweeps": None,
         "specs": None, "hosts": None},
        {"name": "narrow", "profile": "aurus-h2h", "jobs": 1, "sweeps": None,
         "specs": None, "hosts": {"av2": list(range(13))}},
        {"name": "elsewhere", "profile": "aurus-h2h", "jobs": 1, "sweeps": None,
         "specs": None, "hosts": {"av3": list(range(13, 25))}},
    ],
    "branch": "feat/x",
    "build": "make",
}
entry = C.new_entry(frozen_campaign, "av2", 3, "c" * 40)
check(entry["seeds"], "0-34", "the entry still freezes the campaign range")
check(entry["phase_seeds"], ["0-34", "0-12", ""],
      "and freezes each phase's own range beside it")
check((entry["commit"], entry["build"]), ("c" * 40, "make"),
      "and the commit and build command a tick stages with, which it cannot "
      "read off a branch it is not standing on")

# The build command reaches the host as a TOML string in the entry, and the
# ones that need `sh -c` carry both quote kinds plus `&&`. weakening-arbiter's
# is the shape in use, and a build that half survives the trip stages a host
# with no configs on it.
compound = ("sh -c 'cmake --build build-release && python3 "
            "scripts/gen_configs.py --sweeps O --weakening off'")
check(C.parse_toml(C.dump_toml({"build": compound}))["build"], compound,
      "a compound build command round-trips through the entry unchanged")

check(C.entry_phase_seeds(entry, 1), "0-12",
      "a tick reads the phase's frozen range")
check(C.entry_phase_seeds(entry, 2), "",
      "and an empty one means the phase is skipped on this host")
# An entry written before phases could narrow the split has no phase_seeds and
# must keep running on the campaign range its finished phases were keyed on.
check(C.entry_phase_seeds({"seeds": "0-9"}, 3), "0-9",
      "an entry from before this feature falls back to the campaign range")


# ── stage: the refusals ───────────────────────────────────────────────────────

PROBE = """##GIT
feat/fretish-order-pairs
b093374f0a0d8e83d0a9fe3412e6935bffc5b078
##DIRTY
 M src/main.cpp
##PS
zsh -zsh
peredur /home/benandrew/projects/counter/build-release/peredur --input x
##BIN
commit=b093374f0a0d8e83d0a9fe3412e6935bffc5b078
commit_short=b093374
dirty=0
##QUEUE
##QFILE experiments/queue/001-fretish-order-pairs.toml
campaign = "fretish-order-pairs"
state = "queued"
##ENDQFILE
##END
"""
probe = C.parse_stage_probe(PROBE)
check(probe.branch, "feat/fretish-order-pairs", "the probe reads the branch")
check(probe.head[:7], "b093374", "and the full head")
check(probe.dirty, ["M src/main.cpp"], "and the modified files by name")
check([p["comm"] for p in probe.processes], ["peredur"],
      "and the live engine, keyed on comm as everywhere else")
check(probe.binary["commit_short"], "b093374", "and the binary's commit")
check(probe.queue[0]["campaign"], "fretish-order-pairs",
      "and the host's queue, in the one round trip")

check(C.parse_stage_probe("##ERR not a directory: /nope").error,
      "not a directory: /nope", "a missing checkout reports rather than raises")
check_true(C.parse_stage_probe("##GIT\nmain\nabc").error,
           "a probe that never reached ##END is not a clean reading")

# A shell that prints nothing and exits zero returns no error either. Every
# field still has to read as absent rather than be missing, because the callers
# read them after the error guard and a KeyError mid-stage is the worst place
# for one.
empty_probe = C.parse_stage_probe("")
check_true(empty_probe.error, "an empty answer is an error, not a clean probe")
check((empty_probe.branch, empty_probe.binary, empty_probe.queue),
      ("?", {}, []), "and every field still answers")

# Each refusal is a way to destroy work that cannot be recovered from this
# side. The hosts sit on someone else's branch with a live run on one of them,
# so every one of these fires on the machines as they are.
def host_probe(**kwargs) -> C.HostProbe:
    base = {"branch": "feat/x", "head": "a" * 40,
            "binary": {"commit": "a" * 40, "commit_short": "aaaaaaa",
                       "dirty": "0"}}
    base.update(kwargs)
    return C.HostProbe(**base)


clean = host_probe()
check(C.stage_refusals(clean, "feat/x"), [],
      "a clean, idle host on the right branch is staged without a fight")
check([k for k, _ in C.stage_refusals(host_probe(dirty=[" M x.cpp"]),
                                      "feat/x")],
      ["dirty"], "a dirty checkout is refused")
check([k for k, _ in C.stage_refusals(
    host_probe(processes=[{"comm": "peredur", "profile": None}]), "feat/x")],
    ["busy"], "a live run is refused")
check([k for k, _ in C.stage_refusals(clean, "feat/other")],
      ["branch"], "a host on another branch is refused")
check(len(C.stage_refusals(host_probe(dirty=[" M x"], processes=[
    {"comm": "peredur", "profile": None}]), "feat/other")), 3,
    "and all three are reported at once, not one per attempt")

apply_forced = C.stage_apply_script("/r", "feat/x", "s" * 40, "make",
                                    "gen --out x", ["experiments/configs"],
                                    True)
apply_plain = C.stage_apply_script("/r", "feat/x", "s" * 40, "make", None,
                                   ["experiments/configs"], False)
check_true("checkout -f" in apply_forced,
           "--force discards tracked modifications")
check_true("checkout -f" not in apply_plain,
           "and nothing else does")
check_true("git clean" not in apply_forced,
           "no git clean, ever: a host's untracked files are its results")
check_true("rev-list --count" in apply_plain,
           "an unforced stage refuses a checkout ahead of the pushed commit")
check_true('if [ "1" = "0" ]' in apply_forced,
           "and a forced one leaves that guard unreachable")
check_true(" -- -B " not in apply_plain,
           "`git checkout -- -B x` would read -B as a path name")
check_true("| tail" not in apply_forced.split("@")[0] + apply_forced,
           "a build piped into tail reports tail's exit status, so a failed "
           "build would read as a staged host")

# The configs step: the declared command reaches the script, runs after the
# build and before the version read, and the check runs either way.
check_true("( gen --out x )" in apply_forced,
           "the declared configs command reaches the apply script, in a "
           "subshell so `! a && b` cannot bind the negation to half a chain")
check_true("gen --out x" not in apply_plain,
           "and a campaign declaring none gets no command")
positions = [apply_forced.index(s)
             for s in ("make", "gen --out x", "--version")]
check(positions, sorted(positions),
      "configs run after the build and before the binary is asked its commit, "
      "so a generator failure reports as itself")
check_true("##CONFIGS" in apply_forced and "##CONFIGS" in apply_plain,
           "and the step has its own marker section on both paths")
for script, why in ((apply_forced, "with a command"),
                    (apply_plain, "and without one")):
    check_true("no config files under" in script,
               f"the configs check runs {why}: a declaration written before "
               f"the key existed is exactly the case that broke")
    check_true("find \"$cfgdir\" -name '*.toml' -print -quit" in script,
               "the check is a find, stopping at the first hit")
    check_true("for cfgdir in experiments/configs" in script,
               "over the profile's own configs directory, named as a path the "
               "host can resolve under its own checkout")
    # zsh's default NOMATCH aborts the whole script where a pattern matches
    # nothing -- which is precisely the state being tested for -- and every
    # section below it then vanishes with no error anybody reads.
    check_true("-name '*.toml'" in script,
               "the glob is quoted, so zsh cannot expand it and abort")
    for line in script.splitlines():
        stripped = line.strip()
        if "*.toml" in stripped:
            check_true("'*.toml'" in stripped or '"*.toml"' in stripped,
                       f"every *.toml is quoted, not just the first: {line!r}")
check_true(C.configs_block(None, ["a b", "c"]).count("'a b'") == 1,
           "a configs directory with a space in it survives the substitution")

# And functionally, under the shell that would break it. ssh hands the script
# to the lab login shell, which is zsh, and zsh's default NOMATCH never lets an
# unquoted `*.toml` mean what it means in sh: with nothing matching it the find
# is skipped outright and the check reads as a missing configs directory, and
# with a stray .toml in the working directory it expands to that name and the
# find searches for the wrong one. Both readings are the same wrong answer.
if shutil.which("zsh"):
    nomatch_root = Path(tempfile.mkdtemp(prefix="campaign-nomatch-"))
    try:
        block = C.configs_block(None, ["cfg"]).replace("@M@", C.MARK)
        script = block + 'echo "##END"\n'
        for made, want_end, why in (
                ([], False, "an absent directory stops at the error"),
                (["cfg/notes.txt"], False, "and so does one with no .toml"),
                (["cfg/a/b.toml"], True, "a .toml anywhere under it passes")):
            shutil.rmtree(nomatch_root / "cfg", ignore_errors=True)
            for rel in made:
                (nomatch_root / rel).parent.mkdir(parents=True, exist_ok=True)
                (nomatch_root / rel).write_text("x\n")
            proc = subprocess.run(["zsh", "-c", script], cwd=nomatch_root,
                                  capture_output=True, text=True)
            check_true(("##END" in proc.stdout) is want_end,
                       f"under zsh, {why}: {proc.stdout!r} {proc.stderr!r}")
            if not want_end:
                check_true("no config files under" in proc.stdout
                           and proc.returncode == 10,
                           f"with the check's own message and status, not a "
                           f"shell abort: {proc.stdout!r} {proc.stderr!r}")

        # The quoting is load-bearing, and zsh says so: the same block with the
        # pattern unquoted refuses a directory that does hold configs, because
        # the stray .toml beside it is what the pattern expanded to.
        (nomatch_root / "stray.toml").write_text("x\n")
        broken = script.replace("-name '*.toml'", "-name *.toml")
        check_true(broken != script, "the unquoted variant differs")
        for variant, want_end, why in ((script, True, "quoted"),
                                       (broken, False, "unquoted")):
            proc = subprocess.run(["zsh", "-c", variant], cwd=nomatch_root,
                                  capture_output=True, text=True)
            check_true(("##END" in proc.stdout) is want_end,
                       f"the {why} pattern reads the directory "
                       f"{'right' if want_end else 'wrong'} under zsh: "
                       f"{proc.stdout!r} {proc.stderr!r}")
    finally:
        shutil.rmtree(nomatch_root, ignore_errors=True)


# ── stage, end to end against a throwaway checkout ────────────────────────────

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


def make_repo(root: Path, name: str, declaration: str, branch: str) -> None:
    """A checkout carrying one campaign declaration, on `branch`.

    With an origin to push to, because that is the route by which the branch
    (and the declaration on it) reaches a host: stage pushes here, the host
    fetches from here.
    """
    root.mkdir(parents=True, exist_ok=True)
    git(root, "init", "-q", ".")
    origin = root.with_name(root.name + "-origin.git")
    subprocess.run(["git", "init", "-q", "--bare", str(origin)], check=True)
    git(root, "remote", "add", "origin", str(origin))
    # Generated configs are untracked on a real host, and have to be untracked
    # here too: tracked, a `git add -A` on one branch would take them away
    # again on the next checkout, which is a state no lab machine is ever in.
    (root / ".gitignore").write_text("experiments/configs*/\n")
    write_declaration(root, name, declaration)
    git(root, "add", "-A")
    # --no-verify: a globally configured core.hooksPath would otherwise run
    # this repo's own pre-commit inside a fixture that is not this repo.
    git(root, "commit", "-q", "--no-verify", "-m", "init")
    git(root, "checkout", "-q", "-b", branch)
    git(root, "checkout", "-q", "-")
    # Both branches pushed, which is how a host's checkout comes to be on one:
    # it fetched it. A tick refuses to move off a HEAD no remote branch
    # contains, so a fixture that never pushed would refuse for a reason no
    # lab machine is ever in.
    git(root, "push", "-q", "origin", "--all")


def local_only(host: str, script: str, timeout: int = C.SSH_TIMEOUT_S):
    """Every host is this machine. No ssh, no lab machine, no launch."""
    return REAL_RUN_SHELL(C.LOCAL, script, timeout)


REAL_RUN_SHELL = C.run_shell
REAL_PROBE_PROCESSES = C.probe_processes

# The probe's `ps` is host-wide by design, and stage refuses on any live run
# whether or not it belongs to the checkout being staged, which is the reading
# a shared lab machine needs. A fixture pointed at a local checkout therefore
# inherits this machine's own PEREDUR and ltlsynt processes and refuses for
# reasons that have nothing to do with the fixture: the suite passed on an idle
# box and failed on a busy one. The list is supplied here instead, and both
# directions are asserted below, which makes ambient state a tested input.
FIXTURE_PROCESSES: list = []

FIXTURE_DECL = """
name = "fixture"
branch = "feat/fixture"
build = "true"
hosts = { av2 = "0-1", av3 = "2-3" }
phases = [ { profile = "fretish-order-pairs", jobs = 2 } ]
"""

# Stands in for build-release/peredur, reporting the commit the checkout is
# standing on. Untracked, exactly as the real binary is, so it survives the
# checkout the stage script performs.
FAKE_BINARY = """#!/bin/sh
echo "commit=$(git rev-parse HEAD)"
echo "commit_short=$(git rev-parse --short HEAD)"
echo "dirty=0"
"""

stage_root = Path(tempfile.mkdtemp(prefix="campaign-stage-"))
try:
    repo = stage_root / "checkout"
    make_repo(repo, "fixture", FIXTURE_DECL, "feat/fixture")
    C.REPO_ROOT = repo
    C.run_shell = local_only
    C.source_path = lambda host: str(repo)
    C.probe_processes = lambda _lines: FIXTURE_PROCESSES

    def stage_args(**kwargs) -> argparse.Namespace:
        base = {"campaign": "fixture", "host": ["av2"], "force": False,
                "dry_run": True, "build_timeout": 60}
        base.update(kwargs)
        return argparse.Namespace(**base)

    # The checkout is on master/main, not on the campaign's branch, which is
    # the position both lab machines are in right now.
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args())
    check(code, 1, "a host on another branch is not staged")
    check_true("REFUSED" in buffer.getvalue(), "and says so")
    check_true("--force" in buffer.getvalue(), "and names the way past it")

    git(repo, "checkout", "-q", "feat/fixture")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args())
    check(code, 0, "an idle host on the right branch stages")
    check_true("would stage" in buffer.getvalue(), "dry run, so nothing ran")

    # The other direction, from the same fixture: a run in flight refuses,
    # whether or not it belongs to this checkout. Resetting a shared machine
    # under somebody's run is what the refusal exists to prevent.
    FIXTURE_PROCESSES = [{"comm": "peredur", "profile": None}]
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args())
    check(code, 1, "a host with a live PEREDUR is not staged")
    check_true("busy" in buffer.getvalue(), "and busy is the reason given")
    FIXTURE_PROCESSES = []

    (repo / "experiments" / "fixture" / "campaign.toml").write_text(
        FIXTURE_DECL + "\n# touched\n")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args())
    check(code, 1, "a dirty host is not staged even on the right branch")
    check_true("campaign.toml" in buffer.getvalue(),
               "and the file it would discard is named")

    # --force without a terminal cannot confirm, so it refuses. That is what
    # keeps a scripted or cron-driven stage from resetting a host.
    stdin = sys.stdin
    sys.stdin = io.StringIO("av2\n")
    try:
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = C.cmd_stage(stage_args(force=True, dry_run=False))
        check(code, 1, "--force with nothing to confirm on must refuse")
        check_true("needs a terminal" in buffer.getvalue(),
                   "and say why, rather than proceeding")
        check_true("modified tracked file" in buffer.getvalue(),
                   "having first named what it would have discarded")
    finally:
        sys.stdin = stdin
    check(git(repo, "rev-parse", "--abbrev-ref", "HEAD"), "feat/fixture",
          "and the checkout is untouched")
    git(repo, "checkout", "-q", "--", ".")

    # A host that is clean, idle and on the campaign's own branch yields no
    # refusal from the probe, and --force must still take effect on it. The
    # apply script declines a fourth thing the probe cannot see -- a checkout
    # ahead of the target commit, which needs the fetch to detect -- so a
    # --force that only engaged when a probe-level refusal existed was inert
    # for exactly this host, and skipped the confirmation with it. That is the
    # state a rebased branch leaves every staged machine in.
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args(force=True, dry_run=False))
    check(code, 1, "--force on an unrefused host still needs confirming")
    check_true("needs a terminal" in buffer.getvalue(),
               "so it refuses without a terminal rather than staging quietly")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args(force=True))
    check_true("(--force)" in buffer.getvalue(),
               "and a forced dry run says so even with nothing to refuse")

    # The whole apply path, against a stub binary that reports the commit its
    # checkout is on: push, fetch, checkout, build, and the version read back.
    # The last step is the one that matters -- a campaign whose binary predates
    # its branch produces rows that look valid and name the wrong commit.
    binary = repo / C.PEREDUR_BINARY
    binary.parent.mkdir(parents=True, exist_ok=True)
    binary.write_text(FAKE_BINARY)
    binary.chmod(0o755)

    # The failure this exists for. run_experiments.py exits 1 on a configs
    # directory with nothing in it, and a queued campaign spends its attempts
    # discovering that one tick at a time; the stage is where it is cheap.
    configs_dir = repo / C.profile_configs_dir("fretish-order-pairs")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args(dry_run=False))
    check(code, 1, "a host whose configs directory does not exist is refused")
    check_true("no config files under" in buffer.getvalue()
               and str(configs_dir) in buffer.getvalue(),
               f"and the directory is named: {buffer.getvalue()!r}")
    configs_dir.mkdir(parents=True, exist_ok=True)
    (configs_dir / "notes.txt").write_text("not a config\n")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args(dry_run=False))
    check(code, 1, "an empty configs directory is refused the same way")
    (configs_dir / "default" / "x.toml").parent.mkdir(exist_ok=True)
    (configs_dir / "default" / "x.toml").write_text("generations = 1\n")

    # A declared command runs on the host, after the build. This one writes
    # the file the check then finds, which is the whole sequence in order.
    generated = repo / C.profile_configs_dir("fretish-order-pairs") / "gen" / "made.toml"
    # Two commands joined with && , which is what a campaign generating for
    # two profiles writes, and which `! a && b` would mis-bind outside the
    # subshell the step wraps it in.
    with_cmd = FIXTURE_DECL + (
        f'configs = "mkdir -p {generated.parent} && touch {generated}"\n')
    write_declaration(repo, "fixture", with_cmd)
    git(repo, "add", "experiments/fixture/campaign.toml")
    git(repo, "commit", "-q", "--no-verify", "-m", "configs")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args(dry_run=False))
    check(code, 0, f"the configs command runs and stages: {buffer.getvalue()}")
    check_true(generated.exists(),
               "and the command it declared actually ran on the host")
    generated.unlink()

    # A configs command that fails is its own error, not a build failure and
    # not a bad binary -- which is why it sits between the two.
    write_declaration(repo, "fixture", FIXTURE_DECL + 'configs = "false"\n')
    git(repo, "add", "experiments/fixture/campaign.toml")
    git(repo, "commit", "-q", "--no-verify", "-m", "bad configs")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args(dry_run=False))
    check(code, 1, "a failing configs command fails the stage")
    check_true("configs command failed" in buffer.getvalue(),
               f"naming itself, not the build: {buffer.getvalue()!r}")

    # `if ! false && true` is true, so an unwrapped chain would stage a host
    # whose first generator never produced a file.
    write_declaration(repo, "fixture", FIXTURE_DECL + 'configs = "false && '
                                                      'true"\n')
    git(repo, "add", "experiments/fixture/campaign.toml")
    git(repo, "commit", "-q", "--no-verify", "-m", "bad chain")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args(dry_run=False))
    check(code, 1, "a chain whose first command fails fails the stage too")

    write_declaration(repo, "fixture", FIXTURE_DECL)
    git(repo, "add", "experiments/fixture/campaign.toml")
    git(repo, "commit", "-q", "--no-verify", "-m", "restore")

    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args(dry_run=False))
    check(code, 0, f"a clean host stages for real: {buffer.getvalue()}")
    check_true("staged" in buffer.getvalue()
               and "MISMATCH" not in buffer.getvalue(),
               "with the binary's commit matching the branch it was staged to")

    # A binary left behind by an earlier campaign must fail the same check.
    binary.write_text("#!/bin/sh\necho commit=deadbee\necho commit_short="
                      "deadbee\necho dirty=0\n")
    binary.chmod(0o755)
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_stage(stage_args(dry_run=False))
    check(code, 1, "a stale binary fails the stage")
    check_true("BINARY MISMATCH" in buffer.getvalue(),
               f"and says which check: {buffer.getvalue()!r}")
    binary.write_text(FAKE_BINARY)
    binary.chmod(0o755)

finally:
    C.run_shell = REAL_RUN_SHELL
    C.probe_processes = REAL_PROBE_PROCESSES
    shutil.rmtree(stage_root, ignore_errors=True)


# ── The queue ─────────────────────────────────────────────────────────────────

STUB_RUNNER = '''#!/usr/bin/env python3
"""Stands in for run_experiments.py: records its arguments, never runs one."""
import os, sys, time
here = os.path.dirname(os.path.abspath(__file__))


def control(name):
    path = os.path.join(here, name)
    return open(path).read().strip() if os.path.exists(path) else None


with open(os.path.join(here, "calls.txt"), "a") as handle:
    handle.write(" ".join(sys.argv[1:]) + "\\n")
with open(os.path.join(here, "runner.pid"), "w") as handle:
    handle.write(str(os.getpid()))
if control("sleep"):
    time.sleep(float(control("sleep")))
sys.exit(int(control("exit-code") or 0))
'''

TWO_PHASE_DECL = """
name = "queued"
branch = "feat/queued"
build = "true"
hosts = { local = "0-3" }
phases = [ { profile = "fretish-order-pairs", jobs = 1 }, { profile = "aurus-h2h", jobs = 2 } ]
"""

OTHER_DECL = """
name = "other"
branch = "feat/other"
build = "true"
hosts = { local = "4-5" }
phases = [ { profile = "fretish-order-pairs", jobs = 1 } ]
"""

SCORED_DECL = """
name = "scored"
branch = "feat/scored"
build = "true"
hosts = { local = "6-7" }
phases = [
  { profile = "fretish-order-pairs", jobs = 1 },
  { kind = "score", profile = "fretish-order-pairs", workers = 2, cores = 1, deadline_s = 10 },
]
"""

queue_root = Path(tempfile.mkdtemp(prefix="campaign-queue-"))
try:
    repo = queue_root / "checkout"
    make_repo(repo, "queued", TWO_PHASE_DECL, "feat/queued")
    default_branch = git(repo, "rev-parse", "--abbrev-ref", "HEAD")
    git(repo, "checkout", "-q", "feat/queued")
    # A tick stages before it runs, and staging reads the binary back the way
    # run_experiments.py does. Untracked, so the checkout it performs leaves it
    # alone, exactly as the real build-release/peredur is untracked.
    binary = repo / C.PEREDUR_BINARY
    binary.parent.mkdir(parents=True, exist_ok=True)
    binary.write_text(FAKE_BINARY)
    binary.chmod(0o755)
    # The configs a staged host already has. A tick checks them before it
    # runs a phase, so a fixture with none would be testing the empty-configs
    # refusal on every tick rather than the phase it queued.
    def put_config(profile: str) -> Path:
        path = (repo / C.profile_configs_dir(profile) / "default" / "x.toml")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("generations = 1\n")
        return path

    for profile in ("fretish-order-pairs", "aurus-h2h"):
        put_config(profile)
    control_dir = queue_root / "stub"
    control_dir.mkdir()
    stub = control_dir / "stub_runner.py"
    stub.write_text(STUB_RUNNER)
    calls = control_dir / "calls.txt"
    lock = queue_root / "queue.lock"

    C.REPO_ROOT = repo
    C.RUNNER_CMD = f"{sys.executable} {stub}"
    C.run_shell = local_only
    C.source_path = lambda host: str(repo)
    # Machine-wide by design, so a fixture would otherwise refuse to stage
    # whenever this box happened to be running a campaign of its own. Supplied
    # here instead, and asserted in both directions below.
    C.local_processes = lambda: FIXTURE_PROCESSES
    FIXTURE_PROCESSES = []
    campaign = C.load_campaign("queued", repo)

    def tick_args(**kwargs) -> argparse.Namespace:
        base = {"host": "local", "root": str(repo), "lock": str(lock),
                "dry_run": False, "no_stage": False}
        base.update(kwargs)
        return argparse.Namespace(**base)

    def tick(**kwargs) -> tuple:
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = C.cmd_tick(tick_args(**kwargs))
        return code, buffer.getvalue()

    def only_entry() -> dict:
        entries = C.queue_entries(repo)
        check(len(entries), 1, "exactly one queue entry")
        return entries[0]

    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_enqueue(argparse.Namespace(
            campaign="queued", host=["local"], max_attempts=2, again=False,
            dry_run=False))
    check(code, 0, "enqueue writes an entry")
    entry = only_entry()
    check(entry["file"], "001-queued.toml", "numbered from one")
    check(entry["state"], "queued", "and starts queued")
    check(entry["seeds"], "0-3", "carrying the host's declared seeds")
    check(entry["phases"], 2, "and the phase count it was declared with")

    # A campaign already queued is not queued twice by accident: two entries
    # for one campaign is two runners on one results CSV.
    with contextlib.redirect_stdout(io.StringIO()):
        code = C.cmd_enqueue(argparse.Namespace(
            campaign="queued", host=["local"], max_attempts=2, again=False,
            dry_run=False))
    check(code, 1, "a second enqueue is refused")
    check(len(C.queue_entries(repo)), 1, "and writes nothing")

    # Lock contention. acquire_lock is the only guard on the queue, and what
    # stops a tick typed by hand racing the cron tick into a second runner over
    # the same CSV. The second acquire below is denied inside a single process,
    # because flock attaches to the open file description rather than the
    # process -- which is also why the crontab must not wrap the tick in flock
    # on this file. See the cron_line check further down.
    held = C.acquire_lock(lock)
    check_true(held is not None, "the lock is free to start with")
    check_true(C.acquire_lock(lock) is None, "and exclusive once taken")
    code, printed = tick()
    check(code, 0, "a tick that cannot take the lock exits quietly")
    check_true("is held" in printed, "saying that another tick has it")
    check(only_entry()["state"], "queued", "and changes nothing")
    check_true(not calls.exists(), "and runs nothing")
    assert held is not None  # checked above; narrows for the type checker
    held.close()

    code, printed = tick()
    check(code, 0, "the first real tick runs phase 0")
    entry = only_entry()
    check(entry["state"], "queued", "and requeues for the second phase")
    check(entry["phase"], 1, "having advanced the phase index")
    check(calls.read_text().count("\n"), 1, "one runner invocation")
    check_true("--profile fretish-order-pairs --jobs 1 --seeds 0 1 2 3"
               in calls.read_text(), "on the declared profile and seeds")

    code, printed = tick()
    check(code, 0, "the second tick runs the last phase")
    entry = only_entry()
    check(entry["state"], "done", "which finishes the entry")
    check_true("--profile aurus-h2h --jobs 2" in calls.read_text(),
               "having run the second phase, not the first again")

    code, printed = tick()
    check_true("nothing queued" in printed, "a done entry is not run again")
    check(calls.read_text().count("\n"), 2, "and no third invocation")

    # A tick killed mid-phase must cost nothing. The entry is left `running`,
    # and the next tick puts it back in the queue and runs the same phase over
    # the same seeds -- run_experiments.py resumes off the results CSV, so the
    # work already done is not repeated.
    calls.unlink()
    (control_dir / "sleep").write_text("30")
    entry = only_entry()
    entry.update({"state": "queued", "phase": 0, "attempts": 0})
    C.write_entry(entry["path"], entry)

    env = dict(os.environ, PEREDUR_RUNNER_CMD=f"{sys.executable} {stub}")
    child = subprocess.Popen(
        [sys.executable, str(CAMPAIGN_PY), "tick",
         "--root", str(repo), "--lock", str(lock), "--host", "local"],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    deadline = time.time() + 60
    while time.time() < deadline and only_entry()["state"] != "running":
        time.sleep(0.1)
    check(only_entry()["state"], "running", "the tick marks the entry running")
    child.send_signal(signal.SIGKILL)
    child.wait(timeout=30)
    check(only_entry()["state"], "running",
          "and a killed tick leaves it that way")

    (control_dir / "sleep").unlink()
    code, printed = tick()
    check_true("was left running" in printed,
               "the next tick recovers the interrupted entry")
    entry = only_entry()
    check(entry["phase"], 1, "and the recovered phase ran to completion")
    check(entry["state"], "queued", "leaving the second phase to run")
    invocations = [ln for ln in calls.read_text().splitlines() if ln]
    check(len(invocations), 2, "the interrupted phase was attempted twice")
    check(invocations[0], invocations[1],
          "over exactly the same selection, so the runner resumes it")

    # A failing phase is visible rather than retried for ever.
    calls.unlink()
    (control_dir / "exit-code").write_text("3")
    entry = only_entry()
    entry.update({"state": "queued", "phase": 0, "attempts": 0,
                  "max_attempts": 2})
    C.write_entry(entry["path"], entry)
    code, printed = tick()
    check(code, 1, "a failed phase fails the tick")
    entry = only_entry()
    check(entry["state"], "queued", "the first failure is retried")
    check(entry["attempts"], 1, "with the attempt counted")
    code, printed = tick()
    entry = only_entry()
    check(entry["state"], "failed", "the cap stops it")
    check_true("exited 3" in entry["last_error"], "and records why")
    code, printed = tick()
    check_true("nothing queued" in printed, "a failed entry stops moving")
    check(len(calls.read_text().splitlines()), 2,
          "so the machine is not spent re-running it")

    with contextlib.redirect_stdout(io.StringIO()):
        check(C.cmd_requeue(argparse.Namespace(host=C.LOCAL,
                                               entry="001-queued.toml")), 0,
              "requeue clears a failed entry by hand")
    entry = only_entry()
    check((entry["state"], entry["attempts"]), ("queued", 0),
          "which is the only way out of failed")

    # ── dequeue stops a live phase ────────────────────────────────────────────
    #
    # The whole point of the verb is the `running` case, and the ordering it
    # depends on is invisible in the entry afterwards: the tick has to die
    # before the entry is rewritten, or it writes its own copy back at the end
    # of the phase and the cancellation disappears hours later. The runner is
    # killed too -- it is what actually spends the machine, and it outlives the
    # tick that started it.
    calls.unlink() if calls.exists() else None
    runner_pid_file = control_dir / "runner.pid"
    runner_pid_file.unlink() if runner_pid_file.exists() else None
    (control_dir / "sleep").write_text("120")
    entry = only_entry()
    entry.update({"state": "queued", "phase": 0, "attempts": 0,
                  "max_attempts": 2, "last_error": ""})
    C.write_entry(entry["path"], entry)
    child = subprocess.Popen(
        [sys.executable, str(CAMPAIGN_PY), "tick",
         "--root", str(repo), "--lock", str(lock), "--host", "local"],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    deadline = time.time() + 60
    while time.time() < deadline and not runner_pid_file.exists():
        time.sleep(0.1)
    check(only_entry()["state"], "running", "the tick holds the entry")
    runner_pid = int(runner_pid_file.read_text())

    def alive(pid: int) -> bool:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
        return True

    def dequeue(**kwargs) -> tuple:
        base = {"host": C.LOCAL, "entry": "001-queued.toml", "reason": "",
                "dry_run": False}
        base.update(kwargs)
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            code = C.cmd_dequeue(argparse.Namespace(**base))
        return code, buffer.getvalue()

    code, printed = dequeue(dry_run=True)
    check(code, 0, "a dry-run dequeue succeeds")
    check_true(str(child.pid) in printed and str(runner_pid) in printed,
               "naming the tick and the runner under it")
    check(only_entry()["state"], "running", "and changes nothing")
    check_true(alive(child.pid) and alive(runner_pid),
               "leaving both processes alone")

    code, printed = dequeue(reason="audit says stop")
    check(code, 0, "dequeue succeeds")
    check_true(str(child.pid) in printed, "reporting what it killed")
    child.wait(timeout=30)
    deadline = time.time() + 30
    while time.time() < deadline and alive(runner_pid):
        time.sleep(0.1)
    check_true(not alive(runner_pid),
               "the runner dies with the tick that started it")
    entry = only_entry()
    check(entry["state"], "cancelled", "and the entry is out of the queue")
    check(entry["last_error"], "audit says stop", "carrying the reason given")
    check_true(any("cancelled while running" in line
                   for line in entry.get("log", [])),
               "and the log says what state it was taken out of")
    code, printed = tick()
    check_true("nothing queued" in printed, "no tick picks a cancelled entry")

    code, printed = dequeue()
    check(code, 0, "dequeuing it again is not an error")
    check_true("already cancelled" in printed, "and says so")

    with contextlib.redirect_stdout(io.StringIO()):
        check(C.cmd_requeue(argparse.Namespace(host=C.LOCAL,
                                               entry="001-queued.toml")), 0,
              "requeue is the way back")
    check(only_entry()["state"], "queued", "which is a stop, not a withdrawal")
    (control_dir / "sleep").unlink()
    calls.unlink() if calls.exists() else None

    # ── a confined phase: launched through the wrapper, stopped through it ────
    #
    # Stub systemd-run and taskset on PATH record their arguments and exec the
    # rest of the line, as the real ones do (a scope execs its command in
    # place). The runner is then still the tick's child, so dequeue's process
    # tree reaches it and status still reads it as a runner by its profile.
    # The tick runs with neither XDG_RUNTIME_DIR nor DBUS_SESSION_BUS_ADDRESS,
    # as under cron, where `systemd-run --user` cannot find the bus without one.
    confine_root = queue_root / "confine"
    confine_repo = confine_root / "checkout"
    make_repo(confine_repo, "confined", """
name = "confined"
branch = "feat/confined"
build = "true"
hosts = { local = "0-1" }
confine = { local = { cpus = "0", memory_max = "1G" } }
phases = [ { profile = "fretish-order-pairs", jobs = 1 } ]
""", "feat/confined")
    git(confine_repo, "checkout", "-q", "feat/confined")
    confine_bin = confine_repo / C.PEREDUR_BINARY
    confine_bin.parent.mkdir(parents=True, exist_ok=True)
    confine_bin.write_text(FAKE_BINARY)
    confine_bin.chmod(0o755)
    confine_cfg = (confine_repo / C.profile_configs_dir("fretish-order-pairs")
                   / "default" / "x.toml")
    confine_cfg.parent.mkdir(parents=True, exist_ok=True)
    confine_cfg.write_text("generations = 1\n")
    wrap_dir = confine_root / "bin"
    wrap_dir.mkdir()
    wrapped = confine_root / "wrapped.txt"
    wrapper_stub = f'''#!{sys.executable}
import os, sys
name = os.path.basename(sys.argv[0])
args = sys.argv[1:]
with open({str(wrapped)!r}, "a") as handle:
    handle.write(name + " " + " ".join(args) + " XDG="
                 + os.environ.get("XDG_RUNTIME_DIR", "") + "\\n")
i = 0
if name == "systemd-run":
    while args[i].startswith("-"):
        i += 2 if args[i] in ("--unit", "-p") else 1
else:
    i = 2
os.execvp(args[i], args[i:])
'''
    for tool in ("systemd-run", "taskset"):
        (wrap_dir / tool).write_text(wrapper_stub)
        (wrap_dir / tool).chmod(0o755)
    runner_pid_file.unlink() if runner_pid_file.exists() else None
    (control_dir / "sleep").write_text("120")
    confine_entry = C.new_entry(C.load_campaign("confined", confine_repo),
                                "local", 2,
                                git(confine_repo, "rev-parse", "HEAD").strip())
    confine_entry["file"] = "001-confined.toml"
    confine_entry["path"] = C.queue_dir(confine_repo) / confine_entry["file"]
    confine_entry["path"].parent.mkdir(parents=True, exist_ok=True)
    C.write_entry(confine_entry["path"], confine_entry)
    confine_env = {k: v for k, v in env.items()
                   if k not in ("XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS")}
    confine_env["PATH"] = f"{wrap_dir}{os.pathsep}{env.get('PATH', '')}"
    child = subprocess.Popen(
        [sys.executable, str(CAMPAIGN_PY), "tick", "--root", str(confine_repo),
         "--lock", str(confine_root / "queue.lock"), "--host", "local"],
        env=confine_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True)
    deadline = time.time() + 60
    while time.time() < deadline and not runner_pid_file.exists():
        time.sleep(0.1)
    check_true(runner_pid_file.exists(), "the confined runner starts")
    runner_pid = int(runner_pid_file.read_text() or 0)
    check(wrapped.read_text().splitlines(),
          ["systemd-run --user --scope -q --collect --unit "
           "peredur-001-confined-p0 -p MemoryMax=1G -p MemorySwapMax=0 "
           f"taskset -c 0 {sys.executable} {stub} --profile fretish-order-pairs "
           f"--jobs 1 --seeds 0 1 XDG=/run/user/{os.getuid()}",
           f"taskset -c 0 {sys.executable} {stub} --profile fretish-order-pairs "
           f"--jobs 1 --seeds 0 1 XDG=/run/user/{os.getuid()}"],
          "the tick launches the runner through the scope and taskset, "
          "giving systemd-run the user bus cron does not")
    # live_processes keys on comm and --profile. The stub is not named
    # run_experiments.py, so the two are read off ps directly: the wrapper
    # has exec'd away and left the runner's own comm and arguments.
    comm, _, ps_args = subprocess.run(
        ["ps", "-o", "comm=,args=", "-p", str(runner_pid)],
        capture_output=True, text=True).stdout.strip().partition(" ")
    check((comm.startswith("python"), C.profile_of_args(ps_args)),
          (True, "fretish-order-pairs"),
          f"status reads the wrapped runner by its profile ({comm} {ps_args})")
    ppid = subprocess.run(["ps", "-o", "ppid=", "-p", str(runner_pid)],
                          capture_output=True, text=True).stdout.strip()
    check(ppid, str(child.pid), "and the runner is still the tick's child")
    saved_repo_root = C.REPO_ROOT
    C.REPO_ROOT = confine_repo
    try:
        code, printed = dequeue(entry="001-confined.toml")
    finally:
        C.REPO_ROOT = saved_repo_root
        if child.poll() is None and code != 0:
            child.kill()
    check(code, 0, "dequeue stops a confined phase")
    check_true(str(runner_pid) in printed,
               f"killing the runner under the wrapper ({printed!r})")
    child.wait(timeout=30)
    deadline = time.time() + 30
    while time.time() < deadline and alive(runner_pid):
        time.sleep(0.1)
    check_true(not alive(runner_pid), "which dies with the tick")
    (control_dir / "sleep").unlink()
    calls.unlink() if calls.exists() else None

    # ── the tick generates the configs its phase needs ────────────────────────
    #
    # run_experiments.py exits 1 on a configs directory holding no .toml, and
    # the attended stage path has checked for that since aurus-h2h spent two of
    # its three attempts on it. The tick had no such check at all, so the same
    # failure reached the path with nobody watching.
    calls.unlink() if calls.exists() else None
    generated = repo / C.profile_configs_dir("fretish-order-pairs") / "default" / "x.toml"
    generated.unlink()
    entry = only_entry()
    entry.update({"state": "queued", "phase": 0, "attempts": 0,
                  "max_attempts": 3})
    C.write_entry(entry["path"], entry)
    code, printed = tick()
    check(code, 1, "a tick whose configs directory is empty runs no phase")
    check_true("no config files under" in only_entry()["last_error"],
               "and the entry names the directory, in the stage path's words")
    check_true("campaign.toml" in only_entry()["last_error"],
               "and says the declaration is where the command belongs")
    check_true(not calls.exists(),
               "with the runner never reached, so the attempts are not spent "
               "one tick at a time on a directory the tick can see is empty")
    generated.write_text("generations = 1\n")
    entry = only_entry()
    entry.update({"state": "queued", "attempts": 0})
    C.write_entry(entry["path"], entry)

    # ── the tick stages the checkout it needs ─────────────────────────────────
    #
    # An entry whose branch the checkout has left must not run its phase there:
    # the rows would come from code the campaign never declared. Until the tick
    # could stage, that meant burning attempts until somebody staged it by
    # hand, which is unusable for a queue holding two campaigns on two
    # branches. It now moves the checkout itself.
    (control_dir / "exit-code").unlink()
    calls.unlink() if calls.exists() else None
    git(repo, "checkout", "-q", default_branch)

    # Not while somebody's edits are in the tree. This is one of the two
    # refusals the tick keeps, because a reset here destroys work that no
    # fetch brings back.
    (repo / "dirty.txt").write_text("x")
    git(repo, "add", "dirty.txt")
    git(repo, "commit", "-q", "--no-verify", "-m", "tracked")
    git(repo, "push", "-q", "origin", "HEAD")
    (repo / "dirty.txt").write_text("edited")
    code, printed = tick()
    check(code, 1, "a dirty checkout is not staged out from under its edits")
    check_true("modified tracked file" in only_entry()["last_error"],
               "and the entry names what stopped it")
    check(C.checkout_branch(repo)[0], default_branch, "the branch did not move")
    check_true(not calls.exists(), "and nothing was run")
    git(repo, "checkout", "-q", "--", "dirty.txt")

    # Nor under a live run. Swapping the binary mid-campaign would leave every
    # row after it naming a commit it did not come from.
    FIXTURE_PROCESSES = [{"comm": "peredur", "profile": None}]
    entry = only_entry()
    entry.update({"state": "queued", "attempts": 0})
    C.write_entry(entry["path"], entry)
    code, printed = tick()
    check(code, 1, "a live run stops the staging too")
    check_true("live process" in only_entry()["last_error"], "and says which")
    check(C.checkout_branch(repo)[0], default_branch, "branch still not moved")
    FIXTURE_PROCESSES = []

    # --no-stage puts the old refusal back, for a host driven by hand.
    entry = only_entry()
    entry.update({"state": "queued", "attempts": 0})
    C.write_entry(entry["path"], entry)
    code, printed = tick(no_stage=True)
    check(code, 1, "--no-stage never moves the checkout")
    check_true("stage it first" in only_entry()["last_error"],
               "and says so in the words the tick used before it could stage")

    # Clean, idle, and the branch is the only thing in the way: it stages.
    entry = only_entry()
    entry.update({"state": "queued", "attempts": 0})
    C.write_entry(entry["path"], entry)
    code, printed = tick()
    check(code, 0, "an idle, clean checkout is staged onto the entry's branch")
    check(C.checkout_branch(repo)[0], "feat/queued", "which is where it lands")
    check(git(repo, "rev-parse", "HEAD"), only_entry()["commit"],
          "at the commit the entry froze, not wherever the branch now points")
    check_true("staging feat/queued" in printed, "and the tick says it did")
    check_true("--profile fretish-order-pairs" in calls.read_text(),
               "then runs the phase it was staged for")

    # ── a stage that moves the tick's own sources restarts the tick ───────────
    # The tick reads the declaration only after staging, and the running
    # process keeps the campaign.py it imported before the checkout moved: a
    # declaration using keys that older revision does not define fails to
    # parse, which reads as a bad declaration rather than as a stale parser.
    # aurus-curves spent an attempt on exactly that. The phase does not run in
    # the staging tick any more, the restarted one runs it.
    calls.unlink()
    git(repo, "checkout", "-q", "feat/queued")
    moved = repo / "scripts" / "campaign.py"
    moved.parent.mkdir(parents=True, exist_ok=True)
    moved.write_text("# a revision this tick has not read\n")
    git(repo, "add", "scripts/campaign.py")
    git(repo, "-c", "user.email=t@t", "-c", "user.name=t",
        "commit", "-q", "-m", "scripts move")
    moved_sha = git(repo, "rev-parse", "HEAD")
    git(repo, "checkout", "-q", default_branch)
    entry = only_entry()
    entry.update({"state": "queued", "phase": 0, "attempts": 0,
                  "commit": moved_sha})
    C.write_entry(entry["path"], entry)

    execs = []
    real_execve = os.execve
    os.execve = lambda path, argv, env: execs.append((path, argv, env))
    try:
        code, printed = tick()
    finally:
        os.execve = real_execve
    check(code, 0, "a restart is not a failure")
    check(len(execs), 1, "the tick re-execs itself once")
    check(execs[0][0], sys.executable, "through this interpreter")
    check(Path(execs[0][1][1]).name, "campaign.py",
          "running campaign.py from the staged checkout's own path")
    check(execs[0][2].get(C.TICK_RESTART_ENV), "1",
          "the restart marks itself, so it cannot loop")
    check_true(execs[0][2].get(C.TICK_LOCK_FD_ENV, "").isdigit(),
               "and hands the queue lock's descriptor on rather than "
               "dropping it for the length of the exec")
    check(calls.exists(), False,
          "the staging tick runs no phase: its own code is the stale half")
    entry = only_entry()
    check(entry["state"], "queued",
          "the entry is back in the queue for the restarted tick")
    check(entry["attempts"], 0, "and the restart spends no attempt")

    # Restarted once and the scripts moved again: it runs on what it has
    # rather than exec'ing a second time.
    git(repo, "checkout", "-q", default_branch)
    entry.update({"state": "queued", "phase": 0, "attempts": 0})
    C.write_entry(entry["path"], entry)
    execs.clear()
    os.environ[C.TICK_RESTART_ENV] = "1"
    os.execve = lambda path, argv, env: execs.append((path, argv, env))
    try:
        code, printed = tick()
    finally:
        os.execve = real_execve
        os.environ.pop(C.TICK_RESTART_ENV, None)
    check(len(execs), 0, "a tick that has already restarted does not again")
    check(code, 0, "and runs the phase on the sources it has")
    check_true("--profile fretish-order-pairs" in calls.read_text(),
               "which is the old behaviour, not a refusal")

    # The lock is adopted from the descriptor rather than taken again.
    check(C.adopted_lock(), None, "no descriptor named, no lock adopted")
    handle = C.acquire_lock(lock)
    # Duplicated, standing in for the descriptor a real restart inherits: the
    # exec'd process is new and owns the one it is handed, where here both
    # objects live in this process and would close the same descriptor twice.
    os.environ[C.TICK_LOCK_FD_ENV] = str(os.dup(handle.fileno()))
    adopted = C.adopted_lock()
    check_true(adopted is not None, "a named descriptor is adopted")
    check(os.environ.get(C.TICK_LOCK_FD_ENV), None,
          "and the variable is consumed, so a child cannot adopt it twice")
    adopted.close()
    handle.close()

    # A staging that cannot finish costs an attempt and holds the reason,
    # exactly as a failed phase does. A build that half ran is the case worth
    # catching: it leaves the previous commit's binary in place, and every row
    # the phase went on to write would name a commit it did not come from.
    calls.unlink()
    git(repo, "checkout", "-q", default_branch)
    entry = only_entry()
    entry.update({"state": "queued", "phase": 0, "attempts": 0,
                  "max_attempts": 3, "build": "false"})
    C.write_entry(entry["path"], entry)
    code, printed = tick()
    check(code, 1, "a build that fails fails the staging")
    check_true("build command failed" in only_entry()["last_error"],
               "and the entry holds the reason")
    check(only_entry()["attempts"], 1, "one attempt spent")
    check_true(not calls.exists(), "and the phase never ran")

    # The checkout landed before the build failed, so HEAD is already at the
    # entry's commit and only the binaries are stale. The next tick must still
    # build rather than take HEAD as proof: av3 ran a whole campaign's phase on
    # the previous commit's compare after exactly this.
    check(git(repo, "rev-parse", "HEAD"), only_entry()["commit"],
          "the failed staging left HEAD at the entry's commit")
    code, printed = tick()
    check(code, 1, "and the next tick rebuilds, which fails again")
    check_true("rebuilding feat/queued" in only_entry()["last_error"],
               "naming the rebuild as the step that failed")
    check(only_entry()["attempts"], 2, "spending a second attempt")
    check_true(not calls.exists(), "and still running no phase")

    entry = only_entry()
    entry.update({"state": "queued", "attempts": 0, "build": "true"})
    C.write_entry(entry["path"], entry)
    code, printed = tick()
    check(code, 0, "once the build passes, the phase runs")
    check_true("--profile fretish-order-pairs" in calls.read_text(), "on the rebuilt binary")
    calls.unlink()

    entry = only_entry()
    entry.update({"state": "queued", "attempts": 0, "build": "true",
                  "commit": "0" * 40})
    C.write_entry(entry["path"], entry)
    code, printed = tick()
    check(code, 1, "so does a commit no fetch can produce")
    check_true(not calls.exists(), "with nothing run")

    # ── two campaigns, two branches, one queue ────────────────────────────────
    #
    # The case the whole thing is for: a second campaign on a second branch,
    # queued behind the first, staged by the tick when its turn comes.
    entry = only_entry()
    entry.update({"state": "done", "commit": git(repo, "rev-parse",
                                                 "feat/queued")})
    C.write_entry(entry["path"], entry)
    calls.unlink() if calls.exists() else None

    git(repo, "checkout", "-q", "-b", "feat/other")
    write_declaration(repo, "other", OTHER_DECL)
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "--no-verify", "-m", "other campaign")
    with contextlib.redirect_stdout(io.StringIO()):
        code = C.cmd_enqueue(argparse.Namespace(
            campaign="other", host=["local"], max_attempts=2, again=False,
            dry_run=False))
    check(code, 0, "the second campaign is enqueued from its own branch")
    other_sha = git(repo, "rev-parse", "feat/other")
    # And the host goes back to where the first campaign left it, which is the
    # position that used to need `stage --force` and a human.
    git(repo, "checkout", "-q", "feat/queued")

    code, printed = tick()
    check(code, 0, "the tick takes the next entry and stages its branch")
    check(C.checkout_branch(repo)[0], "feat/other", "moving to it unattended")
    check(git(repo, "rev-parse", "HEAD"), other_sha, "at its frozen commit")
    entries = C.queue_entries(repo)
    check(len(entries), 2, "both entries are still on the host")
    check(entries[1]["state"], "done", "the second campaign ran to completion")
    check_true("--seeds 4 5" in calls.read_text(),
               "over the seeds its own declaration gives this host")

    rows = C.queue_rows([{"host": "local",
                          "queue": [C.entry_body(entries[1])]}])
    check(rows[0][3], f"feat/other@{other_sha[:7]}",
          "the queue table names the branch each entry needs")
    check(rows[0][4], "done", "beside its state")
    check(rows[0][5], "1/1", "and the next phase against the total")

    # ── a score phase in the queue ────────────────────────────────────────────
    #
    # The offline pass after the search, as a phase: the tick runs it in the
    # foreground under the lock exactly as a run phase, against the scorer
    # rather than the runner, and refuses it by name where the results
    # directory it would read is not on this host.
    scorer_dir = queue_root / "scorer"
    scorer_dir.mkdir()
    stub_scorer = scorer_dir / "stub_scorer.py"
    stub_scorer.write_text(STUB_RUNNER)
    scorer_calls = scorer_dir / "calls.txt"
    C.SCORER_CMD = f"{sys.executable} {stub_scorer}"
    calls.unlink() if calls.exists() else None

    git(repo, "checkout", "-q", "-b", "feat/scored")
    write_declaration(repo, "scored", SCORED_DECL)
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "--no-verify", "-m", "scored campaign")
    with contextlib.redirect_stdout(io.StringIO()):
        code = C.cmd_enqueue(argparse.Namespace(
            campaign="scored", host=["local"], max_attempts=3, again=False,
            dry_run=False))
    check(code, 0, "a campaign with a score phase is enqueued")
    git(repo, "checkout", "-q", "feat/other")

    def last_entry() -> dict:
        return C.queue_entries(repo)[-1]

    check(last_entry()["phase_seeds"], ["6-7", "6-7"],
          "the score phase's seeds are frozen beside the run phase's")
    code, printed = tick()
    check(code, 0, f"the run phase runs first: {printed}")
    check((last_entry()["state"], last_entry()["phase"]), ("queued", 1),
          "leaving the score phase queued")
    check_true("--profile fretish-order-pairs --jobs 1 --seeds 6 7" in calls.read_text(),
               "having run the search over this host's seeds")
    check_true(not scorer_calls.exists(), "and not the scorer")

    # The results directory the phase reads does not exist on this host: the
    # search above was a stub, and wrote nothing. A dry run says what it
    # would run and what blocks it, and spends nothing.
    code, printed = tick(dry_run=True)
    check(code, 0, "a dry run of the score phase exits 0")
    check_true(f"would run: {C.SCORER_CMD} "
               f"--results experiments/results-fretish-order-pairs "
               f"--out experiments/curves-fretish-order-pairs "
               f"--workers 2 --cores 1 --cuts 20 "
               f"--maximal-timeout 900 --compare-timeout 600 --deadline-s 10 "
               f"--wall-cap-s 910 --seeds 6 7" in printed,
               f"printing the scorer command it would run: {printed!r}")
    check_true("blocked: no results directory" in printed,
               "and the missing results directory that blocks it")
    check((last_entry()["state"], last_entry()["attempts"]), ("queued", 0),
          "without spending an attempt")

    code, printed = tick()
    check(code, 1, "a score phase whose results directory is missing fails")
    entry = last_entry()
    check_true("no results directory" in entry["last_error"]
               and "experiments/results-fretish-order-pairs" in entry["last_error"]
               and "curves" in entry["last_error"],
               f"with the directory and the phase named in the entry: "
               f"{entry['last_error']!r}")
    check((entry["state"], entry["attempts"], entry["phase"]),
          ("queued", 1, 1), "costing an attempt, not the phase")
    check_true(not scorer_calls.exists(),
               "and the scorer never reached, so the reason is the "
               "directory rather than an exit status")

    (repo / "experiments" / "results-fretish-order-pairs").mkdir(parents=True)
    entry.update({"state": "queued", "attempts": 0})
    C.write_entry(entry["path"], entry)
    code, printed = tick()
    check(code, 0, f"with the directory in place the score phase runs: "
                   f"{printed}")
    check(last_entry()["state"], "done", "and finishes the entry")
    check(scorer_calls.read_text().strip(),
          "--results experiments/results-fretish-order-pairs "
          "--out experiments/curves-fretish-order-pairs "
          "--workers 2 --cores 1 --cuts 20 --maximal-timeout 900 "
          "--compare-timeout 600 --deadline-s 10 --wall-cap-s 910 "
          "--seeds 6 7",
          "the scorer got every budget and this host's seeds, and nothing "
          "from the runner's vocabulary")
    check(calls.read_text().count("\n"), 1,
          "and the runner was not invoked a second time")

    # ── a compare phase in the queue ──────────────────────────────────────────
    #
    # Not seed-split: the host's own pair list, {host} expanded to the entry's
    # host, refused by name where that list is not on the host.
    cmp_dir = queue_root / "comparer"
    cmp_dir.mkdir()
    stub_cmp = cmp_dir / "stub_compare_pairs.py"
    stub_cmp.write_text(STUB_RUNNER)
    cmp_calls = cmp_dir / "calls.txt"
    C.COMPARE_PAIRS_CMD = f"{sys.executable} {stub_cmp}"
    git(repo, "checkout", "-q", "-b", "feat/compared")
    write_declaration(repo, "compared", """
name = "compared"
branch = "feat/compared"
build = "true"
hosts = { local = "" }

[[phases]]
kind = "compare"
name = "sub"
pairs = "experiments/p-{host}.csv"
out = "experiments/o-{host}.csv"
jobs = 2
wall_timeout = 9
""")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "--no-verify", "-m", "compared campaign")
    enqueued = io.StringIO()
    with contextlib.redirect_stdout(enqueued):
        code = C.cmd_enqueue(argparse.Namespace(
            campaign="compared", host=["local"], max_attempts=3, again=False,
            dry_run=False))
    check(code, 0, f"a campaign of a compare phase alone is enqueued: "
                   f"{enqueued.getvalue()}")
    check((last_entry()["seeds"], last_entry()["phase_seeds"]), ("", [""]),
          "with no seeds frozen")

    want_args = ("experiments/p-local.csv experiments/o-local.csv --jobs 2 "
                 "--black-timeout 300 --wall-timeout 9 --vmem-kb 8000000 "
                 "--manifest experiments/compare-manifests/compared.sub.json")
    code, printed = tick(dry_run=True)
    check(code, 0, f"a dry run of the compare phase exits 0: {printed}")
    check_true(f"would run: {C.COMPARE_PAIRS_CMD} {want_args}" in printed,
               f"printing the command it would run: {printed!r}")
    check_true("blocked: no pair list" in printed,
               "and the missing pair list that blocks it")
    check((last_entry()["state"], last_entry()["attempts"]), ("queued", 0),
          "without spending an attempt")

    code, printed = tick()
    check(code, 1, "a compare phase whose pair list is missing fails fast")
    entry = last_entry()
    check_true("no pair list" in entry["last_error"]
               and "p-local.csv" in entry["last_error"],
               f"naming the expanded path: {entry['last_error']!r}")
    check((entry["state"], entry["attempts"], entry["phase"]),
          ("queued", 1, 0), "costing an attempt")
    check_true(not cmp_calls.exists(), "with compare_pairs.py never reached")

    (repo / "experiments" / "p-local.csv").write_text("id,a_path,b_path\n")
    code, printed = tick()
    check(code, 0, f"with the pair list in place the phase runs: {printed}")
    check(last_entry()["state"], "done", "and finishes the entry")
    check(cmp_calls.read_text().strip(), want_args,
          "compare_pairs.py got the host's paths and every budget")

    (cmp_dir / "exit-code").write_text("1")
    entry = last_entry()
    entry.update({"state": "queued", "phase": 0, "attempts": 0})
    C.write_entry(entry["path"], entry)
    code, printed = tick()
    check((code, last_entry()["state"], last_entry()["phase"]),
          (1, "queued", 0),
          "a pass that exits non-zero, as a partial one does, is not done")
    (cmp_dir / "exit-code").unlink()

    # ── a fingerprint phase, then the compare phase it plans ─────────────────
    #
    # The fingerprint phase is refused by name where its committed inputs are
    # missing, and its pair list is what unblocks the compare phase after it.
    fp_dir = queue_root / "fingerprinter"
    fp_dir.mkdir()
    stub_fp = fp_dir / "stub_fingerprint_members.py"
    stub_fp.write_text(STUB_RUNNER)
    fp_calls = fp_dir / "calls.txt"
    C.FINGERPRINT_CMD = f"{sys.executable} {stub_fp}"
    cmp_calls.unlink()
    entry = last_entry()
    entry.update({"state": "done"})
    C.write_entry(entry["path"], entry)
    git(repo, "checkout", "-q", "-b", "feat/fingerprinted")
    write_declaration(repo, "fingerprinted", """
name = "fingerprinted"
branch = "feat/fingerprinted"
build = "true"
hosts = { local = "" }

[[phases]]
kind = "fingerprint"
name = "draw"
subjects = "experiments/fingerprinted/subjects-{host}.txt"
members = "experiments/fingerprinted/members"
work = "experiments/fpw"
out = "experiments/fpo"
pairs = "experiments/fpw/pairs-{host}.csv"
words = 64

[[phases]]
kind = "compare"
name = "sub"
pairs = "experiments/fpw/pairs-{host}.csv"
out = "experiments/fpo/sub-{host}.csv"
""")
    git(repo, "add", "experiments/fingerprinted/campaign.toml")
    git(repo, "commit", "-q", "--no-verify", "-m", "fingerprinted campaign")
    enqueued = io.StringIO()
    with contextlib.redirect_stdout(enqueued):
        code = C.cmd_enqueue(argparse.Namespace(
            campaign="fingerprinted", host=["local"], max_attempts=3,
            again=False, dry_run=False))
    check(code, 0, f"a fingerprint-then-compare chain is enqueued: "
                   f"{enqueued.getvalue()}")
    code, printed = tick()
    check(code, 1, "a fingerprint phase with no subject list fails fast")
    entry = last_entry()
    check_true("no subject list" in entry["last_error"]
               and "subjects-local.txt" in entry["last_error"],
               f"naming the expanded path: {entry['last_error']!r}")
    check((entry["attempts"], entry["phase"]), (1, 0), "costing an attempt")
    check_true(not fp_calls.exists(), "with the driver never reached")

    (repo / "experiments" / "fingerprinted" / "members").mkdir(parents=True)
    (repo / "experiments" / "fingerprinted" / "subjects-local.txt").write_text(
        "fsm\n")
    code, printed = tick()
    check(code, 0, f"with its inputs in place the fingerprint phase runs: "
                   f"{printed}")
    check((last_entry()["state"], last_entry()["phase"]), ("queued", 1),
          "and the entry moves on to the compare phase")
    check(fp_calls.read_text().strip(),
          "experiments/fingerprinted/subjects-local.txt "
          "experiments/fingerprinted/members experiments/fpw experiments/fpo "
          "--pairs experiments/fpw/pairs-local.csv --words 64 --seed 3 "
          "--workers 8 --black-timeout 30 "
          "--manifest experiments/compare-manifests/fingerprinted.draw.json",
          "the driver got the host's paths and every budget")
    code, printed = tick()
    check((code, "no pair list" in last_entry()["last_error"]), (1, True),
          "the compare phase waits on the pair list the stub never wrote")
    (repo / "experiments" / "fpw").mkdir(parents=True)
    (repo / "experiments" / "fpw" / "pairs-local.csv").write_text(
        "id,a_path,b_path,dirs\n")
    code, printed = tick()
    check((code, last_entry()["state"]), (0, "done"),
          f"and runs once it is there, finishing the chain: {printed}")
    check_true("experiments/fpw/pairs-local.csv experiments/fpo/sub-local.csv"
               in cmp_calls.read_text(),
               "with the pair list the fingerprint phase names")

    pool_dir = queue_root / "pooler"
    pool_dir.mkdir()
    stub_pool = pool_dir / "stub_pool_members.py"
    stub_pool.write_text(STUB_RUNNER)
    pool_calls = pool_dir / "calls.txt"
    C.POOL_CMD = f"{sys.executable} {stub_pool}"
    entry = last_entry()
    entry.update({"state": "done"})
    C.write_entry(entry["path"], entry)
    git(repo, "checkout", "-q", "-b", "feat/pooled")
    write_declaration(repo, "pooled", """
name = "pooled"
branch = "feat/pooled"
build = "true"
hosts = { local = "" }

[[phases]]
kind = "pool"
design = "experiments/pooled/design.toml"
subjects = "experiments/pooled/subjects-{host}.txt"
copy_from = "experiments/pooled/members"
out = "experiments/pooled-members"
""")
    git(repo, "add", "experiments/pooled/campaign.toml")
    git(repo, "commit", "-q", "--no-verify", "-m", "pooled campaign")
    enqueued = io.StringIO()
    with contextlib.redirect_stdout(enqueued):
        code = C.cmd_enqueue(argparse.Namespace(
            campaign="pooled", host=["local"], max_attempts=3, again=False,
            dry_run=False))
    check(code, 0, f"a pool campaign is enqueued: {enqueued.getvalue()}")
    code, printed = tick()
    check((code, "no design at" in last_entry()["last_error"],
           pool_calls.exists()), (1, True, False),
          "a pool phase without its design fails fast, the driver unreached")
    (repo / "experiments" / "pooled" / "members").mkdir(parents=True)
    (repo / "experiments" / "pooled" / "design.toml").write_text("draws = 1\n")
    (repo / "experiments" / "pooled" / "subjects-local.txt").write_text("fsm\n")
    code, printed = tick()
    check((code, last_entry()["state"]), (0, "done"),
          f"with its inputs in place the pool phase runs: {printed}")
    check(pool_calls.read_text().strip(),
          "experiments/pooled/design.toml experiments/pooled-members "
          "--subjects-file experiments/pooled/subjects-local.txt "
          "--copy-from experiments/pooled/members "
          "--manifest experiments/compare-manifests/pooled.pool.json",
          "the driver got the host's paths and the manifest")

    check_true("tick --host av1" in C.cron_line("av1", "/x"),
               "av1 gets a crontab line like the others")

    line = C.cron_line("av2", "/home/benandrew/projects/counter")
    check_true("tick --host av2" in line, "the crontab line names its host")
    # Regression: the line carried `flock -n` on the queue lock until
    # 2026-08-13, which deadlocked every tick against its own wrapper -- the
    # inherited descriptor denies acquire_lock's second open of the same path.
    # Both exit 0, so the only symptom was a queue that never started a phase.
    check_true("flock" not in line,
               "and does not wrap the tick in flock on its own lock file")
    check_true("*/5 * * * *" in line, "every five minutes")
    check_true("python3" in line,
               "with python3: av2's shell has no `python` at all")
finally:
    C.run_shell = REAL_RUN_SHELL
    shutil.rmtree(queue_root, ignore_errors=True)


# ── The configs check, locally ────────────────────────────────────────────────
#
# config_dirs_missing is the local twin of the CONFIGS_CHECK shell snippet the
# ssh stage path runs on the host, and has to answer the same question the same
# way. Both halves are load-bearing: gen_configs.py writes a directory per
# factor level and nothing at the top, so a check that did not recurse would
# call a full tree empty; and a directory that is not there at all is the state
# a host is in before anything generated anything, not an error.

configs_root = Path(tempfile.mkdtemp(prefix="campaign-configs-"))
try:
    nested = configs_root / "cfg" / "level" / "one.toml"
    nested.parent.mkdir(parents=True)
    nested.write_text("generations = 1\n")
    check(C.config_dirs_missing(configs_root, ["cfg"]), [],
          "a .toml under a subdirectory counts, as `find` counts it")
    check(C.config_dirs_missing(configs_root, ["absent"]), ["absent"],
          "a configs directory that does not exist is missing")
    empty = configs_root / "empty"
    empty.mkdir()
    (empty / "notes.txt").write_text("not a config\n")
    check(C.config_dirs_missing(configs_root, ["empty", "cfg"]), ["empty"],
          "and one holding no .toml is missing however much else is in it")

    log = configs_root / "configs.log"
    marker = configs_root / "ran"
    check(C.ensure_configs({"config_dirs": ["cfg"],
                            "configs": f"touch {marker}"},
                           configs_root, log), None,
          "configs already on the host need nothing generating")
    check_true(not marker.exists(),
               "and the command is not run: the check comes first, so a host "
               "staged by hand pays one directory scan and no generator")

    made = configs_root / "made" / "gen" / "config.toml"
    check(C.ensure_configs(
        {"config_dirs": ["made"],
         "configs": f"mkdir -p {made.parent} && touch {made} && "
                    f"touch {marker}"}, configs_root, log), None,
        "an empty directory is filled by the declared command")
    check_true(marker.exists() and made.is_file(),
               "which really ran, in a subshell, so both halves of an && line "
               "are watched")

    why = C.ensure_configs({"config_dirs": ["empty"], "configs": None},
                           configs_root, log)
    check_true(why is not None and "empty" in why and "campaign.toml" in why,
               "a campaign declaring no configs command fails naming the "
               "directory, in the words the stage path uses")

    why = C.ensure_configs({"config_dirs": ["nothing"], "configs": "false"},
                           configs_root, log)
    check_true(why is not None and "configs command failed" in why,
               "a generator that fails reports as itself")

    why = C.ensure_configs({"config_dirs": ["nothing"], "configs": "true"},
                           configs_root, log)
    check_true(why is not None and "left no config files" in why,
               "and one that exits 0 without writing a config is caught by "
               "the re-check, rather than by run_experiments.py an hour later")
finally:
    shutil.rmtree(configs_root, ignore_errors=True)


# ── Colour ────────────────────────────────────────────────────────────────────
#
# Two failures are worth guarding, and only one of them is visible. Escapes in
# a redirected stream are the loud one: `tick` runs from cron into
# $HOME/.peredur-queue.log and `status` is piped and captured, and the C++ side
# already carries a note about a status line that logged 59KB of escapes for
# 1.2KB of content. The silent one is alignment -- column widths come from
# len(cell), so an escape counted into a width shifts every column right of it
# and the table still looks like a table. Both are tested against the real
# rows, since a painter is only wrong on the cells it fires for.

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


class FakeStream(io.StringIO):
    """A stream that answers isatty() however it was told to."""

    def __init__(self, tty: bool):
        super().__init__()
        self.tty = tty

    def isatty(self) -> bool:
        return self.tty


TTY, PIPE = FakeStream(True), FakeStream(False)

check(C.colour_enabled(stream=TTY, env={}), True, "a terminal is coloured")
check(C.colour_enabled(stream=PIPE, env={}), False,
      "a redirected stream is not: this is what keeps the cron tick's log and "
      "every captured status readable")
check(C.colour_enabled(stream=PIPE, env={"CLICOLOR_FORCE": "1"}), True,
      "CLICOLOR_FORCE forces it back on down a pipe, for `less -R`")
check(C.colour_enabled(stream=PIPE, env={"CLICOLOR_FORCE": "0"}), False,
      "and 0 forces nothing")
check(C.colour_enabled(stream=TTY,
                       env={"NO_COLOR": "1", "CLICOLOR_FORCE": "1"}), False,
      "NO_COLOR wins over the terminal and over the force alike")
check(C.colour_enabled(stream=TTY, env={"NO_COLOR": ""}), True,
      "an empty NO_COLOR is an unset one, per no-color.org")
check(C.colour_enabled(no_color=True, stream=TTY,
                       env={"CLICOLOR_FORCE": "1"}), False,
      "--no-color beats everything, being the one thing typed on purpose")
check(C.colour_enabled(env={}), sys.stdout.isatty(),
      "and the stream defaults to stdout")

closed = open(os.devnull)
closed.close()
check(C.colour_enabled(stream=closed, env={}), False,
      "a closed stream raises rather than answering, and is not a terminal")
check(C.colour_enabled(stream=object(), env={}), False,
      "nor is a stream with no isatty at all")

# Fixtures chosen to fire every branch of every painter: a reachable host, an
# unreachable one, and one campaign per STATE.
COLOUR_REPORTS = [
    {"host": "av2", "reachable": True, "hostname": "av2", "branch": "feat/x",
     "head": "abc1234", "dirty": False, "hidden": 0,
     "processes": [{"comm": "peredur", "profile": "aurus-h2h"}],
     "campaigns": [dict(done, profile="aurus-h2h", state="running",
                        branch="feat/x", binary_commit="abc1234")],
     "queue": [
         {"file": "001-aurus-h2h.toml", "campaign": "aurus-h2h", "state": "running",
          "phase": 1, "phases": 2, "attempts": 1, "max_attempts": 3,
          "seeds": "0-99", "updated": "2026-08-17T10:00:00"},
         {"file": "002-x.toml", "campaign": "x", "state": "queued", "phase": 0,
          "phases": 1, "attempts": 0, "max_attempts": 3, "seeds": "0-9",
          "updated": "2026-08-17T09:00:00"},
         {"file": "003-y.toml", "campaign": "y", "state": "failed", "phase": 0,
          "phases": 1, "attempts": 3, "max_attempts": 3, "seeds": "0-9",
          "updated": "2026-08-17T09:00:00", "last_error": "phase exited 3"},
         {"file": "004-z.toml", "error": "unknown key `job`"}]},
    {"host": "av3", "reachable": True, "hostname": "av3", "branch": "main",
     "head": "deadbee", "dirty": True, "hidden": 0, "processes": [],
     "campaigns": [
         dict(done, profile="probe", state="stuck", branch="feat/other",
              binary_commit="b093374", dirty_binary=True, rows_from_csv=True),
         dict(done, profile="muc", state="done", branch="main",
              binary_commit="deadbee"),
         dict(done, profile="wellsep", state="stalled", branch="main",
              binary_commit="deadbee")],
     "queue": []},
    dict(unreachable, host="av4"),
    dict(unreachable, host="av5", error="timed out after 45s"),
]

STAGE_ROWS = [["av2", "feat/x", "abc1234", "abc1234", "staged"],
              ["av3", "?", "?", "?", "stage failed"],
              ["av4", "feat/x", "abc1234", "abc1234", "refused"],
              ["av5", "feat/x", "abc1234", "abc1234", "not confirmed"],
              ["av6", "feat/x", "abc1234", "abc1234", "BINARY MISMATCH"],
              ["av7", "main", "deadbee", "deadbee", "would stage (--force)"]]

COLOUR_TABLES = [
    ("checkout", C.CHECKOUT_HEADERS, C.checkout_rows(COLOUR_REPORTS),
     C.checkout_paint),
    ("campaign", C.HEADERS, C.status_rows(COLOUR_REPORTS), C.status_paint),
    ("queue", C.QUEUE_HEADERS, C.queue_rows(COLOUR_REPORTS), C.queue_paint),
    ("stage", C.STAGE_HEADERS, STAGE_ROWS, C.stage_paint),
]

try:
    for name, headers, rows, paint in COLOUR_TABLES:
        C.set_colour(False)
        plain = C.render_table(rows, headers, paint)
        check_true("\x1b" not in plain,
                   f"the {name} table carries no escape with colour off")
        C.set_colour(True)
        painted = C.render_table(rows, headers, paint)
        check_true("\x1b" in painted,
                   f"the {name} table is actually coloured with colour on, so "
                   f"the comparison below is not vacuous")
        # The property that breaks: colour is decoration over a fixed layout,
        # so removing it must give back the layout unchanged -- padding,
        # rstripped line ends and all.
        check(strip_ansi(painted), plain,
              f"the {name} table's columns must not move when it is coloured")

    # Every state the two STATE columns can hold is painted, or the colour is
    # saying nothing about the reading it is meant to make findable.
    C.set_colour(True)
    for state in ("done", "running", "stuck", "stalled"):
        check_true("\x1b" in C.status_paint(0, C.HEADERS.index("STATE"),
                                            state, C.STYLE),
                   f"campaign STATE {state} is coloured")
    for state in C.QUEUE_STATES:
        check_true("\x1b" in C.queue_paint(0, C.QUEUE_HEADERS.index("STATE"),
                                           state, C.STYLE),
                   f"queue STATE {state} is coloured")
    # The two marks the legend explains are warnings, and only the mark is
    # painted: the branch name beside it is not the warning.
    branch = C.status_paint(0, C.HEADERS.index("BRANCH"), "feat/x!", C.STYLE)
    check(strip_ansi(branch), "feat/x!", "the ! keeps its cell's width")
    check_true(branch.startswith("feat/x\x1b"), "and only the ! is painted")
    binary = C.status_paint(0, C.HEADERS.index("BINARY"), "b093374*", C.STYLE)
    check_true(binary.startswith("b093374\x1b"), "same for the * on BINARY")

    # A painter is not called at all with colour off, so a table pays nothing
    # for having one.
    def loud(row, col, cell, style):
        raise AssertionError("painter called with colour off")

    C.set_colour(False)
    check(C.render_table([["a", "b"]], ["H1", "H2"], loud), "H1  H2\na   b",
          "a painter is skipped entirely when colour is off")

    # print_status is the whole of `status`, three tables and the notes, and
    # the same property has to hold over the lot of it.
    def status_text() -> str:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            C.print_status(COLOUR_REPORTS)
        return buf.getvalue()

    C.set_colour(False)
    plain_status = status_text()
    C.set_colour(True)
    painted_status = status_text()
    check_true("\x1b" in painted_status, "print_status colours its tables")
    check(strip_ansi(painted_status), plain_status,
          "and the whole of `status` strips back to the uncoloured output")
finally:
    C.set_colour(False)

parser = C.build_parser()
for argv in (["status"], ["queue"], ["collect"], ["stage", "c"]):
    check(parser.parse_args(argv).no_color, False,
          f"`{argv[0]}` prints a table, so it takes --no-color")
    check(parser.parse_args(argv + ["--no-color"]).no_color, True,
          f"`{argv[0]} --no-color` turns it off by hand")
check_true(not hasattr(parser.parse_args(["tick", "--host", "av2"]),
                       "no_color"),
           "a verb that prints no table takes no such flag, and main() reads "
           "the flag with a default rather than assuming one")

# The wiring itself, through main(), which is the only thing that decides
# whether a real run is coloured. A stubbed verb reports what main set.
REAL_CMD_QUEUE = C.cmd_queue
SEEN: list = []


def colour_probe(args) -> int:
    SEEN.append(C.STYLE.enabled)
    return 0


C.cmd_queue = colour_probe
SAVED_ENV = {k: os.environ.pop(k) for k in ("NO_COLOR", "CLICOLOR_FORCE")
             if k in os.environ}
try:
    for argv, tty, want, why in [
            (["queue"], False, False,
             "a redirected `queue` is never coloured, whatever the terminal "
             "the run was started from"),
            (["queue"], True, True, "a terminal is"),
            (["queue", "--no-color"], True, False, "--no-color reaches main"),
            (["queue", "--json"], True, False,
             "and --json is never coloured, being parsed rather than read")]:
        SEEN.clear()
        old_argv, sys.argv = sys.argv, ["campaign.py", *argv]
        try:
            with contextlib.redirect_stdout(FakeStream(tty)):
                with contextlib.suppress(SystemExit):
                    C.main()
        finally:
            sys.argv = old_argv
        check(SEEN, [want], why)
finally:
    C.cmd_queue = REAL_CMD_QUEUE
    os.environ.update(SAVED_ENV)
    C.set_colour(False)


if FAILURES:
    print(f"\n{len(FAILURES)} campaign.py test(s) failed.")
    sys.exit(1)
print("All campaign.py tests passed.")
