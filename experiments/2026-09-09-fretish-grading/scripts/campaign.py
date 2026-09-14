#!/usr/bin/env python3
"""Drive experiment campaigns on the lab machines.

    python scripts/campaign.py status            # one table of live state
    python scripts/campaign.py status --json     # the same, machine-readable
    python scripts/campaign.py collect --profile tlsf --dry-run
    python scripts/campaign.py collect --profile tlsf
    python scripts/campaign.py stage arbiter-probe --dry-run
    python scripts/campaign.py start arbiter-probe
    python scripts/campaign.py enqueue arbiter-probe
    python scripts/campaign.py queue
    python scripts/campaign.py tick --host av2
    python scripts/campaign.py cron --print --host av2
    python scripts/campaign.py describe --all --json

A campaign is declared once, in ``experiments/<name>/campaign.toml``: the
branch it runs on, the seed range each host takes, and the phases to run. That
file is the only place a seed split is written down, so the split cannot differ
between the launch, a resume and the merge. ``stage``, ``start`` and ``enqueue``
all read it and none of them accepts a seed range as an argument.

``status`` is read-only. It asks each host for its launch manifests
(``experiments/<stem>-manifest-<host>.json``, written by run_experiments.py),
its git state, its process table and the mtimes underneath each campaign's
result directory, then asks ``run_experiments.py --dry-run`` on that same host
how many rows the launch's selection plans. Nothing here re-derives a sweep:
the manifest says what the launch asked for and the runner says how many rows
that expands to, so a profile edit cannot make this script disagree with the
runner about the size of a campaign.

``collect`` wraps merge_experiments.py rather than reimplementing it -- the
rsync, the natural key and the merge all come from that module -- and adds the
verification the manual step never had: row counts per host against the merged
total, and a duplicate-key scan.

``stage`` prepares a host and refuses by default: a dirty checkout, a live
run, or a branch other than the campaign's each block it, because the recovery
from a wrong ``git checkout -f`` there is somebody's unpushed work.

``enqueue`` and ``tick`` are the unattended path. A queue entry lives on the
host that will run it, under ``experiments/queue/NNN-<name>.toml``, and a cron
tick takes the lowest-numbered queued entry and runs its next phase under a
lock. Nothing polls: the tick is the only thing that watches a run.

A queue holds campaigns on different branches, so a tick stages the checkout
onto the branch and commit its entry froze at enqueue, then rebuilds. It does
that for the branch alone. A dirty checkout and a live run still stop it dead,
because those are the two things a reset destroys and nothing here can bring
back; ``stage --force`` remains the only way past either, and it wants a human
at a terminal.

``describe`` is the archive's half of the same idea, for the campaigns that
closed before any of this existed. It derives a declaration from what the
archive already carries -- the merged results CSV, the per-host CSVs and
PROVENANCE.json -- and prints it. It writes nothing: the derivation's sources
sit in the directory it read, and a campaign is reproduced from its vendored
``scripts/`` at a commit where this file does not exist.

Hosts are reached by their ssh-config alias (``av2``), not by the FQDN in
merge_experiments.REMOTES: the FQDN form falls through to password auth under
BatchMode, which a status poll cannot answer.
"""

import argparse
import ast
import errno
import fcntl
import getpass
import json
import os
import re
import shlex
import string
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import merge_experiments as merge  # noqa: E402

REPO_ROOT = Path(__file__).parent.parent

# The lab machines, by ssh-config alias. Bare aliases deliberately: see the
# module docstring. The repo path on each is merge_experiments.REMOTE_ROOT.
HOSTS: tuple[str, ...] = ("av2", "av3")
LOCAL = "local"

SSH_OPTS = ["-o", "ConnectTimeout=8", "-o", "BatchMode=yes"]
SSH_TIMEOUT_S = 45
# collect --dry-run's measure step walks a whole result tree with du and find.
# On a cold cache over the 29GB experiments dir that outlasts the status
# deadline by a wide margin, and a status deadline applied here reports a
# perfectly healthy machine as unreachable.
MEASURE_TIMEOUT_S = 600

# How long a campaign's newest run.log may go untouched before a live process
# stops corroborating it. Bounded by the harness's own limits rather than
# guessed: no single run outlasts them, and the largest any profile allows is
# a 3600s counter timeout (gen40, and the largest timeout_caps entry) plus an
# 1800s compare timeout, so 90 minutes bounds one run's silence. Doubled, for
# the granularity of the log writes and for a jobs=1 host, where the next
# run.log only appears once the previous run has finished.
STALE_RUN_S = 3 * 3600

# av2's non-interactive ssh shell has no `python` on PATH, only `python3`.
REMOTE_PYTHON = "python3"

# Marker protocol for the remote scripts. Chosen over one-shot JSON because the
# remote side has to stay a plain shell script: an inline python heredoc would
# need quoting through ssh and would hide its own parse errors as empty output.
MARK = "##"

# A remote script's fillable slots, which is also what render_script checks a
# template against. Deliberately narrow -- upper case, digits and underscores
# between two @ -- so it cannot match an address or a path a value happens to
# carry; only the template is ever scanned, never a substituted value.
PLACEHOLDER_RE = re.compile(r"@[A-Z0-9_]+@")


def render_script(template: str, **subs: str) -> str:
    """Fill a remote script template's ``@NAME@`` slots.

    ``@M@`` is the marker, is implicit, and goes last: a value spliced in --
    the stage script's configs block -- carries markers of its own, and
    substituting the marker first would leave those literal.

    A slot the caller does not name raises rather than shipping. The remote
    side is a shell script, so an unsubstituted ``@ROOT@`` is not an obvious
    placeholder over there but a bare word, and `cd @ROOT@` on a lab machine
    fails somewhere other than where the mistake is.
    """
    named = {f"@{key}@" for key in subs} | {"@M@"}
    unfilled = sorted(set(PLACEHOLDER_RE.findall(template)) - named)
    if unfilled:
        raise ValueError(f"unsubstituted placeholder(s) {' '.join(unfilled)} "
                         f"in remote script template")
    for key, value in subs.items():
        template = template.replace(f"@{key}@", value)
    return template.replace("@M@", MARK)


# The runner, as a command line rather than an import: tick shells it in the
# checkout it is standing in, and COUNTER_RUNNER_CMD points it at a stub so the
# launch paths can be tested without launching a campaign. REMOTE_PYTHON for
# the same reason it is used everywhere else -- av2's ssh shell has no `python`.
RUNNER_CMD = os.environ.get("COUNTER_RUNNER_CMD",
                            f"{REMOTE_PYTHON} scripts/run_experiments.py")
# The scoring twin, for a `kind = "score"` phase: one score_curves.py per run
# directory over this host's seeds, driven by scripts/score_campaign.py. The
# same override pattern, so a tick running a score phase can be tested against
# a stub exactly as a run phase is.
SCORER_CMD = os.environ.get("COUNTER_SCORER_CMD",
                            f"{REMOTE_PYTHON} scripts/score_campaign.py")

# Rebuilt by stage. The lab machines have no Nix, so this is the incremental
# build against an already-configured preset directory, not a configure step;
# a campaign whose build differs overrides it with `build` in campaign.toml.
DEFAULT_BUILD_CMD = "cmake --build build-release"
COUNTER_BINARY = "build-release/counter"

# Queue entries live under the checkout that runs them, not in git: a tick
# rewrites the entry's state on every transition, and a tracked file doing that
# would leave the checkout dirty, which is exactly what stage refuses to touch.
# experiments/ is ignored by content, so the directory needs no .gitignore work.
QUEUE_DIR = "experiments/queue"
QUEUE_LOCK = "~/.counter-queue.lock"
QUEUE_STATES = ("queued", "running", "done", "failed", "cancelled")
# ensure_staged's third answer, beside None (go on) and an exit code (stop):
# the checkout moved and took the tick's own sources with it, so the rest of
# this tick belongs to a process that has read them. See restart_after_stage.
RESTART = "restart"
TICK_RESTART_ENV = "COUNTER_TICK_RESTARTED"
TICK_LOCK_FD_ENV = "COUNTER_TICK_LOCK_FD"
DEFAULT_MAX_ATTEMPTS = 3
# Kept in the entry so a state history survives the ticks that wrote it.
QUEUE_LOG_LINES = 20


def source_root(host: str) -> str:
    """rsync/ssh root of a host's counter checkout.

    The single place a host name becomes a transfer source, which is what lets
    the tests point collect at a local fixture instead of a lab machine.
    """
    return f"{host}:{merge.REMOTE_ROOT}"


def source_path(host: str) -> str:
    """Filesystem path of that checkout, for a shell probe run on the host."""
    _, sep, path = source_root(host).partition(":")
    return path if sep else source_root(host)


# -- Remote inventory ---------------------------------------------------------

# The queue dump, shared verbatim by the inventory and the stage probe. Both
# want the same thing -- every entry's name and its whole body -- and both put
# it last, so an entry's own text cannot be read as one of the sections above
# it. That means it also carries the closing ``@M@END``, and a template ending
# in it needs nothing after.
QUEUE_BLOCK = r"""echo "@M@QUEUE"
find @QUEUE@ -maxdepth 1 -type f -name '*.toml' 2>/dev/null | sort |
while IFS= read -r q; do
  echo "@M@QFILE $q"
  cat "$q"
  echo "@M@ENDQFILE"
done
echo "@M@END"
"""

INVENTORY_SCRIPT = r"""
cd @ROOT@ 2>/dev/null || { echo "@M@ERR not a directory: @ROOT@"; exit 3; }
echo "@M@PS"
ps -o comm=,args= -u "$(id -un)" 2>/dev/null
echo "@M@HOST"
hostname
date +%s
echo "@M@GIT"
git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "?"
git rev-parse --short HEAD 2>/dev/null || echo "?"
git status --porcelain --untracked-files=no 2>/dev/null | wc -l
echo "@M@MANIFESTS"
find experiments -maxdepth 1 -type f -name '*-manifest-*.json' 2>/dev/null |
while IFS= read -r m; do
  echo "@M@FILE $m"
  cat "$m"
  echo "@M@ENDFILE"
done
echo "@M@SCOREMANIFESTS"
find experiments -mindepth 2 -maxdepth 2 -type f -name 'score-manifest-*.json' 2>/dev/null |
while IFS= read -r m; do
  echo "@M@SFILE $m"
  cat "$m"
  echo "@M@ENDSFILE"
done
""" + QUEUE_BLOCK
# A score phase's manifest sits inside its output directory rather than
# beside a CSV, one level down, so it gets a sweep of its own: widening the
# first to depth two would pull in the per-host manifests archived campaigns
# keep under their own directories.
# The manifest sweep goes through `find`, not a glob, because the lab login
# shell is zsh: its default NOMATCH aborts the whole script where a glob matches
# nothing, so on a host with no manifest every section after the loop vanishes.
# sh and bash substitute the unmatched pattern instead, which is why this only
# ever fails in production.


def inventory_script(root: str) -> str:
    return render_script(INVENTORY_SCRIPT,
                         ROOT=shlex.quote(root),
                         QUEUE=shlex.quote(QUEUE_DIR))


def plan_args(campaign: dict) -> str:
    """``--dry-run`` arguments replaying one manifest's selection."""
    args = ["--profile", campaign["profile"], "--dry-run"]
    if campaign.get("sweeps"):
        args += ["--sweeps", *campaign["sweeps"]]
    if campaign.get("specs"):
        args += ["--specs", *campaign["specs"]]
    if campaign.get("seeds"):
        args += ["--seeds", *[str(s) for s in campaign["seeds"]]]
    return " ".join(shlex.quote(a) for a in args)


def detail_script(root: str, campaigns: list[dict]) -> str:
    """Shell reporting rows, mtimes and the runner's own plan per campaign.

    The plan comes from ``run_experiments.py --dry-run`` with the manifest's
    selection replayed onto it, so the count is whatever the runner would
    itself execute rather than a second expansion of the sweep. A profile the
    host's checkout no longer defines simply yields no plan line, and the
    caller reports that campaign's total as unknown.
    """
    lines = [f"cd {shlex.quote(root)} 2>/dev/null || exit 3"]
    for c in campaigns:
        if c.get("kind") == "score":
            # A score phase's progress is a directory listing: one CSV per
            # scored run, moved into place whole, and the newest file under
            # the output directory for staleness. No plan to ask for -- the
            # queued count is in the manifest already.
            q_out = shlex.quote(c["out"])
            lines += [
                f'echo "{MARK}CAMPAIGN {c["profile"]}"',
                f"if [ -d {q_out} ]; then",
                f'  echo "{MARK}CSVS $(find {q_out} -maxdepth 1 -type f '
                "-name '*.csv' 2>/dev/null | wc -l)\"",
                f'  echo "{MARK}OUTMTIME $(find {q_out} -maxdepth 1 -type f '
                "-printf '%T@\\n' 2>/dev/null | sort -rn | head -1)\"",
                "fi",
            ]
            continue
        q_csv = shlex.quote(c["results_csv"])
        q_dir = shlex.quote(c["results_dir"])
        lines += [
            f'echo "{MARK}CAMPAIGN {c["profile"]}"',
            f"if [ -f {q_csv} ]; then",
            f'  echo "{MARK}CSVROWS $(wc -l < {q_csv})"',
            f'  echo "{MARK}CSVMTIME $(stat -c %Y {q_csv})"',
            "fi",
            f"if [ -d {q_dir} ]; then",
            f'  echo "{MARK}LOGMTIME $(find {q_dir} -maxdepth 2 -name run.log '
            "-printf '%T@\\n' 2>/dev/null | sort -rn | head -1)\"",
            f'  echo "{MARK}RUNDIRS $(find {q_dir} -maxdepth 1 -mindepth 1 '
            '-type d 2>/dev/null | wc -l)"',
            "fi",
            f"{REMOTE_PYTHON} scripts/run_experiments.py {plan_args(c)} 2>/dev/null"
            f' | sed -n "s/^Plan:/{MARK}PLAN/p" | head -1',
        ]
    lines.append(f'echo "{MARK}END"')
    return "\n".join(lines)


TIMEOUT_PREFIX = "timed out after"


def is_timeout(error: str | None) -> bool:
    """Whether a run_shell error was the deadline rather than a failure.

    The two want different words in the output and often different fixes: a
    timeout on a healthy machine means the probe was too cheap a deadline for
    the tree it walked, not that the host is down.
    """
    return bool(error) and error.startswith(TIMEOUT_PREFIX)


def run_shell(host: str, script: str, timeout: int = SSH_TIMEOUT_S):
    """Run a shell script on ``host`` (or locally). Returns (stdout, error).

    A timeout returns whatever had already arrived alongside the error, rather
    than discarding it. The markers stream out in order, so a probe cut short
    still carries every section that completed, and throwing them away flips a
    finished campaign to "stalled" purely because the poll was slow.
    """
    if host == LOCAL:
        cmd = ["sh", "-c", script]
    else:
        cmd = ["ssh", *SSH_OPTS, host, script]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        partial = exc.stdout or ""
        if isinstance(partial, bytes):
            partial = partial.decode("utf-8", "replace")
        return (partial or None), f"{TIMEOUT_PREFIX} {timeout}s"
    except OSError as exc:
        return None, str(exc)
    if proc.returncode != 0 and not proc.stdout.strip():
        tail = [ln.strip() for ln in (proc.stderr or "").splitlines() if ln.strip()]
        return None, tail[-1] if tail else f"exit {proc.returncode}"
    return proc.stdout, None


def parse_inventory(text: str) -> dict:
    """Split the inventory script's marker-delimited output into fields."""
    out: dict = {"ps": [], "manifests": [], "score_manifests": [],
                 "queue": [], "hostname": "?", "epoch": None, "branch": "?",
                 "head": "?", "dirty": None, "error": None}
    section = None
    manifest_lines: list[str] = []
    queue_name = ""
    score_name = ""
    buf: list[str] = []

    def flush() -> None:
        if section == "host":
            out["hostname"] = buf[0].strip() if buf else "?"
            if len(buf) > 1 and buf[1].strip().isdigit():
                out["epoch"] = int(buf[1].strip())
        elif section == "git" and len(buf) >= 3:
            out["branch"], out["head"] = buf[0].strip(), buf[1].strip()
            out["dirty"] = buf[2].strip().isdigit() and int(buf[2].strip()) > 0

    for line in text.splitlines():
        if line.startswith(MARK + "ERR"):
            out["error"] = line[len(MARK) + 3:].strip()
            return out
        if line.startswith(MARK + "FILE "):
            section, manifest_lines = "file", []
            continue
        if line.startswith(MARK + "ENDFILE"):
            try:
                out["manifests"].append(json.loads("\n".join(manifest_lines)))
            except json.JSONDecodeError:
                pass
            section = "manifests"
            continue
        if line.startswith(MARK + "SFILE "):
            section = "sfile"
            score_name = line[len(MARK) + 6:].strip()
            manifest_lines = []
            continue
        if line.startswith(MARK + "ENDSFILE"):
            try:
                manifest = json.loads("\n".join(manifest_lines))
                manifest["file"] = score_name
                out["score_manifests"].append(manifest)
            except (json.JSONDecodeError, TypeError):
                pass
            section = "scoremanifests"
            continue
        if line.startswith(MARK + "QFILE "):
            section = "qfile"
            queue_name = line[len(MARK) + 6:].strip()
            manifest_lines = []
            continue
        if line.startswith(MARK + "ENDQFILE"):
            # A half-written entry is skipped with its name kept: a tick
            # interrupted between the temp file and the rename cannot leave one,
            # but a hand-edited entry can, and silence there hides the queue.
            try:
                entry = parse_toml("\n".join(manifest_lines))
                entry["file"] = Path(queue_name).name
            except TomlError as exc:
                entry = {"file": Path(queue_name).name, "error": str(exc)}
            out["queue"].append(entry)
            section = "queue"
            continue
        if line.startswith(MARK):
            flush()
            section, buf = line[len(MARK):].strip().lower(), []
            continue
        if section == "ps":
            out["ps"].append(line)
        elif section in ("file", "qfile", "sfile"):
            manifest_lines.append(line)
        else:
            buf.append(line)
    flush()
    return out


def parse_plan_line(line: str) -> dict:
    """Pull the counts out of run_experiments.py's ``Plan:`` line.

    Format: ``N result rows (M via aliasing), K already done; L runs to
    execute``. Each number is read from the phrase that names it rather than
    from a fixed position, so a clause added to that line later does not
    silently reassign the ones already there.
    """
    out: dict = {}
    for phrase, key in (("result rows", "rows_planned"),
                        ("already done", "rows_done_plan"),
                        ("runs to execute", "runs_left")):
        idx = line.find(phrase)
        if idx < 0:
            continue
        words = line[:idx].replace("(", " ").replace(";", " ").split()
        for word in reversed(words):
            if word.isdigit():
                out[key] = int(word)
                break
    return out


def parse_detail(text: str) -> dict:
    """Marker output of detail_script, keyed by profile name.

    ``rows_done`` is the runner's own already-done count, out of the ``Plan:``
    line, which is scoped to the same selection ``rows_planned`` is. The whole
    CSV's length is not that number and cannot be substituted for it: profiles
    share CSVs (``replicate``, ``replicate-wkoff`` and ``replicate-recap`` all
    write ``results-replicate.csv``), and a top-up relaunch rewrites the
    manifest to a couple of seeds while the file still holds every earlier row,
    which read the file's length as progress reports 400/40 and calls a
    just-started campaign finished. The length is kept only as a fallback for
    when no plan came back, and a fallback row is marked in the output.
    """
    per: dict = {}
    current: dict | None = None
    for line in text.splitlines():
        if not line.startswith(MARK):
            continue
        tag, _, rest = line[len(MARK):].partition(" ")
        rest = rest.strip()
        if tag == "CAMPAIGN":
            current = per.setdefault(rest, {})
        elif current is None:
            continue
        elif tag == "CSVROWS" and rest.isdigit():
            # wc -l counts the header too; a header-only CSV has zero rows.
            current["csv_rows"] = max(0, int(rest) - 1)
        elif tag == "CSVMTIME" and rest.isdigit():
            current["csv_mtime"] = int(rest)
        elif tag in ("LOGMTIME", "OUTMTIME") and rest:
            # One staleness field for both kinds: the newest run.log under a
            # results directory, or the newest file under a curves directory.
            try:
                current["log_mtime"] = int(float(rest))
            except ValueError:
                pass
        elif tag == "CSVS" and rest.isdigit():
            current["csvs"] = int(rest)
        elif tag == "RUNDIRS" and rest.isdigit():
            current["run_dirs"] = int(rest)
        elif tag == "PLAN":
            current["plan"] = rest
            current.update(parse_plan_line(rest))
    for entry in per.values():
        if "rows_done_plan" in entry:
            entry["rows_done"] = entry["rows_done_plan"]
        elif "csvs" in entry:
            entry["rows_done"] = entry["csvs"]
        elif "csv_rows" in entry:
            entry["rows_done"] = entry["csv_rows"]
            entry["rows_from_csv"] = True
    return per


# -- Process detection --------------------------------------------------------

# `maximal` is here for the scoring pass, which runs it under score_curves.py
# beside `compare`; both are engine processes a stage must not reset under.
ENGINE_COMMS = ("counter", "compare", "maximal", "ltlsynt", "black")


def live_processes(ps_lines: list[str]) -> list[dict]:
    """Runner, scorer and engine processes, from a ``comm args`` listing.

    Matched on ``comm``, never on the whole command line. ``pgrep -f
    run_experiments.py`` would match this script's own ssh command, whose text
    names it; the comm of that process is the login shell, so keying on comm
    excludes it by construction.

    A scorer (score_campaign.py, the score phase's twin of the runner) is
    reported with ``kind = "score"`` and the ``--out`` directory it writes,
    which is what a score phase's status row is matched against; it names no
    profile, so nothing that keys on one mistakes it for a run.
    """
    found = []
    for line in ps_lines:
        comm, _, args = line.strip().partition(" ")
        args = args.strip()
        if comm in ENGINE_COMMS:
            found.append({"comm": comm, "profile": None, "args": args})
        elif comm.startswith("python") and "run_experiments.py" in args:
            if "--dry-run" in args:
                continue
            found.append({"comm": comm, "profile": profile_of_args(args),
                          "args": args})
        elif comm.startswith("python") and "score_campaign.py" in args:
            if "--dry-run" in args:
                continue
            found.append({"comm": comm, "profile": None, "kind": "score",
                          "out": option_of_args(args, "--out"), "args": args})
    return found


