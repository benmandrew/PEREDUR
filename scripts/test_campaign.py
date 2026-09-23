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
  "profile": "arbiter-probe",
  "hostname": "av3",
  "started": "2026-08-11T12:53:14+0100",
  "git": {"branch": "feat/arbiter-probe", "describe": "x",
          "head": "b093374f0a0d8e83d0a9fe3412e6935bffc5b078"},
  "binaries": {"peredur": {"commit_short": "b093374", "dirty": "0"}},
  "sweep": {"sweeps": ["R"], "specs": ["amba"], "seeds": [20, 21], "jobs": 1,
            "results_csv": "experiments/results-arbiter-probe.csv",
            "results_dir": "experiments/results-arbiter-probe"}
}"""

INVENTORY = f"""##PS
zsh -zsh
python3 python3 scripts/run_experiments.py --profile arbiter-probe --jobs 1
##HOST
av3
1786460000
##GIT
feat/arbiter-probe
b093374
0
##MANIFESTS
##FILE experiments/results-arbiter-probe-manifest-av3.json
{MANIFEST}
##ENDFILE
##END
"""

inv = C.parse_inventory(INVENTORY)
check(inv["hostname"], "av3", "hostname section")
check(inv["epoch"], 1786460000, "host clock sampled in the same call")
check(inv["branch"], "feat/arbiter-probe", "checkout branch")
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
check(c["profile"], "arbiter-probe", "profile from the manifest")
check(c["branch"], "feat/arbiter-probe", "launch branch from the manifest")
check(c["binary_commit"], "b093374", "binary commit from the manifest")
check(c["results_csv"], "experiments/results-arbiter-probe.csv", "csv path")

legacy = C.campaigns_from_manifests([{
    **inv["manifests"][0],
    "binaries": {"counter": {"commit_short": "0129e6c", "dirty": "1"}}}])
check(legacy[0]["binary_commit"], "0129e6c",
      "a manifest from before the rename keys the engine as counter")
check(legacy[0]["dirty_binary"], True, "and its dirty flag reads the same key")

check(C.plan_args(c),
      "--profile arbiter-probe --dry-run --sweeps R --specs amba --seeds 20 21",
      "plan args replay the manifest's own selection")

no_sweeps = dict(c, sweeps=None)
check_true("--sweeps" not in C.plan_args(no_sweeps),
           "a manifest recording every sweep must not pin one")

DETAIL = """##CAMPAIGN arbiter-probe
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
check(detail["arbiter-probe"]["rows_done"], 400, "done rows from the plan")
check(detail["arbiter-probe"]["rows_planned"], 400, "planned rows from the plan")
check(detail["arbiter-probe"]["csv_rows"], 400, "wc -l counts the header too")
check(detail["arbiter-probe"]["log_mtime"], 1786455673, "fractional mtime")
check_true("rows_from_csv" not in detail["arbiter-probe"],
           "a campaign with a plan is not on the fallback")
check(detail["empty-one"]["rows_done"], 0, "a header-only CSV has zero rows")
check_true("rows_planned" not in detail["empty-one"],
           "a profile the checkout no longer defines reports no plan")
check(detail["empty-one"]["rows_from_csv"], True,
      "and falls back to the file's length, marked as such")

# The CSV's length is not this launch's progress. Five of the profiles in
# merge_experiments.PROFILE_CSVS share three files, and a top-up relaunch
# rewrites the manifest to a couple of seeds while the file keeps every earlier
# row. Reading the file's length as done would report 400/40 here and, worse,
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

# Profiles really do share CSVs, which is what makes the whole-file count wrong
# rather than merely imprecise: results-ablate-tlsf.csv is written by two
# profiles and results-replicate.csv by three.
names = list(merge.PROFILE_CSVS.values())
shared = sorted({n for n in names if names.count(n) > 1})
check_true(shared, "PROFILE_CSVS still has profiles sharing a CSV")
check_true(sum(names.count(n) for n in shared) >= 4,
           "and several profiles on each, so the file's length is not one "
           "campaign's progress")


# ── Process detection ─────────────────────────────────────────────────────────

# The last line is this tool's own ssh command as the remote ps sees it: its
# text names run_experiments.py, and matching on the command line rather than
# on comm would report an idle machine as running.
PS = [
    "zsh -zsh",
    "sshd sshd: benandrew@notty",
    "zsh zsh -c cd /home/benandrew/projects/counter && python3 "
    "scripts/run_experiments.py --profile full --dry-run",
    "python3 python3 scripts/run_experiments.py --profile tlsf --jobs 4",
    "peredur /home/benandrew/projects/counter/build-release/peredur --input x",
]
procs = C.live_processes(PS)
check([p["comm"] for p in procs], ["python3", "peredur"],
      "only the runner and the engine, never the shell that names them")
check([p["comm"] for p in C.live_processes([
    "counter /home/benandrew/projects/counter/build-release/counter --input x"])],
    ["counter"], "an engine binary built before the rename is still an engine")
check(procs[0]["profile"], "tlsf", "the runner's profile is read off its args")

check(C.live_processes([
    "python3 python3 scripts/run_experiments.py --profile full --dry-run"]),
    [], "a --dry-run runner is a status probe, not a campaign")

check(C.profile_of_args("python3 scripts/run_experiments.py --jobs 4"), "full",
      "an unflagged runner is on the default profile")
check(C.profile_of_args("python3 x.py --profile=muc"), "muc", "--profile=VALUE")


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

# An aurus-score phase reports the same way, off a manifest of its own name.
# The sweep that finds it has to widen without catching anything else: a
# manifest missed here reads as a pass that never started.
AURUS_MANIFEST = """{
  "kind": "aurus-score",
  "pass": "wellsep",
  "hostname": "av2",
  "started": "2026-09-23T10:00:00+0100",
  "finished": null,
  "results": "experiments/results-aurus-rerun",
  "out": "experiments/wellsep-uncensored",
  "seeds": [0, 1],
  "jobs": 4,
  "budgets": {"ltlsynt_timeout": 60, "pattern": "*.tlsf",
              "fast_path": "off"},
  "git": {"branch": "campaign/aurus-rerun-anytime", "head": "beef123cafe"},
  "binaries": {"ltlsynt": {"path": "third_party/spot/bin/ltlsynt",
                           "version": "ltlsynt (spot) 2.15.1"}},
  "allow_stale_binary": false,
  "counts": {"queued": 40, "already_scored": 0, "scored": 0, "failed": 0}
}"""

ainv = C.parse_inventory(f"""##PS
python3 python3 scripts/aurus_score_campaign.py --pass wellsep --out experiments/wellsep-uncensored --seeds 0 1
##HOST
av2
1786460000
##GIT
campaign/aurus-rerun-anytime
beef123
0
##MANIFESTS
##SCOREMANIFESTS
##SFILE experiments/wellsep-uncensored/aurus-score-manifest-av2.json
{AURUS_MANIFEST}
##ENDSFILE
##END
""")
check(len(ainv["score_manifests"]), 1, "an aurus-score manifest is parsed")
check_true("aurus-score-manifest-*.json" in C.INVENTORY_SCRIPT
           and "score-manifest-*.json" in C.INVENTORY_SCRIPT,
           "because the inventory sweep looks for both manifest names")
aprocs = C.live_processes(ainv["ps"])
check((aprocs[0].get("kind"), aprocs[0].get("out"), aprocs[0]["profile"]),
      ("score", "experiments/wellsep-uncensored", None),
      "the AuRUS scorer is read as a scorer naming its output directory")
arec = C.campaigns_from_score_manifests(ainv["score_manifests"])[0]
check((arec["profile"], arec["label"], arec["rows_planned"]),
      ("wellsep-uncensored", "aurus-score:wellsep-uncensored", 40),
      "and its row is labelled for its kind, counted against its own queue")
check(arec["binary_commit"], "?",
      "a wellsep pass names no PEREDUR commit: ltlsynt carries none")
check(C.campaigns_from_score_manifests(
    [dict(json.loads(AURUS_MANIFEST), **{"pass": "anytime", "binaries": {
        "compare": {"commit_short": "abc1234", "dirty": "1"}}})])[0]
    ["binary_commit"], "abc1234",
    "an anytime pass names compare's, which decided its rows")


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
finally:
    merge.REPO_ROOT = real_merge_root
    shutil.rmtree(tmp, ignore_errors=True)

# ── TOML, in the subset the campaign files use ────────────────────────────────

# campaign.py parses TOML itself because tick runs on av2 and av3, whose
# python3 is 3.10 with no tomllib and no tomli. The risk that buys is a parser
# that quietly disagrees with every other reader of the same file, so each
# fixture below is checked against tomllib wherever tomllib exists.
importlib.reload(C)

DECLARATION = """
# The campaign this file declares.
name = "arbiter-probe"
branch = "feat/arbiter-probe"
profile = "arbiter-probe"
hosts = { av2 = "0-99", av3 = "100-199" }
phases = [ { profile = "arbiter-probe", jobs = 4 } ]
"""

HEADER_FORM = """
name = "two-phase"
branch = "feat/two-phase"
hosts = { av2 = "0-9" }

[[phases]]
name = "warm"
profile = "tlsf"
jobs = 2
specs = ["amba", "lily02"]

[[phases]]
profile = "muc"
jobs = 4
"""

try:
    import tomllib
