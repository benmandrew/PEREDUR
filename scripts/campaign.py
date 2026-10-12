#!/usr/bin/env python3
"""Drive experiment campaigns on the lab machines.

    python scripts/campaign.py status            # one table of live state
    python scripts/campaign.py status --json     # the same, machine-readable
    python scripts/campaign.py collect --profile tlsf --dry-run
    python scripts/campaign.py collect --profile tlsf
    python scripts/campaign.py stage arbiter-probe --dry-run
    python scripts/campaign.py enqueue arbiter-probe
    python scripts/campaign.py queue
    python scripts/campaign.py requeue --host av2 001-tlsf.toml
    python scripts/campaign.py dequeue --host av2 001-tlsf.toml
    python scripts/campaign.py tick --host av2
    python scripts/campaign.py cron --print --host av2

A campaign is declared once, in ``experiments/<name>/campaign.toml``: the
branch it runs on, the seed range each host takes, and the phases to run.
``stage`` and ``enqueue`` read that file, and neither accepts a seed range as
an argument. ``status`` and ``queue`` are read-only. ``collect`` wraps
merge_experiments.py and verifies the merge. ``tick`` runs on a host, from
cron, and runs the next phase of the lowest-numbered queued entry under a lock.

Hosts are reached by their ssh-config alias (``av2``), not by the FQDN in
merge_experiments.REMOTES: the FQDN form falls through to password auth under
BatchMode, which a status poll cannot answer.

The operating manual is docs/dev/campaigns.md.
"""

import argparse
import errno
import fcntl
import getpass
import json
import os
import re
import shlex
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
HOSTS: tuple[str, ...] = ("av1", "av2", "av3")
LOCAL = "local"

SSH_OPTS = ["-o", "ConnectTimeout=8", "-o", "BatchMode=yes"]
SSH_TIMEOUT_S = 45
# collect --dry-run's measure step walks a whole result tree with du and find,
# which on a cold cache outlasts the status deadline by a wide margin.
MEASURE_TIMEOUT_S = 600

# How long a campaign's newest run.log may go untouched before a live process
# stops corroborating it. The largest any profile allows is a 3600s PEREDUR
# timeout plus an 1800s compare timeout, so 90 minutes bounds one run's
# silence. Doubled, because on a jobs=1 host the next run.log only appears
# once the previous run has finished.
STALE_RUN_S = 3 * 3600

# av2's non-interactive ssh shell has no `python` on PATH, only `python3`.
REMOTE_PYTHON = "python3"

# Marker protocol for the remote scripts, which stay plain shell: an inline
# python heredoc would hide its own parse errors as empty output.
MARK = "##"

# A remote script's fillable slots. Narrow -- upper case, digits and
# underscores between two @ -- so it cannot match an address or a path.
PLACEHOLDER_RE = re.compile(r"@[A-Z0-9_]+@")


def render_script(template: str, **subs: str) -> str:
    """Fill a remote script template's ``@NAME@`` slots.

    ``@M@`` is the marker, is implicit, and goes last: a value spliced in
    carries markers of its own. A slot the caller does not name raises, since
    an unsubstituted ``@ROOT@`` is a bare word to the remote shell.
    """
    named = {f"@{key}@" for key in subs} | {"@M@"}
    unfilled = sorted(set(PLACEHOLDER_RE.findall(template)) - named)
    if unfilled:
        raise ValueError(f"unsubstituted placeholder(s) {' '.join(unfilled)} "
                         f"in remote script template")
    for key, value in subs.items():
        template = template.replace(f"@{key}@", value)
    return template.replace("@M@", MARK)


# The runner, as a command line: tick shells it in the checkout it is standing
# in, and PEREDUR_RUNNER_CMD points it at a stub for the tests.
RUNNER_CMD = os.environ.get("PEREDUR_RUNNER_CMD",
                            f"{REMOTE_PYTHON} scripts/run_experiments.py")
# The scoring twin, for a `kind = "score"` phase, with the same override.
SCORER_CMD = os.environ.get("PEREDUR_SCORER_CMD",
                            f"{REMOTE_PYTHON} scripts/score_campaign.py")

# Rebuilt by stage. The lab machines have no Nix, so this is the incremental
# build; a campaign overrides it with `build` in campaign.toml.
DEFAULT_BUILD_CMD = "cmake --build build-release"
PEREDUR_BINARY = "build-release/peredur"