def option_of_args(args: str, option: str):
    try:
        words = shlex.split(args)
    except ValueError:
        words = args.split()
    for i, word in enumerate(words):
        if word == option and i + 1 < len(words):
            return words[i + 1]
        if word.startswith(option + "="):
            return word.split("=", 1)[1]
    return None


def profile_of_args(args: str) -> str:
    return option_of_args(args, "--profile") or "full"


# -- Gathering ----------------------------------------------------------------

def campaigns_from_manifests(manifests: list[dict]) -> list[dict]:
    """One record per manifest, flattened to the fields status reports on."""
    out = []
    for m in manifests:
        sweep = m.get("sweep") or {}
        git = m.get("git") or {}
        counter = (m.get("binaries") or {}).get("counter") or {}
        out.append({
            "profile": m.get("profile", "?"),
            "manifest_host": m.get("hostname", "?"),
            "started": m.get("started"),
            "branch": git.get("branch", "?"),
            "head": (git.get("head") or "?")[:7],
            "binary_commit": counter.get("commit_short", "?"),
            # The runner refuses to launch off a dirty binary without
            # --allow-stale-binary, so a true here says the override was used
            # and the campaign's rows name a commit they did not come from.
            "dirty_binary": counter.get("dirty") == "1",
            "results_csv": sweep.get("results_csv", ""),
            "results_dir": sweep.get("results_dir", ""),
            "sweeps": sweep.get("sweeps"),
            "specs": sweep.get("specs") or [],
            "seeds": sweep.get("seeds") or [],
            "jobs": sweep.get("jobs"),
        })
    out.sort(key=lambda c: (c.get("started") or "", c["profile"]))
    return out


def campaigns_from_score_manifests(manifests: list[dict]) -> list[dict]:
    """One record per score manifest, in the shape the run records take.

    The output directory's name stands where a profile would, since a score
    phase has none of its own, and ``label`` is what the table prints. The
    planned count is the manifest's queued count -- the scorer wrote it
    knowing this host's seeds -- and the done count comes from the detail
    probe's listing of the directory, so a pass that is still running reads
    live rather than from the counts its manifest froze at launch.
    """
    out = []
    for m in manifests:
        git = m.get("git") or {}
        maximal = (m.get("binaries") or {}).get("maximal") or {}
        counts = m.get("counts") or {}
        out_dir = str(m.get("out") or Path(m.get("file", "?")).parent)
        name = Path(out_dir).name
        out.append({
            "kind": "score",
            "profile": name,
            "label": f"score:{name}",
            "out": out_dir,
            "results": m.get("results", ""),
            "manifest_host": m.get("hostname", "?"),
            "started": m.get("started"),
            "finished": m.get("finished"),
            "branch": git.get("branch", "?"),
            "head": (git.get("head") or "?")[:7],
            "binary_commit": maximal.get("commit_short", "?"),
            # A maximal built dirty, or a pass that overrode the gate: either
            # way the curves name a commit they may not have come from.
            "dirty_binary": (maximal.get("dirty") == "1"
                             or bool(m.get("allow_stale_binary"))),
            "rows_planned": counts.get("queued"),
            "seeds": m.get("seeds") or [],
        })
    out.sort(key=lambda c: (c.get("started") or "", c["profile"]))
    return out


def gather_host(host: str, root: str, only: str | None, want_plan: bool,
                show_all: bool) -> dict:
    """Everything status reports for one host.

    Never raises: an unreachable machine costs one row of the table, not the
    run, so a poll against a lab that is half up still says what the other half
    is doing.
    """
    report: dict = {"host": host, "root": root, "reachable": False,
                    "error": None, "campaigns": [], "hidden": 0,
                    "processes": [], "queue": []}
    text, err = run_shell(host, inventory_script(root))
    inv = parse_inventory(text or "")
    if inv["error"]:
        report["error"] = inv["error"]
        return report
    # A timed-out inventory is still usable when its git section completed --
    # the sections stream in order, so a branch means everything before it
    # arrived. Without one there is nothing to report but the failure.
    if err is not None:
        if not (is_timeout(err) and inv["branch"] != "?"):
            report["error"] = err
            return report
        report["partial"] = True
    report.update({
        "reachable": True,
        "hostname": inv["hostname"],
        "epoch": inv["epoch"] or int(time.time()),
        "branch": inv["branch"],
        "head": inv["head"],
        "dirty": inv["dirty"],
        "processes": live_processes(inv["ps"]),
        "queue": inv["queue"],
    })
    campaigns = (campaigns_from_manifests(inv["manifests"])
                 + campaigns_from_score_manifests(inv["score_manifests"]))
    if only:
        campaigns = [c for c in campaigns if c["profile"] == only]
    elif not show_all:
        # Manifests accumulate: a machine keeps one per campaign it has ever
        # launched. The ones whose branch the checkout has since left cannot be
        # the campaign now in flight, and their profile is usually no longer
        # defined either, so they report as unknowns and drown the live rows.
        live = [c for c in campaigns if c["branch"] == report["branch"]]
        report["hidden"] = len(campaigns) - len(live)
        campaigns = live
    if campaigns and want_plan:
        text, err = run_shell(host, detail_script(root, campaigns))
        # Parsed unconditionally: the campaigns are reported one after another,
        # so a probe cut short still holds complete records for the ones that
        # got their turn, and dropping them reports those as stalled.
        detail = parse_detail(text or "")
        if err is not None:
            report["partial"] = True
        for c in campaigns:
            c.update(detail.get(c["profile"], {}))
    for c in campaigns:
        annotate(c, report)
    report["campaigns"] = campaigns
    return report


def annotate(c: dict, host_report: dict) -> None:
    """Derive percent, staleness, state and ETA for one campaign.

    Times are differenced against the host's own clock, sampled in the same
    inventory call, because av3 runs minutes away from av2 whenever NTP is off
    and a staleness measured across that skew is fiction.

    A process is attributed to a campaign only when it names that campaign's
    profile. An engine process names none, and matching those against every
    campaign made one unrelated `counter` on av2 report all six of its archived
    campaigns as running while idle av3 reported the same six from the same
    data as stalled. Unattributable processes belong in the host's PROCESSES
    column, which is where they still appear.

    A matched process is corroborated against the campaign's own output before
    it counts as progress. `running` beside a nine-day staleness is the reading
    that stops someone investigating a dead run, which is the failure this tool
    exists to prevent, so that combination reports as `stuck` instead — worth
    seeing rather than hidden behind either plain state.
    """
    now = host_report.get("epoch") or int(time.time())
    done = c.get("rows_done")
    planned = c.get("rows_planned")
    c["pct"] = (100.0 * done / planned) if done is not None and planned else None
    c["stale_s"] = now - c["log_mtime"] if c.get("log_mtime") else None
    c["running"] = any(claims_campaign(p, c) for p in host_report["processes"])
    started = parse_started(c.get("started"))
    c["elapsed_s"] = int(now - started) if started else None

    # A log with no mtime at all is a campaign whose first run directory does
    # not exist yet, so silence there is the launch, not a stall.
    silent = c["stale_s"] is not None and c["stale_s"] > STALE_RUN_S
    if planned is not None and done is not None and done >= planned:
        c["state"] = "done"
    elif c["running"] and silent:
        c["state"] = "stuck"
    elif c["running"]:
        c["state"] = "running"
    else:
        c["state"] = "stalled"

    c["eta_s"] = None
    if (c["state"] == "running" and done and planned
            and c["elapsed_s"] and c["elapsed_s"] > 0):
        # Deliberately crude: rows per second since the manifest was written,
        # which credits this launch with any rows it merely resumed onto. It
        # answers "roughly when", not "when".
        rate = done / c["elapsed_s"]
        if rate > 0:
            c["eta_s"] = int((planned - done) / rate)


def claims_campaign(proc: dict, c: dict) -> bool:
    """Whether a live process is this campaign's own.

    A runner claims the campaign whose profile it names; a scorer claims the
    score phase whose output directory it names, compared by name since the
    manifest and the process may spell the path relative or absolute. Neither
    claims the other's kind, and an engine process claims nothing.
    """
    if c.get("kind") == "score":
        return (proc.get("kind") == "score" and bool(proc.get("out"))
                and Path(proc["out"]).name == Path(c.get("out", "")).name)
    return bool(proc.get("profile")) and proc["profile"] == c["profile"]


def parse_started(value: str | None) -> float | None:
    if not value:
        return None
    for fmt in ("%Y-%m-%dT%H:%M:%S%z", "%Y-%m-%dT%H:%M:%S"):
        try:
            return datetime.strptime(value, fmt).timestamp()
        except ValueError:
            continue
    return None


# -- Colour -------------------------------------------------------------------

# Plain SGR escapes, no colorama and no rich: av2 and av3 run python 3.10.12
# with no third-party packages at all, which is the same reason TOML is parsed
# by hand below.
ANSI = {
    "bold": "\033[1m",
    "dim": "\033[2m",
    "red": "\033[31m",
    "green": "\033[32m",
    "yellow": "\033[33m",
    "cyan": "\033[36m",
}
ANSI_RESET = "\033[0m"


class Style:
    """Paints text, or hands it back untouched when colour is off.

    Held as one module-level instance rather than threaded through every
    caller: the decision is made once from the command line and every table
    renderer needs it. Colour is decoration over a plain layout, never part of
    it -- ``render_table`` measures the unpainted text and pads outside the
    escapes, so stripping the escapes from a coloured table gives back the
    plain one byte for byte.
    """

    def __init__(self, enabled: bool = False) -> None:
        self.enabled = bool(enabled)

    def __call__(self, text: str, *names: str) -> str:
        if not (self.enabled and text and names):
            return text
        return "".join(ANSI[n] for n in names) + text + ANSI_RESET


STYLE = Style(False)


def colour_enabled(no_color: bool = False, stream=None, env=None) -> bool:
    """Whether to colour, deciding in the order the conventions are read in.

    Off wherever stdout is not a terminal, which is the case that matters
    here: `tick` runs from cron into $HOME/.counter-queue.log and `status` is
    routinely piped or captured, and escapes in either are noise nobody can
    read through. It is the failure the C++ side already guards with
    ``stdout_is_tty()`` -- one unguarded status line was 59KB of escapes for
    1.2KB of content.

    ``--no-color`` beats everything, then NO_COLOR (any non-empty value, per
    no-color.org), then CLICOLOR_FORCE, which forces colour on down a pipe and
    is what makes this testable and `less -R` usable.
    """
    env = os.environ if env is None else env
    if no_color:
        return False
    if env.get("NO_COLOR", ""):
        return False
    force = env.get("CLICOLOR_FORCE", "")
    if force and force != "0":
        return True
    stream = sys.stdout if stream is None else stream
    try:
        return bool(stream.isatty())
    except (AttributeError, ValueError):  # a closed or non-file stream
        return False


def set_colour(enabled: bool) -> None:
    STYLE.enabled = bool(enabled)


# -- Formatting ---------------------------------------------------------------

def human_duration(seconds: float | None) -> str:
    if seconds is None:
        return "-"
    seconds = int(seconds)
    if seconds < 60:
        return f"{seconds}s"
    if seconds < 3600:
        return f"{seconds // 60}m"
    if seconds < 86400:
        return f"{seconds // 3600}h{(seconds % 3600) // 60:02d}m"
    return f"{seconds // 86400}d{(seconds % 86400) // 3600:02d}h"


def human_bytes(n: int | None) -> str:
    if n is None:
        return "-"
    size = float(n)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if size < 1024 or unit == "TB":
            return f"{int(size)}B" if unit == "B" else f"{size:.1f}{unit}"
        size /= 1024
    return "-"


def render_table(rows: list[list[str]], headers: list[str],
                 paint=None) -> str:
    """Left-aligned columns, headers bold, cells optionally coloured.

    ``paint(row, col, cell, style) -> str`` may wrap a cell in escapes. It is
    called for its cell alone and every width is computed from the unpainted
    text, with the padding added *outside* whatever it returns -- so an escape
    can neither widen a column nor survive the trailing ``rstrip``, and a
    coloured table strips back to exactly the plain one. Passing a painter
    costs nothing when colour is off, where it is not called at all.
    """
    widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(cell))

    def line(cells: list[str], painter) -> str:
        return "  ".join(
            painter(i, cell) + " " * (w - len(cell))
            for i, (cell, w) in enumerate(zip(cells, widths))).rstrip()

    out = [line(headers, lambda i, cell: STYLE(cell, "bold"))]
    for r, row in enumerate(rows):
        if paint and STYLE.enabled:
            out.append(line(row,
                            lambda i, c, r=r: paint(r, i, c, STYLE)))
        else:
            out.append(line(row, lambda i, cell: cell))
    return "\n".join(out)


HEADERS = ["HOST", "CAMPAIGN", "ROWS", "PCT", "STATE", "STALE", "ETA",
           "BRANCH", "BINARY"]

# Only the readings a poll is looking for are coloured: the two that are fine
# (done, running) and the two that want a person (stuck, stalled). Everything
# else in the table stays the terminal's own foreground, so colour marks a
# state rather than decorating a row.
STATE_COLOURS = {"done": ("green",), "running": ("cyan",),
                 "stuck": ("red",), "stalled": ("yellow",)}


def status_paint(row: int, col: int, cell: str, style: Style) -> str:
    header = HEADERS[col]
    if header == "CAMPAIGN":
        if cell.startswith("(unreachable:") or cell.startswith("(no answer:"):
            return style(cell, "red")
        # "(no campaigns)" and the rest are the absence of a reading rather
        # than a bad one.
        return style(cell, "dim") if cell.startswith("(") else cell
    if header == "STATE":
        return style(cell, *STATE_COLOURS.get(cell, ()))
    # Both marks qualify the cell they follow, so only the mark is painted:
    # see the legend print_status closes with.
    if header == "BRANCH" and cell.endswith("!"):
        return cell[:-1] + style("!", "red")
    if header == "BINARY" and cell.endswith("*"):
        return cell[:-1] + style("*", "yellow")
    return cell


def status_rows(reports: list[dict]) -> list[list[str]]:
    rows = []
    for r in reports:
        if not r["reachable"]:
            # Truncated: an ssh failure can be a paragraph, and one wide cell
            # widens the whole column for every other host.
            why = (r["error"] or "?").replace("\t", " ")
            if len(why) > 44:
                why = why[:41] + "..."
            label = "no answer" if is_timeout(r["error"]) else "unreachable"
            rows.append([r["host"], f"({label}: {why})",
                         "-", "-", "-", "-", "-", "-", "-"])
            continue
        if not r["campaigns"]:
            note = "(busy, no manifest)" if r["processes"] else "(no campaigns)"
            if r.get("hidden"):
                note = f"(none on this branch, {r['hidden']} older)"
            rows.append([r["host"], note, "-", "-", "-", "-", "-", "-", "-"])
            continue
        for c in r["campaigns"]:
            done = c.get("rows_done")
            planned = c.get("rows_planned")
            # The campaign's own branch, out of its manifest, flagged when the
            # checkout has since moved off it -- that disagreement is exactly
            # what makes a resumed campaign produce rows from another commit.
            branch = c["branch"] + ("!" if c["branch"] != r["branch"] else "")
            # "~" marks a row whose done count is the whole CSV rather than
            # this launch's share of it, which is the one row that can still
            # overstate progress. See parse_detail.
            done_cell = ("~" if c.get("rows_from_csv") else "") + (
                str(done) if done is not None else "?")
            rows.append([
                r["host"],
                c.get("label") or c["profile"],
                f"{done_cell}/{planned if planned is not None else '?'}",
                # Truncated, not rounded: 797/800 reading "100%" is the one
                # number a status poll must not get wrong.
                f"{int(c['pct'])}%" if c.get("pct") is not None else "-",
                c["state"],
                human_duration(c.get("stale_s")),
                human_duration(c.get("eta_s")),
                branch,
                c["binary_commit"] + ("*" if c.get("dirty_binary") else ""),
            ])
    return rows


def status_notes(reports: list[dict]) -> list[str]:
    notes = []
    for r in reports:
        if not r["reachable"]:
            continue
        if r.get("hidden"):
            notes.append(f"{r['host']}: {r['hidden']} manifest(s) from other "
                         f"branches hidden — pass --all to see them")
        for p in r["processes"]:
            if p["profile"]:
                notes.append(f"{r['host']}: runner live on "
                             f"--profile {p['profile']}")
            elif p.get("kind") == "score":
                notes.append(f"{r['host']}: scorer live on "
                             f"--out {p.get('out') or '?'}")
        for c in r["campaigns"]:
            if c["state"] == "stuck" and c.get("kind") == "score":
                notes.append(
                    f"{r['host']}/{c.get('label') or c['profile']}: a scorer "
                    f"is alive on this output directory but nothing under it "
                    f"has been touched for {human_duration(c['stale_s'])} — "
                    f"longer than one run's scoring may take, so it is "
                    f"producing nothing")
            elif c["state"] == "stuck":
                notes.append(
                    f"{r['host']}/{c['profile']}: a runner is alive on this "
                    f"profile but its newest run.log has not been touched for "
                    f"{human_duration(c['stale_s'])} — longer than any single "
                    f"run may take, so it is producing nothing")
            if c["branch"] != r["branch"]:
                notes.append(f"{r['host']}/{c['profile']}: launched on "
                             f"{c['branch']}, checkout now on {r['branch']} (!)")
            if c.get("dirty_binary"):
                notes.append(f"{r['host']}/{c['profile']}: launched off a "
                             f"binary built dirty (* on BINARY)")
            if c.get("rows_planned") is None and c.get("kind") == "score":
                notes.append(f"{r['host']}/{c.get('label')}: the score "
                             f"manifest records no queued count")
            elif c.get("rows_planned") is None:
                notes.append(f"{r['host']}/{c['profile']}: no plan — that "
                             f"checkout may no longer define the profile")
            if c.get("rows_from_csv"):
                notes.append(
                    f"{r['host']}/{c['profile']}: ~ROWS counts the whole CSV, "
                    f"not this launch's selection — it overstates progress "
                    f"where profiles share a CSV or a relaunch topped up")
        if r.get("partial"):
            notes.append(f"{r['host']}: probe timed out; this host's rows are "
                         f"whatever had arrived by then")
    return list(dict.fromkeys(notes))


CHECKOUT_HEADERS = ["HOST", "MACHINE", "BRANCH", "HEAD", "TREE", "PROCESSES"]


def checkout_paint(row: int, col: int, cell: str, style: Style) -> str:
    header = CHECKOUT_HEADERS[col]
    if header == "TREE" and cell != "-":
        # Dirty is not an error, but it is the first thing `stage` refuses.
        return style(cell, "green" if cell == "clean" else "yellow")
    if header == "PROCESSES":
        if cell == "unreachable":
            return style(cell, "red")
        return style(cell, "dim" if cell == "idle" else "cyan")
    return cell


def checkout_rows(reports: list[dict]) -> list[list[str]]:
    """The checkout each host is sitting on, independent of any campaign.

    Separate from the campaign table because the two can disagree: a campaign
    row names the branch its manifest was written on, which is the branch its
    results came from, while this names the branch a resumed run would use.
    """
    rows = []
    for r in reports:
        if not r["reachable"]:
            rows.append([r["host"], "-", "-", "-", "-", "unreachable"])
            continue
        procs = ", ".join(dict.fromkeys(p["comm"] for p in r["processes"]))
        rows.append([r["host"], r.get("hostname", "?"), r["branch"], r["head"],
                     "dirty" if r.get("dirty") else "clean", procs or "idle"])
    return rows


def print_status(reports: list[dict]) -> None:
    print(render_table(checkout_rows(reports), CHECKOUT_HEADERS,
                       checkout_paint))
    print()
    print(render_table(status_rows(reports), HEADERS, status_paint))
    # The queue table is printed only where there is a queue: it comes free
    # with the inventory the poll already ran, and an empty one is a row of
    # dashes saying nothing.
    queue = queue_rows(reports)
    if queue:
        print()
        print(render_table(queue, QUEUE_HEADERS, queue_paint))
    notes = status_notes(reports)
    if notes:
        print()
        for note in notes:
            print(f"note: {note}")
    print("\nBRANCH is the branch the campaign was launched on, per its "
          "manifest, and is flagged\nwith ! where the checkout has since moved "
          "off it. ROWS is done against planned, both\nfrom the plan "
          "run_experiments.py reports for that manifest's selection; a ~ marks "
          "a\ndone count taken from the whole CSV because no plan came back. "
          "STALE is the age of\nthe newest run.log under the campaign's "
          "results dir, on the host's own clock.\nSTATE is running only where "
          "a runner names this profile and the log is fresher than\n"
          f"{human_duration(STALE_RUN_S)}; stuck means the runner is there and "
          "the log is not moving. ETA\nextrapolates rows-so-far over time "
          "since the manifest was written, and is crude by\nconstruction. "
          "A score: row is a scoring pass: ROWS is curves written against "
          "runs queued,\nfrom its score-manifest, STALE is the newest file "
          "under its output directory, and\nrunning means a scorer names "
          "that directory.")


def cmd_status(args: argparse.Namespace) -> int:
    hosts = [args.host] if args.host else list(HOSTS)
    targets = [(h, source_path(h)) for h in hosts if h != LOCAL]
    if not args.host or args.host == LOCAL:
        targets.append((LOCAL, str(REPO_ROOT)))
    with ThreadPoolExecutor(max_workers=max(1, len(targets))) as pool:
        reports = list(pool.map(
            lambda t: gather_host(t[0], t[1], args.campaign, not args.no_plan,
                                  args.all),
            targets))
    if args.json:
        print(json.dumps({"generated": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                          "hosts": reports}, indent=2))
    else:
        print_status(reports)
    return 0 if all(r["reachable"] for r in reports) else 1


# -- collect ------------------------------------------------------------------