except ImportError:  # 3.10, which is what the lab machines run.
    tomllib = None

for label, text in (("inline form", DECLARATION), ("header form", HEADER_FORM),
                    ("an entry", C.dump_toml(
                        {"campaign": "x", "state": "queued", "phase": 0,
                         "log": ['a "quoted" line', "b"], "done": True}))):
    parsed = C.parse_toml(text)
    if tomllib is not None:
        check(parsed, tomllib.loads(text),
              f"the built-in parser must agree with tomllib on {label}")

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
    ("a = 1.5", "a float, which nothing here uses"),
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
    # arbiter-probe's own profile lives on its branch, which is the normal case
    # and is why load_campaign checks against the runner rather than a list.
    write_declaration(decl_root, "probe", DECLARATION
                      .replace("arbiter-probe", "probe")
                      .replace('profile = "probe"', 'profile = "tlsf"'))
    campaign = C.load_campaign("probe", decl_root)
    check(campaign["branch"], "feat/probe", "branch from the file")
    check(campaign["hosts"]["av2"], list(range(100)), "a range expands")
    check(len(campaign["hosts"]["av3"]), 100, "and so does the other host's")
    check(campaign["phases"][0]["profile"], "tlsf", "the phase profile")
    check(campaign["build"], C.DEFAULT_BUILD_CMD, "the build command defaults")

    # `configs` is optional and has no default: there is no command that is
    # right for every campaign, and gen_configs.py run with the wrong flags is
    # worse than not run at all. The check that follows it is not optional,
    # which is the half that covers the declarations written before the key.
    check(campaign["configs"], None, "and configs defaults to no command")
    check(campaign["config_dirs"],
          [C.profile_configs_dir("tlsf")],
          "with the phase's own configs directory derived for the check")
    check_true(not Path(campaign["config_dirs"][0]).is_absolute(),
               "relative, since the absolute form names this machine's home "
               "directory and the check runs on somebody else's")

    write_declaration(decl_root, "gen", DECLARATION
                      .replace("arbiter-probe", "gen")
                      .replace('profile = "gen"', 'profile = "tlsf"')
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
phases = [ {{ profile = "full", jobs = 1 }} ]
{bad}
""")
        check_true("configs must be a non-empty string" in got,
                   f"{why} must be refused ({got!r})")

    # Every profile the phases name, not only the campaign-level default: the
    # second phase's configs directory is the one nothing else would check.
    write_declaration(decl_root, "twoprofiles", """
name = "twoprofiles"
branch = "feat/x"
hosts = { av2 = "0-1" }
phases = [ { profile = "full", jobs = 1 }, { profile = "tlsf", jobs = 1 } ]
""")
    two = C.load_campaign("twoprofiles", decl_root)
    check(two["config_dirs"],
          sorted({C.profile_configs_dir("full"), C.profile_configs_dir("tlsf")}),
          "a campaign straddling two profiles checks both configs directories")

    # The error worth the most noise. Overlapping ranges cost twice the machine
    # time and yield one row per key, so the campaign is smaller and slower
    # than it says while every table reads normal.
    overlap = declaration_error(decl_root, "overlap", """
name = "overlap"
branch = "feat/x"
hosts = { av2 = "0-99", av3 = "90-199" }
phases = [ { profile = "full", jobs = 1 } ]
""")
    check_true("share 10 seed(s)" in overlap and "av2" in overlap
               and "av3" in overlap,
               f"overlapping seed ranges must be refused by name: {overlap!r}")
    check_true("90" in overlap, "and must name a shared seed")

    for text, expect_in, why in (
        ('hosts = { av2 = "0-" }', "not a seed", "an open-ended range"),
        ('hosts = { av2 = "9-0" }', "backwards", "a range counting backwards"),
        ('hosts = { av2 = "amba" }', "not a seed", "a non-numeric range"),
        ('hosts = { av2 = "" }', "non-empty", "an empty range"),
        ('hosts = { av2 = "0-2,1-3" }', "repeats", "a range repeating itself"),
        ('hosts = { nowhere = "0-1" }', "unknown host", "an unknown host"),
    ):
        got = declaration_error(decl_root, "malformed", f"""
name = "malformed"
branch = "feat/x"
{text}
phases = [ {{ profile = "full", jobs = 1 }} ]
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
    check_true("full" in unknown, "and the known ones listed")

    # campaign.toml names a profile; it never redefines one. Eighteen archived
    # campaigns vendor their own copy of run_experiments.py, so a second copy
    # of that table here would be the nineteenth to keep in step.
    check(C.known_profiles(), set(__import__("run_experiments").PROFILES),
          "the profile list comes from the runner, not from a copy")

    for text, expect_in, why in (
        ('name = "wrong"\nbranch = "b"\nhosts = { av2 = "0" }\n'
         'phases = [ { profile = "full" } ]', "directory is",
         "a name that disagrees with its directory"),
        ('name = "decl"\nbranch = "b"\nhosts = { av2 = "0" }\n'
         'phases = []', "non-empty array", "no phases"),
        ('name = "decl"\nbranch = "b"\nhosts = { }\n'
         'phases = [ { profile = "full" } ]', "non-empty table", "no hosts"),
        ('name = "decl"\nbranch = "b"\nseeds = "0-9"\nhosts = { av2 = "0" }\n'
         'phases = [ { profile = "full" } ]', "unknown key",
         "a key nobody reads"),
        ('name = "decl"\nbranch = "b"\nhosts = { av2 = "0" }\n'
         'phases = [ { profile = "full", jobs = 0 } ]', "positive integer",
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

    # A phase may narrow the split. Two paths whose sample sizes come from
    # separate power calculations cannot share one range: the wider one
    # multiplies the narrower phase's row count without saying so.
    write_declaration(decl_root, "narrowed", """
name = "narrowed"
branch = "feat/x"
hosts = { av2 = "0-34", av3 = "35-69" }

[[phases]]
name = "wide"
profile = "full"
jobs = 4

[[phases]]
name = "narrow"
profile = "tlsf"
jobs = 1
hosts = { av2 = "0-12", av3 = "13-24" }

[[phases]]
name = "av2-only"
profile = "tlsf"
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
phases = [ { profile = "full", hosts = { av2 = "0-12", av3 = "10-24" } } ]
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
phases = [ { profile = "full", hosts = { av2 = "0-9", local = "10-19" } } ]
""")
    check_true("not declared at the campaign level" in added and "local" in added,
               f"a phase narrows the split; it cannot add a host: {added!r}")

    phase_bad = declaration_error(decl_root, "phase-bad-range", """
name = "phase-bad-range"
branch = "feat/x"
hosts = { av2 = "0-34" }
phases = [ { profile = "full", hosts = { av2 = "9-0" } } ]
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
profile = "tlsf"
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
    check(score["results"], C.profile_results_dir("tlsf"),
          "which scores the campaign profile's results directory by default")
    check(score["results"], "experiments/results-tlsf",
          "relative to the checkout, as the configs directory is")
    check(score["out"], "experiments/curves-tlsf",
          "and writes to curves-<stem> beside it")
    check(score["name"], "curves-tlsf", "named after its output by default")
    check({k: score[k] for k in C.SCORE_BUDGET_KEYS},
          {"workers": 8, "cores": 4, "cuts": 20, "maximal_timeout": 900,
           "compare_timeout": 600, "deadline_s": 4500, "wall_cap_s": 5400,
           "maximality": "on", "ideals": "on", "epsilon": "",
           "fingerprint_words": 256, "fingerprint_seed": 0},
          "every budget defaults to the scorer's own value, and the wall cap "
          "sits 900s past the deadline")
    check(C.score_defaults(),
          __import__("score_campaign").DEFAULTS,
          "the defaults come from score_campaign.py, not from a copy")
    check((old["results"], old["out"], old["name"], old["profile"]),
          ("experiments/results-old", "experiments/curves-again", "old",
           "tlsf"),
          "an explicit results and out are carried through")
    check({k: old[k] for k in C.SCORE_BUDGET_KEYS},
          {"workers": 2, "cores": 8, "cuts": 5, "maximal_timeout": 60,
           "compare_timeout": 30, "deadline_s": 100, "wall_cap_s": 1000,
           "maximality": "on", "ideals": "on", "epsilon": "",
           "fingerprint_words": 256, "fingerprint_seed": 0},
          "and so is every budget it states")
    check(old["hosts"], {"av2": list(range(5))},
          "a score phase narrows the split exactly as a run phase does")
    check(scored["config_dirs"], [C.profile_configs_dir("tlsf")],
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
        ('phases = [ { kind = "score", results = "x", specs = ["a"] } ]',
         "unknown key(s) specs on a score phase", "specs on a score phase"),
        ('phases = [ { profile = "full", workers = 2 } ]',
         "unknown key(s) workers on a run phase", "a budget on a run phase"),
        ('phases = [ { kind = "grade", profile = "full" } ]',
         "kind must be one of", "an unknown kind"),
        ('phases = [ { kind = "score", results = "x", workers = 0 } ]',
         "workers must be a positive integer", "zero workers"),
        ('phases = [ { kind = "score", results = "x", cores = true } ]',
         "cores must be an integer", "a boolean core count"),
        ('phases = [ { kind = "score", results = "x", deadline_s = "1" } ]',
         "deadline_s must be an integer", "a string deadline"),
        ('phases = [ { kind = "score", results = "" } ]',
         "results must be a non-empty string", "an empty results"),
        ('phases = [ { kind = "score", results = "x", out = 3 } ]',
         "out must be a non-empty string", "a numeric out"),
        ('phases = [ { kind = "score", results = "x", maximality = "yes" } ]',
         "maximality must be one of", "a maximality that is not on or off"),
        ('phases = [ { kind = "score", results = "x", epsilon = 3 } ]',
         "epsilon must be a string", "a numeric epsilon"),
        ('phases = [ { kind = "score", results = "x", '
         'fingerprint_seed = -1 } ]',
         "fingerprint_seed must be a non-negative integer",
         "a negative fingerprint seed"),
        ('phases = [ { kind = "score", results = "x", maximality = "off" } ]',
         "would score nothing", "a phase with both stages off"),
    ):
        got = declaration_error(decl_root, "badscore", f"""
name = "badscore"
branch = "feat/x"
hosts = {{ av2 = "0-1" }}
{text}
""")
        check_true(expect_in in got, f"{why} must be refused ({got!r})")

    # ── aurus-score phases ────────────────────────────────────────────────────
    #
    # A third kind, and the first with two passes under one name. The passes
    # share a shape and no budget at all, so what is worth guarding is that
    # each one's keys are refused on the other: a `compare_timeout` sitting
    # unread on a wellsep phase is a budget somebody expected to bind.
    write_declaration(decl_root, "aurusscored", """
name = "aurusscored"
branch = "feat/aurus"
profile = "tlsf"
hosts = { av2 = "0-14", av3 = "15-29" }

[[phases]]
kind = "aurus-score"
pass = "anytime"
results = "experiments/aurus-rerun-out"

[[phases]]
kind = "aurus-score"
pass = "wellsep"
name = "separated"
results = "experiments/results-aurus-rerun"
out = "experiments/wellsep-uncensored"
jobs = 8
ltlsynt_timeout = 120
pattern = "spec*.tlsf"
fast_path = "on"
hosts = { av2 = "0-4" }
""")
    aurus = C.load_campaign("aurusscored", decl_root)
    anytime, wellsep = aurus["phases"]
    check((anytime["kind"], anytime["pass"]), ("aurus-score", "anytime"),
          "an anytime phase parses as its own kind and pass")
    check((wellsep["kind"], wellsep["pass"]), ("aurus-score", "wellsep"),
          "and so does a wellsep one")
    check(anytime["out"], "experiments/anytime-rerun-out",
          "out defaults to <pass>-<stem> beside the tree it reads")
    check(anytime["name"], "anytime-rerun-out",
          "and the phase is named after its output by default")
    check({"jobs": anytime["jobs"],
           "compare_timeout": anytime["compare_timeout"]},
          {"jobs": 4, "compare_timeout": 3600},
          "every anytime budget defaults to the scorer's own value")
    check(C.aurus_defaults(), __import__("aurus_score_campaign").DEFAULTS,
          "which come from aurus_score_campaign.py, not from a copy")
    check(anytime["profile"], None,
          "an aurus-score phase inherits no campaign-level profile")
    check({k: wellsep[k] for k in ("name", "results", "out", "jobs",
                                   "ltlsynt_timeout", "pattern", "fast_path")},
          {"name": "separated", "results": "experiments/results-aurus-rerun",
           "out": "experiments/wellsep-uncensored", "jobs": 8,
           "ltlsynt_timeout": 120, "pattern": "spec*.tlsf",
           "fast_path": "on"},
          "and every wellsep key it states is carried through")
    check(wellsep["hosts"], {"av2": list(range(5))},
          "an aurus-score phase narrows the split as any other phase does")
    check(aurus["config_dirs"], [],
          "it adds no configs directory to the stage check")
    check(aurus["results_dirs"],
          {"av2": ["experiments/aurus-rerun-out",
                   "experiments/results-aurus-rerun"],
           "av3": ["experiments/aurus-rerun-out"]},
          "but both trees are checked, on the hosts their phase runs on")
    check(C.aurus_out_dir_for("experiments/results-aurus-rerun", "wellsep"),
          "experiments/wellsep-aurus-rerun",
          "the adapted tree's stem loses its results- prefix")

    for text, expect_in, why in (
        ('phases = [ { kind = "aurus-score", results = "x" } ]',
         "pass must be one of", "an aurus-score phase with no pass"),
        ('phases = [ { kind = "aurus-score", pass = "both", results = "x" } ]',
         "pass must be one of", "an unknown pass"),
        ('phases = [ { kind = "aurus-score", pass = "anytime" } ]',
         "results must be a non-empty string",
         "an aurus-score phase naming no tree"),
        ('phases = [ { kind = "aurus-score", pass = "anytime", '
         'results = "x", profile = "tlsf" } ]',
         "unknown key(s) profile on a aurus-score phase",
         "a profile on an aurus-score phase"),
        ('phases = [ { kind = "aurus-score", pass = "anytime", '
         'results = "x", sweeps = ["R"] } ]',
         "unknown key(s) sweeps on a aurus-score phase",
         "a run key on an aurus-score phase"),
        ('phases = [ { kind = "aurus-score", pass = "anytime", '
         'results = "x", specs = ["a"] } ]',
         "unknown key(s) specs on a aurus-score phase",
         "specs on an aurus-score phase"),
        ('phases = [ { kind = "aurus-score", pass = "anytime", '
         'results = "x", workers = 2, cuts = 5 } ]',
         "unknown key(s) cuts, workers on a aurus-score phase",
         "a score budget on an aurus-score phase"),
        ('phases = [ { kind = "aurus-score", pass = "anytime", '
         'results = "x", deadline_s = 100 } ]',
         "unknown key(s) deadline_s on a aurus-score phase",
         "a score deadline on an aurus-score phase"),
        ('phases = [ { kind = "aurus-score", pass = "anytime", '
         'results = "x", ltlsynt_timeout = 60 } ]',
         "unknown key(s) ltlsynt_timeout on a aurus-score phase",
         "a wellsep budget on an anytime phase"),
        ('phases = [ { kind = "aurus-score", pass = "wellsep", '
         'results = "x", compare_timeout = 60 } ]',
         "unknown key(s) compare_timeout on a aurus-score phase",
         "an anytime budget on a wellsep phase"),
        ('phases = [ { kind = "aurus-score", pass = "anytime", '
         'results = "x", jobs = 0 } ]',
         "jobs must be a positive integer", "zero jobs"),
        ('phases = [ { kind = "aurus-score", pass = "anytime", '
         'results = "x", compare_timeout = "1" } ]',
         "compare_timeout must be an integer", "a string budget"),
        ('phases = [ { kind = "aurus-score", pass = "wellsep", '
         'results = "x", fast_path = "yes" } ]',
         "fast_path must be one of", "a fast_path that is not on or off"),
        ('phases = [ { kind = "aurus-score", pass = "wellsep", '
         'results = "x", pattern = "" } ]',
         "pattern must be a non-empty string", "an empty pattern"),
        ('phases = [ { kind = "aurus-score", pass = "anytime", '
         'results = "x", out = 3 } ]',
         "out must be a non-empty string", "a numeric out"),
        ('phases = [ { profile = "full", pass = "anytime" } ]',
         "unknown key(s) pass on a run phase", "a pass on a run phase"),
        ('phases = [ { kind = "score", results = "x", pass = "anytime" } ]',
         "unknown key(s) pass on a score phase", "a pass on a score phase"),
    ):
        got = declaration_error(decl_root, "badaurus", f"""
name = "badaurus"
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

phase = {"name": "p", "profile": "tlsf", "jobs": 4, "sweeps": ["R"],
         "specs": None}
check(C.phase_args(phase, [0, 1, 2]),
      ["--profile", "tlsf", "--jobs", "4", "--sweeps", "R",
       "--seeds", "0", "1", "2"],
      "a phase becomes runner arguments, seeds last")
check_true(C.phase_command(phase, [0, 1]).startswith(C.RUNNER_CMD + " "),
           "and a run phase's command is the runner's")

score_phase = {"name": "curves", "kind": "score", "profile": None,
               "results": "experiments/results-x", "out": "experiments/curves-x",
               "workers": 3, "cores": 2, "cuts": 5, "maximal_timeout": 60,
               "compare_timeout": 30, "deadline_s": 100, "wall_cap_s": 1000,
               "maximality": "on", "ideals": "on", "epsilon": "",
           "fingerprint_words": 256, "fingerprint_seed": 0}
check(C.phase_args(score_phase, [0, 1]),
      ["--results", "experiments/results-x", "--out", "experiments/curves-x",
       "--workers", "3", "--cores", "2", "--cuts", "5",
       "--maximal-timeout", "60", "--compare-timeout", "30",
       "--deadline-s", "100", "--wall-cap-s", "1000",
       "--maximality", "on", "--ideals", "on", "--epsilon", "",
       "--fingerprint-words", "256", "--fingerprint-seed", "0",
       "--seeds", "0", "1"],
      "a score phase becomes scorer arguments, every budget stated, seeds last")
check(C.phase_command(score_phase, [0, 1]),
      C.SCORER_CMD + " --results experiments/results-x --out "
      "experiments/curves-x --workers 3 --cores 2 --cuts 5 "
      "--maximal-timeout 60 --compare-timeout 30 --deadline-s 100 "
      "--wall-cap-s 1000 --maximality on --ideals on --epsilon '' "
      "--fingerprint-words 256 --fingerprint-seed 0 --seeds 0 1",
      "and its command is the scorer's, not the runner's")
check(C.phase_launcher({"profile": "tlsf"}), C.RUNNER_CMD,
      "a phase record with no kind at all launches the runner")

anytime_phase = {"name": "anytime", "kind": "aurus-score", "pass": "anytime",
                 "profile": None, "results": "experiments/aurus-out",
                 "out": "experiments/anytime-out", "jobs": 6,
                 "compare_timeout": 1800}
check(C.phase_args(anytime_phase, [0, 1]),
      ["--pass", "anytime", "--results", "experiments/aurus-out",
       "--out", "experiments/anytime-out", "--jobs", "6",
       "--compare-timeout", "1800", "--seeds", "0", "1"],
      "an anytime phase becomes AuRUS scorer arguments, seeds last")
wellsep_phase = {"name": "wellsep", "kind": "aurus-score", "pass": "wellsep",
                 "profile": None, "results": "experiments/results-aurus",
                 "out": "experiments/wellsep-aurus", "jobs": 4,
                 "ltlsynt_timeout": 90, "pattern": "spec*.tlsf",
                 "fast_path": "off"}
check(C.phase_args(wellsep_phase, [7]),
      ["--pass", "wellsep", "--results", "experiments/results-aurus",
       "--out", "experiments/wellsep-aurus", "--jobs", "4",
       "--ltlsynt-timeout", "90", "--pattern", "spec*.tlsf",
       "--fast-path", "off", "--seeds", "7"],
      "and a wellsep phase states its own budgets and nothing else")
check_true(C.phase_command(wellsep_phase, [7, 8]).startswith(
    C.AURUS_SCORER_CMD + " "),
    "an aurus-score phase's command is the AuRUS scorer's")
check_true(C.phase_command(wellsep_phase, [7, 8]).endswith("--seeds 7 8"),
           "and the host's seeds reach the command line, never a typed range")

# The freeze has to reach the overrides. Freezing the campaign range alone
# would pin the phases that do not narrow it and leave every phase that does
# free to move under rows already written against the old split.
frozen_campaign = {
    "name": "frozen", "hosts": {"av2": list(range(35))},
    "phases": [
        {"name": "wide", "profile": "tlsf", "jobs": 4, "sweeps": None,
         "specs": None, "hosts": None},
        {"name": "narrow", "profile": "tlsf", "jobs": 1, "sweeps": None,
         "specs": None, "hosts": {"av2": list(range(13))}},
        {"name": "elsewhere", "profile": "tlsf", "jobs": 1, "sweeps": None,
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
            "scripts/gen_configs.py --tlsf --weakening both'")
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


# ── describe: deriving a closed campaign's declaration ────────────────────────
#
# The failure this guards is a plausible field. describe reports on campaigns
# nobody can re-run, so a derivation that quietly supplies a default -- a
# selection scheme for a CSV whose header predates the column, a host split for
# two files that are not one -- reads exactly like a recorded fact and is worth
# less than an absent field.

VENDORED_RUNNER = '''
PROFILES: dict[str, dict] = {
    "old": {"results_csv": EXPERIMENTS_DIR / "results-old.csv"},
    "wide": {"results_csv": EXPERIMENTS_DIR / "results-wide.csv"},
    "wide-twin": {"results_csv": EXPERIMENTS_DIR / "results-wide.csv"},
}
'''

WIDE_HEADER = ["sweep", "level_name", "level_value", "selection", "weakening",
               "metric", "repair_mode", "spec", "seed", "found_repair"]
# The 2026-07-10 vintage: no selection, weakening, metric or repair_mode
# column exists, so that campaign cannot have recorded one.
NARROW_HEADER = ["sweep", "level_name", "level_value", "spec", "seed",
                 "found_repair"]


def csv_rows(header: list, rows: list) -> str:
    return "".join(",".join(str(c) for c in line) + "\n"
                   for line in [header, *rows])


def make_archive(root: Path, name: str, files: dict) -> Path:
    archive = root / "experiments" / name
    (archive / "scripts").mkdir(parents=True, exist_ok=True)
    for relative, text in files.items():
        path = archive / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    return archive


def wide_rows(seeds, spec="amba", selection="nsga2", metric="direct"):
    return [["R", "elit0", "0", selection, "wkon", metric, "mono", spec, s, 1]
            for s in seeds]


back_root = Path(tempfile.mkdtemp(prefix="campaign-describe-"))
try:
    # Per-host CSVs are named av2.results-X.csv in five archives and
    # av2-results-X.csv in eight. Both must yield the same split.
    for label, sep in (("a dot", "."), ("a hyphen", "-")):
        archive = make_archive(back_root, "sep", {
            "scripts/run_experiments.py": VENDORED_RUNNER,
            "results-wide.csv": csv_rows(WIDE_HEADER, wide_rows(range(4))),
            f"av2{sep}results-wide.csv": csv_rows(WIDE_HEADER,
                                                  wide_rows([0, 1])),
            f"av3{sep}results-wide.csv": csv_rows(WIDE_HEADER,
                                                  wide_rows([2, 3])),
        })
        table = C.describe_campaign("sep", archive)
        check(table["hosts"], {"av2": "0-1", "av3": "2-3"},
              f"the host split comes back through {label} separator")
        shutil.rmtree(archive)

    archive = make_archive(back_root, "cross", {
        "scripts/run_experiments.py": VENDORED_RUNNER,
        "scripts/driver.sh": ("run_experiments.py --profile wide\n"
                              "run_experiments.py --profile old\n"),
        "PROVENANCE.json": json.dumps({
            "profile_commit": {"sha": "0123456789abcdef", "branch": "feat/x"},
            "kind": "A/B comparison"}),
        "results-wide.csv": csv_rows(
            WIDE_HEADER,
            wide_rows([0, 1], spec="amba")
            + wide_rows([0, 1], spec="lift", selection="weighted",
                        metric="log")),
        "av2-results-wide.csv": csv_rows(WIDE_HEADER, wide_rows([0])),
        "av3-results-wide.csv": csv_rows(WIDE_HEADER, wide_rows([1])),
        "results-old.csv": csv_rows(NARROW_HEADER,
                                    [["A", "gen5", "5", "fsm", 7, 1]]),
        # An analysis output with no merged counterpart is not a host split.
        "av2.well-separation-verdicts.csv": csv_rows(["seed"], [[9]]),
    })
    table = C.describe_campaign("cross", archive)
    check(table["attribution"], "inferred",
          "the output marks itself a reading of the archive")
    check(table["branch"], "feat/x", "the branch comes from PROVENANCE.json")
    check(table["derived_from"]["branch"],
          "PROVENANCE.json: profile_commit.branch",
          "and says which key it came from")
    names = [p["name"] for p in table["phases"]]
    check(names, ["wide", "old"],
          "the driver's --profile order orders the phases")
    check_true("phase_order" not in table["not_recorded"],
               "so phase order is recorded rather than omitted")
    check(table["derived_from"]["phase_order"], "scripts/driver.sh",
          "against the file that states it")

    wide, old = table["phases"]
    check(wide["profile"], "wide",
          "the CSV name breaks the tie between two profiles claiming it")
    check(wide["schemes"], ["nsga2", "weighted"], "every scheme that ran")
    check(wide["specs"], ["amba", "lift"], "every spec that ran")
    check(wide["metrics"], ["direct", "log"], "every metric that ran")
    check(wide["levels"], {"R": ["elit0"]}, "levels, grouped by their sweep")
    check(wide["rows"], 4, "the row count the claim rests on")
    check(wide["cross"], "results-wide.csv", "and the file it was read from")
    check(wide["hosts"], {"av2": "0", "av3": "1"},
          "the split for a phase whose hosts differ from the other's")
    check_true("hosts" not in table,
               "so no single split is claimed for the campaign")

    # The narrow header cannot express a scheme, so none is invented.
    check_true(not any(k in old for k in ("schemes", "weakenings", "metrics",
                                         "repair_modes")),
               "a column the header lacks yields no key at all")
    check(old["not_recorded"],
          ["schemes", "weakenings", "metrics", "repair_modes"],
          "and the file says so rather than defaulting them")
    check(old["profile"], "old", "a profile claimed by one entry is recorded")
    check(old["seeds"], "7", "seeds come from the column, not from a range")

    # merge_experiments.fill_defaults would have supplied these.
    check_true(merge.LEGACY_SELECTION not in str(old),
               "the legacy defaults must never reach a described campaign")

    # Two per-host files that overlap are not a seed split, whatever they are.
    make_archive(back_root, "overlap", {
        "results-wide.csv": csv_rows(WIDE_HEADER, wide_rows(range(4))),
        "av2-results-wide.csv": csv_rows(WIDE_HEADER, wide_rows([0, 1, 2])),
        "av3-results-wide.csv": csv_rows(WIDE_HEADER, wide_rows([2, 3])),
    })
    table = C.describe_campaign("overlap", back_root / "experiments"
                                / "overlap")
    check_true("hosts" not in table and "hosts" not in table["phases"][0],
               "overlapping per-host CSVs claim no split")
    check_true("hosts" in table["not_recorded"], "and say so")
    check_true("profile" in table["phases"][0]["not_recorded"],
               "an archive with no vendored runner records no profile")
    check_true("branch" in table["not_recorded"],
               "and one with no PROVENANCE.json records no branch")

    # No merged CSV at all: the soak and the engine comparison drive their own
    # scripts and never ran a factor cross.
    make_archive(back_root, "stub", {
        "PROVENANCE.json": json.dumps({"kind": "soak"}),
        "scripts/soak.py": "",
        "scripts/analyse_soak.py": "",
    })
    table = C.describe_campaign("stub", back_root / "experiments" / "stub")
    check_true("phases" not in table, "a stub declares no phases")
    check_true("phases" in table["not_recorded"], "and says why it has none")
    check(table["kind"], "soak", "carrying the kind the archive records")
    check(table["scripts"], ["analyse_soak.py", "soak.py"],
          "and the drivers it vendors")

    # jobs is nowhere in the archive: the one launch script in it passes no
    # --jobs, so every run took its profile's default_jobs, which is the
    # runner's value rather than the campaign's.
    check_true("jobs" in table["not_recorded"], "jobs is never recorded")

    # The verb is a query: it prints, and experiments/ is left as it was.
    before = sorted(p.name for p in
                    (back_root / "experiments" / "cross").iterdir())
    args = argparse.Namespace(campaigns=["cross"], all=False, json=False,
                              archive_root=str(back_root / "experiments"))
    with contextlib.redirect_stdout(io.StringIO()) as out:
        check(C.cmd_describe(args), 0, "describe prints a declaration")
    printed = out.getvalue()
    check(sorted(p.name for p in (back_root / "experiments"
                                  / "cross").iterdir()), before,
          "and writes nothing into the archive it read")
    check_true(not (back_root / "experiments" / "cross"
                    / "campaign.toml").exists(),
               "least of all a campaign.toml, which would be an anachronism")
    check_true('attribution = "inferred"' in printed,
               "the printed declaration carries its attribution")
    check_true("derived_from" in printed and "disjoint seed blocks" in printed,
               "and the per-field provenance a reader has to check")
    check(C.parse_toml(printed)["phases"][0]["schemes"], ["nsga2", "weighted"],
          "and it parses back as the TOML it looks like")

    args = argparse.Namespace(campaigns=["cross"], all=False, json=True,
                              archive_root=str(back_root / "experiments"))
    with contextlib.redirect_stdout(io.StringIO()) as out:
        check(C.cmd_describe(args), 0, "--json describes the same campaign")
    check(json.loads(out.getvalue())[0]["phases"][0]["schemes"],
          ["nsga2", "weighted"], "machine-readably")

    args = argparse.Namespace(campaigns=["never-archived"], all=False,
                              json=False,
                              archive_root=str(back_root / "experiments"))
    with contextlib.redirect_stdout(io.StringIO()):
        check(C.cmd_describe(args), 1, "a campaign with no archive fails")
finally:
    shutil.rmtree(back_root, ignore_errors=True)


# ── describe against the real archive ─────────────────────────────────────────
#
# The tracked half of every archive -- PROVENANCE.json and the vendored
# scripts -- is present in any checkout, so these run everywhere. The results
# CSVs are gitignored, so the checks that need one run only where it is.

ARCHIVE_ROOT = C.REPO_ROOT / "experiments"
archives = sorted(p.parent.name for p in
                  ARCHIVE_ROOT.glob("*/PROVENANCE.json"))
check_true(len(archives) >= 19,
           f"every closed campaign is describable ({len(archives)})")
for name in archives:
    table = C.describe_campaign(name, ARCHIVE_ROOT / name)
    check(table["name"], name, f"{name} describes")
    check(table["attribution"], "inferred", f"{name} says it was derived")
    check_true("jobs" in table["not_recorded"],
               f"{name} records no job count, because nothing recorded one")

elitism = ARCHIVE_ROOT / "2026-08-07-elitism"
order, source = C.driver_order(elitism)
check(order, ["elitism-fret", "elitism-tlsf"],
      "the elitism driver states its phase order, and nothing else does")
check(source, "scripts/campaign_driver.sh", "from the script that states it")

branch, source = C.provenance_branch(elitism)
check((branch, source), ("feat/elitism-campaign",
                         "PROVENANCE.json: profile_commit.branch"),
      "and its branch, from the key that holds it")

# arbiter-probe's branch value continues into prose after the branch name.
probe_branch, _ = C.provenance_branch(ARCHIVE_ROOT
                                      / "2026-08-10-arbiter-probe")
check(probe_branch, "feat/arbiter-probe",
      "only the branch name is taken, not the sentence around it")

by_csv = C.vendored_profile_csvs(ARCHIVE_ROOT / "2026-07-24-ablation")
check(sorted(by_csv["results-ablate-tlsf.csv"]), ["ablate-tlsf", "h2h-tlsf"],
      "two profiles claim one CSV in the ablation archive")
check(C.profile_of_csv("results-ablate-tlsf.csv", by_csv), "ablate-tlsf",
      "and the CSV name breaks the tie")
check(C.vendored_profile_csvs(ARCHIVE_ROOT / "2026-07-10-fretish-sweeps"), {},
      "the 2026-07-10 runner has no PROFILES dict at all")

ablate_csv = ARCHIVE_ROOT / "2026-07-24-ablation" / "results-ablate-fret.csv"
if ablate_csv.exists():
    cross = C.read_cross(ablate_csv)
    check(cross["rows"], 1280, "the ablation cross is 2x2x2x4x40 rows")
    check(sorted(cross["schemes"]), ["nsga2", "weighted"], "two schemes")
    check(sorted(cross["metrics"]), ["direct", "log"], "two metrics")
    check(len(cross["seeds"]), 40, "forty seeds")
    check(cross["not_recorded"], [], "and a header wide enough for all of it")
    table = C.describe_campaign("2026-07-24-ablation",
                                ARCHIVE_ROOT / "2026-07-24-ablation")
    check([p["profile"] for p in table["phases"]],
          ["ablate-fret", "ablate-tlsf"], "both of the ablation's phases")
    check_true("phase_order" in table["not_recorded"],
               "in the order of their CSV names, which nothing recorded")
    check(C.parse_toml(C.dump_campaign(table)), {k: v for k, v in
                                                 table.items()},
          "and the printed form round-trips through the parser")


# ── stage: the refusals ───────────────────────────────────────────────────────

PROBE = """##GIT
feat/arbiter-probe
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
##QFILE experiments/queue/001-arbiter-probe.toml
campaign = "arbiter-probe"
state = "queued"
##ENDQFILE
##END
"""
probe = C.parse_stage_probe(PROBE)
check(probe.branch, "feat/arbiter-probe", "the probe reads the branch")
check(probe.head[:7], "b093374", "and the full head")
check(probe.dirty, ["M src/main.cpp"], "and the modified files by name")
check([p["comm"] for p in probe.processes], ["peredur"],
      "and the live engine, keyed on comm as everywhere else")
check(probe.binary["commit_short"], "b093374", "and the binary's commit")
check(probe.queue[0]["campaign"], "arbiter-probe",
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


# ── stage and start, end to end against a throwaway checkout ──────────────────

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
phases = [ { profile = "full", jobs = 2 } ]
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
    configs_dir = repo / C.profile_configs_dir("full")
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
    generated = repo / C.profile_configs_dir("full") / "gen" / "made.toml"
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

    # start: the seed split comes from the declaration and from nowhere else.
    head = git(repo, "rev-parse", "HEAD")

    def start_args(**kwargs) -> argparse.Namespace:
        base = {"campaign": "fixture", "host": None, "dry_run": True,
                "ignore_queue": False}
        base.update(kwargs)
        return argparse.Namespace(**base)

    started = io.StringIO()
    with contextlib.redirect_stdout(started):
        code = C.cmd_start(start_args())
    printed = started.getvalue()
    check_true("--seeds 0 1" in printed and "--seeds 2 3" in printed,
               "each host is launched on its own declared seeds")
    check_true("--profile full" in printed, "with the declared profile")
    check(code, 0, "a staged host is ready to be launched on")

    # A score phase joins the chain like any other, after the run phase that
    # writes what it scores, on the same host seeds. The declaration is
    # edited in place and put back: start reads the working tree and refuses
    # on nothing a dirty tree would trip.
    write_declaration(repo, "fixture", FIXTURE_DECL.replace(
        'phases = [ { profile = "full", jobs = 2 } ]',
        'phases = [ { profile = "full", jobs = 2 }, '
        '{ kind = "score", profile = "full", workers = 2, cores = 1 } ]'))
    started = io.StringIO()
    with contextlib.redirect_stdout(started):
        code = C.cmd_start(start_args())
    printed = started.getvalue()
    check(code, 0, f"a campaign with a score phase is launchable: {printed}")
    chain = [ln for ln in printed.splitlines() if " && " in ln]
    check(len(chain), 2, "one chain per host")
    check_true(all(ln.strip().startswith(C.RUNNER_CMD) and
                   f" && {C.SCORER_CMD} --results experiments/results --out "
                   f"experiments/curves --workers 2 --cores 1 " in ln
                   for ln in chain),
               f"the runner first, then the scorer over the same results "
               f"directory: {chain}")
    check_true("--seeds 0 1 --seeds" not in printed
               and any("--seeds 0 1 && " in ln and ln.rstrip().endswith(
                   "--seeds 0 1") for ln in chain),
               "with av2's seeds on both of av2's phases")
    write_declaration(repo, "fixture", FIXTURE_DECL)

    # The runner's own freshness gate, asked one step early: a launch that
    # dies on the far side of a nohup leaves its message in a log nobody is
    # reading yet.
    binary.unlink()
    blocked = io.StringIO()
    with contextlib.redirect_stdout(blocked):
        code = C.cmd_start(start_args())
    check(code, 1, "a host with no binary is not launched on")
    check_true("no build-release/peredur" in blocked.getvalue(),
               "and the missing binary is what it names")
    binary.write_text(FAKE_BINARY)
    binary.chmod(0o755)

    fixture = {"branch": "feat/fixture", "name": "fixture"}
    stale = C.start_refusals(
        C.HostProbe(branch="feat/fixture", head=head,
                    binary={"commit": "b" * 40, "commit_short": "bbbbbbb",
                            "dirty": "0"}), fixture, head)
    check(len(stale), 1, "a binary from another commit is the only complaint")
    check_true("bbbbbbb" in stale[0], "and it names the commit it was built at")
    ready = C.HostProbe(branch="feat/fixture", head=head,
                        binary={"commit": head, "commit_short": head[:7],
                                "dirty": "0"})
    busy = C.start_refusals(
        replace(ready, processes=[{"comm": "python3", "profile": "tlsf"}]),
        fixture, head)
    check_true(any("already live" in r for r in busy),
               "a host already running a campaign is not launched on again")

    # A pending queue entry is a launch waiting to happen: starting beside it
    # is two runners resuming off one CSV as soon as the tick fires. Same
    # reasoning as enqueue's refusal of a second entry, so the same default.
    queued = replace(ready, queue=[{"file": "003-fixture.toml",
                                    "campaign": "fixture", "state": "queued"}])
    racing = C.start_refusals(queued, fixture, head)
    check(len(racing), 1, "a queued entry for this campaign refuses the start")
    check_true("003-fixture.toml" in racing[0] and "queued" in racing[0],
               f"naming the entry and its state: {racing}")
    check_true("--ignore-queue" in racing[0], "and the way past it")
    check(C.start_refusals(queued, fixture, head, ignore_queue=True), [],
          "which the override clears")
    running_entry = C.start_refusals(
        replace(ready, queue=[{"file": "003-fixture.toml",
                               "campaign": "fixture", "state": "running"}]),
        fixture, head)
    check_true(len(running_entry) == 1 and "running" in running_entry[0],
               "a running entry refuses on the same ground, named as running")
    for spent in ("done", "failed"):
        check(C.start_refusals(
            replace(ready, queue=[{"file": "003-fixture.toml",
                                   "campaign": "fixture", "state": spent}]),
            fixture, head), [],
            f"a {spent} entry is not pending, so it does not block a start")
    check(C.start_refusals(
        replace(ready, queue=[{"file": "003-other.toml", "campaign": "other",
                               "state": "queued"}]), fixture, head), [],
        "and another campaign's entry is not this campaign's race")

    # End to end, so the refusal and the override are exercised through the
    # probe rather than only against a constructed one.
    (repo / C.QUEUE_DIR).mkdir(parents=True, exist_ok=True)
    (repo / C.QUEUE_DIR / "007-fixture.toml").write_text(
        C.dump_toml({"campaign": "fixture", "host": "av2", "state": "queued"}))
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_start(start_args())
    check(code, 1, "a start beside a pending entry is refused")
    check_true("007-fixture.toml" in buffer.getvalue(), "and names the entry")
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = C.cmd_start(start_args(ignore_queue=True))
    check(code, 0, "--ignore-queue launches anyway")
    check_true("racing queue entry 007-fixture.toml" in buffer.getvalue(),
               "and still names what it is racing")
    shutil.rmtree(repo / C.QUEUE_DIR)

    chain = " && ".join(C.phase_command(p, [0, 1]) for p in
                        C.load_campaign("fixture", repo)["phases"])
    script = C.launch_script("/r", chain, "experiments/fixture/x.log")
    check_true("nohup" in script and "< /dev/null" in script,
               "the launch survives the ssh session that started it")
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
phases = [ { profile = "full", jobs = 1 }, { profile = "tlsf", jobs = 2 } ]
"""

OTHER_DECL = """
name = "other"
branch = "feat/other"
build = "true"
hosts = { local = "4-5" }
phases = [ { profile = "full", jobs = 1 } ]
"""

SCORED_DECL = """
name = "scored"
branch = "feat/scored"
build = "true"
hosts = { local = "6-7" }
phases = [
  { profile = "full", jobs = 1 },
  { kind = "score", profile = "full", workers = 2, cores = 1, deadline_s = 10 },
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

    for profile in ("full", "tlsf"):
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
    check_true("--profile full --jobs 1 --seeds 0 1 2 3"
               in calls.read_text(), "on the declared profile and seeds")

    code, printed = tick()
    check(code, 0, "the second tick runs the last phase")
    entry = only_entry()
    check(entry["state"], "done", "which finishes the entry")
    check_true("--profile tlsf --jobs 2" in calls.read_text(),
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

    # ── the tick generates the configs its phase needs ────────────────────────
    #
    # run_experiments.py exits 1 on a configs directory holding no .toml, and
    # the attended stage path has checked for that since aurus-h2h spent two of
    # its three attempts on it. The tick had no such check at all, so the same
    # failure reached the path with nobody watching.
    calls.unlink() if calls.exists() else None
    generated = repo / C.profile_configs_dir("full") / "default" / "x.toml"
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
    check_true("--profile full" in calls.read_text(),
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
    check_true("--profile full" in calls.read_text(),
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
    check_true("--profile full --jobs 1 --seeds 6 7" in calls.read_text(),
               "having run the search over this host's seeds")
    check_true(not scorer_calls.exists(), "and not the scorer")

    # The results directory the phase reads does not exist on this host: the
    # search above was a stub, and wrote nothing. A dry run says what it
    # would run and what blocks it, and spends nothing.
    code, printed = tick(dry_run=True)
    check(code, 0, "a dry run of the score phase exits 0")
    check_true(f"would run: {C.SCORER_CMD} --results experiments/results "
               f"--out experiments/curves --workers 2 --cores 1 --cuts 20 "
               f"--maximal-timeout 900 --compare-timeout 600 --deadline-s 10 "
               f"--wall-cap-s 910 --maximality on --ideals on --epsilon '' "
               f"--fingerprint-words 256 --fingerprint-seed 0 "
               f"--seeds 6 7" in printed,
               f"printing the scorer command it would run: {printed!r}")
    check_true("blocked: no results directory" in printed,
               "and the missing results directory that blocks it")
    check((last_entry()["state"], last_entry()["attempts"]), ("queued", 0),
          "without spending an attempt")

    code, printed = tick()
    check(code, 1, "a score phase whose results directory is missing fails")
    entry = last_entry()
    check_true("no results directory" in entry["last_error"]
               and "experiments/results" in entry["last_error"]
               and "curves" in entry["last_error"],
               f"with the directory and the phase named in the entry: "
               f"{entry['last_error']!r}")
    check((entry["state"], entry["attempts"], entry["phase"]),
          ("queued", 1, 1), "costing an attempt, not the phase")
    check_true(not scorer_calls.exists(),
               "and the scorer never reached, so the reason is the "
               "directory rather than an exit status")

    (repo / "experiments" / "results").mkdir(parents=True)
    entry.update({"state": "queued", "attempts": 0})
    C.write_entry(entry["path"], entry)
    code, printed = tick()
    check(code, 0, f"with the directory in place the score phase runs: "
                   f"{printed}")
    check(last_entry()["state"], "done", "and finishes the entry")
    check(scorer_calls.read_text().strip(),
          "--results experiments/results --out experiments/curves "
          "--workers 2 --cores 1 --cuts 20 --maximal-timeout 900 "
          "--compare-timeout 600 --deadline-s 10 --wall-cap-s 910 "
          "--maximality on --ideals on --epsilon  --fingerprint-words 256 "
          "--fingerprint-seed 0 --seeds 6 7",
          "the scorer got every budget and this host's seeds, and nothing "
          "from the runner's vocabulary")
    check(calls.read_text().count("\n"), 1,
          "and the runner was not invoked a second time")

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

# The relation sidecar. compare's verdict on every accumulated candidate is
# collapsed to the implying names and the rest dropped, so without this the
# only way back to it is the whole pass again. The distinction the file keeps
# is the one the docstring draws: an empty map is compare saying nothing
# implied an ideal, and no map at all is the question never asked.
import score_curves as SCV  # noqa: E402

relations_root = Path(tempfile.mkdtemp(prefix="campaign-relations-"))
try:
    curve = relations_root / "run_seed0.csv.part"
    check(SCV.relations_path(curve), relations_root / "run_seed0.relations.tsv",
          "the sidecar is named for the run, with .part and .csv stripped")
    check(SCV.relations_path(relations_root / "run_seed0.csv"),
          relations_root / "run_seed0.relations.tsv",
          "and lands in the same place once the curve is moved into position")
    check_true(not str(SCV.relations_path(curve)).endswith(".csv"),
               "never as a second .csv: status counts curves by globbing "
               "*.csv and collect --curves joins them, and a foreign header "
               "under that glob is caught by neither")
    SCV.write_relations(curve, {"b.tlsf": "strictly stronger",
                                "a.tlsf": "incomparable"})
    check(SCV.relations_path(curve).read_text(),
          "file\trelation\na.tlsf\tincomparable\nb.tlsf\tstrictly stronger\n",
          "every candidate's relation is kept, not just the implying ones")
    SCV.write_relations(curve, {})
    check(SCV.relations_path(curve).read_text(), "file\trelation\n",
          "and compare finding nothing is a header alone, not a missing file")
    check_true("run_seed0.relations.tsv" in [
        p.name for p in __import__("score_campaign").sidecar_paths(
            relations_root, Path("run_seed0"))],
        "the scorer unlinks it with the others when an attempt fails, so no "
        "reader sees a sidecar whose curve was thrown away")
finally:
    shutil.rmtree(relations_root, ignore_errors=True)

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


# ── aurus_score_campaign.py ───────────────────────────────────────────────────
#
# The AuRUS scorer, against two fake trees and a stub in place of each pass's
# script. The bookkeeping is what matters here for the same reason it does
# above -- a resume that re-scores a finished repeat, or skips a failed one,
# is invisible until the bill arrives -- and one thing more: the two passes
# read differently shaped trees that sit beside each other under experiments/,
# so a phase pointed at the wrong one has to say so rather than report an
# empty queue.

AURUS_CAMPAIGN_PY = Path(__file__).resolve().parent / "aurus_score_campaign.py"

STUB_PASS = '''#!/usr/bin/env python3
"""Stands in for either pass's script: writes rows, or fails as its unit says."""
import os, sys
out = sys.argv[sys.argv.index("--out") + 1]
target = sys.argv[-1]
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "calls.txt"), "a") as handle:
    handle.write(" ".join(sys.argv[1:]) + "\\n")
name = os.path.basename(os.path.dirname(target)) + "/" + os.path.basename(target)
if "fail" in name:
    sys.exit(3)
if "empty" in name:
    open(out, "w").close()
    sys.exit(0)
with open(out, "w") as handle:
    handle.write("spec,verdict\\n" + os.path.basename(target) + ",ok\\n")
print("scored", name)
# The undecided exit: a verdict the CSV carries, not a failed unit.
sys.exit(1 if "undecided" in name else 0)
'''

aurus_root = Path(tempfile.mkdtemp(prefix="campaign-aurus-"))
try:
    abins = aurus_root / "bin"
    abins.mkdir()
    real_head = git(AURUS_CAMPAIGN_PY.parent.parent, "rev-parse", "HEAD")
    fake_binary(abins / "compare", real_head)
    astub_dir = aurus_root / "stub"
    astub_dir.mkdir()
    astub = astub_dir / "stub_pass.py"
    astub.write_text(STUB_PASS)
    acalls = astub_dir / "calls.txt"

    # The raw AuRUS tree the anytime pass reads: <spec>/repeat-<NN>, one
    # repeat standing where a seed does. Solution counts chosen so the
    # smallest-first order differs from the alphabetical one.
    raw = aurus_root / "aurus-out"
    for spec, repeats in (("arbiter", ((0, 3), (1, 1), (9, 5))),
                          ("fail", ((0, 2),)), ("empty", ((1, 2),))):
        for repeat, solutions in repeats:
            unit = raw / spec / f"repeat-{repeat:02d}"
            unit.mkdir(parents=True)
            (unit / "run.log").write_text("")
            for i in range(solutions):
                (unit / f"spec{i}.tlsf").write_text("x")

    # The adapted tree the wellsep pass reads, as aurus_adapt.py leaves it.
    adapted = aurus_root / "results-aurus"
    for run, rows in (("aurus_arbiter_seed00", 3), ("aurus_amba_seed01", 1),
                      ("aurus_undecided_seed00", 2),
                      ("aurus_other_seed42", 1)):
        accumulated = adapted / run / "accumulated"
        accumulated.mkdir(parents=True)
        (accumulated / "index.tsv").write_text("header\n" + "row\n" * rows)
        (accumulated / "spec0.tlsf").write_text("x")
    (adapted / "notes.txt").write_text("not a run\n")

    def aurus(which_pass: str, results: Path, out: Path, *extra: str,
              seeds=("0", "1")) -> subprocess.CompletedProcess:
        env = dict(os.environ, PEREDUR_BIN_DIR=str(abins),
                   PEREDUR_ANYTIME_CMD=f"{sys.executable} {astub}",
                   PEREDUR_WELLSEP_CMD=f"{sys.executable} {astub}")
        return subprocess.run(
            [sys.executable, str(AURUS_CAMPAIGN_PY), "--pass", which_pass,
             "--results", str(results), "--out", str(out), "--seeds", *seeds,
             *extra],
            cwd=str(aurus_root), env=env, capture_output=True, text=True)

    # The shapes are checked, and each pass refuses the other's tree by name.
    # An empty queue would be the alternative, and that reads as a host whose
    # search has not run yet.
    proc = aurus("anytime", adapted, aurus_root / "any")
    check(proc.returncode, 2, "the anytime pass refuses the adapted tree")
    check_true("run.log" in proc.stderr and "repeat-" in proc.stderr,
               f"saying it needs the raw one, and why: {proc.stderr!r}")
    proc = aurus("wellsep", raw, aurus_root / "ws")
    check(proc.returncode, 2, "and the wellsep pass refuses the raw tree")
    check_true("accumulated" in proc.stderr,
               f"saying where its candidates live: {proc.stderr!r}")

    anyout = aurus_root / "anytime-out"
    proc = aurus("anytime", raw, anyout, "--dry-run", "--jobs", "1")
    check(proc.returncode, 0, f"a dry run exits 0: {proc.stderr}")
    check_true("4 unit(s)" in proc.stdout and "4 to score" in proc.stdout,
               f"and counts this host's repeats alone: {proc.stdout}")
    check_true("arbiter_repeat-09" not in proc.stdout,
               "another host's repeat is not queued here")
    check_true("--dirs <unit-dir>" in proc.stdout
               and "--timeout 3600" in proc.stdout,
               f"the command template is printed: {proc.stdout}")
    check_true(not anyout.exists() and not acalls.exists(),
               "and a dry run writes nothing and scores nothing")

    # The gate: compare from another commit refuses, and nothing is written.
    fake_binary(abins / "compare", "a" * 40)
    proc = aurus("anytime", raw, anyout, "--jobs", "1")
    check(proc.returncode, 1, "a stale compare refuses to score")
    check_true("STALE BINARY" in proc.stdout and "compare" in proc.stdout,
               f"naming the binary: {proc.stdout}")
    check_true(not anyout.exists(), "before writing anything")
    fake_binary(abins / "compare", real_head)

    proc = aurus("anytime", raw, anyout, "--jobs", "1")
    check(proc.returncode, 1, "a pass with failures exits 1")
    units = [ln.split()[-1] for ln in acalls.read_text().splitlines()]
    check([Path(u).parent.name + "/" + Path(u).name for u in units],
          ["arbiter/repeat-01", "empty/repeat-01", "fail/repeat-00",
           "arbiter/repeat-00"],
          "smallest first by solution count, ties on the name")
    check(sorted(p.name for p in anyout.glob("*.csv")),
          ["arbiter_repeat-00.csv", "arbiter_repeat-01.csv"],
          "the repeats that scored have their rows in place")
    check(list(anyout.glob("*.part")), [], "and no .part file is left behind")
    failures = {Path(ln.split()[1]).parent.name: ln.split()[0]
                for ln in (anyout / "failures.txt").read_text().splitlines()}
    check(failures, {"fail": "3", "empty": "0"},
          "an empty output is a failure, as is a non-zero exit")
    timings = [ln.split() for ln in
               (anyout / "timings.txt").read_text().splitlines()]
    check([t[3] for t in timings],
          ["arbiter_repeat-01", "empty_repeat-01", "fail_repeat-00",
           "arbiter_repeat-00"],
          "timings.txt has one line per attempt, in order")
    check({t[4] for t in timings}, {"compare_timeout=3600"},
          "carrying the budget the unit was scored under")
    check_true("scored arbiter/repeat-00" in
               (anyout / "warnings.log").read_text(),
               "and the scorers' output lands in warnings.log")

    amanifests = list(anyout.glob("aurus-score-manifest-*.json"))
    check(len(amanifests), 1, "one manifest, named for the host")
    amanifest = json.loads(amanifests[0].read_text())
    check((amanifest["kind"], amanifest["pass"]), ("aurus-score", "anytime"),
          "the manifest names the kind and the pass")
    check(amanifest["counts"],
          {"queued": 4, "already_scored": 0, "scored": 2, "failed": 2},
          "counts the pass")
    check(amanifest["seeds"], [0, 1], "records this host's seeds")
    check(amanifest["budgets"], {"compare_timeout": 3600},
          "every budget it ran under")
    check(amanifest["binaries"]["compare"]["commit"], real_head,
          "the binary that decided the rows")
    check(amanifest["git"]["head"], real_head, "the checkout's head")
    check(amanifest["allow_stale_binary"], False,
          "whether the gate was overridden")
    check_true(amanifest["finished"] is not None and amanifest["started"],
               "and both timestamps")
    check_true(amanifest["invocation"].endswith("--dirs <unit-dir>"),
               "and the invocation template")

    # Resume: the failures are retried, the rows already there are not.
    acalls.unlink()
    proc = aurus("anytime", raw, anyout, "--jobs", "2")
    check(proc.returncode, 1, "still failing, so still 1")
    retried = sorted(Path(ln.split()[-1]).parent.name
                     for ln in acalls.read_text().splitlines())
    check(retried, ["empty", "fail"],
          "a rerun scores only the repeats with no rows")
    check(json.loads(amanifests[0].read_text())["counts"]["already_scored"], 2,
          "and the manifest says which were already there")

    # Every unit present: exit 0, so a tick moves the entry on.
    (raw / "fail").rename(raw / "fixed")
    (raw / "empty").rename(raw / "full")
    proc = aurus("anytime", raw, anyout, "--jobs", "2")
    check(proc.returncode, 0, f"a complete pass exits 0: {proc.stdout}")
    check_true("4/4 present" in proc.stdout, "saying so")

    # The wellsep pass: its own tree, its own budgets, and exit 1 from
    # check_well_separated.py is an undecided verdict rather than a failure.
    acalls.unlink()
    wsout = aurus_root / "wellsep-out"
    proc = aurus("wellsep", adapted, wsout, "--jobs", "2",
                 "--ltlsynt-timeout", "30", "--pattern", "spec*.tlsf",
                 "--fast-path", "on")
    check(proc.returncode, 0, f"an undecided verdict is not a failure: "
                              f"{proc.stdout} {proc.stderr}")
    check(sorted(p.name for p in wsout.glob("*.csv")),
          ["aurus_amba_seed01.csv", "aurus_arbiter_seed00.csv",
           "aurus_undecided_seed00.csv"],
          "one CSV per run directory on this host's seeds")
    check((wsout / "failures.txt").read_text(), "",
          "and nothing in failures.txt")
    targets = [ln.split()[-1] for ln in acalls.read_text().splitlines()]
    check_true(all(Path(t).name == "accumulated" for t in targets),
               f"the scorer is pointed at the candidates, not the run: "
               f"{targets}")
    call = acalls.read_text().splitlines()[0]
    check_true("--ltlsynt-timeout" not in call and "--timeout 30" in call
               and "--pattern spec*.tlsf" in call and "--fast-path" in call
               and "--jobs 1" in call,
               f"with every budget stated in the scorer's own spelling: "
               f"{call}")
    wsmanifest = json.loads(
        next(wsout.glob("aurus-score-manifest-*.json")).read_text())
    check(wsmanifest["budgets"],
          {"ltlsynt_timeout": 30, "pattern": "spec*.tlsf", "fast_path": "on"},
          "and the manifest records them")
    check("compare" in wsmanifest["binaries"], False,
          "the wellsep pass runs no PEREDUR binary")
    check_true("ltlsynt" in wsmanifest["binaries"],
               "and names the third-party one it did run")

    # Nothing to score is never a finished pass.
    proc = aurus("wellsep", adapted, wsout, "--seeds", "7", seeds=("7",))
    check(proc.returncode, 2, "a queue with no repeat on this host's seeds is "
                              "an error")
    check_true("nothing to score" in proc.stderr,
               f"saying so: {proc.stderr!r}")

    cpus = os.cpu_count()
    if cpus is not None:
        proc = aurus("wellsep", adapted, wsout, "--jobs", str(cpus + 1))
        check(proc.returncode, 2, "more jobs than the host has CPUs is "
                                  "refused at startup")

    for bad, why in ((["--jobs", "0"], "zero jobs"),
                     (["--compare-timeout", "-1"], "a negative compare budget"),
                     (["--ltlsynt-timeout", "0"], "a zero ltlsynt budget"),
                     (["--pass", "both"], "an unknown pass")):
        proc = aurus("anytime", raw, anyout, *bad)
        check(proc.returncode, 2, f"{why} is an argument error")
    proc = subprocess.run(
        [sys.executable, str(AURUS_CAMPAIGN_PY), "--pass", "anytime",
         "--results", str(aurus_root / "absent"), "--out", str(anyout),
         "--seeds", "1"], capture_output=True, text=True)
    check_true(proc.returncode != 0 and "no results directory" in proc.stderr,
               "a missing tree is refused by name")

    # The whole point of the re-run, end to end through the real scorer: a
    # repeat written by the fork carries solution-times.csv and is dated from
    # it to the microsecond, and one written by upstream AuRUS falls back to
    # the run.log's per-iteration #Sol column, rounded to the second. The
    # `dated_by` column is what tells the two apart afterwards, so it has to
    # reach the pass's own output and not only the reader.
    vintages = aurus_root / "vintages"
    for repeat, has_times in (("repeat-00", True), ("repeat-01", False)):
        unit = vintages / "arbiter" / repeat
        unit.mkdir(parents=True)
        for i in range(2):
            (unit / f"spec{i}.tlsf").write_text("x")
        # Two iterations, the second reaching #Sol 2, dated 0m 41s.
        (unit / "run.log").write_text(
            "1\t0.5\tx\t0\t1\nElapsed Time: 0 m 17 s\n"
            "2\t0.5\tx\t0\t2\nElapsed Time: 0 m 41 s\n")
        if has_times:
            (unit / "solution-times.csv").write_text(
                "solution,seconds,generation,fitness,timestamp\n"
                "spec0.tlsf,12.345678,1,0.5,t0\n"
                "spec1.tlsf,40.987654,-1,0.5,t1\n")
    vex = aurus_root / "examples" / "arbiter" / "fixes"
    vex.mkdir(parents=True)
    (vex / "fix1.tlsf").write_text("x")
    # One binary answering both probes: the freshness gate's --version, and
    # the scorer's own --repairs/--ideals call.
    (abins / "compare").write_text(
        f'#!/bin/sh\ncase "$1" in --version) echo commit={real_head}; '
        f'echo commit_short={real_head[:7]}; echo dirty=0;; '
        f'*) echo "spec0.tlsf : equivalent to fix1.tlsf"; '
        f'echo "spec1.tlsf : incomparable with fix1.tlsf";; esac\n')
    (abins / "compare").chmod(0o755)
    vout = aurus_root / "vintage-out"
    proc = subprocess.run(
        [sys.executable, str(AURUS_CAMPAIGN_PY), "--pass", "anytime",
         "--results", str(vintages), "--out", str(vout),
         "--seeds", "0", "1", "--jobs", "1"],
        cwd=str(aurus_root),
        env=dict(os.environ, PEREDUR_BIN_DIR=str(abins),
                 PEREDUR_EXAMPLES_DIR=str(aurus_root / "examples")),
        capture_output=True, text=True)
    check(proc.returncode, 0,
          f"the real scorer runs under the pass: {proc.stdout} {proc.stderr}")
    with (vout / "arbiter_repeat-00.csv").open() as handle:
        forked = {r["index"]: r for r in csv.DictReader(handle)}
    check({r["dated_by"] for r in forked.values()}, {"solution-times"},
          "a repeat carrying solution-times.csv is dated from it")
    check((forked["0"]["found_elapsed_s"], forked["1"]["found_elapsed_s"]),
          ("12.345678", "40.987654"),
          "to the microsecond, which is what the re-run exists to keep")
    check(forked["1"]["found_iter"], "-1",
          "with the fork's own generation, -1 for a post-loop confirmation")
    with (vout / "arbiter_repeat-01.csv").open() as handle:
        logged = {r["index"]: r for r in csv.DictReader(handle)}
    check({r["dated_by"] for r in logged.values()}, {"run-log"},
          "a repeat with only a run.log falls back to the iteration series")
    check((logged["0"]["found_elapsed_s"], logged["1"]["found_elapsed_s"]),
          ("17", "41"), "rounded to the second, as that log records it")
    check_true("dated_by" in
               (vout / "arbiter_repeat-00.csv").read_text().splitlines()[0],
               "and the column reaches the pass's output, not just the reader")
finally:
    shutil.rmtree(aurus_root, ignore_errors=True)


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
     "processes": [{"comm": "peredur", "profile": "tlsf"}],
     "campaigns": [dict(done, profile="tlsf", state="running",
                        branch="feat/x", binary_commit="abc1234")],
     "queue": [
         {"file": "001-tlsf.toml", "campaign": "tlsf", "state": "running",
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
START_ROWS = [["av2", "0-99", "12345", "launched"],
              ["av3", "100-199", "-", "dry run"],
              ["av4", "200-299", "-", "blocked"],
              ["av5", "300-399", "-", "failed: no pid returned"]]

COLOUR_TABLES = [
    ("checkout", C.CHECKOUT_HEADERS, C.checkout_rows(COLOUR_REPORTS),
     C.checkout_paint),
    ("campaign", C.HEADERS, C.status_rows(COLOUR_REPORTS), C.status_paint),
    ("queue", C.QUEUE_HEADERS, C.queue_rows(COLOUR_REPORTS), C.queue_paint),
    ("stage", C.STAGE_HEADERS, STAGE_ROWS, C.stage_paint),
    ("start", C.START_HEADERS, START_ROWS, C.start_paint),
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
for argv in (["status"], ["queue"], ["collect"], ["stage", "c"],
             ["start", "c"]):
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