# Queue entries live under the checkout that runs them, not in git: a tick
# rewrites the entry's state on every transition, and a tracked file doing that
# would leave the checkout dirty, which stage refuses to touch.
QUEUE_DIR = "experiments/queue"
QUEUE_LOCK = "~/.peredur-queue.lock"
QUEUE_STATES = ("queued", "running", "done", "failed", "cancelled")
# ensure_staged's third answer, beside None (go on) and an exit code (stop):
# the checkout moved the tick's own sources. See restart_after_stage.
RESTART = "restart"
TICK_RESTART_ENV = "PEREDUR_TICK_RESTARTED"
TICK_LOCK_FD_ENV = "PEREDUR_TICK_LOCK_FD"
DEFAULT_MAX_ATTEMPTS = 3
# Kept in the entry so a state history survives the ticks that wrote it.
QUEUE_LOG_LINES = 20


def source_root(host: str) -> str:
    """rsync/ssh root of a host's PEREDUR checkout.

    The single place a host name becomes a transfer source, which is what lets
    the tests point collect at a local fixture instead of a lab machine.
    """
    return f"{host}:{merge.REMOTE_ROOT}"


def source_path(host: str) -> str:
    """Filesystem path of that checkout, for a shell probe run on the host."""
    _, sep, path = source_root(host).partition(":")
    return path if sep else source_root(host)


# -- Remote inventory ---------------------------------------------------------

# The queue dump, shared by the inventory and the stage probe. Both put it
# last, so an entry's own text cannot be read as one of the sections above it,
# and it carries the closing ``@M@END``.
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
# A score phase's manifest sits one level down, inside its output directory,
# so it gets a sweep of its own: widening the first to depth two would pull in
# the per-host manifests archived campaigns keep under their own directories.
# The sweeps go through `find`, not a glob, because the lab login shell is
# zsh: its default NOMATCH aborts the whole script where a glob matches
# nothing.


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
    selection replayed onto it. A profile the host's checkout no longer
    defines yields no plan line.
    """
    lines = [f"cd {shlex.quote(root)} 2>/dev/null || exit 3"]
    for c in campaigns:
        if c.get("kind") == "score":
            # A score phase's progress is a directory listing: one CSV per
            # scored run, and the newest file for staleness.
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
    """Whether a run_shell error was the deadline rather than a failure."""
    return bool(error) and error.startswith(TIMEOUT_PREFIX)


def run_shell(host: str, script: str, timeout: int = SSH_TIMEOUT_S,
              ssh_opts: list[str] = SSH_OPTS):
    """Run a shell script on ``host`` (or locally). Returns (stdout, error).

    A timeout returns whatever had already arrived alongside the error: the
    markers stream out in order, so every section that completed is usable.
    """
    if host == LOCAL:
        cmd = ["sh", "-c", script]
    else:
        cmd = ["ssh", *ssh_opts, host, script]
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


def marker_blocks(text: str, tag: str) -> list:
    """(name, body) for each @TAG name / @ENDTAG pair a marker script emitted."""
    out: list = []
    name, body, inside = "", [], False
    for line in text.splitlines():
        if line.startswith(f"{MARK}{tag} "):
            name, body, inside = line[len(MARK) + len(tag) + 1:].strip(), [], True
        elif line.startswith(f"{MARK}END{tag}"):
            out.append((name, "\n".join(body)))
            inside = False
        elif inside:
            body.append(line)
    return out


def parse_toml_blocks(text: str, tag: str) -> list:
    """The TOML documents a marker script emitted, one per block.

    A half-written entry is kept with its name and the parse error: a tick
    cannot leave one, but a hand-edited entry can, and silence hides the queue.
    """
    out: list = []
    for name, body in marker_blocks(text, tag):
        try:
            entry = parse_toml(body)
        except TomlError as exc:
            entry = {"error": str(exc)}
        # The base name, as `requeue` and the queue table both name an entry.
        entry["file"] = Path(name).name
        out.append(entry)
    return out


def parse_inventory(text: str) -> dict:
    """Split the inventory script's marker-delimited output into fields."""
    out: dict = {"ps": [], "manifests": [], "score_manifests": [],
                 "queue": parse_toml_blocks(text, "QFILE"), "hostname": "?",
                 "epoch": None, "branch": "?", "head": "?", "dirty": None,
                 "error": None}
    for _, body in marker_blocks(text, "FILE"):
        try:
            out["manifests"].append(json.loads(body))
        except json.JSONDecodeError:
            pass
    for name, body in marker_blocks(text, "SFILE"):
        try:
            manifest = json.loads(body)
            manifest["file"] = name
            out["score_manifests"].append(manifest)
        except (json.JSONDecodeError, TypeError):
            pass
    section = None
    buf: list[str] = []

    def flush() -> None:
        if section == "host":
            out["hostname"] = buf[0].strip() if buf else "?"
            if len(buf) > 1 and buf[1].strip().isdigit():
                out["epoch"] = int(buf[1].strip())
        elif section == "git" and len(buf) >= 3:
            out["branch"], out["head"] = buf[0].strip(), buf[1].strip()
            out["dirty"] = buf[2].strip().isdigit() and int(buf[2].strip()) > 0

    # A block's own lines land in `buf` under its marker's name, which flush
    # ignores.
    for line in text.splitlines():
        if line.startswith(MARK + "ERR"):
            out["error"] = line[len(MARK) + 3:].strip()
            return out
        if line.startswith(MARK):
            flush()
            section, buf = line[len(MARK):].strip().lower(), []
        elif section == "ps":
            out["ps"].append(line)
        else:
            buf.append(line)
    flush()
    return out