def resolve_profile(args: argparse.Namespace) -> tuple[str, str | None]:
    """``(csv_name, result_dir_name)`` for the profile, or explicit overrides.

    merge_experiments.PROFILE_CSVS is the authority. A campaign living on an
    unmerged branch is absent from it, which is what the overrides are for
    rather than a guess at ``results-<profile>.csv``: a wrong guess merges a
    campaign into a file nobody is watching, and says nothing while it does.
    """
    if args.csv or args.results_dir:
        if not args.csv:
            sys.exit("--results-dir needs --csv alongside it")
        default_dir = Path(args.csv).stem
        return args.csv, (None if args.no_results
                          else (args.results_dir or default_dir))
    if args.profile not in merge.PROFILE_CSVS:
        sys.exit(f"Unknown profile {args.profile!r}. Known: "
                 f"{', '.join(sorted(merge.PROFILE_CSVS))}\n"
                 f"For a campaign on an unmerged branch, pass "
                 f"--csv results-<name>.csv [--results-dir results-<name>].")
    return (merge.PROFILE_CSVS[args.profile],
            None if args.no_results else merge.PROFILE_RESULT_DIRS[args.profile])


MEASURE_SCRIPT = r"""
cd @ROOT@ 2>/dev/null || { echo "@M@ERR not a directory: @ROOT@"; exit 3; }
if [ -f @CSV@ ]; then
  echo "@M@CSVROWS $(wc -l < @CSV@)"
  echo "@M@CSVBYTES $(stat -c %s @CSV@)"
else
  echo "@M@NOCSV"
fi
if [ -d @DIR@ ]; then
  echo "@M@DIRBYTES $(du -sb @DIR@ | cut -f1)"
  echo "@M@DIRFILES $(find @DIR@ -type f | wc -l)"
fi
echo "@M@END"
"""

MEASURE_KEYS = {"CSVROWS": "csv_rows", "CSVBYTES": "csv_bytes",
                "DIRBYTES": "dir_bytes", "DIRFILES": "dir_files"}


def measure_source(host: str, csv_name: str, result_dir: str | None) -> dict:
    """Size of what sits on a host, without transferring any of it."""
    csv_path = f"experiments/{csv_name}"
    dir_path = f"experiments/{result_dir}" if result_dir else "/nonexistent"
    script = render_script(MEASURE_SCRIPT,
                           ROOT=shlex.quote(source_path(host)),
                           CSV=shlex.quote(csv_path),
                           DIR=shlex.quote(dir_path))
    text, err = run_shell(host, script, timeout=MEASURE_TIMEOUT_S)
    out: dict = {"host": host, "error": err, "timed_out": is_timeout(err)}
    for line in (text or "").splitlines():
        if not line.startswith(MARK):
            continue
        tag, _, rest = line[len(MARK):].partition(" ")
        rest = rest.strip()
        if tag == "ERR":
            out["error"] = rest
        elif tag in MEASURE_KEYS:
            out[MEASURE_KEYS[tag]] = int(rest) if rest.isdigit() else None
    if out.get("csv_rows"):
        out["csv_rows"] -= 1  # header
    return out


def rsync_pending(host: str, csv_name: str, result_dir: str | None) -> dict:
    """What an rsync would actually move, via ``rsync -a --dry-run --stats``.

    Read-only on both ends: rsync builds its file list and reports on it, and
    writes nothing. That is the whole point of collect's dry run against a
    29GB tree.
    """
    out: dict = {}
    root = source_root(host)
    targets = [("csv", f"{root}/experiments/{csv_name}",
                str(REPO_ROOT / "experiments" / csv_name))]
    if result_dir:
        local_dir = REPO_ROOT / "experiments" / result_dir
        targets.insert(0, ("results", f"{root}/experiments/{result_dir}/",
                           f"{local_dir}/"))
    for label, src, dst in targets:
        proc = subprocess.run(
            ["rsync", "-a", "--dry-run", "--stats",
             "-e", " ".join(["ssh", *SSH_OPTS]), src, dst],
            capture_output=True, text=True)
        stats: dict = {"ok": proc.returncode == 0}
        for line in proc.stdout.splitlines():
            if line.startswith("Number of regular files transferred:"):
                stats["files"] = int(line.split(":")[1].strip().replace(",", ""))
            elif line.startswith("Total transferred file size:"):
                stats["bytes"] = int(
                    line.split(":")[1].split()[0].replace(",", ""))
        if not stats["ok"]:
            tail = [ln.strip() for ln in proc.stderr.splitlines() if ln.strip()]
            stats["error"] = tail[-1] if tail else f"exit {proc.returncode}"
        out[label] = stats
    return out


def key_set(path: Path) -> set:
    _, rows = merge.read_rows(path)
    return {merge.key_of(r) for r in rows}


def duplicate_keys(path: Path) -> list:
    counts: dict = {}
    _, rows = merge.read_rows(path)
    for row in rows:
        k = merge.key_of(row)
        counts[k] = counts.get(k, 0) + 1
    return sorted(k for k, n in counts.items() if n > 1)


def verify_merge(results_csv: Path, before: set, per_host: dict,
                 missing: list | tuple = ()) -> bool:
    """Row count, duplicate keys, and per-host counts against the merged total.

    The arithmetic that matters is the union, not the sum. Two hosts running
    disjoint seed ranges overlap on nothing, while re-collecting an already
    merged campaign overlaps on everything; only the union tells those apart
    from a silent loss.

    ``missing`` names hosts that were asked for and contributed nothing. The
    union is computed over the survivors alone and so agrees with itself
    perfectly, which is exactly why a host dropping out has to be carried in
    here rather than inferred: a half-collected campaign otherwise verifies
    clean and exits zero.
    """
    _, merged_rows = merge.read_rows(results_csv)
    merged_keys = {merge.key_of(r) for r in merged_rows}
    expected = set(before)
    for keys in per_host.values():
        expected |= keys
    ok = not missing

    print("\nVerification")
    if missing:
        print(f"  INCOMPLETE: no rows from {', '.join(missing)} — the merged "
              f"file is missing that host's share of the campaign.")
        print("  Everything below is over the hosts that did answer, so it "
              "cannot detect that.")
    print(f"  merged rows:    {len(merged_rows)}")
    print(f"  distinct keys:  {len(merged_keys)}")
    dups = duplicate_keys(results_csv)
    if dups:
        ok = False
        print(f"  DUPLICATE KEYS: {len(dups)} (e.g. {dups[0]})")
    else:
        print("  duplicate keys: none")

    print(f"  local before:   {len(before)}")
    for host, keys in per_host.items():
        print(f"  {host}: {len(keys)} rows ({len(keys & before)} already local)")
    pairs = list(per_host.items())
    for i, (h1, k1) in enumerate(pairs):
        for h2, k2 in pairs[i + 1:]:
            if k1 & k2:
                print(f"  overlap {h1}/{h2}: {len(k1 & k2)} shared keys")
    print(f"  expected union: {len(expected)}")

    if len(merged_rows) != len(merged_keys):
        ok = False
        print("  MISMATCH: the merged file holds more rows than distinct keys")
    if merged_keys != expected:
        ok = False
        absent = expected - merged_keys
        extra = merged_keys - expected
        print(f"  MISMATCH: {len(absent)} expected key(s) absent, "
              f"{len(extra)} unexpected key(s) present")
        for k in sorted(absent)[:5]:
            print(f"    missing: {k}")
        for k in sorted(extra)[:5]:
            print(f"    extra:   {k}")
    if ok:
        print("  OK: every source row is present exactly once.")
    return ok


def collect_dry_run(hosts: list[str], csv_name: str, result_dir: str | None,
                    results_csv: Path) -> int:
    print(f"Local {csv_name}: {len(key_set(results_csv))} rows")
    rows = []
    ok = True
    for host in hosts:
        measured = measure_source(host, csv_name, result_dir)
        if measured.get("error"):
            ok = False
            # A deadline and a dead host need different words: the first says
            # the probe was too cheap for the tree, the second that nobody is
            # home, and only the second means the collect cannot proceed.
            why = ("measure timed out" if measured["timed_out"]
                   else f"unreachable: {measured['error']}")
            rows.append([host, f"({why})", "-", "-", "-", "-"])
            continue
        pending = rsync_pending(host, csv_name, result_dir)
        results = pending.get("results", {})
        csv_stat = pending.get("csv", {})
        for stat in (results, csv_stat):
            if not stat.get("ok", True):
                ok = False
                print(f"  rsync probe failed on {host}: {stat.get('error')}")
        rows.append([
            host,
            str(measured.get("csv_rows", "-")),
            human_bytes(measured.get("dir_bytes")),
            str(measured.get("dir_files", "-")),
            (f"{results.get('files', 0)} files / "
             f"{human_bytes(results.get('bytes'))}") if result_dir else "(skipped)",
            f"{csv_stat.get('files', 0)} files / "
            f"{human_bytes(csv_stat.get('bytes'))}",
        ])
    print()
    print(render_table(rows, ["HOST", "ROWS", "DIR SIZE", "DIR FILES",
                              "WOULD TRANSFER (results)",
                              "WOULD TRANSFER (csv)"]))
    print("\nDry run — nothing transferred, nothing written.")
    return 0 if ok else 1


# -- collect --curves ---------------------------------------------------------
#
# A score phase leaves one CSV per run under <out>/ on each host, beside the
# timings, failures and warnings files and the host's score manifest. Each
# host's directory is pulled whole into experiments/<out>/<host>/ -- the raw
# per-host record, which is what an archive's maximality_pass block is
# assembled from -- and the per-run CSVs are then joined into
# experiments/<out>.csv, header once. The join is a union by run name, as the
# results merge is a union by key: two hosts on disjoint seeds share no run
# name, and a run scored on both is a split violation worth reporting.

CURVES_SIDE_FILES = ("timings.txt", "failures.txt", "warnings.log")


def curves_dir_local(out_name: str) -> Path:
    return REPO_ROOT / "experiments" / out_name


def host_curves(directory: Path) -> dict:
    """``{run name: path}`` for the per-run CSVs under one host's pull."""
    return {p.name[:-len(".csv")]: p for p in sorted(directory.glob("*.csv"))
            if p.is_file() and p.stat().st_size > 0}


def csv_header(path: Path) -> str:
    with open(path, newline="") as handle:
        return handle.readline().rstrip("\r\n")


def count_csv_rows(path: Path) -> int:
    if not path.is_file():
        return 0
    with open(path, newline="") as handle:
        return max(0, sum(1 for _ in handle) - 1)


def pull_curves(host: str, out_name: str, dry_run: bool) -> dict:
    """rsync one host's ``<out>/`` into ``experiments/<out>/<host>/``.

    The whole directory, not the CSVs alone: the timings, failures and
    warnings files and the score manifest are the record of how each curve
    was produced, and they travel with it. Returns rsync's verdict; a dry run
    reports the transfer it would make and moves nothing.
    """
    src = f"{source_root(host)}/experiments/{out_name}/"
    dst = curves_dir_local(out_name) / host
    if not dry_run:
        dst.mkdir(parents=True, exist_ok=True)
    cmd = ["rsync", "-a", "-e", " ".join(["ssh", *SSH_OPTS])]
    if dry_run:
        cmd += ["--dry-run", "--stats"]
    proc = subprocess.run([*cmd, src, f"{dst}/"], capture_output=True,
                          text=True)
    out: dict = {"ok": proc.returncode == 0, "dest": dst}
    for line in proc.stdout.splitlines():
        if line.startswith("Number of regular files transferred:"):
            out["files"] = int(line.split(":")[1].strip().replace(",", ""))
        elif line.startswith("Total transferred file size:"):
            out["bytes"] = int(line.split(":")[1].split()[0].replace(",", ""))
    if not out["ok"]:
        tail = [ln.strip() for ln in proc.stderr.splitlines() if ln.strip()]
        out["error"] = tail[-1] if tail else f"exit {proc.returncode}"
    return out


def merge_curves(per_host: dict, merged_csv: Path) -> tuple:
    """Join every host's per-run CSVs into one file, header once.

    Returns ``(union, problems)``: the run names that would be written, and
    the reasons the join is not clean. A run present on two hosts is written
    once, from the first host, and reported unless the two copies are
    byte-identical -- the seeds are split, so two differing copies of one
    run mean two hosts scored it, under budgets that may differ. Two
    distinct headers among the curves mean two vintages of score_curves.py,
    and the join refuses naming both rather than concatenating one set of
    rows under the other's columns.

    The merged file is written only when every curve was accepted. A join
    with any problem leaves whatever ``merged_csv`` held before, so a bad
    collect cannot overwrite a good file with a partial one.
    """
    problems: list = []
    chosen: dict = {}
    for host, curves in per_host.items():
        for run, path in curves.items():
            if run in chosen:
                other = chosen[run][1]
                if path.read_bytes() != other.read_bytes():
                    problems.append(f"{run} scored on both {chosen[run][0]} "
                                    f"and {host}, and the two curves differ")
                continue
            chosen[run] = (host, path)
    if not chosen:
        return [], problems
    ordered = sorted(chosen)
    headers: dict = {}
    for run in ordered:
        headers.setdefault(csv_header(chosen[run][1]), []).append(run)
    if len(headers) > 1:
        shown = "; ".join(f"{header!r} on {len(runs)} curve(s) e.g. {runs[0]}"
                          for header, runs in headers.items())
        problems.append(f"{len(headers)} different headers among the curves, "
                        f"which is more than one score_curves.py vintage: "
                        f"{shown}")
    if problems:
        return ordered, problems
    header = next(iter(headers))
    tmp = merged_csv.with_name(merged_csv.name + ".tmp")
    with open(tmp, "w", newline="") as out:
        out.write(header + "\n")
        for run in ordered:
            with open(chosen[run][1], newline="") as handle:
                handle.readline()
                for line in handle:
                    if line.strip():
                        out.write(line if line.endswith("\n") else line + "\n")
    os.replace(tmp, merged_csv)
    return ordered, problems


def verify_curves(merged_csv: Path, before_rows: int, per_host: dict,
                  union: list, problems: list, missing: list,
                  host_dirs: dict) -> bool:
    """The results merge's verification, over run names rather than keys.

    Every per-run row is expected in the merged file exactly once, so the
    merged row count must equal the sum over the union of each chosen
    curve's rows. A host that answered nothing is INCOMPLETE, for the reason
    verify_merge gives: arithmetic over the hosts that did answer agrees
    with itself.
    """
    ok = not missing and not problems
    print("\nVerification")
    if missing:
        print(f"  INCOMPLETE: no curves from {', '.join(missing)} — the "
              f"merged file is missing that host's share of the pass.")
        print("  Everything below is over the hosts that did answer, so it "
              "cannot detect that.")
    for problem in problems:
        print(f"  MISMATCH: {problem}")
    merged_rows = count_csv_rows(merged_csv)
    print(f"  merged rows:    {merged_rows}")
    print(f"  curves merged:  {len(union)}")
    print(f"  local before:   {before_rows} rows")
    for host, curves in per_host.items():
        failures = host_dirs[host] / "failures.txt"
        failed = (len([ln for ln in failures.read_text().splitlines()
                       if ln.strip()]) if failures.is_file() else 0)
        print(f"  {host}: {len(curves)} curves"
              + (f", {failed} failed attempt(s) in failures.txt"
                 if failed else ""))
    pairs = list(per_host.items())
    for i, (h1, c1) in enumerate(pairs):
        for h2, c2 in pairs[i + 1:]:
            shared = set(c1) & set(c2)
            if shared:
                print(f"  overlap {h1}/{h2}: {len(shared)} run(s) scored on "
                      f"both")
    if problems:
        print(f"  the merged file was not written; {merged_csv} holds what "
              f"it held before ({before_rows} rows)")
        return False
    # Each run counted once: the first host holding it is the one written.
    seen: set = set()
    expected = 0
    for host, curves in per_host.items():
        for run, path in curves.items():
            if run in union and run not in seen:
                seen.add(run)
                expected += count_csv_rows(path)
    print(f"  expected rows:  {expected}")
    if merged_rows != expected:
        ok = False
        print(f"  MISMATCH: the merged file holds {merged_rows} rows against "
              f"{expected} across the chosen curves")
    if ok:
        print("  OK: every curve is present exactly once.")
    return ok


def collect_curves(hosts: list, out_name: str, dry_run: bool) -> int:
    local_out = curves_dir_local(out_name)
    merged_csv = REPO_ROOT / "experiments" / f"{out_name}.csv"
    print(f"Collecting curves: experiments/{out_name}/ from each host into "
          f"experiments/{out_name}/<host>/, joined into "
          f"experiments/{out_name}.csv")
    print(f"Hosts:             {', '.join(hosts)}\n")
    if dry_run:
        rows, ok = [], True
        for host in hosts:
            pending = pull_curves(host, out_name, True)
            if not pending["ok"]:
                ok = False
                rows.append([host, f"(rsync probe failed: "
                                   f"{pending.get('error')})", "-"])
                continue
            rows.append([host, f"experiments/{out_name}/{host}/",
                         f"{pending.get('files', 0)} files / "
                         f"{human_bytes(pending.get('bytes'))}"])
        print(render_table(rows, ["HOST", "INTO", "WOULD TRANSFER"]))
        print("\nDry run — nothing transferred, nothing written.")
        return 0 if ok else 1

    before_rows = count_csv_rows(merged_csv)
    per_host: dict = {}
    host_dirs: dict = {}
    missing: list = []
    for host in hosts:
        pulled = pull_curves(host, out_name, False)
        if not pulled["ok"]:
            print(f"  ERROR: {host} transfer failed ({pulled.get('error')})")
            missing.append(host)
            continue
        curves = host_curves(pulled["dest"])
        if not curves:
            print(f"  ERROR: {host} contributed no curves under "
                  f"experiments/{out_name}/")
            missing.append(host)
            continue
        print(f"  {host}: {len(curves)} curve(s) pulled")
        per_host[host] = curves
        host_dirs[host] = pulled["dest"]
    if not per_host:
        print(f"\nNo curves found on any host — nothing merged.")
        return 1
    union, problems = merge_curves(per_host, merged_csv)
    if not problems:
        print(f"\nMerged {len(union)} curve(s) → {merged_csv}")
    return 0 if verify_curves(merged_csv, before_rows, per_host, union,
                              problems, missing, host_dirs) else 1


def cmd_collect(args: argparse.Namespace) -> int:
    hosts = [args.host] if args.host else list(HOSTS)
    if getattr(args, "curves", None):
        return collect_curves(hosts, args.curves, args.dry_run)
    csv_name, result_dir = resolve_profile(args)
    results_csv = REPO_ROOT / "experiments" / csv_name
    # Named for what actually chose the file: --csv bypasses --profile
    # entirely, and a banner reading "Profile: full" over an arbiter-probe
    # collect describes a run that is not happening.
    source = (f"{csv_name} (--csv override, --profile unused)" if args.csv
              else f"{args.profile} → {csv_name}")
    print(f"Collecting: {source}"
          + (f" + experiments/{result_dir}/" if result_dir else " (CSV only)"))
    print(f"Hosts:      {', '.join(hosts)}\n")

    if args.dry_run:
        return collect_dry_run(hosts, csv_name, result_dir, results_csv)

    before = key_set(results_csv)
    tmp_dir = REPO_ROOT / "experiments" / ".merge_tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    pulled: dict = {}
    missing: list[str] = []
    for host in hosts:
        # merge_experiments does the transfer and the merge; only the ssh
        # destination differs, so pull_source is called directly rather than
        # through resolve_source, whose REMOTES entry is the FQDN form.
        # It wraps only the CSV rsync against failure, so the per-run transfer
        # raises out of here on an unreachable host and would otherwise lose
        # the reachable host's rows to a traceback.
        try:
            path = merge.pull_source(host, source_root(host),
                                     False, tmp_dir, csv_name, result_dir)
        except subprocess.CalledProcessError as exc:
            print(f"  ERROR: {host} transfer failed (rsync exit "
                  f"{exc.returncode})")
            missing.append(host)
            continue
        if path is None:
            print(f"  ERROR: {host} contributed no {csv_name}")
            missing.append(host)
            continue
        pulled[host] = path

    if not pulled:
        print(f"\nNo {csv_name} found on any host — nothing merged.")
        return 1
    per_host = {host: key_set(path) for host, path in pulled.items()}
    # Merge what did arrive: the merge is idempotent and keyed, so a later run
    # against the recovered host completes the file rather than redoing it.
    # The exit status still fails, because a partial collect that reports
    # success is the silent loss this verification exists to catch.
    merge.merge_csv(list(pulled.values()), results_csv)
    return 0 if verify_merge(results_csv, before, per_host, missing) else 1


# -- TOML, in the subset these files use --------------------------------------

# Written by hand rather than through tomllib because tick runs on the lab
# machines, whose python3 is 3.10.12 -- tomllib arrived in 3.11, and neither
# tomli nor toml is installed there. A campaign that could only be enqueued
# from this workstation would defeat the point of the queue. The subset is
# strings, integers, booleans, arrays, inline tables and [table] /
# [[array-of-tables]] headers, which is everything campaign.toml and a queue
# entry use; test_campaign.py checks it against tomllib on every fixture, so
# the two cannot drift apart in what they accept.


class TomlError(ValueError):
    """A campaign or queue file that does not parse."""


BARE_KEY_CHARS = frozenset(string.ascii_letters + string.digits + "_-")
STRING_ESCAPES = {'"': '"', "\\": "\\", "n": "\n", "t": "\t", "r": "\r"}


class TomlReader:
    def __init__(self, text: str):
        self.text = text
        self.pos = 0

    # -- primitives
    def fail(self, message: str):
        line = self.text.count("\n", 0, min(self.pos, len(self.text))) + 1
        raise TomlError(f"line {line}: {message}")

    def peek(self) -> str:
        return self.text[self.pos] if self.pos < len(self.text) else ""

    def skip(self, newlines: bool) -> None:
        while self.pos < len(self.text):
            ch = self.text[self.pos]
            if ch in " \t" or (newlines and ch in "\r\n"):
                self.pos += 1
            elif ch == "#":
                end = self.text.find("\n", self.pos)
                self.pos = len(self.text) if end < 0 else end
            else:
                return

    def expect(self, ch: str) -> None:
        if self.peek() != ch:
            self.fail(f"expected {ch!r}, found {self.peek()!r}")
        self.pos += 1

    # -- values
    def read_key(self) -> str:
        if self.peek() in "\"'":
            return self.read_string()
        start = self.pos
        while self.peek() in BARE_KEY_CHARS and self.peek():
            self.pos += 1
        if self.pos == start:
            self.fail(f"expected a key, found {self.peek()!r}")
        return self.text[start:self.pos]

    def read_dotted_key(self) -> list:
        parts = [self.read_key()]
        while True:
            self.skip(False)
            if self.peek() != ".":
                return parts
            self.pos += 1
            self.skip(False)
            parts.append(self.read_key())

    def read_string(self) -> str:
        quote = self.peek()
        self.pos += 1
        out = []
        while True:
            if self.pos >= len(self.text):
                self.fail("unterminated string")
            ch = self.text[self.pos]
            if ch == "\n":
                self.fail("unterminated string")
            self.pos += 1
            if ch == quote:
                return "".join(out)
            if ch == "\\" and quote == '"':
                esc = self.text[self.pos:self.pos + 1]
                if esc not in STRING_ESCAPES:
                    self.fail(f"unsupported escape \\{esc}")
                out.append(STRING_ESCAPES[esc])
                self.pos += 1
            else:
                out.append(ch)

    def read_value(self):
        ch = self.peek()
        if ch in "\"'":
            return self.read_string()
        if ch == "[":
            return self.read_array()
        if ch == "{":
            return self.read_inline_table()
        start = self.pos
        while self.peek() and self.peek() not in ",]}\r\n#":
            self.pos += 1
        word = self.text[start:self.pos].strip()
        if word == "true":
            return True
        if word == "false":
            return False
        digits = word.replace("_", "")
        if digits and (digits.lstrip("+-").isdigit() and digits.lstrip("+-")):
            return int(digits)
        self.pos = start
        self.fail(f"unsupported value {word!r}: campaign files hold strings, "
                  f"integers, booleans, arrays and inline tables only")

    def read_array(self) -> list:
        self.expect("[")
        out = []
        while True:
            self.skip(True)
            if self.peek() == "]":
                self.pos += 1
                return out
            if not self.peek():
                self.fail("unterminated array")
            out.append(self.read_value())
            self.skip(True)
            if self.peek() == ",":
                self.pos += 1
            elif self.peek() != "]":
                self.fail(f"expected ',' or ']', found {self.peek()!r}")

    def read_inline_table(self) -> dict:
        # Newlines are rejected inside braces, as TOML itself rejects them:
        # accepting more than tomllib does would let a file parse here and fail
        # for the editor and for any later reader.
        self.expect("{")
        out: dict = {}
        while True:
            self.skip(False)
            if self.peek() == "}":
                self.pos += 1
                return out
            if self.peek() in "\r\n" or not self.peek():
                self.fail("unterminated inline table (no newlines inside {})")
            key = self.read_key()
            self.skip(False)
            self.expect("=")
            self.skip(False)
            if key in out:
                self.fail(f"duplicate key {key!r}")
            out[key] = self.read_value()
            self.skip(False)
            if self.peek() == ",":
                self.pos += 1
            elif self.peek() != "}":
                self.fail(f"expected ',' or '}}', found {self.peek()!r}")

    # -- document
    def document(self) -> dict:
        root: dict = {}
        table = root
        while True:
            self.skip(True)
            if not self.peek():
                return root
            if self.peek() == "[":
                table = self.read_header(root)
                continue
            key = self.read_key()
            self.skip(False)
            self.expect("=")
            self.skip(False)
            if key in table:
                self.fail(f"duplicate key {key!r}")
            table[key] = self.read_value()
            self.skip(False)
            if self.peek() and self.peek() not in "\r\n#":
                self.fail(f"trailing text after a value: {self.peek()!r}")

    def read_header(self, root: dict) -> dict:
        self.expect("[")
        array = self.peek() == "["
        if array:
            self.pos += 1
        self.skip(False)
        parts = self.read_dotted_key()
        self.skip(False)
        self.expect("]")
        if array:
            self.expect("]")
        table = root
        for part in parts[:-1]:
            table = table.setdefault(part, {})
            if not isinstance(table, dict):
                self.fail(f"{part!r} is not a table")
        last = parts[-1]
        if array:
            table.setdefault(last, [])
            if not isinstance(table[last], list):
                self.fail(f"{last!r} is not an array of tables")
            table[last].append({})
            return table[last][-1]
        if last in table:
            self.fail(f"duplicate table [{'.'.join(parts)}]")
        table[last] = {}
        return table[last]


def parse_toml(text: str) -> dict:
    return TomlReader(text).document()