def parse_plan_line(line: str) -> dict:
    """Pull the counts out of run_experiments.py's ``Plan:`` line.

    Each number is read from the phrase that names it, not from a fixed
    position, so a clause added to that line does not reassign the others.
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

    ``rows_done`` is the runner's own already-done count from the ``Plan:``
    line, scoped to the same selection as ``rows_planned``. The CSV's length
    is only a fallback for when no plan came back, and is marked as one:
    profiles may share a CSV, and a top-up relaunch narrows the manifest
    while the file still holds every earlier row.
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

# `maximal` runs under score_curves.py in the scoring pass. `counter` is the
# engine binary's name before the rename to peredur.
ENGINE_COMMS = ("peredur", "counter", "compare", "maximal", "ltlsynt", "black")


def live_processes(ps_lines: list[str]) -> list[dict]:
    """Runner, scorer and engine processes, from a ``comm args`` listing.

    Matched on ``comm``, never on the whole command line, which would match
    this script's own ssh command. A scorer is reported with ``kind =
    "score"`` and the ``--out`` directory it writes; it names no profile.
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
        binaries = m.get("binaries") or {}
        # Manifests written before the rename key the engine as `counter`.
        binary = binaries.get("peredur") or binaries.get("counter") or {}
        out.append({
            "profile": m.get("profile", "?"),
            "manifest_host": m.get("hostname", "?"),
            "started": m.get("started"),
            "branch": git.get("branch", "?"),
            "head": (git.get("head") or "?")[:7],
            "binary_commit": binary.get("commit_short", "?"),
            # True means --allow-stale-binary was used: the campaign's rows
            # name a commit they did not come from.
            "dirty_binary": binary.get("dirty") == "1",
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

    The output directory's name stands where a profile would, and ``label``
    is what the table prints. The planned count is the manifest's queued
    count; the done count comes from the detail probe.
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
    """Everything status reports for one host. Never raises."""
    report: dict = {"host": host, "root": root, "reachable": False,
                    "error": None, "campaigns": [], "hidden": 0,
                    "processes": [], "queue": []}
    text, err = run_shell(host, inventory_script(root))
    inv = parse_inventory(text or "")
    if inv["error"]:
        report["error"] = inv["error"]
        return report
    # A timed-out inventory is still usable when its git section completed:
    # the sections stream in order.
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
        # A machine keeps one manifest per campaign it has ever launched.
        # Those on a branch the checkout has left cannot be in flight.
        live = [c for c in campaigns if c["branch"] == report["branch"]]
        report["hidden"] = len(campaigns) - len(live)
        campaigns = live
    if campaigns and want_plan:
        text, err = run_shell(host, detail_script(root, campaigns))
        # Parsed unconditionally: a probe cut short still holds complete
        # records for the campaigns that got their turn.
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
    inventory call, because the hosts' clocks can be minutes apart.

    A process is attributed to a campaign only when it names that campaign's
    profile; an engine process names none. A matched process whose campaign
    output has gone silent reports as `stuck`, not `running`.
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
        # Crude: rows per second since the manifest was written, which
        # credits this launch with any rows it merely resumed onto.
        rate = done / c["elapsed_s"]
        if rate > 0:
            c["eta_s"] = int((planned - done) / rate)


def claims_campaign(proc: dict, c: dict) -> bool:
    """Whether a live process is this campaign's own.

    A runner claims the campaign whose profile it names; a scorer claims the
    score phase whose output directory it names, compared by name since the
    path may be relative or absolute. An engine process claims nothing.
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

# Plain SGR escapes, no colorama and no rich: the hosts have no third-party
# packages.
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

    Colour is decoration over a plain layout: stripping the escapes from a
    coloured table gives back the plain one byte for byte.
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

    ``--no-color`` beats everything, then NO_COLOR (any non-empty value),
    then CLICOLOR_FORCE, which forces colour on down a pipe. Otherwise colour
    is off wherever stdout is not a terminal, as under cron.
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

    ``paint(row, col, cell, style) -> str`` may wrap a cell in escapes.
    Widths come from the unpainted text and the padding goes outside whatever
    it returns. It is not called when colour is off.
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
# (done, running) and the two that want a person (stuck, stalled).
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
            # checkout has since moved off it.
            branch = c["branch"] + ("!" if c["branch"] != r["branch"] else "")
            # "~" marks a done count taken from the whole CSV, which can
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
    """The checkout each host is sitting on, independent of any campaign."""
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
    # The queue table is printed only where there is a queue.
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

    merge_experiments.PROFILE_CSVS is the authority. A campaign on an unmerged
    branch is absent from it and needs the overrides; the name is never
    guessed.
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
    """What an rsync would move, via ``rsync -a --dry-run --stats``.

    Read-only on both ends.
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

    The check is against the union, not the sum: disjoint seed ranges overlap
    on nothing and a re-collect overlaps on everything.

    ``missing`` names hosts that were asked for and contributed nothing. It
    is passed in because the union over the surviving hosts agrees with
    itself, so a half-collected campaign would otherwise verify clean.
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
            # A deadline and a dead host need different words: only the
            # second means the collect cannot proceed.
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
# A score phase leaves one CSV per run under <out>/ on each host. Each host's
# directory is pulled whole into experiments/<out>/<host>/ and the per-run
# CSVs are joined into experiments/<out>.csv, header once. The join is a union
# by run name: a run scored on two hosts is a split violation.


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

    The whole directory, not the CSVs alone. Returns rsync's verdict; a dry
    run reports the transfer it would make and moves nothing.
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
    byte-identical. Two distinct headers among the curves are a problem.

    The merged file is written only when there is no problem, so a bad
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

    The merged row count must equal the sum over the union of each chosen
    curve's rows. A host that answered nothing is INCOMPLETE, as in
    verify_merge.
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
    # Named for what chose the file: --csv bypasses --profile entirely.
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
        # pull_source is called directly, not through resolve_source, whose
        # REMOTES entry is the FQDN form. It wraps only the CSV rsync against
        # failure, so the per-run transfer raises on an unreachable host.
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
    # Merge what did arrive: the merge is idempotent and keyed, so a later
    # run completes the file. The exit status still fails.
    merge.merge_csv(list(pulled.values()), results_csv)
    return 0 if verify_merge(results_csv, before, per_host, missing) else 1


# -- TOML ----------------------------------------------------------------------

# tomllib arrived in Python 3.11, which the lab hosts have. tomli is the same
# parser under its old name, for a workstation still on 3.10.
try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib  # type: ignore[no-redef]

TomlError = tomllib.TOMLDecodeError


def parse_toml(text: str) -> dict:
    return tomllib.loads(text)


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
    raise TypeError(f"cannot write {type(value).__name__} to TOML")


def dump_toml(table: dict) -> str:
    """Emit a flat table. Key order is the caller's, so entries stay diffable."""
    return "".join(f"{key} = {toml_value(value)}\n"
                   for key, value in table.items())


# -- The campaign declaration -------------------------------------------------

class CampaignError(Exception):
    """A campaign.toml that cannot be acted on. Always fatal."""


CAMPAIGN_KEYS = {"name", "branch", "profile", "hosts", "phases", "build",
                 "configs", "description"}
# A phase is a search run (`kind = "run"`, the default) or an offline scoring
# pass over a results directory (`kind = "score"`). Each kind reads its own
# keys, and a key from the other kind is refused by name.
PHASE_KINDS = ("run", "score")
RUN_PHASE_KEYS = {"name", "kind", "profile", "jobs", "sweeps", "specs", "hosts"}
SCORE_BUDGET_KEYS = ("workers", "cores", "cuts", "maximal_timeout",
                     "compare_timeout", "deadline_s", "wall_cap_s")
SCORE_PHASE_KEYS = {"name", "kind", "profile", "results", "out", "hosts",
                    *SCORE_BUDGET_KEYS}


def parse_seed_range(text: str, where: str) -> list:
    """``"0-99"``, ``"7"`` or ``"0-9,20-29"`` to an explicit, sorted seed list.
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

    Shared by the campaign-level table and a phase's override, so the overlap
    check runs at both levels.
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
    """The inverse of parse_seed_range, collapsing runs."""
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

    Imported lazily and never mirrored here.
    """
    import run_experiments  # noqa: PLC0415
    return set(run_experiments.PROFILES)


def profile_configs_dir(profile: str) -> str:
    """A profile's configs directory, relative to the repo root.

    ``PROFILES`` holds it as an absolute path under this checkout. A lab
    machine's checkout sits under a different home directory, so only the
    relative form means the same thing on both sides.
    """
    return profile_dir(profile, "configs_dir", "configs")


def profile_results_dir(profile: str) -> str:
    """A profile's results directory, relative to the repo root."""
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

    Raises CampaignError on any fault, including a seed shared by two hosts.
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
    # directories. A score phase reads no configs.
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

    ``results`` defaults to the named profile's results directory, relative
    to the repo root; ``out`` defaults to ``curves-<stem>`` beside it. Every
    budget defaults to the scorer's own value and is carried explicitly to
    the command line, so the host's manifest records the declaration's values.
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

    A directory an earlier run phase of the same campaign produces does not
    exist at stage time and is not demanded. A phase narrowed away from a
    host demands nothing there.
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

    A phase that declares a split and omits a host runs nowhere on it: an
    absent host is a deliberate narrowing, with no fallback to the campaign
    range.
    """
    if phase.get("hosts") is None:
        return default
    return phase["hosts"].get(host, [])


def entry_phase_seeds(entry: dict, index: int) -> str:
    """The frozen seed range one queue entry runs its `index`-th phase on.

    An entry that carries no `phase_seeds` falls back to the campaign range
    it was enqueued under, never to the current declaration.
    """
    frozen = list(entry.get("phase_seeds") or [])
    if index < len(frozen):
        return frozen[index]
    return entry.get("seeds", "")


def phase_args(phase: dict, seeds: list) -> list:
    """The runner arguments for one phase over one host's seeds.

    The seeds come from the declaration, never from an argument typed at
    launch time.
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

    Stated so the manifest the scorer writes records the declaration's values.
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
# the sections above it.


def stage_probe_script(root: str) -> str:
    return render_script(STAGE_PROBE_SCRIPT,
                         ROOT=shlex.quote(root),
                         BIN=shlex.quote("./" + PEREDUR_BINARY),
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
    """``peredur --version`` output as a dict, ignoring anything unkeyed."""
    out: dict = {}
    for line in lines:
        key, sep, value = line.strip().partition("=")
        if sep:
            out[key.strip()] = value.strip()
    return out


@dataclass
class HostProbe:
    """What one host answered. Every field is present on every path.

    An empty answer with no error gets past the callers' ``error`` guard, so
    no field may be absent.
    """
    error: str | None = None
    branch: str = "?"
    head: str = "?"
    dirty: list = field(default_factory=list)
    processes: list = field(default_factory=list)
    binary: dict = field(default_factory=dict)
    queue: list = field(default_factory=list)


def probe_processes(ps_lines: list) -> list:
    """The probe's live-process list.

    `stage` refuses on any live `peredur` or `run_experiments.py` on the
    machine, so the verdict depends on the whole machine. Tests replace this
    function, as they replace run_shell.
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

    Each of the three destroys work that cannot be recovered from this side:
    uncommitted edits, a run in flight, and somebody else's branch.
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
# reports as itself. The command goes in a subshell: a campaign may join two
# gen_configs.py calls with `&&`, and `! a && b` binds the negation to the
# first alone.
CONFIGS_STEP = r"""configlog=$(mktemp) || { echo "@M@ERR mktemp failed"; exit 9; }
if ! ( @CONFIGS_CMD@ ) > "$configlog" 2>&1; then
  tail -20 "$configlog"; rm -f "$configlog"
  echo "@M@ERR configs command failed"; exit 9
fi
tail -5 "$configlog"; rm -f "$configlog"
"""

# Runs whether or not the campaign declares a command, so an empty configs
# directory fails here and not on the host three ticks later.
#
# `find` with a quoted pattern, not a glob: the lab login shell is zsh, whose
# default NOMATCH skips the check where `*.toml` matches nothing. -print -quit
# stops at the first hit.
CONFIGS_CHECK = r"""for cfgdir in @CONFIG_DIRS@; do
  if [ -z "$(find "$cfgdir" -name '*.toml' -print -quit 2>/dev/null)" ]; then
    echo "@M@ERR no config files under $(pwd)/$cfgdir — generate them with scripts/gen_configs.py, and declare that command as configs = ... in campaign.toml"
    exit 10
  fi
done
"""
# No `git clean`: a host's untracked files are its results. `checkout -f`
# discards tracked modifications only. The build goes through a temporary file
# and not a pipe into `tail`, whose exit status is the one a pipeline reports.


# A score phase's counterpart: the results directory it reads must be on the
# host already, unless a run phase of the same campaign writes it first, in
# which case the tick checks it when the phase's turn comes.
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
    check. The check is never conditional. A score phase's results directory
    is checked the same way, after it."""
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
    # block carries markers of its own. BUILD and BIN go in unquoted -- the
    # first is a command line, the second is spliced into `./@BIN@`.
    return render_script(
        STAGE_APPLY_SCRIPT,
        CONFIGS=configs_block(configs, config_dirs, results_dirs),
        ROOT=shlex.quote(root),
        BRANCH=shlex.quote(branch),
        SHA=shlex.quote(sha),
        BUILD=build,
        BIN=PEREDUR_BINARY,
        # Never `--` here: `git checkout -- -B x` reads -B as a path.
        CHECKOUT_FLAGS="-f" if force else "",
        FORCE="1" if force else "0")


STAGE_HEADERS = ["HOST", "BRANCH", "HEAD", "BINARY", "RESULT"]


def stage_paint(row: int, col: int, cell: str, style: Style) -> str:
    """RESULT is green only for a host that is staged and verified."""
    if STAGE_HEADERS[col] != "RESULT":
        return cell
    if cell == "staged":
        return style(cell, "green")
    if cell.startswith("would stage"):
        return style(cell, "dim")
    return style(cell, "red")


def probe_host(host: str) -> HostProbe:
    """One round trip for everything stage decides on."""
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

    Refused outright when stdin is not a terminal, which makes --force
    unusable from cron or from a script.
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
        # remote script also declines to discard a checkout ahead of the
        # target commit, and the probe cannot see that: the host may not hold
        # the target sha until the apply script fetches.
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
# `running` with no tick holding the lock is an interrupted phase: a tick runs
# its phase in the foreground. It costs an attempt, which stops a phase that
# kills its tick every time from cycling for ever.


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
        # command. The declaration lives on the campaign's own branch, so a
        # tick standing on another branch cannot read it before the checkout
        # these two fields drive. Freezing the commit also keeps every phase
        # on one binary.
        "commit": commit,
        "build": campaign["build"],
        # Frozen at enqueue: an edit to campaign.toml must not move a host's
        # share of the seeds under a run that has already produced rows.
        # Phases that narrow the split are frozen one by one.
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
# base64, not a heredoc: the body is TOML with quotes and brackets in it, and
# it crosses ssh into a zsh login shell.


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
    # Pushed here because a tick stages the entry itself and fetches from
    # origin to do it.
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

    The only lock on the queue. flock is per open file description, not per
    process, so two `open()`s of this path in one process contend. That is
    why the crontab line must not wrap the tick in `flock` on the same file.
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

    The restarted process inherits the locked descriptor: a lock dropped for
    the length of an exec would let a hand-typed tick in between.
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

    The running process keeps the campaign.py it imported before the checkout
    moved, which may not parse the declaration on the staged branch.

    ensure_staged has already put the entry back in `queued`, so the restart
    spends no attempt. An exec that fails is not fatal: the tick carries on
    with the sources it has.
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
        print(f"tick: could not restart ({exc}); carrying on unstaged")
        return None
    # Reached only under a stubbed exec, which must stop the tick the way the
    # real one does.
    return 0


def run_step(root: Path, command, log_path: Path, shell: bool = False) -> int:
    """Run one command in the checkout, appending everything it says to a log.

    No deadline: a phase is hours of work and the lock keeps a second tick
    off it. A killed tick costs nothing, since run_experiments.py resumes off
    the results CSV.
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

    Asked before the attempt is spent, so the entry's `last_error` names the
    directory.
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
# `stage` is the attended path and refuses three things: a dirty checkout, a
# live run, and another branch. A tick overrides the branch refusal alone: it
# re-points the checkout at the entry's own commit, which is on origin, and
# rebuilds. Uncommitted work and a live run still stop it, because nothing on
# this side can reconstruct either.


def local_processes() -> list:
    """Runner and engine processes on this machine.

    Machine-wide by design, so tests replace it, as they do probe_processes.
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
    # here can bring back, so HEAD must be on some remote branch.
    contained, err = git_output(["branch", "-r", "--contains", "HEAD"], root)
    if err is None and not contained.strip():
        head, _ = git_output(["rev-parse", "--short", "HEAD"], root)
        out.append(f"HEAD ({head or '?'}) is on no remote branch — push it, "
                   f"or stage this host by hand")
    return out


def stage_checkout(root: Path, entry: dict, log_path: Path):
    """Fetch, check out the entry's commit, rebuild, verify. None on success.

    No `git checkout -f`: the tree is clean by the time this runs. No
    `git clean`: a host's untracked files are its results.
    """
    branch, commit = entry["branch"], entry["commit"]
    if run_step(root, ["git", "fetch", "origin", branch], log_path):
        return f"git fetch origin {branch} failed"
    if run_step(root, ["git", "checkout", "-B", branch, commit], log_path):
        return f"git checkout -B {branch} {commit[:7]} failed"
    return build_and_verify(root, entry, log_path)


def build_and_verify(root: Path, entry: dict, log_path: Path):
    """Build the checkout and read its binary back. None on success.

    Also run when the checkout is already at the entry's commit: an earlier
    staging whose build failed leaves HEAD right and the binary stale.
    """
    commit = entry["commit"]
    build = entry.get("build") or DEFAULT_BUILD_CMD
    if run_step(root, build, log_path, shell=True):
        return f"the build command failed: {build}"
    binary = root / PEREDUR_BINARY
    if not binary.is_file():
        return f"no {PEREDUR_BINARY} after the build"
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
    The commit is checked as well as the branch.
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
        if not commit or args.no_stage or args.dry_run:
            return None
        # Rebuilt even here: see build_and_verify. The build is incremental,
        # so on a host that did build it costs seconds.
        why = build_and_verify(root, entry, entry_log_path(entry, root))
        if why:
            fail_or_requeue(entry, f"rebuilding {branch} at {commit[:7]}: "
                                   f"{why}")
            write_entry(entry["path"], entry)
            print(f"tick: {entry['file']} {entry['state']}: "
                  f"{entry['last_error']}")
            return 1
        return None

    want = entry.get("branch")
    want = f"{want} at {commit[:7]}" if commit else str(want)
    if not commit or args.no_stage:
        # An entry that names no commit has nothing safe to stage to.
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
    # Back to queued: a tick killed between here and the phase must not spend
    # an attempt on a staging that succeeded. tick_entry marks it running.
    entry["state"] = "queued"
    log_line(entry, f"staged {want}")
    write_entry(entry["path"], entry)
    print(f"tick: {entry['file']} staged {want}")
    # Restart only where the checkout moved this tick's own sources.
    changed, _ = git_output(["diff", "--name-only", head, "HEAD", "--",
                             "scripts"], root)
    if not changed.strip():
        return None
    if os.environ.get(TICK_RESTART_ENV):
        # Restarted once already and the scripts moved again: run on what
        # this tick has, so the restart cannot loop.
        print(f"tick: {entry['file']} scripts moved again after a restart; "
              f"running on the sources this tick was started with")
        return None
    return RESTART


def config_dirs_missing(root: Path, config_dirs: list) -> list:
    """The declared configs directories holding no `.toml`. Empty means ready.

    The local twin of CONFIGS_CHECK and must answer the same way: recursive,
    because gen_configs.py writes a directory per factor level, and first hit
    only. A directory that does not exist is missing, not an error.
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

    Checked before the generator is run. A campaign declaring no command
    fails here, before the runner spends the attempts.
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
    # Re-checked: a generator called with the wrong flags writes a different
    # directory and exits 0.
    still = config_dirs_missing(root, campaign["config_dirs"])
    if still:
        return (f"the configs command left no config files under "
                f"{', '.join(still)}: {configs}")
    return None


def fail_or_requeue(entry: dict, why: str) -> None:
    """One attempt spent. Past the cap the entry stops moving and says why."""
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
        # Before load_campaign: the declaration is tracked on the campaign's
        # own branch, which the checkout may not be on yet.
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
        # After load_campaign: the command and the directories to check are
        # keys of campaign.toml.
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
    # No branch check here: ensure_staged alone decides what the checkout may
    # run.
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
    # Refused here, before the scorer would, so the entry names the missing
    # results directory and not an exit status.
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
            # The branch is what the tick stages to.
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


def named_entry(args: argparse.Namespace):
    """(root, entry) for the queue entry a verb names, or its exit code."""
    host = args.host
    root = source_path(host) if host != LOCAL else str(REPO_ROOT)
    report = gather_host(host, root, None, False, False)
    if not report["reachable"]:
        print(f"error: {host} unreachable: {report['error']}")
        return 1
    matches = [e for e in report["queue"]
               if Path(e.get("file", "")).name == args.entry]
    if not matches:
        print(f"error: {host} has no queue entry {args.entry!r}")
        return 2
    return root, entry_body(matches[0])


def entry_b64(entry: dict) -> str:
    import base64  # noqa: PLC0415
    return shlex.quote(base64.b64encode(dump_toml(entry).encode()).decode())


def cmd_requeue(args: argparse.Namespace) -> int:
    """Clear a failed entry's attempt count so the next tick tries it again."""
    host, found = args.host, named_entry(args)
    if isinstance(found, int):
        return found
    root, entry = found
    entry["state"] = "queued"
    entry["attempts"] = 0
    entry["last_error"] = ""
    log_line(entry, "requeued by hand")
    text, err = run_shell(host, render_script(
        REQUEUE_SCRIPT,
        ROOT=shlex.quote(root),
        PATH=shlex.quote(f"{QUEUE_DIR}/{args.entry}"),
        B64=entry_b64(entry)))
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
# The tick is killed first and the entry rewritten last, in one script: a live
# tick writes the entry's state at the end of every phase from the copy it
# holds in memory, and would overwrite the rewrite. Its descendants are
# collected before it dies, since a dead parent's children reparent to init,
# and killed after it, or the runner and engine processes outlive the tick.
#
# The tree is a fixpoint over one `ps -eo pid=,ppid=` snapshot, not a loop
# over `pgrep -P`, because the remote shell is zsh: an unquoted parameter does
# not word-split there, so `for p in $FRONTIER` iterates once over the whole
# frontier. Every loop here reads its pids from a file, one per line, for the
# same reason.


def cmd_dequeue(args: argparse.Namespace) -> int:
    """Take an entry out of the queue, stopping the phase it is mid-way through.

    The entry is marked cancelled, not deleted: its log is the record of what
    it did, and `enqueue` numbers entries from the names already present.
    `requeue` puts a cancelled entry back.
    """
    host, found = args.host, named_entry(args)
    if isinstance(found, int):
        return found
    root, entry = found
    was = str(entry.get("state", "?"))
    if was == "cancelled":
        print(f"{host}: {args.entry} is already cancelled.")
        return 0
    # Only a `running` entry names a live tick. The pid on any other state is
    # long dead and may since have been reused.
    try:
        pid = int(entry.get("pid", 0)) if was == "running" else 0
    except (TypeError, ValueError):
        pid = 0
    why = args.reason or "dequeued by hand"
    entry["state"] = "cancelled"
    entry["last_error"] = why
    log_line(entry, f"cancelled while {was}: {why}")
    text, err = run_shell(host, render_script(
        DEQUEUE_SCRIPT,
        ROOT=shlex.quote(root),
        PATH=shlex.quote(f"{QUEUE_DIR}/{args.entry}"),
        PID=str(pid),
        KILL="no" if args.dry_run else "yes",
        WRITE="no" if args.dry_run else "yes",
        B64=entry_b64(entry)), timeout=SSH_TIMEOUT_S + 10)
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
    # No `flock -n` wrapper. flock locks attach to the open file description,
    # and a wrapper's fd is inherited across the exec: acquire_lock() then
    # opens the same path a second time and is denied by the lock its own
    # parent holds, silently, since both exit 0. acquire_lock() alone is the
    # guard, and it also blocks a tick typed by hand.
    return (f"*/5 * * * * cd {root} && "
            f"{REMOTE_PYTHON} scripts/campaign.py tick --host {host} "
            f">> $HOME/.peredur-queue.log 2>&1")


def cmd_cron(args: argparse.Namespace) -> int:
    """Print the crontab line. Nothing is installed: a script must not edit
    a crontab on a lab machine."""
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
    # A no-op: cmd_cron prints whether or not it is passed. It stays accepted
    # because `cron --print` is the documented invocation.
    cron.add_argument("--print", dest="print_only", action="store_true",
                      help="Print the line. Printing is all this verb does; "
                           "nothing is installed anywhere.")
    cron.set_defaults(func=cmd_cron)
    return parser


def main() -> None:
    args = build_parser().parse_args()
    # --json is never coloured, whatever the terminal says: it is parsed, not
    # read.
    set_colour(not getattr(args, "json", False)
               and colour_enabled(getattr(args, "no_color", False)))
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