def toml_value(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        body = value.replace("\\", "\\\\").replace('"', '\\"')
        return '"' + body.replace("\n", "\\n").replace("\t", "\\t") + '"'
    if isinstance(value, (list, tuple)):
        return "[" + ", ".join(toml_value(v) for v in value) + "]"
    if isinstance(value, dict):
        if not value:
            return "{}"
        return "{ " + ", ".join(f"{k} = {toml_value(v)}"
                                for k, v in value.items()) + " }"
    raise TomlError(f"cannot write {type(value).__name__} to TOML")


def dump_toml(table: dict) -> str:
    """Emit a flat table. Key order is the caller's, so entries stay diffable."""
    return "".join(f"{key} = {toml_value(value)}\n"
                   for key, value in table.items())


# -- The campaign declaration -------------------------------------------------

class CampaignError(Exception):
    """A campaign.toml that cannot be acted on. Always fatal: acting on half a
    declaration is what a bad seed split looks like from the inside."""


CAMPAIGN_KEYS = {"name", "branch", "profile", "hosts", "phases", "build",
                 "configs", "description"}
# A phase is a search run (`kind = "run"`, the default and what every phase
# was before the key existed) or an offline scoring pass over a results
# directory (`kind = "score"`). Each kind reads its own keys: the runner's
# selection keys mean nothing to the scorer, and the scorer's budgets mean
# nothing to the runner, so a key from the other kind is refused by name
# rather than carried along unread.
PHASE_KINDS = ("run", "score")
RUN_PHASE_KEYS = {"name", "kind", "profile", "jobs", "sweeps", "specs", "hosts"}
SCORE_BUDGET_KEYS = ("workers", "cores", "cuts", "maximal_timeout",
                     "compare_timeout", "deadline_s", "wall_cap_s")
SCORE_PHASE_KEYS = {"name", "kind", "profile", "results", "out", "hosts",
                    *SCORE_BUDGET_KEYS}
PHASE_KEYS = RUN_PHASE_KEYS | SCORE_PHASE_KEYS

# `describe` prints a declaration for a campaign that has already closed. It
# is not one of these: the factor cross below is what the results CSV carries,
# which a live declaration has no use for, since the runner's profile holds
# the cross for a campaign that has not run yet.
ARCHIVE_ATTRIBUTION = "inferred"
ARCHIVE_FACTOR_KEYS = ("sweeps", "schemes", "weakenings", "metrics",
                       "repair_modes", "specs")


def parse_seed_range(text: str, where: str) -> list:
    """``"0-99"``, ``"7"`` or ``"0-9,20-29"`` to an explicit, sorted seed list.

    Explicit because run_experiments.py's ``--seeds`` takes a list of integers
    and nothing else, and because an overlap between two hosts is only visible
    once both ranges are sets.
    """
    if not isinstance(text, str) or not text.strip():
        raise CampaignError(f"{where}: seed range must be a non-empty string")
    seeds: list = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            raise CampaignError(f"{where}: empty component in {text!r}")
        lo_text, dash, hi_text = part.partition("-")
        lo_text, hi_text = lo_text.strip(), hi_text.strip()
        if not lo_text.isdigit() or (dash and not hi_text.isdigit()):
            raise CampaignError(
                f"{where}: {part!r} is not a seed or a LOW-HIGH range")
        lo = int(lo_text)
        hi = int(hi_text) if dash else lo
        if hi < lo:
            raise CampaignError(f"{where}: {part!r} counts backwards")
        seeds.extend(range(lo, hi + 1))
    if len(set(seeds)) != len(seeds):
        raise CampaignError(f"{where}: {text!r} repeats a seed")
    return sorted(seeds)


def parse_host_split(hosts, where: str) -> dict:
    """A ``hosts`` table to a host -> seed list map, checked for overlap.

    Shared by the campaign-level table and a phase's override, so a phase's
    split is held to exactly the standard the campaign's is: the overlap check
    is the one that has to run at both levels, since a phase that re-declares
    a split is a second chance to write the same seed twice.
    """
    if not isinstance(hosts, dict) or not hosts:
        raise CampaignError(f"{where} must be a non-empty table, "
                            f"e.g. hosts = {{ av2 = \"0-99\" }}")
    seeds_by_host: dict = {}
    for host, text in hosts.items():
        if host not in HOSTS and host != LOCAL:
            raise CampaignError(f"{where}: unknown host {host!r}; "
                                f"known: {', '.join([*HOSTS, LOCAL])}")
        seeds_by_host[host] = parse_seed_range(text, f"{where}.{host}")
    pairs = list(seeds_by_host.items())
    for i, (h1, s1) in enumerate(pairs):
        for h2, s2 in pairs[i + 1:]:
            shared = sorted(set(s1) & set(s2))
            if shared:
                raise CampaignError(
                    f"{where}: hosts {h1} and {h2} share {len(shared)} seed(s) "
                    f"(e.g. {', '.join(str(s) for s in shared[:5])}). Both "
                    f"hosts would run them and the merge would keep one row "
                    f"per key, so the overlap costs time and yields nothing.")
    return seeds_by_host


def format_seed_range(seeds: list) -> str:
    """The inverse, collapsing runs, so a queue entry reads like what was asked
    for rather than as a hundred integers."""
    parts, run = [], []
    for seed in sorted(seeds):
        if run and seed == run[-1] + 1:
            run.append(seed)
            continue
        if run:
            parts.append(f"{run[0]}-{run[-1]}" if len(run) > 1 else str(run[0]))
        run = [seed]
    if run:
        parts.append(f"{run[0]}-{run[-1]}" if len(run) > 1 else str(run[0]))
    return ",".join(parts)


def known_profiles() -> set:
    """Profiles the runner in *this* checkout defines.

    Imported lazily and never mirrored here: run_experiments.PROFILES is the
    definition, campaign.toml only names one of them. Eighteen archived
    campaigns vendor their own copy of that file, so a second copy of the table
    would be the nineteenth thing to keep in sync.
    """
    import run_experiments  # noqa: PLC0415
    return set(run_experiments.PROFILES)


def profile_configs_dir(profile: str) -> str:
    """A profile's configs directory, relative to the repo root.

    ``PROFILES`` holds it as an absolute path, built from the ``REPO_ROOT`` of
    whichever checkout imported the runner — this one. The stage script runs on
    a lab machine whose checkout sits under a different home directory, so the
    absolute form names a path that does not exist over there, and a check
    against it would pass or fail for a reason that has nothing to do with the
    campaign. Relative to the runner's own root is the one form that means the
    same thing on both sides, since the script has already cd'd to the host's
    checkout before it looks.
    """
    return profile_dir(profile, "configs_dir", "configs")


def profile_results_dir(profile: str) -> str:
    """A profile's results directory, relative to the repo root.

    What a score phase scores when it names a profile rather than a
    directory: the runner wrote that profile's runs there, on this host, in
    the phase before.
    """
    return profile_dir(profile, "results_dir", "results")


def profile_dir(profile: str, key: str, what: str) -> str:
    import run_experiments  # noqa: PLC0415
    path = Path(run_experiments.PROFILES[profile][key])
    try:
        return str(path.relative_to(run_experiments.REPO_ROOT))
    except ValueError:
        raise CampaignError(
            f"profile {profile!r} puts its {what} at {path}, outside the "
            f"checkout — stage cannot name that path on a host") from None


def curves_dir_for(results: str) -> str:
    """Default output directory for a score phase: ``curves-<stem>``, the stem
    being the results directory's name without its ``results-`` prefix, so
    ``results-rematch`` scores into ``curves-rematch`` beside it."""
    stem = Path(results).name
    if stem.startswith("results-"):
        stem = stem[len("results-"):]
    elif stem == "results":
        stem = ""
    parent = str(Path(results).parent)
    name = f"curves-{stem}" if stem else "curves"
    return name if parent in ("", ".") else f"{parent}/{name}"


def score_defaults() -> dict:
    """The scorer's own defaults, read from it rather than copied here."""
    import score_campaign  # noqa: PLC0415
    return dict(score_campaign.DEFAULTS)


def campaign_path(name: str, root: Path | None = None) -> Path:
    return (root or REPO_ROOT) / "experiments" / name / "campaign.toml"


def load_campaign(name: str, root: Path | None = None) -> dict:
    """Read and validate ``experiments/<name>/campaign.toml``.

    Every failure here is louder than the thing it prevents. An overlapping
    seed range is the one worth the most noise: both hosts run the overlap,
    which doubles the work, and the merge keys those rows together and keeps
    one, so the campaign silently costs more and yields less than it says.
    """
    path = campaign_path(name, root)
    try:
        raw = parse_toml(path.read_text())
    except FileNotFoundError:
        raise CampaignError(f"no campaign declaration at {path}") from None
    except TomlError as exc:
        raise CampaignError(f"{path}: {exc}") from None

    unknown = sorted(set(raw) - CAMPAIGN_KEYS)
    if unknown:
        raise CampaignError(f"{path}: unknown key(s) {', '.join(unknown)}; "
                            f"known: {', '.join(sorted(CAMPAIGN_KEYS))}")
    declared = raw.get("name")
    if declared != name:
        raise CampaignError(
            f"{path}: name = {declared!r} but the directory is {name!r} — "
            f"the archive and the declaration must agree")
    branch = raw.get("branch")
    if not isinstance(branch, str) or not branch:
        raise CampaignError(f"{path}: branch must be a non-empty string")

    seeds_by_host = parse_host_split(raw.get("hosts"), f"{path}: hosts")

    phases = raw.get("phases")
    if not isinstance(phases, list) or not phases:
        raise CampaignError(f"{path}: phases must be a non-empty array of "
                            f"tables, e.g. phases = [ {{ profile = \"x\", "
                            f"jobs = 4 }} ]")
    profiles = known_profiles()
    normalised = []
    for index, phase in enumerate(phases):
        where = f"{path}: phases[{index}]"
        if not isinstance(phase, dict):
            raise CampaignError(f"{where} is not a table")
        kind = phase.get("kind", "run")
        if kind not in PHASE_KINDS:
            raise CampaignError(f"{where}: kind must be one of "
                                f"{', '.join(PHASE_KINDS)}, not {kind!r}")
        allowed = SCORE_PHASE_KEYS if kind == "score" else RUN_PHASE_KEYS
        unknown = sorted(set(phase) - allowed)
        if unknown:
            raise CampaignError(f"{where}: unknown key(s) {', '.join(unknown)} "
                                f"on a {kind} phase; known: "
                                f"{', '.join(sorted(allowed))}")
        profile = phase.get("profile", raw.get("profile"))
        if kind == "score":
            results = phase.get("results")
            if results is not None and (not isinstance(results, str)
                                        or not results):
                raise CampaignError(f"{where}: results must be a non-empty "
                                    f"string naming a directory under the "
                                    f"checkout")
            if results is None and profile is None:
                raise CampaignError(
                    f"{where}: a score phase needs `results` (a results "
                    f"directory) or `profile` (whose results directory it "
                    f"scores), and has neither")
        # A score phase naming a results directory needs no profile. Every
        # other phase does, and a profile named anywhere must be one this
        # checkout's runner defines.
        if profile is not None or kind == "run":
            if not isinstance(profile, str) or not profile:
                raise CampaignError(f"{where}: no profile, and no top-level "
                                    f"profile to fall back on")
            if profile not in profiles:
                raise CampaignError(
                    f"{where}: run_experiments.py in this checkout defines "
                    f"no profile {profile!r}. A campaign names a profile the "
                    f"runner defines; it does not declare one. Known: "
                    f"{', '.join(sorted(profiles))}")
        phase_hosts = None
        if phase.get("hosts") is not None:
            phase_hosts = parse_host_split(phase["hosts"], f"{where}: hosts")
            extra = sorted(set(phase_hosts) - set(seeds_by_host))
            if extra:
                raise CampaignError(
                    f"{where}: hosts {', '.join(extra)} are not declared at "
                    f"the campaign level. A phase narrows the split; it "
                    f"cannot add a host, which stage never staged and the "
                    f"other phases would never run on.")
        if kind == "score":
            normalised.append(normalise_score_phase(phase, where, profile,
                                                    phase_hosts))
            continue
        jobs = phase.get("jobs")
        if jobs is not None and (isinstance(jobs, bool) or not isinstance(jobs, int)
                                 or jobs < 1):
            raise CampaignError(f"{where}: jobs must be a positive integer")
        for key in ("sweeps", "specs"):
            value = phase.get(key)
            if value is not None and not (
                    isinstance(value, list)
                    and all(isinstance(v, str) for v in value)):
                raise CampaignError(f"{where}: {key} must be an array of "
                                    f"strings")
        normalised.append({"name": phase.get("name", profile), "kind": "run",
                           "profile": profile, "jobs": jobs,
                           "sweeps": phase.get("sweeps"),
                           "specs": phase.get("specs"),
                           "hosts": phase_hosts})

    build = raw.get("build", DEFAULT_BUILD_CMD)
    if not isinstance(build, str) or not build:
        raise CampaignError(f"{path}: build must be a non-empty string")
    configs = raw.get("configs")
    if configs is not None and (not isinstance(configs, str) or not configs):
        raise CampaignError(f"{path}: configs must be a non-empty string")
    # Every profile the run phases name, not just the campaign-level one: a
    # campaign whose phases straddle two profiles reads two configs
    # directories, and checking only the default leaves the second phase to
    # fail on the host, hours after the stage said the host was ready. A score
    # phase reads no configs; what it reads is a results directory, checked
    # separately below.
    config_dirs = sorted({profile_configs_dir(phase["profile"])
                          for phase in normalised if phase_kind(phase) == "run"})
    return {"name": name, "branch": branch, "build": build, "path": path,
            "configs": configs, "config_dirs": config_dirs,
            "results_dirs": staged_results_dirs(normalised, seeds_by_host),
            "hosts": seeds_by_host, "phases": normalised,
            "description": raw.get("description", "")}


def normalise_score_phase(phase: dict, where: str, profile,
                          phase_hosts) -> dict:
    """A score phase's record: where it reads, where it writes, its budgets.

    ``results`` defaults to the named profile's results directory, made
    relative the way the configs directory is, since the same host-side
    check reads it; ``out`` defaults to ``curves-<stem>`` beside it. Every
    budget defaults to the scorer's own value and is carried explicitly to the
    command line, so the manifest the host writes names what the declaration
    meant rather than what the scorer happened to default to.
    """
    # `results` and the profile were checked by the caller; one of the two is
    # present by the time this runs.
    results = phase.get("results") or profile_results_dir(profile)
    out = phase.get("out", curves_dir_for(results))
    if not isinstance(out, str) or not out:
        raise CampaignError(f"{where}: out must be a non-empty string naming "
                            f"a directory under the checkout")
    budgets = score_defaults()
    for key in SCORE_BUDGET_KEYS:
        value = phase.get(key)
        if value is None:
            continue
        if isinstance(value, bool) or not isinstance(value, int) or value < 1:
            raise CampaignError(f"{where}: {key} must be a positive integer")
        budgets[key] = value
    if "wall_cap_s" not in budgets:
        import score_campaign  # noqa: PLC0415
        budgets["wall_cap_s"] = score_campaign.wall_cap_default(
            budgets["deadline_s"])
    return {"name": phase.get("name", Path(out).name), "kind": "score",
            "profile": profile, "jobs": None, "sweeps": None, "specs": None,
            "hosts": phase_hosts, "results": results, "out": out, **budgets}


def phase_kind(phase: dict) -> str:
    """`run` unless the phase says otherwise. A phase record built before
    the key existed carries none, and every one of those is a run."""
    return phase.get("kind") or "run"


def staged_results_dirs(phases: list, seeds_by_host: dict) -> dict:
    """Per host, the results directories `stage` has to find there already.

    A score phase reads a results directory the way a run phase reads a
    configs directory, and an absent one fails the phase on the host after
    the stage said the host was ready. But the common case is a score phase
    scoring the run phase declared before it in the same campaign, whose
    results directory does not exist until that phase has run, so those are
    not demanded at stage time: only a directory no earlier run phase of this
    campaign produces is, and only on the hosts the phase runs on, since a
    phase narrowed away from a host reads nothing there.
    """
    out: dict = {}
    for host, seeds in seeds_by_host.items():
        produced: set = set()
        wanted: list = []
        for phase in phases:
            if not phase_seeds(phase, host, seeds):
                continue
            if phase_kind(phase) == "run":
                produced.add(profile_results_dir(phase["profile"]))
            elif (phase["results"] not in produced
                  and phase["results"] not in wanted):
                wanted.append(phase["results"])
        out[host] = sorted(wanted)
    return out


def campaign_hosts(campaign: dict, only: list | None) -> list:
    """The hosts to act on: every declared one, or the named subset."""
    declared = list(campaign["hosts"])
    if not only:
        return declared
    unknown = [h for h in only if h not in campaign["hosts"]]
    if unknown:
        raise CampaignError(
            f"{campaign['name']} declares no host(s) {', '.join(unknown)}; "
            f"declared: {', '.join(declared)}")
    return [h for h in declared if h in only]


def phase_seeds(phase: dict, host: str, default: list) -> list:
    """One host's seeds for one phase: the phase's own split, or the campaign's.

    A phase overrides the split where its row count is set by something other
    than the campaign's seed budget — two paths whose sample sizes come from
    separate power calculations are the motivating case, since one range
    cannot serve both and the wider one silently multiplies the narrower
    phase's cost.

    A phase that declares a split and omits a host runs nowhere on it: an
    absent host is a deliberate narrowing, and falling back to the campaign
    range there would hand it the seeds the override exists to withhold.
    """
    if phase.get("hosts") is None:
        return default
    return phase["hosts"].get(host, [])


def entry_phase_seeds(entry: dict, index: int) -> str:
    """The frozen seed range one queue entry runs its `index`-th phase on.

    An entry enqueued before phases could narrow the split carries no
    `phase_seeds`, and falls back to the campaign range it was enqueued under:
    that range is what its finished phases ran on and what their rows are
    keyed by, so re-reading it from a since-edited declaration is the one
    thing the freeze exists to prevent.
    """
    frozen = list(entry.get("phase_seeds") or [])
    if index < len(frozen):
        return frozen[index]
    return entry.get("seeds", "")


def phase_args(phase: dict, seeds: list) -> list:
    """The runner arguments for one phase over one host's seeds.

    The seeds come from the declaration by way of the caller and never from an
    argument typed at launch time: a hand-typed range is how two hosts end up
    running the same seeds.
    """
    if phase_kind(phase) == "score":
        return score_phase_args(phase, seeds)
    args = ["--profile", phase["profile"]]
    if phase.get("jobs"):
        args += ["--jobs", str(phase["jobs"])]
    if phase.get("sweeps"):
        args += ["--sweeps", *phase["sweeps"]]
    if phase.get("specs"):
        args += ["--specs", *phase["specs"]]
    return args + ["--seeds", *[str(s) for s in seeds]]


def score_phase_args(phase: dict, seeds: list) -> list:
    """The scorer's arguments: every budget stated, then the seeds.

    Stated rather than left to the scorer's defaults so the two never
    disagree about what a phase ran under, and so the manifest the scorer
    writes on the host records the declaration's values.
    """
    args = ["--results", phase["results"], "--out", phase["out"]]
    for key in SCORE_BUDGET_KEYS:
        args += [f"--{key.replace('_', '-')}", str(phase[key])]
    return args + ["--seeds", *[str(s) for s in seeds]]


def phase_launcher(phase: dict) -> str:
    """The command a phase's arguments follow: the runner, or the scorer."""
    return SCORER_CMD if phase_kind(phase) == "score" else RUNNER_CMD


def phase_command(phase: dict, seeds: list) -> str:
    return " ".join([phase_launcher(phase)]
                    + [shlex.quote(a) for a in phase_args(phase, seeds)])


# -- describe -----------------------------------------------------------------
#
# A closed campaign's declaration is derived on demand and printed; nothing is
# written into experiments/. The archive is the source, and a file caching this
# derivation beside its own inputs could only drift from them. It would also be
# an anachronism: a campaign is reproduced at the commit its PROVENANCE.json
# names, through the vendored scripts/, and campaign.py does not exist there.

# The results CSV's key columns are the factor cross that ran, one archive key
# per column. The CSV is preferred over the vendored PROFILES dict throughout:
# the dict says what was intended and the CSV says what happened, and the two
# differ wherever a campaign was launched with --sweeps or --specs, or stopped
# early. A column the header lacks is reported as unrecorded, never defaulted —
# merge_experiments.fill_defaults would supply LEGACY_SELECTION and friends
# here, which is exactly the invention this verb must not make.
CROSS_COLUMNS = (("sweep", "sweeps"), ("selection", "schemes"),
                 ("weakening", "weakenings"), ("metric", "metrics"),
                 ("repair_mode", "repair_modes"), ("spec", "specs"))

# Per-host CSVs are named both ways in the archive -- av2.results-muc.csv and
# av2-results-factorial.csv -- so the separator is matched, not assumed. Eight
# campaigns use the hyphen and five the dot.
HOST_CSV = "^(?P<host>av[0-9]+)[.-](?P<stem>.+)[.]csv$"


def archive_scripts(archive: Path) -> list:
    return sorted(p.name for p in (archive / "scripts").glob("*")
                  if p.is_file())


def archive_kind(archive: Path) -> str | None:
    try:
        facts = json.loads((archive / "PROVENANCE.json").read_text())
    except (OSError, ValueError):
        return None
    kind = facts.get("kind")
    return kind if isinstance(kind, str) and kind else None


def vendored_profile_csvs(archive: Path) -> dict:
    """``{results CSV name: [profile, ...]}`` from the vendored runner.

    Read with ``ast`` rather than by importing: the vendored copies span a
    month of the runner's history and are records, not modules, so nothing
    here should execute one. A vintage with no PROFILES dict at all -- the
    2026-07-10 runner took --sweeps/--specs/--seeds on the command line --
    yields an empty map, and the profile is then simply not recorded.
    """
    path = archive / "scripts" / "run_experiments.py"
    try:
        tree = ast.parse(path.read_text())
    except (OSError, SyntaxError):
        return {}
    table = None
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and len(node.targets) == 1:
            target, assigned = node.targets[0], node.value
        elif isinstance(node, ast.AnnAssign):
            target, assigned = node.target, node.value
        else:
            continue
        if (isinstance(target, ast.Name) and target.id == "PROFILES"
                and isinstance(assigned, ast.Dict)):
            table = assigned
    if table is None:
        return {}
    out: dict = {}
    for key, value in zip(table.keys, table.values):
        if not (isinstance(key, ast.Constant) and isinstance(key.value, str)
                and isinstance(value, ast.Dict)):
            continue
        for field_key, field_value in zip(value.keys, value.values):
            if not (isinstance(field_key, ast.Constant)
                    and field_key.value == "results_csv"):
                continue
            for part in ast.walk(field_value):
                if (isinstance(part, ast.Constant)
                        and isinstance(part.value, str)
                        and part.value.endswith(".csv")):
                    out.setdefault(part.value, []).append(key.value)
    return out


def profile_of_csv(csv_name: str, by_csv: dict) -> str | None:
    """The profile a merged CSV belongs to, where exactly one claims it.

    Several profiles share one CSV (``ablate-tlsf`` and ``h2h-tlsf``, three
    ``replicate`` variants), so the name is the tiebreak: results-X.csv
    belongs to profile X where such a profile is among the claimants. Where
    the tie does not break, no profile is recorded.
    """
    claimants = by_csv.get(csv_name, [])
    if len(claimants) == 1:
        return claimants[0]
    stem = csv_name[len("results-"):-len(".csv")] \
        if csv_name.startswith("results-") else ""
    named = [p for p in claimants if p == stem]
    return named[0] if len(named) == 1 else None


def archive_crosses(archive: Path) -> list:
    """The merged results CSVs, each with the per-host CSVs that fed it.

    A per-host file counts only where its stem matches a merged one, which is
    what keeps av2.well-separation-verdicts.csv -- a post-hoc analysis output
    with no merged counterpart -- from being read as a campaign's host split.
    """
    files = sorted(p.name for p in archive.glob("*.csv"))
    merged = [f for f in files
              if f.startswith("results") and ".bak." not in f
              and not re.match(HOST_CSV, f)]
    out = []
    for name in merged:
        stem = name[:-len(".csv")]
        hosts = {}
        for other in files:
            match = re.match(HOST_CSV, other)
            if match and match.group("stem") == stem:
                hosts[match.group("host")] = archive / other
        out.append((name, hosts))
    return out


def read_cross(path: Path) -> dict:
    """The factor cross one merged results CSV records.

    Values keep the order of their first appearance rather than being sorted
    here, so the list is the file's own order; a merged CSV is sorted on the
    natural key, so most of these read alphabetically and the ones that do not
    (ablate-tlsf's arbiter-aurus, appended after the merge) say so.
    """
    header, rows = merge.read_rows(path)
    order: dict = {key: [] for _, key in CROSS_COLUMNS}
    levels: dict = {}
    seeds = set()
    for record in rows:
        for column, key in CROSS_COLUMNS:
            value = record.get(column)
            if value and value not in order[key]:
                order[key].append(value)
        level = record.get("level_name")
        sweep = record.get("sweep")
        if level and sweep is not None:
            seen = levels.setdefault(sweep, [])
            if level not in seen:
                seen.append(level)
        seed = record.get("seed")
        if seed is not None and seed.isdigit():
            seeds.add(int(seed))
    missing = [key for column, key in CROSS_COLUMNS if column not in header]
    if "level_name" not in header:
        missing.append("levels")
    if "seed" not in header:
        missing.append("seeds")
    cross = {key: values for key, values in order.items() if values}
    return {"rows": len(rows), "levels": levels, "seeds": sorted(seeds),
            "not_recorded": missing, **cross}


def host_split_of(host_csvs: dict) -> dict:
    """``{host: "0-19"}`` from the per-host CSVs, if the blocks are disjoint.

    An overlap means the two files are not a seed split -- a re-collect, or one
    host's file copied over the other's -- so nothing is claimed about it.
    """
    seeds = {}
    for host, path in sorted(host_csvs.items()):
        _, rows = merge.read_rows(path)
        block = {int(r["seed"]) for r in rows
                 if r.get("seed", "").isdigit()}
        if not block:
            return {}
        seeds[host] = block
    blocks = list(seeds.values())
    for i, first in enumerate(blocks):
        for second in blocks[i + 1:]:
            if first & second:
                return {}
    return {host: format_seed_range(sorted(block))
            for host, block in seeds.items()}


def driver_order(archive: Path) -> tuple:
    """The profile order a vendored shell driver runs, and the file saying so.

    Only the elitism campaign has one. Everywhere else the order the phases
    appear in the printed form is the order of their CSV names and carries no
    claim, which is what phase_order in not_recorded says.
    """
    for path in sorted((archive / "scripts").glob("*.sh")):
        found = []
        for profile in re.findall(r"--profile\s+([A-Za-z0-9_-]+)",
                                  path.read_text()):
            if profile not in found:
                found.append(profile)
        if len(found) > 1:
            return found, f"scripts/{path.name}"
    return [], None


def provenance_branch(archive: Path) -> tuple:
    """The branch the archive states, and where it states it.

    Two spellings are in use: a top-level ``branch`` and ``profile_commit
    .branch``. Only a leading bare branch name is taken -- arbiter-probe's
    value continues into prose after it -- and a value that is prose from the
    first character is not a branch name and is left alone.
    """
    try:
        facts = json.loads((archive / "PROVENANCE.json").read_text())
    except (OSError, ValueError):
        return None, None
    profile = facts.get("profile_commit")
    for value, where in ((facts.get("branch"), "branch"),
                         (profile.get("branch")
                          if isinstance(profile, dict) else None,
                          "profile_commit.branch")):
        if not isinstance(value, str):
            continue
        match = re.match(r"^([A-Za-z0-9._/-]+)(\s|$)", value.strip())
        if match:
            return match.group(1), f"PROVENANCE.json: {where}"
    return None, None


def describe_campaign(name: str, archive: Path) -> dict:
    """Derive a campaign declaration from what its archive carries.

    Every field is read out of a file in the archive and recorded against it in
    derived_from; a field no file records is listed in not_recorded and left
    out. An absent field is a true statement about the archive, which a
    plausible default would not be.
    """
    if not archive.is_dir():
        raise CampaignError(f"no archive directory at {archive}")
    table: dict = {"name": name, "attribution": ARCHIVE_ATTRIBUTION}
    sources: dict = {}
    unrecorded = []

    branch, branch_source = provenance_branch(archive)
    if branch:
        table["branch"] = branch
        sources["branch"] = branch_source
    else:
        unrecorded.append("branch")

    crosses = archive_crosses(archive)
    by_csv = vendored_profile_csvs(archive)
    phases = []
    for csv_name, host_csvs in crosses:
        cross = read_cross(archive / csv_name)
        phase: dict = {"name": csv_name[len("results"):-len(".csv")]
                       .lstrip("-") or name}
        profile = profile_of_csv(csv_name, by_csv)
        missing = list(cross["not_recorded"])
        if profile:
            phase["profile"] = profile
        else:
            missing.append("profile")
        phase["cross"] = csv_name
        phase["rows"] = cross["rows"]
        for key in ARCHIVE_FACTOR_KEYS:
            if cross.get(key):
                phase[key] = cross[key]
        if cross["levels"]:
            phase["levels"] = cross["levels"]
        if cross["seeds"]:
            phase["seeds"] = format_seed_range(cross["seeds"])
        split = host_split_of(host_csvs)
        if split:
            phase["hosts"] = split
        if missing:
            phase["not_recorded"] = missing
        phases.append(phase)

    splits = [p.get("hosts") for p in phases]
    if splits and all(s and s == splits[0] for s in splits):
        # One split for the campaign: state it once, at the top, where a live
        # declaration states it.
        table["hosts"] = splits[0]
        sources["hosts"] = "per-host CSVs (disjoint seed blocks)"
        for phase in phases:
            phase.pop("hosts", None)
    elif any(splits):
        sources["hosts"] = "per-host CSVs, per phase (disjoint seed blocks)"
    else:
        unrecorded.append("hosts")

    order, order_source = driver_order(archive)
    if order:
        phases.sort(key=lambda p: (order.index(p["profile"])
                                   if p.get("profile") in order
                                   else len(order)))
        sources["phase_order"] = order_source
    elif len(phases) > 1:
        unrecorded.append("phase_order")

    if phases:
        table["phases"] = phases
        sources["cross"] = ", ".join(csv_name for csv_name, _ in crosses)
    else:
        # No merged results CSV: not a factor-cross campaign at all. The soak
        # and the engine comparison drive their own scripts and share no row
        # schema with the repair campaigns; forcing phases onto them would
        # declare a cross that never existed.
        unrecorded.append("phases")
        scripts = archive_scripts(archive)
        if scripts:
            table["scripts"] = scripts
            sources["scripts"] = "scripts/ (vendored verbatim)"
        kind = archive_kind(archive)
        if kind:
            table["kind"] = kind
            sources["kind"] = "PROVENANCE.json: kind"

    # No campaign passed --jobs: the elitism driver is the only launch script
    # in the archive and it names none, so every run took its profile's
    # default_jobs. That is the runner's value, not the campaign's.
    unrecorded.append("jobs")
    table["not_recorded"] = sorted(unrecorded)
    table["derived_from"] = {key: sources[key] for key in sorted(sources)}
    return table


DESCRIBE_HEADER = """\
# `campaign.py describe {name}`
#
# Derived from that directory just now, not written before the campaign ran.
# attribution = "{attribution}" makes this a reading of the archive rather
# than a record kept at the time: every field is named against the file it
# came from in derived_from, and a field no file records is listed in
# not_recorded rather than defaulted. Nothing here is runnable — reproduce
# this campaign from its scripts/ at the commit PROVENANCE.json names, a
# revision at which campaign.py does not exist.
"""


def dump_campaign(table: dict) -> str:
    """The declaration as TOML: flat keys, then one [[phases]] block each."""
    phases = table.get("phases", [])
    flat = {key: value for key, value in table.items() if key != "phases"}
    out = DESCRIBE_HEADER.format(name=table["name"],
                                 attribution=ARCHIVE_ATTRIBUTION)
    out += dump_toml(flat)
    for phase in phases:
        out += "\n[[phases]]\n" + dump_toml(phase)
    return out


def cmd_describe(args: argparse.Namespace) -> int:
    archive_root = (Path(args.archive_root) if args.archive_root
                    else REPO_ROOT / "experiments")
    names = list(args.campaigns)
    if args.all:
        names = sorted(p.name for p in archive_root.iterdir()
                       if (p / "PROVENANCE.json").is_file())
    if not names:
        print("nothing to describe: name a campaign, or --all")
        return 1
    described, failed = [], 0
    for name in names:
        try:
            described.append(describe_campaign(name, archive_root / name))
        except CampaignError as exc:
            print(f"{name}: {exc}", file=sys.stderr)
            failed += 1
    if args.json:
        print(json.dumps(described, indent=2))
    else:
        print("\n".join(dump_campaign(table) for table in described), end="")
    return 1 if failed else 0


# -- stage --------------------------------------------------------------------

STAGE_PROBE_SCRIPT = r"""
cd @ROOT@ 2>/dev/null || { echo "@M@ERR not a directory: @ROOT@"; exit 3; }
echo "@M@GIT"
git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "?"
git rev-parse HEAD 2>/dev/null || echo "?"
echo "@M@DIRTY"
git status --porcelain --untracked-files=no 2>/dev/null
echo "@M@PS"
ps -o comm=,args= -u "$(id -un)" 2>/dev/null
echo "@M@BIN"
if [ -x @BIN@ ]; then @BIN@ --version 2>/dev/null; fi
""" + QUEUE_BLOCK
# The queue block goes last, so an entry's own text cannot be read as one of
# the sections above it. It is here rather than in a second round trip because
# start has to know whether a tick is about to run the same campaign.


def stage_probe_script(root: str) -> str:
    return render_script(STAGE_PROBE_SCRIPT,
                         ROOT=shlex.quote(root),
                         BIN=shlex.quote("./" + COUNTER_BINARY),
                         QUEUE=shlex.quote(QUEUE_DIR))


def parse_sections(text: str) -> dict:
    """Marker sections to lists of their lines, for the single-value scripts."""
    out: dict = {}
    section = None
    for line in text.splitlines():
        if line.startswith(MARK):
            section = line[len(MARK):].strip().split(" ")[0].lower()
            out.setdefault(section, [])
            rest = line[len(MARK) + len(section):].strip()
            if rest:
                out[section].append(rest)
            continue
        if section is not None:
            out[section].append(line)
    return out


def parse_version_lines(lines: list) -> dict:
    """``counter --version`` output as a dict, ignoring anything unkeyed."""
    out: dict = {}
    for line in lines:
        key, sep, value = line.strip().partition("=")
        if sep:
            out[key.strip()] = value.strip()
    return out


def parse_toml_blocks(text: str, tag: str) -> list:
    """The TOML documents a marker script emitted between @TAG@/END@TAG@ pairs.

    Separate from parse_sections, which flattens a marker into a single key and
    so cannot keep one file's lines apart from the next one's.
    """
    out: list = []
    name, body, inside = "", [], False
    for line in text.splitlines():
        if line.startswith(f"{MARK}{tag} "):
            name, body, inside = line[len(MARK) + len(tag) + 1:].strip(), [], True
        elif line.startswith(f"{MARK}END{tag}"):
            try:
                entry = parse_toml("\n".join(body))
            except TomlError as exc:
                entry = {"error": str(exc)}
            # The base name, as `requeue` and the queue table both name an
            # entry: `find` hands back a path, and one of the three spellings
            # has to win before a message quotes something nobody can retype.
            entry["file"] = Path(name).name
            out.append(entry)
            inside = False
        elif inside:
            body.append(line)
    return out


@dataclass
class HostProbe:
    """What one host answered. Every field is present on every path.

    A dataclass rather than a dict because the two callers read it after an
    ``error`` guard, and a probe that returned early used to leave the rest of
    the keys absent -- an empty answer with no error at all (a shell that
    printed nothing and exited zero) got past that guard and raised a KeyError
    in the middle of staging, which is the worst place for a crash.
    """
    error: str | None = None
    branch: str = "?"
    head: str = "?"
    dirty: list = field(default_factory=list)
    processes: list = field(default_factory=list)
    binary: dict = field(default_factory=dict)
    queue: list = field(default_factory=list)


def probe_processes(ps_lines: list) -> list:
    """The probe's live-process list. A seam, and the only one here.

    `stage` refuses on any live `counter` or `run_experiments.py` on the
    machine, whether or not it belongs to the checkout being staged, which is
    the right reading on a shared host and stays. The consequence is that the
    verdict depends on the whole machine, so a test pointing the probe at a
    local fixture inherits this machine's real processes and refuses for
    reasons that have nothing to do with the fixture. Tests replace this, the
    same way they replace run_shell, and then assert both directions.
    """
    return live_processes(ps_lines)


def parse_stage_probe(text: str, error: str | None = None) -> HostProbe:
    sections = parse_sections(text)
    if "err" in sections:
        return HostProbe(error=" ".join(sections["err"]).strip())
    if "end" not in sections:
        # Covers the empty answer as well as the truncated one: no ##END means
        # nothing below can be trusted, whether or not run_shell reported why.
        return HostProbe(error=error or "probe did not complete")
    git = [ln.strip() for ln in sections.get("git", []) if ln.strip()]
    return HostProbe(
        branch=git[0] if git else "?",
        head=git[1] if len(git) > 1 else "?",
        dirty=[ln.strip() for ln in sections.get("dirty", []) if ln.strip()],
        processes=probe_processes(sections.get("ps", [])),
        binary=parse_version_lines(sections.get("bin", [])),
        queue=parse_toml_blocks(text, "QFILE"),
    )


def stage_refusals(probe: HostProbe, branch: str) -> list:
    """Why this host must not be touched. Empty means it may be.

    Each of the three is a way to destroy work that is not recoverable from
    this side: uncommitted edits, a run in flight, and somebody else's branch.
    The default is to refuse, because the cost of a wrong refusal is a retyped
    command and the cost of a wrong reset is a day of someone's work.
    """
    out = []
    if probe.dirty:
        names = ", ".join(ln.split(None, 1)[-1] for ln in probe.dirty[:5])
        out.append(("dirty", f"{len(probe.dirty)} modified tracked file(s): "
                             f"{names}"))
    if probe.processes:
        comms = ", ".join(dict.fromkeys(p["comm"] for p in probe.processes))
        out.append(("busy", f"live process(es): {comms}"))
    if probe.branch != branch:
        out.append(("branch", f"on {probe.branch}, campaign wants {branch}"))
    return out


STAGE_APPLY_SCRIPT = r"""
cd @ROOT@ 2>/dev/null || { echo "@M@ERR not a directory: @ROOT@"; exit 3; }
git fetch origin @BRANCH@ 2>&1 || { echo "@M@ERR fetch failed"; exit 4; }
if [ "@FORCE@" = "0" ]; then
  ahead=$(git rev-list --count @SHA@..HEAD 2>/dev/null || echo 0)
  if [ "$ahead" != "0" ]; then
    echo "@M@ERR checkout is $ahead commit(s) ahead of @SHA@ — not discarding"
    exit 5
  fi
fi
git checkout @CHECKOUT_FLAGS@ -B @BRANCH@ @SHA@ 2>&1 ||
  { echo "@M@ERR checkout failed"; exit 6; }
echo "@M@BUILD"
buildlog=$(mktemp) || { echo "@M@ERR mktemp failed"; exit 7; }
if ! @BUILD@ > "$buildlog" 2>&1; then
  tail -20 "$buildlog"; rm -f "$buildlog"
  echo "@M@ERR build failed"; exit 7
fi
tail -5 "$buildlog"; rm -f "$buildlog"
echo "@M@CONFIGS"
@CONFIGS@
echo "@M@BIN"
./@BIN@ --version 2>/dev/null || { echo "@M@ERR binary will not report a version"; exit 8; }
echo "@M@END"
"""

# Runs after the build and before the version read, so a generator that fails
# reports as itself rather than as a broken build or a bad binary. The command
# goes in a subshell, unlike the build's: a campaign generating configs for two
# profiles writes two gen_configs.py calls joined with `&&`, and `! a && b`
# binds the negation to the first alone, so the second would run unwatched and
# a failure in the first would read as a successful stage.
CONFIGS_STEP = r"""configlog=$(mktemp) || { echo "@M@ERR mktemp failed"; exit 9; }
if ! ( @CONFIGS_CMD@ ) > "$configlog" 2>&1; then
  tail -20 "$configlog"; rm -f "$configlog"
  echo "@M@ERR configs command failed"; exit 9
fi
tail -5 "$configlog"; rm -f "$configlog"
"""

# The half that runs whether or not the campaign declares a command: every
# declaration written before the key existed still has to fail here rather than
# on the host three ticks later, which is what run_experiments.py exiting 1 on
# an empty configs directory cost the aurus-h2h launch.
#
# `find` with a quoted pattern rather than a glob: the lab login shell is zsh,
# whose default NOMATCH never lets `*.toml` — the exact thing being tested for
# — mean what it means in sh. Unmatched, the find is skipped outright and the
# check reads as an empty directory; matched against a stray .toml in the
# working directory, the find searches for that name instead. Both are the same
# wrong answer, and neither says anything on stdout. -print -quit stops at the
# first hit rather than walking a directory of thousands.
CONFIGS_CHECK = r"""for cfgdir in @CONFIG_DIRS@; do
  if [ -z "$(find "$cfgdir" -name '*.toml' -print -quit 2>/dev/null)" ]; then
    echo "@M@ERR no config files under $(pwd)/$cfgdir — generate them with scripts/gen_configs.py, and declare that command as configs = ... in campaign.toml"
    exit 10
  fi
done
"""
# No `git clean`: a host's untracked files are its results, and a campaign
# staged over them would delete the previous campaign's output. `checkout -f`
# discards tracked modifications only, which is the whole of what --force was
# asked to discard. The build goes through a temporary file rather than a pipe
# into `tail`, whose exit status is the one a pipeline reports: piped, a failed
# build reads as a successful stage and the mismatch only surfaces at the
# version check two lines later.


# A score phase's counterpart: the results directory it reads has to be on
# the host already, unless a run phase of the same campaign writes it first,
# in which case staged_results_dirs leaves it out and the tick checks it when
# the phase's turn comes. A directory rather than a file test, since a results
# tree is thousands of run directories and the question is whether it exists.
RESULTS_CHECK = r"""for resdir in @RESULTS_DIRS@; do
  if [ ! -d "$resdir" ]; then
    echo "@M@ERR no results directory at $(pwd)/$resdir for a score phase — run the campaign that writes it on this host first, or fix results = ... in campaign.toml"
    exit 11
  fi
done
"""


def configs_block(configs: str | None, config_dirs: list,
                  results_dirs: list | None = None) -> str:
    """The configs section: the declared command, then the check, or just the
    check. The check is never conditional — a campaign that declares no command
    is the case that broke, not the case to trust. A score phase's results
    directory is checked the same way, after it."""
    step = (CONFIGS_STEP.replace("@CONFIGS_CMD@", configs) if configs else "")
    # A campaign of score phases alone names no configs directory, and a
    # `for` over an empty list is one shell dialect away from a syntax error.
    check = CONFIGS_CHECK.replace(
        "@CONFIG_DIRS@", " ".join(shlex.quote(d) for d in config_dirs)
    ) if config_dirs else ""
    results = ""
    if results_dirs:
        results = RESULTS_CHECK.replace(
            "@RESULTS_DIRS@", " ".join(shlex.quote(d) for d in results_dirs))
    return step + check + results


def stage_apply_script(root: str, branch: str, sha: str, build: str,
                       configs: str | None, config_dirs: list,
                       force: bool, results_dirs: list | None = None) -> str:
    # CONFIGS first, and the marker last inside render_script: the configs
    # block is itself a script fragment carrying markers of its own.
    # BUILD and BIN go in unquoted -- the first is a command line, the second
    # is spliced into `./@BIN@`.
    return render_script(
        STAGE_APPLY_SCRIPT,
        CONFIGS=configs_block(configs, config_dirs, results_dirs),
        ROOT=shlex.quote(root),
        BRANCH=shlex.quote(branch),
        SHA=shlex.quote(sha),
        BUILD=build,
        BIN=COUNTER_BINARY,
        # Never `--` here: `git checkout -- -B x` reads -B as a path.
        CHECKOUT_FLAGS="-f" if force else "",
        FORCE="1" if force else "0")


STAGE_HEADERS = ["HOST", "BRANCH", "HEAD", "BINARY", "RESULT"]


def stage_paint(row: int, col: int, cell: str, style: Style) -> str:
    """RESULT is green only for a host that is staged and verified.

    Everything else -- refused, not confirmed, stage failed, BINARY MISMATCH
    -- is a host that will run nothing, and reads the same, since the whole
    stage is off if any one of them appears.
    """
    if STAGE_HEADERS[col] != "RESULT":
        return cell
    if cell == "staged":
        return style(cell, "green")
    if cell.startswith("would stage"):
        return style(cell, "dim")
    return style(cell, "red")


def probe_host(host: str) -> HostProbe:
    """One round trip for everything stage and start decide on."""
    text, err = run_shell(host, stage_probe_script(source_path(host)))
    return parse_stage_probe(text or "", err)


def git_output(args: list, root: Path | None = None) -> tuple:
    proc = subprocess.run(["git", "-C", str(root or REPO_ROOT), *args],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        tail = [ln.strip() for ln in proc.stderr.splitlines() if ln.strip()]
        return None, (tail[-1] if tail else f"git {args[0]} exit "
                                            f"{proc.returncode}")
    return proc.stdout.strip(), None


def confirm_discard(host: str, probe: HostProbe, refusals: list) -> bool:
    """Name what --force will destroy, then require the host name typed back.

    A y/n prompt is too cheap for this: the answer has to name the machine, so
    a reflex keystroke cannot approve a reset of the wrong one. Refused
    outright when nothing can be typed, which is what makes --force unusable
    from cron or from a script.
    """
    print(f"\n  --force on {host} will discard:")
    for kind, why in refusals:
        print(f"    {kind}: {why}")
    print(f"    checkout: {probe.branch} at {probe.head[:7]}")
    print("    any commit(s) this checkout holds that the target does not")
    print("  Untracked files, including results, are left alone.")
    if not sys.stdin.isatty():
        print(f"  REFUSED: --force needs a terminal to confirm on; "
              f"{host} untouched.")
        return False
    reply = input(f"  Type {host} to stage anyway: ").strip()
    if reply != host:
        print(f"  Not confirmed; {host} untouched.")
        return False
    return True


def cmd_stage(args: argparse.Namespace) -> int:
    try:
        campaign = load_campaign(args.campaign)
        hosts = campaign_hosts(campaign, args.host)
    except CampaignError as exc:
        print(f"error: {exc}")
        return 2
    branch = campaign["branch"]
    sha, err = git_output(["rev-parse", branch])
    if err:
        print(f"error: no local branch {branch!r} to stage ({err})")
        return 2
    print(f"Staging {campaign['name']} ({branch} at {sha[:7]}) on "
          f"{', '.join(hosts)}")

    if args.dry_run:
        print("Dry run: probing only — nothing pushed, built, or generated, "
              "and the configs check is not run.\n")
    else:
        _, err = git_output(["push", "origin", f"{branch}:{branch}"])
        if err:
            print(f"error: pushing {branch} to origin failed: {err}")
            return 1
        print(f"  pushed {branch} to origin")

    rows, ok = [], True
    for host in hosts:
        probe = probe_host(host)
        if probe.error:
            ok = False
            rows.append([host, "-", "-", "-", f"unreachable: {probe.error}"])
            continue
        commit = probe.binary.get("commit_short", "-")
        refusals = stage_refusals(probe, branch)
        if refusals and not args.force:
            ok = False
            print(f"\n  {host}: REFUSED")
            for kind, why in refusals:
                print(f"    {kind}: {why}")
            print("    pass --force to discard the above (it will name it "
                  "again and ask)")
            rows.append([host, probe.branch, probe.head[:7], commit, "refused"])
            continue
        # Confirm on --force itself, not on whether a refusal was found. The
        # remote script has a fourth thing it declines to discard, a checkout
        # ahead of the target commit, and the probe cannot see it: the host may
        # not hold the target sha until the apply script fetches. So a host
        # that is clean, idle and on the right branch yields no refusal here
        # and still refuses over there. Gating on `refusals` made --force inert
        # for exactly that host and skipped the prompt with it, which is the
        # state a rebased branch leaves both machines in.
        if args.force and not (args.dry_run or confirm_discard(host, probe,
                                                               refusals)):
            ok = False
            rows.append([host, probe.branch, probe.head[:7], commit,
                         "not confirmed"])
            continue
        if args.dry_run:
            rows.append([host, probe.branch, probe.head[:7], commit,
                         "would stage" + (" (--force)" if args.force else "")])
            continue
        text, err = run_shell(
            host, stage_apply_script(source_path(host), branch, sha,
                                     campaign["build"], campaign["configs"],
                                     campaign["config_dirs"], args.force,
                                     (campaign.get("results_dirs") or {})
                                     .get(host)),
            timeout=args.build_timeout)
        result = parse_sections(text or "")
        if err or "err" in result or "end" not in result:
            ok = False
            why = " ".join(result.get("err", [])) or err or "did not complete"
            print(f"\n  {host}: {why}")
            rows.append([host, "?", "?", "?", "stage failed"])
            continue
        version = parse_version_lines(result.get("bin", []))
        fresh = version.get("commit") == sha and version.get("dirty") == "0"
        if not fresh:
            ok = False
        rows.append([host, branch, sha[:7],
                     version.get("commit_short", "?"),
                     "staged" if fresh else "BINARY MISMATCH"])
    print()
    print(render_table(rows, STAGE_HEADERS, stage_paint))
    if not ok:
        print("\nOne or more hosts were not staged. Nothing was launched.")
    return 0 if ok else 1


# -- start --------------------------------------------------------------------

START_HEADERS = ["HOST", "SEEDS", "PID", "RESULT"]


def start_paint(row: int, col: int, cell: str, style: Style) -> str:
    if START_HEADERS[col] != "RESULT":
        return cell
    if cell == "launched":
        return style(cell, "green")
    if cell == "dry run":
        return style(cell, "dim")
    return style(cell, "red")


LAUNCH_SCRIPT = r"""
cd @ROOT@ 2>/dev/null || { echo "@M@ERR not a directory: @ROOT@"; exit 3; }
mkdir -p @LOGDIR@ || { echo "@M@ERR cannot write @LOGDIR@"; exit 4; }
nohup sh -c @CHAIN@ < /dev/null >> @LOG@ 2>&1 &
echo "@M@PID $!"
echo "@M@END"
"""
# nohup and a detached sh, so the run outlives the ssh connection that started
# it. `&&` between phases: a phase that fails stops the chain rather than
# letting the next one run against a half-finished CSV.


def launch_script(root: str, chain: str, log: str) -> str:
    return render_script(LAUNCH_SCRIPT,
                         ROOT=shlex.quote(root),
                         LOGDIR=shlex.quote(str(Path(log).parent)),
                         CHAIN=shlex.quote(chain),
                         LOG=shlex.quote(log))


def pending_queue_entries(probe: HostProbe, name: str) -> list:
    """This campaign's queue entries on that host that a tick could still run.

    A `done` entry is spent, and a `failed` or `cancelled` one needs `requeue`
    before any tick touches it; anything else is a launch waiting to happen.
    """
    return [entry for entry in probe.queue
            if entry.get("campaign") == name
            and entry.get("state") not in ("done", "failed", "cancelled")]


def start_refusals(probe: HostProbe, campaign: dict, sha: str,
                   ignore_queue: bool = False) -> list:
    """Why a host is not ready to be launched on.

    The binary check is the runner's own gate asked one step earlier: it
    refuses a mismatched or dirty binary anyway, and finding that out here
    costs one ssh round trip rather than a launch that dies on the far side of
    a nohup with its message in a log nobody is reading yet.

    The queue check is the same reasoning as `enqueue`'s refusal of a second
    entry: a start beside a queued entry is two runners resuming off one CSV
    as soon as the tick fires, and where a launch path can produce that, the
    default has to be refusal.
    """
    out = []
    if probe.branch != campaign["branch"]:
        out.append(f"on {probe.branch}, campaign wants {campaign['branch']} — "
                   f"run stage first")
    if probe.head != sha:
        out.append(f"head is {probe.head[:7]}, campaign is at {sha[:7]} — "
                   f"run stage first")
    if not probe.binary:
        out.append(f"no {COUNTER_BINARY} that answers --version")
    elif probe.binary.get("commit") != sha:
        out.append(f"binary was built from "
                   f"{probe.binary.get('commit_short', '?')}, not {sha[:7]}")
    elif probe.binary.get("dirty") != "0":
        out.append("binary was built from a dirty tree")
    for proc in probe.processes:
        if proc.get("profile"):
            out.append(f"a runner is already live on --profile "
                       f"{proc['profile']}")
        elif proc.get("kind") == "score":
            out.append(f"a scorer is already live on --out "
                       f"{proc.get('out') or '?'}")
    if not ignore_queue:
        for entry in pending_queue_entries(probe, campaign["name"]):
            out.append(
                f"queue entry {entry.get('file', '?')} is "
                f"{entry.get('state', '?')} for this campaign — the tick will "
                f"run it, and both would resume off one CSV. Pass "
                f"--ignore-queue to launch anyway, or drop the entry.")
    return out


def record_launch(campaign: dict, record: dict) -> Path:
    """Append one launch to ``experiments/<name>/launches.jsonl``.

    Untracked and append-only. The manifest run_experiments.py writes on the
    host is the authority on what a run did; this is the record of what was
    asked for, from the side that asked, which is the half that disappears
    when an ssh session closes.
    """
    path = REPO_ROOT / "experiments" / campaign["name"] / "launches.jsonl"
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a") as handle:
        handle.write(json.dumps(record) + "\n")
    return path


def cmd_start(args: argparse.Namespace) -> int:
    try:
        campaign = load_campaign(args.campaign)
        hosts = campaign_hosts(campaign, args.host)
    except CampaignError as exc:
        print(f"error: {exc}")
        return 2
    sha, err = git_output(["rev-parse", campaign["branch"]])
    if err:
        print(f"error: no local branch {campaign['branch']!r} ({err})")
        return 2
    phases = campaign["phases"]
    print(f"Starting {campaign['name']} on {', '.join(hosts)}: "
          f"{len(phases)} phase(s) per host")

    rows, ok = [], True
    for host in hosts:
        seeds = campaign["hosts"][host]
        # A phase narrowed away from this host contributes no command at all.
        # An empty --seeds is an argparse error rather than a no-op, and the
        # phases are joined with &&, so emitting one would take every later
        # phase on the host down with it.
        chain = " && ".join(phase_command(p, s) for p in phases
                            if (s := phase_seeds(p, host, seeds)))
        if not chain:
            rows.append([host, format_seed_range(seeds), "-",
                         "no phase runs here; every phase narrows it away"])
            continue
        probe = probe_host(host)
        if probe.error:
            ok = False
            rows.append([host, format_seed_range(seeds), "-",
                         f"unreachable: {probe.error}"])
            continue
        refusals = start_refusals(probe, campaign, sha, args.ignore_queue)
        # The override still names what it is racing, so the launch record and
        # the terminal both say a tick may follow this run onto the same CSV.
        for entry in (pending_queue_entries(probe, campaign["name"])
                      if args.ignore_queue else []):
            print(f"\n  {host}: --ignore-queue, racing queue entry "
                  f"{entry.get('file', '?')} ({entry.get('state', '?')})")
        stamp = time.strftime("%Y%m%dT%H%M%S")
        log = f"experiments/{campaign['name']}/launch-{host}-{stamp}.log"
        if args.dry_run:
            # Printed whether or not the host is ready: a dry run is asked in
            # order to read the command, and a host that is merely unstaged
            # would otherwise hide the one thing being checked.
            print(f"\n  {host}: would run\n    {chain}\n    log: {log}")
            for why in refusals:
                print(f"    blocked: {why}")
            ok = ok and not refusals
            rows.append([host, format_seed_range(seeds), "-",
                         "blocked" if refusals else "dry run"])
            continue
        if refusals:
            ok = False
            print(f"\n  {host}: REFUSED")
            for why in refusals:
                print(f"    {why}")
            rows.append([host, format_seed_range(seeds), "-", "refused"])
            continue
        text, err = run_shell(host, launch_script(source_path(host), chain, log))
        sections = parse_sections(text or "")
        if err or "err" in sections or "pid" not in sections:
            ok = False
            why = " ".join(sections.get("err", [])) or err or "no pid returned"
            rows.append([host, format_seed_range(seeds), "-", f"failed: {why}"])
            continue
        pid = (sections["pid"] or ["?"])[0]
        record_launch(campaign, {
            "campaign": campaign["name"], "host": host, "pid": pid,
            "branch": campaign["branch"], "commit": sha,
            "seeds": format_seed_range(seeds), "log": log, "command": chain,
            "phases": [p["name"] for p in phases],
            "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        })
        rows.append([host, format_seed_range(seeds), pid, "launched"])
    print()
    print(render_table(rows, START_HEADERS, start_paint))
    if not args.dry_run:
        print("\nDetached. Poll with `campaign.py status`; do not watch it.")
    return 0 if ok else 1


# -- The queue ----------------------------------------------------------------

# States, and every transition between them:
#
#   (new)    -> queued           enqueue
#   queued   -> running          tick picks the lowest-numbered entry, and
#                                again while it stages the entry's branch
#   running  -> queued           staged, with the phase still to run
#   running  -> queued           phase finished, another phase remains
#   running  -> done             last phase finished
#   running  -> queued           phase failed or was interrupted, attempts left
#   running  -> failed           phase failed or was interrupted, cap reached
#   failed   -> queued           `campaign requeue`, by hand
#   queued   -> cancelled        `campaign dequeue`, by hand
#   running  -> cancelled        `campaign dequeue`, by hand, having killed the
#                                tick holding it first
#   cancelled-> queued           `campaign requeue`, by hand
#
# `running` with no tick holding the lock is an interrupted phase, not a live
# one: a tick runs its phase in the foreground, so the state outliving the
# process means the process died. It costs an attempt, which is what stops a
# phase that kills its tick every time from cycling for ever.


def queue_dir(root: Path | None = None) -> Path:
    return (root or REPO_ROOT) / QUEUE_DIR


def queue_entries(root: Path | None = None) -> list:
    """Every entry in this checkout's queue, lowest number first."""
    directory = queue_dir(root)
    if not directory.is_dir():
        return []
    out = []
    for path in sorted(directory.glob("*.toml")):
        entry: dict
        try:
            entry = parse_toml(path.read_text())
        except (TomlError, OSError) as exc:
            entry = {"error": str(exc)}
        entry["file"] = path.name
        entry["path"] = path
        entry["index"] = entry_index(path.name)
        out.append(entry)
    return sorted(out, key=lambda e: (e["index"], e["file"]))


def entry_index(name: str) -> int:
    head = name.split("-", 1)[0]
    return int(head) if head.isdigit() else 10 ** 6


def entry_body(entry: dict) -> dict:
    """The entry without the fields derived from where it was read."""
    return {k: v for k, v in entry.items()
            if k not in ("file", "path", "index", "error")}


def write_entry(path: Path, entry: dict) -> None:
    """Rewrite an entry atomically, so a tick killed mid-write leaves the old
    state rather than half of the new one."""
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(dump_toml(entry_body(entry)))
    os.replace(tmp, path)


def log_line(entry: dict, message: str) -> None:
    stamp = time.strftime("%Y-%m-%dT%H:%M:%S")
    entry["log"] = (list(entry.get("log", [])) + [f"{stamp} {message}"]
                    )[-QUEUE_LOG_LINES:]
    entry["updated"] = stamp


def new_entry(campaign: dict, host: str, max_attempts: int,
              commit: str) -> dict:
    stamp = time.strftime("%Y-%m-%dT%H:%M:%S")
    entry = {
        "campaign": campaign["name"],
        "host": host,
        "branch": campaign["branch"],
        # The commit the branch was at when this was enqueued, and the build
        # command that turns it into a binary. Both are here rather than read
        # from campaign.toml at tick time because the declaration lives on the
        # campaign's own branch: a tick standing on another branch cannot read
        # a word of it until after the checkout it is about to perform. These
        # two fields are exactly what that checkout needs.
        #
        # Freezing the commit has the same force as freezing the seeds below.
        # A campaign's phases run over hours and requeue between them, and one
        # whose phases straddled two commits would write rows under a single
        # `commit` column that came from two different binaries.
        "commit": commit,
        "build": campaign["build"],
        # Frozen at enqueue: an edit to campaign.toml between enqueue and tick
        # must not silently move a host's share of the seeds under a run that
        # has already produced rows against the old split. Phases that narrow
        # the split are frozen one by one for the same reason — freezing the
        # campaign range alone would leave every override free to move.
        "seeds": format_seed_range(campaign["hosts"][host]),
        "phase_seeds": [format_seed_range(phase_seeds(p, host,
                                                      campaign["hosts"][host]))
                        for p in campaign["phases"]],
        "phase": 0,
        "phases": len(campaign["phases"]),
        "state": "queued",
        "attempts": 0,
        "max_attempts": max_attempts,
        "created": stamp,
        "updated": stamp,
        "last_error": "",
        "log": [],
    }
    log_line(entry, f"queued: {len(campaign['phases'])} phase(s), seeds "
                    f"{entry['seeds']}")
    return entry


ENQUEUE_SCRIPT = r"""
cd @ROOT@ 2>/dev/null || { echo "@M@ERR not a directory: @ROOT@"; exit 3; }
mkdir -p @QUEUE@ || { echo "@M@ERR cannot create @QUEUE@"; exit 4; }
echo "@M@NAMES"
find @QUEUE@ -maxdepth 1 -type f -name '*.toml' -printf '%f\n' 2>/dev/null | sort
echo "@M@END"
"""

WRITE_ENTRY_SCRIPT = r"""
cd @ROOT@ 2>/dev/null || { echo "@M@ERR not a directory: @ROOT@"; exit 3; }
mkdir -p @QUEUE@ || { echo "@M@ERR cannot create @QUEUE@"; exit 4; }
if [ -f @PATH@ ]; then echo "@M@ERR @PATH@ already exists"; exit 5; fi
printf %s @B64@ | base64 -d > @PATH@.tmp || { echo "@M@ERR write failed"; exit 6; }
mv @PATH@.tmp @PATH@ || { echo "@M@ERR rename failed"; exit 7; }
echo "@M@WROTE @PATH@"
echo "@M@END"
"""
# base64 rather than a heredoc: the body is TOML with quotes and brackets in
# it, and it crosses ssh into a zsh login shell. One encoding step removes
# every quoting question at once, and `base64 -d` is in coreutils on both hosts.


def cmd_enqueue(args: argparse.Namespace) -> int:
    import base64  # noqa: PLC0415
    try:
        campaign = load_campaign(args.campaign)
        hosts = campaign_hosts(campaign, args.host)
    except CampaignError as exc:
        print(f"error: {exc}")
        return 2
    branch = campaign["branch"]
    sha, err = git_output(["rev-parse", branch])
    if err:
        print(f"error: no local branch {branch!r} to enqueue ({err})")
        return 2
    # Pushed here rather than left to `stage`, because a tick now stages the
    # entry itself and fetches from origin to do it. An entry naming a commit
    # no host can fetch is one that burns its attempts and stops.
    if not args.dry_run:
        _, err = git_output(["push", "origin", f"{branch}:{branch}"])
        if err:
            print(f"error: pushing {branch} to origin failed: {err}")
            return 1
        print(f"pushed {branch} to origin at {sha[:7]}")
    rows, ok = [], True
    for host in hosts:
        root = source_path(host) if host != LOCAL else str(REPO_ROOT)
        text, err = run_shell(host, render_script(
            ENQUEUE_SCRIPT,
            ROOT=shlex.quote(root),
            QUEUE=shlex.quote(QUEUE_DIR)))
        sections = parse_sections(text or "")
        if err or "err" in sections or "end" not in sections:
            ok = False
            why = " ".join(sections.get("err", [])) or err or "no answer"
            rows.append([host, "-", f"failed: {why}"])
            continue
        names = [n.strip() for n in sections.get("names", []) if n.strip()]
        pending = [n for n in names
                   if n.split("-", 1)[-1] == f"{campaign['name']}.toml"]
        if pending and not args.again:
            ok = False
            rows.append([host, ", ".join(pending),
                         "already queued (pass --again to add another)"])
            continue
        index = max([entry_index(n) for n in names] + [0]) + 1
        name = f"{index:03d}-{campaign['name']}.toml"
        entry = new_entry(campaign, host, args.max_attempts, sha)
        body = dump_toml(entry_body(entry))
        if args.dry_run:
            rows.append([host, name, "dry run"])
            continue
        path = f"{QUEUE_DIR}/{name}"
        encoded = base64.b64encode(body.encode()).decode()
        text, err = run_shell(host, render_script(
            WRITE_ENTRY_SCRIPT,
            ROOT=shlex.quote(root),
            QUEUE=shlex.quote(QUEUE_DIR),
            PATH=shlex.quote(path),
            B64=shlex.quote(encoded)))
        sections = parse_sections(text or "")
        if err or "err" in sections or "end" not in sections:
            ok = False
            why = " ".join(sections.get("err", [])) or err or "no answer"
            rows.append([host, name, f"failed: {why}"])
            continue
        rows.append([host, name, f"queued, {entry['seeds']}"])
    print(render_table(rows, ["HOST", "ENTRY", "RESULT"]))
    if ok and not args.dry_run:
        print(f"\nThe cron tick will pick these up, staging {branch} at "
              f"{sha[:7]} first where the host is on something else. Do not "
              f"wait on them: `campaign.py queue` says where they are.")
    return 0 if ok else 1


# -- tick ---------------------------------------------------------------------

def acquire_lock(path: Path):
    """Non-blocking exclusive lock, or None when another tick holds it.

    The only lock on the queue. A tick typed by hand while cron is mid-phase
    would otherwise run a second runner against the same results CSV, and the
    two resume off the same file.

    Note that flock is per open file description rather than per process, so
    two `open()`s of this path in one process contend with each other. That is
    why the crontab line must not wrap the tick in `flock` on the same file --
    see cron_line().
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = open(path, "a+")
    try:
        fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError as exc:
        handle.close()
        if exc.errno in (errno.EACCES, errno.EAGAIN):
            return None
        raise
    return handle


def adopted_lock():
    """The queue lock carried across the tick's own re-exec, or None.

    The restarted process inherits the locked descriptor rather than taking
    the lock again. flock is per open file description, so a lock dropped for
    the length of an exec would let a hand-typed tick in between the two
    processes and both would then run the same entry -- the failure the lock
    exists to prevent, reached through the mechanism that prevents it.
    """
    raw = os.environ.pop(TICK_LOCK_FD_ENV, "")
    if not raw:
        return None
    try:
        return os.fdopen(int(raw), "a+")
    except (ValueError, OSError) as exc:
        print(f"tick: cannot adopt the queue lock on fd {raw} — {exc}")
        return None


def restart_after_stage(lock, entry: dict):
    """Re-exec this tick so the rest of it runs the sources just checked out.

    A tick reads the campaign declaration only after staging the checkout,
    the declaration being tracked on the campaign's own branch, and the
    running process keeps the campaign.py it imported before the checkout
    moved. A declaration using keys that older revision does not define fails
    to parse, and a phase kind it does not define cannot be rendered even
    where it does parse -- both of which read as a bad declaration rather
    than as a stale parser, and the obvious response, editing the file, is
    wrong.

    ensure_staged has already put the entry back in `queued`, so the
    restarted tick picks it up as it would any other and the restart spends
    no attempt. An exec that fails is not fatal: the tick carries on with the
    sources it has, which is what it did before this existed.
    """
    os.set_inheritable(lock.fileno(), True)
    env = dict(os.environ)
    env[TICK_RESTART_ENV] = "1"
    env[TICK_LOCK_FD_ENV] = str(lock.fileno())
    print(f"tick: {entry['file']} restarting on the staged scripts")
    sys.stdout.flush()
    sys.stderr.flush()
    try:
        os.execve(sys.executable,
                  [sys.executable, str(Path(__file__).resolve()),
                   *sys.argv[1:]], env)
    except OSError as exc:
        # Not fatal: the tick carries on with the sources it has, which is
        # what it did before this existed.
        print(f"tick: could not restart ({exc}); carrying on unstaged")
        return None
    # Unreachable where the exec worked, that call having replaced this
    # process. It is here so that a stubbed exec stops the tick the way the
    # real one does, rather than falling through into the phase.
    return 0


def run_step(root: Path, command, log_path: Path, shell: bool = False) -> int:
    """Run one command in the checkout, appending everything it says to a log.

    No deadline anywhere on this path: a phase is hours of work and the lock is
    what keeps a second tick off it. A tick killed here costs nothing —
    run_experiments.py resumes off the results CSV, so the next one continues
    rather than repeats, and a killed build is simply rebuilt.
    """
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "a") as log:
        shown = command if shell else " ".join(command)
        log.write(f"\n=== {time.strftime('%Y-%m-%dT%H:%M:%S')} {shown}\n")
        log.flush()
        proc = subprocess.run(command, cwd=str(root), shell=shell, stdout=log,
                              stderr=subprocess.STDOUT)
    return proc.returncode


def run_phase(root: Path, phase: dict, seeds: list, log_path: Path) -> int:
    return run_step(root, shlex.split(phase_launcher(phase))
                    + phase_args(phase, seeds), log_path)


def results_dir_missing(root: Path, phase: dict):
    """Why a score phase cannot run here, or None.

    The scorer's own check, asked before the attempt is spent: the results
    directory is produced by an earlier phase or an earlier campaign, and a
    host that never ran either has nothing to score. Named in the entry's
    `last_error`, in the words the stage check uses.
    """
    directory = root / phase["results"]
    if directory.is_dir():
        return None
    return (f"no results directory at {directory} for score phase "
            f"{phase['name']} — the run phase that writes it has not run on "
            f"this host, or `results` in campaign.toml names the wrong "
            f"directory")


def entry_log_path(entry: dict, root: Path) -> Path:
    return queue_dir(root) / (Path(entry["file"]).stem + ".log")


# -- staging, from a tick -----------------------------------------------------
#
# `stage` is the attended path. It reaches a host over ssh and refuses three
# things — a dirty checkout, a live run, and another branch — and the way past
# any of them is a human typing the host name back at a terminal.
#
# A queue holding campaigns on two branches cannot use that. The only thing
# between one entry and the next is a branch, and there is nobody at the
# terminal when the tick fires at 03:05. So the tick stages the one refusal
# that destroys nothing: it re-points the checkout at the entry's own commit,
# which is on origin, and rebuilds. It never answers the other two. Uncommitted
# work and a live run are both cases where the checkout carries something this
# side cannot reconstruct, and resetting either unattended is precisely the
# failure `stage --force` was written to make impossible.


def local_processes() -> list:
    """Runner and engine processes on this machine.

    A seam, for the same reason probe_processes is one on the remote path: the
    check is machine-wide by design, so a test standing in a fixture checkout
    would otherwise inherit whatever this box happens to be running and refuse
    for reasons that have nothing to do with the fixture.
    """
    proc = subprocess.run(["ps", "-o", "comm=,args=", "-u", getpass.getuser()],
                          capture_output=True, text=True)
    return live_processes(proc.stdout.splitlines())


def tick_stage_refusals(root: Path) -> list:
    """Why a tick must not re-point this checkout. Empty means it may.

    Two of stage's three: the branch is the one the tick is here to change.
    """
    out = []
    dirty, err = git_output(["status", "--porcelain", "--untracked-files=no"],
                            root)
    if err:
        return [f"cannot read the checkout's state: {err}"]
    lines = [ln for ln in dirty.splitlines() if ln.strip()]
    if lines:
        names = ", ".join(ln.split(None, 1)[-1] for ln in lines[:5])
        out.append(f"{len(lines)} modified tracked file(s) ({names}) — commit "
                   f"or discard them, then requeue")
    busy = list(dict.fromkeys(p["comm"] for p in local_processes()))
    if busy:
        out.append(f"live process(es): {', '.join(busy)} — a checkout under a "
                   f"running campaign swaps the binary its rows name")
    # An unpushed commit is the third thing a checkout can hold that nothing
    # here can bring back. `stage` guards the same case by refusing a checkout
    # ahead of the pushed commit, which cannot be asked across two unrelated
    # branches; asking whether any remote branch contains HEAD can.
    contained, err = git_output(["branch", "-r", "--contains", "HEAD"], root)
    if err is None and not contained.strip():
        head, _ = git_output(["rev-parse", "--short", "HEAD"], root)
        out.append(f"HEAD ({head or '?'}) is on no remote branch — push it, "
                   f"or stage this host by hand")
    return out


def stage_checkout(root: Path, entry: dict, log_path: Path):
    """Fetch, check out the entry's commit, rebuild, verify. None on success.

    Neither `git checkout -f` nor `git clean`, for two different reasons. The
    tree is clean by the time this runs, so -f would only ever discard
    something the refusals above were meant to stop; and a host's untracked
    files are its results, which `clean` would delete.

    The binary is read back the way `stage` reads it, because that is the
    check run_experiments.py itself makes one step later: a build that half
    ran leaves a binary from the previous commit, and every row the phase then
    wrote would name a commit it did not come from.
    """
    branch, commit = entry["branch"], entry["commit"]
    build = entry.get("build") or DEFAULT_BUILD_CMD
    if run_step(root, ["git", "fetch", "origin", branch], log_path):
        return f"git fetch origin {branch} failed"
    if run_step(root, ["git", "checkout", "-B", branch, commit], log_path):
        return f"git checkout -B {branch} {commit[:7]} failed"
    if run_step(root, build, log_path, shell=True):
        return f"the build command failed: {build}"
    binary = root / COUNTER_BINARY
    if not binary.is_file():
        return f"no {COUNTER_BINARY} after the build"
    proc = subprocess.run([str(binary), "--version"], cwd=str(root),
                          capture_output=True, text=True)
    version = parse_version_lines(proc.stdout.splitlines())
    if version.get("commit") != commit:
        return (f"binary reports {version.get('commit_short') or '?'}, "
                f"not {commit[:7]}")
    if version.get("dirty") != "0":
        return "binary was built from a dirty tree"
    return None


def ensure_staged(entry: dict, root: Path, args: argparse.Namespace):
    """Put the checkout on what the entry names, or say why it stays put.

    None means the tick may go on and run the phase. Anything else is the
    tick's exit code, with the entry already rewritten and the reason printed.

    The commit is checked as well as the branch. An entry froze one at enqueue,
    and a branch that has moved since is a different campaign from the one the
    earlier phases wrote rows for.
    """
    branch, err = checkout_branch(root)
    if err:
        fail_or_requeue(entry, f"cannot read the checkout's branch: {err}")
        write_entry(entry["path"], entry)
        print(f"tick: {entry['file']} {entry['state']}: {entry['last_error']}")
        return 1
    head, _ = git_output(["rev-parse", "HEAD"], root)
    commit = entry.get("commit")
    if branch == entry.get("branch") and (not commit or head == commit):
        return None

    want = entry.get("branch")
    want = f"{want} at {commit[:7]}" if commit else str(want)
    if not commit or args.no_stage:
        # An entry written before this field existed names no commit to stage
        # to, and there is nothing safe to guess: the branch has moved since,
        # or it would not be here. It refuses exactly as every entry used to.
        why = "--no-stage" if commit else "the entry names no commit"
        fail_or_requeue(entry, f"checkout is on {branch}, entry wants {want} "
                               f"— stage it first ({why})")
        write_entry(entry["path"], entry)
        print(f"tick: {entry['file']} {entry['state']}: {entry['last_error']}")
        return 1
    refusals = tick_stage_refusals(root)
    if refusals:
        fail_or_requeue(entry, f"cannot stage {want}: {'; '.join(refusals)}")
        write_entry(entry["path"], entry)
        print(f"tick: {entry['file']} {entry['state']}: {entry['last_error']}")
        return 1

    entry["state"] = "running"
    log_line(entry, f"staging {want}, from {branch}")
    write_entry(entry["path"], entry)
    print(f"tick: {entry['file']} staging {want} (checkout was on {branch})")
    if args.dry_run:
        entry["state"] = "queued"
        log_line(entry, "dry run, not staged")
        write_entry(entry["path"], entry)
        print(f"  would check out {want} and build with: "
              f"{entry.get('build') or DEFAULT_BUILD_CMD}")
        return 0
    why = stage_checkout(root, entry, entry_log_path(entry, root))
    if why:
        fail_or_requeue(entry, f"staging {want}: {why}")
        write_entry(entry["path"], entry)
        print(f"tick: {entry['file']} {entry['state']}: {entry['last_error']}")
        return 1
    # Back to queued rather than left running: the staging succeeded, and a
    # tick killed between here and the phase must not spend an attempt on work
    # that is already done. tick_entry marks it running again immediately.
    entry["state"] = "queued"
    log_line(entry, f"staged {want}")
    write_entry(entry["path"], entry)
    print(f"tick: {entry['file']} staged {want}")
    # Only where the checkout moved this tick's own sources. A campaign that
    # merely sits on another branch of the same scripts is the common case,
    # and restarting for it would cost every such tick a five-minute cycle
    # for nothing.
    changed, _ = git_output(["diff", "--name-only", head, "HEAD", "--",
                             "scripts"], root)
    if not changed.strip():
        return None
    if os.environ.get(TICK_RESTART_ENV):
        # Restarted once already and the scripts moved again. Going round
        # again is the loop this guard exists for, so this tick runs on what
        # it has and the next one starts from the staged sources anyway.
        print(f"tick: {entry['file']} scripts moved again after a restart; "
              f"running on the sources this tick was started with")
        return None
    return RESTART


def config_dirs_missing(root: Path, config_dirs: list) -> list:
    """The declared configs directories holding no `.toml`. Empty means ready.

    The local twin of CONFIGS_CHECK, which the attended stage path runs over
    ssh, and it has to answer the same question the same way: recursive,
    because gen_configs.py writes a directory per factor level and nothing
    lands at the top; first hit only, because a generated tree is thousands of
    files and the question is whether it is empty. A directory that is not
    there at all is missing rather than an error, since that is the state a
    host is in before anything generated anything.
    """
    missing = []
    for name in config_dirs:
        directory = root / name
        if not directory.is_dir() or next(directory.rglob("*.toml"),
                                          None) is None:
            missing.append(name)
    return missing


def ensure_configs(campaign: dict, root: Path, log_path: Path):
    """Generate the campaign's configs where the host has none. None on ready.

    Checked before it is run, so the common case -- a host staged by hand, or
    one that generated them for an earlier phase -- costs one directory scan
    and runs no generator. A campaign declaring no command fails here with the
    words the stage path uses, rather than three ticks later as
    run_experiments.py exiting 1 with the attempts already spent.
    """
    missing = config_dirs_missing(root, campaign["config_dirs"])
    if not missing:
        return None
    configs = campaign.get("configs")
    if not configs:
        return (f"no config files under {', '.join(missing)} — generate them "
                f"with scripts/gen_configs.py, and declare that command as "
                f"configs = ... in campaign.toml")
    if run_step(root, configs, log_path, shell=True):
        return f"the configs command failed: {configs}"
    # Re-checked rather than trusted: a generator called with the wrong flags
    # writes a different directory and exits 0, which is the failure that
    # reaches the runner rather than the log.
    still = config_dirs_missing(root, campaign["config_dirs"])
    if still:
        return (f"the configs command left no config files under "
                f"{', '.join(still)}: {configs}")
    return None


def fail_or_requeue(entry: dict, why: str) -> None:
    """One attempt spent. Past the cap the entry stops moving and says why.

    A failed phase that requeued for ever would look exactly like a slow one,
    and the queue would keep a machine busy re-running something that cannot
    work. The cap is what makes the difference visible in one column.
    """
    entry["attempts"] = int(entry.get("attempts", 0)) + 1
    entry["last_error"] = why
    cap = int(entry.get("max_attempts", DEFAULT_MAX_ATTEMPTS))
    if entry["attempts"] >= cap:
        entry["state"] = "failed"
        log_line(entry, f"failed after {entry['attempts']} attempt(s): {why}")
    else:
        entry["state"] = "queued"
        log_line(entry, f"attempt {entry['attempts']}/{cap} failed: {why}")


def recover_interrupted(entries: list) -> list:
    """Entries left `running` by a tick that died, put back in the queue.

    Safe because this runs holding the lock: no other tick can be running a
    phase, so a `running` entry has no process behind it.
    """
    touched = []
    for entry in entries:
        if entry.get("state") != "running":
            continue
        fail_or_requeue(entry, "tick was interrupted mid-phase")
        write_entry(entry["path"], entry)
        touched.append(entry)
    return touched


def cmd_tick(args: argparse.Namespace) -> int:
    root = Path(args.root).resolve() if args.root else REPO_ROOT
    lock_path = Path(args.lock).expanduser() if args.lock else Path(
        os.path.expanduser(QUEUE_LOCK))
    lock = adopted_lock() or acquire_lock(lock_path)
    if lock is None:
        print(f"tick: {lock_path} is held; another tick is mid-phase.")
        return 0
    try:
        entries = queue_entries(root)
        for entry in recover_interrupted(entries):
            print(f"tick: {entry['file']} was left running; "
                  f"{entry['state']} ({entry['last_error']})")
        for entry in entries:
            if entry.get("error"):
                print(f"tick: {entry['file']} does not parse: {entry['error']}")
        ready = [e for e in entries
                 if e.get("state") == "queued"
                 and (not args.host or e.get("host") in (args.host, None))]
        if not ready:
            print("tick: nothing queued.")
            return 0
        entry = ready[0]
        # Before load_campaign, not after: the declaration is tracked on the
        # campaign's own branch, so a checkout standing on another one cannot
        # read it until this has moved it.
        code = ensure_staged(entry, root, args)
        if code == RESTART:
            code = restart_after_stage(lock, entry)
        if code is not None:
            return code
        try:
            campaign = load_campaign(entry["campaign"], root)
        except CampaignError as exc:
            fail_or_requeue(entry, f"declaration unusable: {exc}")
            write_entry(entry["path"], entry)
            print(f"tick: {entry['file']} {entry['state']}: {exc}")
            return 1
        # After load_campaign rather than inside stage_checkout: the command
        # and the directories to check are keys of campaign.toml, which the
        # comment above is about not being readable any earlier.
        if args.dry_run:
            missing = config_dirs_missing(root, campaign["config_dirs"])
            if missing:
                print(f"  would generate configs for {', '.join(missing)} "
                      f"with: {campaign['configs'] or '(none declared)'}")
        else:
            why = ensure_configs(campaign, root, entry_log_path(entry, root))
            if why is not None:
                fail_or_requeue(entry, why)
                write_entry(entry["path"], entry)
                print(f"tick: {entry['file']} {entry['state']}: "
                      f"{entry['last_error']}")
                return 1
        return tick_entry(entry, campaign, root, args)
    finally:
        lock.close()


def tick_entry(entry: dict, campaign: dict, root: Path,
               args: argparse.Namespace) -> int:
    # No branch check here. ensure_staged is the one place that decides what
    # the checkout may run, and a second test of its own predicate would be
    # dead code rather than a safety net.
    index = int(entry.get("phase", 0))
    if index >= len(campaign["phases"]):
        entry["state"] = "done"
        log_line(entry, "no phases left")
        write_entry(entry["path"], entry)
        return 0
    phase = campaign["phases"][index]
    text = entry_phase_seeds(entry, index)
    if not text:
        entry["phase"] = index + 1
        log_line(entry, f"phase {index} ({phase['name']}) runs no seeds "
                        f"on {entry['host']}; skipped")
        entry["state"] = ("done" if entry["phase"] >= len(campaign["phases"])
                          else "queued")
        write_entry(entry["path"], entry)
        print(f"tick: {entry['file']} phase {index} ({phase['name']}) skipped")
        return 0
    seeds = parse_seed_range(text, f"{entry['file']}: phase {index} seeds")
    log_path = entry_log_path(entry, root)
    # A score phase over a results directory the host does not hold cannot
    # run, and the scorer would say so an attempt later; the refusal is here
    # so the entry names the directory rather than an exit status.
    blocked = (results_dir_missing(root, phase)
               if phase_kind(phase) == "score" else None)
    if blocked is not None and not args.dry_run:
        fail_or_requeue(entry, blocked)
        write_entry(entry["path"], entry)
        print(f"tick: {entry['file']} {entry['state']}: {entry['last_error']}")
        return 1

    entry["state"] = "running"
    entry["pid"] = os.getpid()
    log_line(entry, f"phase {index} ({phase['name']}) started")
    write_entry(entry["path"], entry)
    print(f"tick: {entry['file']} phase {index} ({phase['name']}) "
          f"on seeds {text}")
    if args.dry_run:
        entry["state"] = "queued"
        log_line(entry, "dry run, phase not executed")
        write_entry(entry["path"], entry)
        print(f"  would run: {phase_command(phase, seeds)}")
        if blocked is not None:
            print(f"  blocked: {blocked}")
        return 0

    code = run_phase(root, phase, seeds, log_path)
    if code != 0:
        fail_or_requeue(entry, f"phase {index} ({phase['name']}) exited {code}")
    else:
        entry["phase"] = index + 1
        entry["attempts"] = 0
        entry["last_error"] = ""
        if entry["phase"] >= len(campaign["phases"]):
            entry["state"] = "done"
            log_line(entry, f"phase {index} ({phase['name']}) finished; done")
        else:
            entry["state"] = "queued"
            log_line(entry, f"phase {index} ({phase['name']}) finished")
    write_entry(entry["path"], entry)
    print(f"tick: {entry['file']} now {entry['state']}"
          + (f" ({entry['last_error']})" if entry["last_error"] else ""))
    return 0 if code == 0 else 1


def checkout_branch(root: Path) -> tuple:
    proc = subprocess.run(["git", "-C", str(root), "rev-parse",
                           "--abbrev-ref", "HEAD"], capture_output=True,
                          text=True)
    if proc.returncode != 0:
        return None, (proc.stderr.strip().splitlines() or ["git failed"])[-1]
    return proc.stdout.strip(), None


# -- queue listing and cron ---------------------------------------------------

QUEUE_HEADERS = ["HOST", "ENTRY", "CAMPAIGN", "BRANCH", "STATE", "PHASE",
                 "TRIES", "SEEDS", "UPDATED", "NOTE"]

# `running` matches the campaign table's own running, since `status` prints
# both at once and one word must not mean two colours on one screen.
QUEUE_STATE_COLOURS = {"queued": ("dim",), "running": ("cyan",),
                       "done": ("green",), "failed": ("red",),
                       "cancelled": ("yellow",), "unparseable": ("red",)}


def queue_paint(row: int, col: int, cell: str, style: Style) -> str:
    header = QUEUE_HEADERS[col]
    if header == "STATE":
        return style(cell, *QUEUE_STATE_COLOURS.get(cell, ()))
    # NOTE carries last_error, and is empty on every entry that is fine.
    if header == "NOTE" and cell:
        return style(cell, "red")
    return cell


def queue_rows(reports: list) -> list:
    rows = []
    for report in reports:
        for entry in report.get("queue", []):
            name = Path(entry.get("file", "?")).name
            if entry.get("error"):
                rows.append([report["host"], name, "-", "-", "unparseable",
                             "-", "-", "-", "-", entry["error"]])
                continue
            # The branch is the column that matters once a queue holds
            # campaigns on more than one: it is what the tick stages to, and
            # the reason a checkout moves between two entries.
            commit = str(entry.get("commit", ""))
            rows.append([
                report["host"], name, str(entry.get("campaign", "?")),
                str(entry.get("branch", "?"))
                + (f"@{commit[:7]}" if commit else ""),
                str(entry.get("state", "?")),
                f"{entry.get('phase', '?')}/{entry.get('phases', '?')}",
                f"{entry.get('attempts', 0)}/"
                f"{entry.get('max_attempts', DEFAULT_MAX_ATTEMPTS)}",
                str(entry.get("seeds", "-")),
                str(entry.get("updated", "-")),
                str(entry.get("last_error", "") or ""),
            ])
    return rows


def cmd_queue(args: argparse.Namespace) -> int:
    hosts = [args.host] if args.host else list(HOSTS)
    targets = [(h, source_path(h)) for h in hosts if h != LOCAL]
    if not args.host or args.host == LOCAL:
        targets.append((LOCAL, str(REPO_ROOT)))
    with ThreadPoolExecutor(max_workers=max(1, len(targets))) as pool:
        reports = list(pool.map(
            lambda t: gather_host(t[0], t[1], None, False, False), targets))
    rows = queue_rows(reports)
    if args.json:
        print(json.dumps({"hosts": [{"host": r["host"],
                                     "queue": r.get("queue", []),
                                     "error": r["error"]} for r in reports]},
                         indent=2))
        return 0 if all(r["reachable"] for r in reports) else 1
    for report in reports:
        if not report["reachable"]:
            print(f"note: {report['host']} unreachable: {report['error']}")
    print(render_table(rows, QUEUE_HEADERS, queue_paint) if rows
          else "No queue entries on any host.")
    if rows:
        print("\nPHASE is the next phase against the total; TRIES is attempts "
              "against the cap.\nA failed or cancelled entry stops moving "
              "until `campaign.py requeue` clears it.")
    return 0 if all(r["reachable"] for r in reports) else 1


REQUEUE_SCRIPT = r"""
cd @ROOT@ 2>/dev/null || { echo "@M@ERR not a directory: @ROOT@"; exit 3; }
if [ ! -f @PATH@ ]; then echo "@M@ERR no entry @PATH@"; exit 4; fi
printf %s @B64@ | base64 -d > @PATH@.tmp || { echo "@M@ERR write failed"; exit 5; }
mv @PATH@.tmp @PATH@ || { echo "@M@ERR rename failed"; exit 6; }
echo "@M@END"
"""


def cmd_requeue(args: argparse.Namespace) -> int:
    """Clear a failed entry's attempt count so the next tick tries it again.

    Deliberately a separate verb rather than a tick that resets itself: a
    failed phase has a cause, and the cap exists so that somebody reads the log
    before the machine spends another night on it.
    """
    import base64  # noqa: PLC0415
    host, root = args.host, (source_path(args.host) if args.host != LOCAL
                             else str(REPO_ROOT))
    report = gather_host(host, root, None, False, False)
    if not report["reachable"]:
        print(f"error: {host} unreachable: {report['error']}")
        return 1
    matches = [e for e in report["queue"]
               if Path(e.get("file", "")).name == args.entry]
    if not matches:
        print(f"error: {host} has no queue entry {args.entry!r}")
        return 2
    entry = entry_body(matches[0])
    entry["state"] = "queued"
    entry["attempts"] = 0
    entry["last_error"] = ""
    log_line(entry, "requeued by hand")
    encoded = base64.b64encode(dump_toml(entry).encode()).decode()
    text, err = run_shell(host, render_script(
        REQUEUE_SCRIPT,
        ROOT=shlex.quote(root),
        PATH=shlex.quote(f"{QUEUE_DIR}/{args.entry}"),
        B64=shlex.quote(encoded)))
    sections = parse_sections(text or "")
    if err or "err" in sections or "end" not in sections:
        print(f"error: {' '.join(sections.get('err', [])) or err}")
        return 1
    print(f"{host}: {args.entry} requeued.")
    return 0


DEQUEUE_SCRIPT = r"""
cd @ROOT@ 2>/dev/null || { echo "@M@ERR not a directory: @ROOT@"; exit 3; }
if [ ! -f @PATH@ ]; then echo "@M@ERR no entry @PATH@"; exit 4; fi
PID=@PID@
TREEFILE=$(mktemp) || { echo "@M@ERR no temporary file"; exit 5; }
if [ "$PID" != 0 ] && kill -0 "$PID" 2>/dev/null; then
  case "$(ps -o args= -p "$PID" 2>/dev/null)" in
    *campaign.py*)
      ps -eo pid=,ppid= | awk -v root="$PID" '
        { parent[$1] = $2; seen[NR] = $1; n = NR }
        END {
          tree[root] = 1
          changed = 1
          while (changed) {
            changed = 0
            for (i = 1; i <= n; i++) {
              p = seen[i]
              if (!(p in tree) && (parent[p] in tree)) { tree[p] = 1; changed = 1 }
            }
          }
          for (p in tree) print p
        }' > "$TREEFILE"
      ;;
    *) echo "@M@NOTE pid $PID is alive and is not a tick; nothing killed" ;;
  esac
fi
echo "@M@TREE $(tr '\n' ' ' < "$TREEFILE")"
if [ -s "$TREEFILE" ] && [ @KILL@ = yes ]; then
  kill -TERM "$PID" 2>/dev/null
  while read -r p; do
    [ "$p" = "$PID" ] || kill -TERM "$p" 2>/dev/null
  done < "$TREEFILE"
  sleep 3
  while read -r p; do kill -KILL "$p" 2>/dev/null; done < "$TREEFILE"
  echo "@M@KILLED $(tr '\n' ' ' < "$TREEFILE")"
fi
rm -f "$TREEFILE"
if [ @WRITE@ = yes ]; then
  printf %s @B64@ | base64 -d > @PATH@.tmp || { echo "@M@ERR write failed"; exit 6; }
  mv @PATH@.tmp @PATH@ || { echo "@M@ERR rename failed"; exit 7; }
fi
echo "@M@END"
"""
# The tick goes first and the entry is rewritten last, in one script rather
# than two round trips. A tick writes the entry's state at the end of every
# phase from the copy it holds in memory, so an entry rewritten while its tick
# is alive is overwritten hours later by a process that never heard of this,
# and the queue then disagrees with the machine. Its descendants are collected
# before it dies, since a dead parent's children reparent to init and no walk
# finds them afterwards; they are killed after it, being the runner and the
# `counter` processes the phase launched, which outlive the tick otherwise and
# keep writing rows into a results CSV nothing is waiting for.
#
# The tree is a fixpoint over one `ps -eo pid=,ppid=` snapshot rather than a
# loop over `pgrep -P`, because the remote shell is zsh: an unquoted parameter
# does not word-split there, so `for p in $FRONTIER` over a frontier of four
# pids iterates once over all four as a single word, `pgrep -P` rejects it,
# and the frontier holds the single space the substitution leaves. `[ -n " " ]`
# is true, so that loop never terminates -- it spun at 25% of a core on av3
# until it was killed by hand. Every loop here reads its pids from a file for
# the same reason, one per line.


def cmd_dequeue(args: argparse.Namespace) -> int:
    """Take an entry out of the queue, stopping the phase it is mid-way through.

    The inverse of `enqueue`, and the only supported way to stop a campaign:
    the tick has no stop of its own, so without this the choice was editing
    queue TOML on a lab machine by hand or waiting for the attempt cap to
    spend itself against a run nobody wants any more.

    It tombstones rather than deleting the file, following `p_remove_guarantee`
    and for the same reason: the log is the record of what the entry did, and
    `enqueue` numbers entries from the names already present, so a deleted
    021 lets the next campaign take a number a reader has already seen mean
    something else. `requeue` puts a cancelled entry back, which is what makes
    this a stop rather than a withdrawal -- one verb for both, since a
    cancelled entry and a failed one need exactly the same thing done to them.
    """
    import base64  # noqa: PLC0415
    host, root = args.host, (source_path(args.host) if args.host != LOCAL
                             else str(REPO_ROOT))
    report = gather_host(host, root, None, False, False)
    if not report["reachable"]:
        print(f"error: {host} unreachable: {report['error']}")
        return 1
    matches = [e for e in report["queue"]
               if Path(e.get("file", "")).name == args.entry]
    if not matches:
        print(f"error: {host} has no queue entry {args.entry!r}")
        return 2
    entry = entry_body(matches[0])
    was = str(entry.get("state", "?"))
    if was == "cancelled":
        print(f"{host}: {args.entry} is already cancelled.")
        return 0
    # Only a `running` entry names a live tick. The pid on any other state is
    # the last tick to have held it, long dead, and its number belongs to
    # whatever the machine has started since.
    try:
        pid = int(entry.get("pid", 0)) if was == "running" else 0
    except (TypeError, ValueError):
        pid = 0
    why = args.reason or "dequeued by hand"
    entry["state"] = "cancelled"
    entry["last_error"] = why
    log_line(entry, f"cancelled while {was}: {why}")
    encoded = base64.b64encode(dump_toml(entry).encode()).decode()
    text, err = run_shell(host, render_script(
        DEQUEUE_SCRIPT,
        ROOT=shlex.quote(root),
        PATH=shlex.quote(f"{QUEUE_DIR}/{args.entry}"),
        PID=str(pid),
        KILL="no" if args.dry_run else "yes",
        WRITE="no" if args.dry_run else "yes",
        B64=shlex.quote(encoded)), timeout=SSH_TIMEOUT_S + 10)
    sections = parse_sections(text or "")
    if err or "err" in sections or "end" not in sections:
        print(f"error: {' '.join(sections.get('err', [])) or err}")
        return 1
    for note in sections.get("note", []):
        print(f"note: {note}")
    tree = " ".join(sections.get("tree", [])).split()
    if was == "running" and not tree:
        print(f"note: {args.entry} reads running and no tick holds it; "
              f"nothing to kill.")
    if args.dry_run:
        print(f"{host}: would cancel {args.entry} ({was})"
              + (f", killing {len(tree)} process(es): {' '.join(tree)}"
                 if tree else ""))
        return 0
    if tree:
        print(f"{host}: killed {len(tree)} process(es): {' '.join(tree)}")
    print(f"{host}: {args.entry} cancelled; `campaign.py requeue` puts it "
          f"back.")
    return 0


def cron_line(host: str, root: str) -> str:
    # No `flock -n` wrapper. It used to be here as an outer guard, but flock
    # locks attach to the open file description, and the wrapper's fd is
    # inherited across the exec: acquire_lock() then opens the same path a
    # second time, gets a distinct description, and is denied by the lock its
    # own parent holds. Every tick died that way -- silently, since both the
    # wrapper and the tick exit 0 -- and a queue could never start a phase.
    # acquire_lock() alone is the guard, and it covers strictly more: the
    # wrapper only ever bound cron ticks, while the inner lock also blocks a
    # tick typed by hand against a cron phase already running.
    return (f"*/5 * * * * cd {root} && "
            f"{REMOTE_PYTHON} scripts/campaign.py tick --host {host} "
            f">> $HOME/.counter-queue.log 2>&1")


def cmd_cron(args: argparse.Namespace) -> int:
    """Print the crontab line. Printing is all it does, deliberately: a verb
    that installs a cron entry on a lab machine would be editing somebody
    else's crontab from a script, with no record of what it replaced."""
    root = source_path(args.host) if args.host != LOCAL else str(REPO_ROOT)
    print(cron_line(args.host, root))
    print(f"\n# Not installed. Add it on {args.host} with `crontab -e`.")
    print("# campaign.py takes the queue lock itself, so a phase that runs for "
          "hours\n# simply holds the slot and every tick landing during it "
          "exits at once.\n# Do not wrap this in `flock` on the same lock "
          "file: the wrapper's fd is\n# inherited across the exec and the "
          "tick is then denied by its own parent.")
    return 0


# -- Entry point --------------------------------------------------------------

def add_colour_flag(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--no-color", action="store_true",
        help="Never colour the table. Colour is off already wherever stdout "
             "is not a terminal, and NO_COLOR in the environment does the "
             "same; CLICOLOR_FORCE=1 forces it back on, for `less -R`.")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="verb", required=True)

    status = sub.add_parser("status", help="Live campaign state, per host.")
    status.add_argument("--host", choices=[*HOSTS, LOCAL],
                        help="Only this host (default: every host plus the "
                             "local checkout).")
    status.add_argument("--campaign", metavar="PROFILE",
                        help="Only this profile's campaign.")
    status.add_argument("--all", action="store_true",
                        help="Include manifests left by campaigns launched "
                             "from a branch the checkout has since left.")
    status.add_argument("--json", action="store_true",
                        help="Machine-readable output.")
    status.add_argument("--no-plan", action="store_true",
                        help="Skip the per-campaign --dry-run plan query; "
                             "planned totals then read as unknown.")
    add_colour_flag(status)
    status.set_defaults(func=cmd_status)

    collect = sub.add_parser(
        "collect", help="rsync each host's results back, merge and verify.")
    collect.add_argument("--profile", default="full",
                         help="Profile whose CSV to collect (default: full).")
    collect.add_argument("--host", choices=list(HOSTS),
                         help="Only this host (default: every host).")
    collect.add_argument("--dry-run", action="store_true",
                         help="Report what would be transferred; transfer "
                              "nothing and write nothing.")
    collect.add_argument("--no-results", action="store_true",
                         help="Merge the CSV only; skip the per-run trees.")
    collect.add_argument("--csv", metavar="NAME",
                         help="Results CSV name, for a campaign whose profile "
                              "this checkout does not define.")
    collect.add_argument("--results-dir", metavar="NAME",
                         help="Per-run directory name, alongside --csv.")
    collect.add_argument("--curves", metavar="NAME",
                         help="Collect a score phase's curves instead: pull "
                              "each host's experiments/NAME/ into "
                              "experiments/NAME/<host>/ and join the per-run "
                              "CSVs into experiments/NAME.csv. --profile, "
                              "--csv and --results-dir are not read.")
    add_colour_flag(collect)
    collect.set_defaults(func=cmd_collect)

    stage = sub.add_parser(
        "stage", help="Put each host on the campaign's branch and rebuild.")
    stage.add_argument("campaign", help="Campaign name, i.e. the directory "
                                        "under experiments/.")
    stage.add_argument("--host", action="append", choices=[*HOSTS, LOCAL],
                       help="Only this host; repeatable (default: every host "
                            "the campaign declares).")
    stage.add_argument("--force", action="store_true",
                       help="Stage a host that is dirty, busy or on another "
                            "branch. Names what it will discard and asks; "
                            "refuses outright without a terminal.")
    stage.add_argument("--dry-run", action="store_true",
                       help="Probe and report; push nothing, build nothing.")
    stage.add_argument("--build-timeout", type=int, default=3600,
                       metavar="S", help="Seconds allowed for the remote "
                                         "build (default: 3600).")
    add_colour_flag(stage)
    stage.set_defaults(func=cmd_stage)

    start = sub.add_parser(
        "start", help="Launch the campaign's phases on each host, detached.")
    start.add_argument("campaign")
    start.add_argument("--host", action="append", choices=[*HOSTS, LOCAL])
    start.add_argument("--dry-run", action="store_true",
                       help="Print the command each host would run.")
    start.add_argument("--ignore-queue", action="store_true",
                       help="Launch even where a queue entry for this "
                            "campaign is still pending on that host. Names "
                            "the entry it is racing.")
    add_colour_flag(start)
    start.set_defaults(func=cmd_start)

    enqueue = sub.add_parser(
        "enqueue", help="Add the campaign to each host's queue for the tick.")
    enqueue.add_argument("campaign")
    enqueue.add_argument("--host", action="append", choices=[*HOSTS, LOCAL])
    enqueue.add_argument("--max-attempts", type=int,
                         default=DEFAULT_MAX_ATTEMPTS, metavar="N",
                         help=f"Failed phases before the entry stops "
                              f"(default: {DEFAULT_MAX_ATTEMPTS}).")
    enqueue.add_argument("--again", action="store_true",
                         help="Queue a second entry for a campaign already "
                              "queued on that host.")
    enqueue.add_argument("--dry-run", action="store_true")
    enqueue.set_defaults(func=cmd_enqueue)

    describe = sub.add_parser(
        "describe",
        help="Print a closed campaign's declaration, derived from its "
             "archive. Read-only: it writes nothing.")
    describe.add_argument("campaigns", nargs="*",
                          help="Archived campaign directory name(s).")
    describe.add_argument("--all", action="store_true",
                          help="Every directory under experiments/ holding a "
                               "PROVENANCE.json.")
    describe.add_argument("--archive-root", metavar="DIR",
                          help="Where to read the archives (default: this "
                               "checkout's experiments/). Results CSVs are "
                               "gitignored, so a worktree or a fresh clone "
                               "reads them from the checkout that holds "
                               "them.")
    describe.add_argument("--json", action="store_true",
                          help="Machine-readable output.")
    describe.set_defaults(func=cmd_describe)

    queue = sub.add_parser("queue", help="Every host's queue, read-only.")
    queue.add_argument("--host", choices=[*HOSTS, LOCAL])
    queue.add_argument("--json", action="store_true")
    add_colour_flag(queue)
    queue.set_defaults(func=cmd_queue)

    requeue = sub.add_parser(
        "requeue", help="Clear a failed entry's attempts so it runs again.")
    requeue.add_argument("--host", choices=[*HOSTS, LOCAL], required=True)
    requeue.add_argument("entry", help="Entry file name, e.g. 001-tlsf.toml.")
    requeue.set_defaults(func=cmd_requeue)

    dequeue = sub.add_parser(
        "dequeue",
        help="Take an entry out of the queue, killing its tick if one holds it.")
    dequeue.add_argument("--host", choices=[*HOSTS, LOCAL], required=True)
    dequeue.add_argument("entry", help="Entry file name, e.g. 001-tlsf.toml.")
    dequeue.add_argument("--reason", default="",
                         help="What to record in the entry's log and NOTE.")
    dequeue.add_argument("--dry-run", action="store_true",
                         help="Say what would be killed, and kill nothing.")
    dequeue.set_defaults(func=cmd_dequeue)

    tick = sub.add_parser(
        "tick", help="Run the next phase of the lowest-numbered queued entry. "
                     "Runs on the host, from cron.")
    tick.add_argument("--host", help="Only entries naming this host.")
    tick.add_argument("--root", help="Checkout to work in (default: this one).")
    tick.add_argument("--lock", help=f"Lock file (default: {QUEUE_LOCK}).")
    tick.add_argument("--no-stage", action="store_true",
                      help="Never move the checkout. An entry on another "
                           "branch burns an attempt and says `stage it "
                           "first`, as it did before ticks could stage.")
    tick.add_argument("--dry-run", action="store_true",
                      help="Pick an entry and print its phase; run nothing.")
    tick.set_defaults(func=cmd_tick)

    cron = sub.add_parser("cron", help="Print the crontab line for a host.")
    cron.add_argument("--host", choices=[*HOSTS, LOCAL], required=True)
    # Deliberately a no-op: cmd_cron prints whether or not it is passed, and
    # nothing reads print_only. It stays accepted because `cron --print` is the
    # invocation this module's own docstring, scripts/CLAUDE.md and
    # scripts/README.md all document, and dropping the flag would make the
    # documented line -- and anybody's shell history -- exit 2 instead.
    cron.add_argument("--print", dest="print_only", action="store_true",
                      help="Print the line. Printing is all this verb does; "
                           "nothing is installed anywhere.")
    cron.set_defaults(func=cmd_cron)
    return parser


def main() -> None:
    args = build_parser().parse_args()
    # --json is never coloured, whatever the terminal says: it is parsed, not
    # read, and the guarantee is worth making structural rather than resting
    # on no --json path happening to call render_table.
    set_colour(not getattr(args, "json", False)
               and colour_enabled(getattr(args, "no_color", False)))
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
