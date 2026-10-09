#!/usr/bin/env python3
"""Score GLASS, JVTS-Repair and AMT13 against PEREDUR by coverage.

    python3 scripts/maoz_score.py --pass pool --out experiments/maoz-peredur-pool
    python3 scripts/maoz_score.py --pass screen --out OUT/screen --jobs 16
    python3 scripts/maoz_score.py --pass frontier --screen OUT/screen \\
        --out OUT/frontier --jobs 16
    python3 scripts/maoz_score.py --pass coverage --frontier OUT/frontier \\
        --out OUT/relations --jobs 16
    python3 scripts/maoz_score.py --pass report --screen OUT/screen \\
        --frontier OUT/frontier --coverage OUT/relations --out OUT/report

The registration is section 11 of experiments/2026-10-02-maoz-baselines/
PLAN.md, and each pass computes one part of it. A `kind = "maoz-score"` phase
in campaign.toml runs one pass.

pool
    PEREDUR's side. For each subject and each of the 30 runs of the shipping
    arm of 2026-09-14-paper-rerun, the run's frontier is the set its
    maximality curve holds at its last cut, read from the `.members.tsv`
    sidecar. Every frontier file is copied once into <out>/<spec>/<md5>.tlsf,
    where md5 is taken over the text with `//` comments and all whitespace
    removed, and pool.tsv maps (spec, run, seed, file) to its md5. runs.tsv
    has one row per run, including any run with an empty frontier. The
    sidecars of each host hold half the seeds, so the pass runs where both
    halves are, and the pool is copied to the scoring host.

screen
    Section 11.1. Each tool repair goes through `realize` under a wall budget
    (killing its process group) and through check_well_separated.check_one
    with the fast path off. A repair is admissible when both say yes, and
    undecided when either is undecided and neither says no.

frontier
    Section 11.2. Per (tool, subject), `maximal` over the admissible repairs,
    one representative per mutual-implication class. `maximal` reads an
    undecided implication as false and reports no count of them, so an
    undecided pair can only keep a dominated repair on the frontier or split
    a class; neither can turn a covered frontier into an uncovered one.

coverage
    Section 11.3. Every pair (PEREDUR pool repair, tool frontier
    representative) of a subject is classed by `compare`, with the PEREDUR
    repair as the repair and the tool repair as the ideal: `stronger` means
    the PEREDUR repair implies the tool repair. `compare` prints only when all
    of its pairs are done, so a unit is one tool repair against at most
    `--chunk-size` pool files under `--chunk-timeout`; a unit that overruns
    is re-run one pair at a time under `--pair-timeout`, and a pair that
    overruns that is `undecided`, as is a pair `compare` reports as
    `timeout`.

report
    Sections 11.3 and 11.5: per (tool, subject) coverage per run, coverage by
    the pool, the converse and its guarantee-weakening count, each under both
    readings of an undecided pair, and the Outcome 1/2/3 verdict.

Every pass writes maoz-score-manifest-<host>.json in its out directory, which
`campaign.py status` reads. screen and coverage resume from their CSVs.

Stdlib only, on python 3.10: this runs on the lab hosts from a cron tick.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import os
import platform
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import check_well_separated as W  # noqa: E402
import run_experiments as R  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[3]  # vendored: experiments/<campaign>/scripts
BIN_DIR = Path(os.environ.get("PEREDUR_BIN_DIR", REPO_ROOT / "build-release"))
REALIZE_BIN = BIN_DIR / "realize"
COMPARE_BIN = BIN_DIR / "compare"
MAXIMAL_BIN = BIN_DIR / "maximal"
LTLSYNT_BIN = BIN_DIR / "third_party" / "spot" / "bin" / "ltlsynt"

PASSES = ("pool", "screen", "frontier", "coverage", "report")
SUBJECTS = ("rg1", "rg2", "lift", "gyro-var1", "gyro-var2", "humanoid-458",
            "humanoid-503", "humanoid-531", "humanoid-742", "pcar-v2-888")
# The tool directories maoz_adapt.py writes, and the artifact's code for each.
TOOLS = {"glass": "UF", "jvts-repair": "BFS", "amt13": "ALUR"}

# The PEREDUR arm section 11.2 names, and where its two halves live.
ARM = "sweep_G_mrs_nsga2-apportion_wkoff_log"
N_RUNS = 30
DEFAULT_CURVES = (Path.home() / "projects/counter/.claude/worktrees/"
                  "agent-a1a7f56e34a7cc540/experiments/curves-paper-rerun")
DEFAULT_RERUN = Path.home() / "projects/counter/experiments/results-paper-rerun"

# The values a maoz-score phase takes when campaign.toml omits a key.
# campaign.py reads them from here, so the declaration and the CLI agree.
DEFAULTS = {
    "jobs": 16,
    # Section 11.1: realize within 600 s, the well-separation ltlsynt in 60 s.
    "realize_timeout": 600,
    "wellsep_timeout": 60,
    # maximal's per-solver-call budget, and the wall cap on one maximal.
    "maximal_timeout": 20,
    "maximal_wall_s": 14400,
    # compare's units: pool files per call, the wall cap on one call, and the
    # cap on one pair when a call overruns.
    "chunk_size": 50,
    "chunk_timeout": 900,
    "pair_timeout": 120,
    # Cores each coverage worker is pinned to; compare runs one solver thread
    # per core it sees.
    "cores": 1,
}

# Where the inputs live under the checkout when a pass is not told otherwise.
INPUT_DEFAULTS = {"results": "experiments/results-maoz-baselines",
                  "pool": "experiments/maoz-peredur-pool",
                  "maoz_out": "experiments/maoz-baselines-out"}

MANIFEST_STEM = "maoz-score-manifest"
SCREEN_CSV = "screen.csv"
FRONTIER_CSV = "frontier.csv"
FRONTIER_SUMMARY_CSV = "frontier-summary.csv"
RELATIONS_CSV = "relations.csv"
TIMINGS_CSV = "timings.csv"
REPORT_CSV = "report.csv"
REPORT_TXT = "report.txt"
POOL_TSV = "pool.tsv"
RUNS_TSV = "runs.tsv"

SCREEN_FIELDS = ["tool", "spec", "index", "file", "bytes", "realizable",
                 "realize_s", "realize_detail", "well_separated",
                 "wellsep_s", "wellsep_detail", "admissible"]
FRONTIER_FIELDS = ["tool", "spec", "file", "class", "representative",
                   "n_identical"]
SUMMARY_FIELDS = ["tool", "spec", "n_admissible", "status", "files",
                  "distinct", "maximal", "classes", "unparsed", "wall_s",
                  "detail"]
RELATION_FIELDS = ["tool", "spec", "tool_file", "peredur_md5", "relation",
                   "source"]
TIMING_FIELDS = ["tool", "spec", "tool_file", "chunk", "n_pairs", "mode",
                 "rc", "wall_s"]
# compare's words, as printed after the repair's name, to this file's.
COMPARE_WORDS = (("equivalent to ", "equivalent"),
                 ("strictly stronger than ", "stronger"),
                 ("strictly weaker than ", "weaker"),
                 ("incomparable", "incomparable"),
                 ("timeout", "undecided"))
RELATIONS = ("equivalent", "stronger", "weaker", "incomparable", "undecided")
COMPARE_LINE = re.compile(r"^(\S+)\s+: (.*)$")
MAXIMAL_CLASS = re.compile(r"^class (\d+)\s+(\S+)(?:\s+\(\+(\d+) identical\))?$")
SPEC_FILE = re.compile(r"^spec(\d+)\.tlsf$")
TIMEOUT_RC = 124


# -- shared ---------------------------------------------------------------


def now() -> str:
    return dt.datetime.now().astimezone().isoformat(timespec="seconds")


def host_name() -> str:
    return socket.gethostname().split(".")[0]


def resolve(path_text) -> Path:
    path = Path(path_text).expanduser()
    return path if path.is_absolute() else REPO_ROOT / path


def tool_dir(results: Path, tool: str, spec: str) -> Path:
    return results / f"maoz-{tool}_{spec}_seed00" / "accumulated"


def normalised_md5(text: str) -> str:
    """The pool's dedup key: `//` comments and all whitespace removed."""
    stripped = re.sub(r"//[^\n]*", "", text)
    stripped = re.sub(r"\s+", "", stripped)
    return hashlib.md5(stripped.encode("utf-8")).hexdigest()


def read_csv(path: Path) -> list[dict]:
    if not path.exists():
        return []
    with open(path, newline="") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, fields: list, rows: list[dict]) -> None:
    part = path.with_name(path.name + ".part")
    with open(part, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields,
                                extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    os.replace(part, path)


class Appender:
    """One CSV appended row by row from several threads, under one lock, so
    a killed pass keeps every row it finished."""

    def __init__(self, path: Path, fields: list):
        self.path = path
        self.fields = fields
        self.lock = threading.Lock()
        if not path.exists() or path.stat().st_size == 0:
            with open(path, "w", newline="") as handle:
                csv.DictWriter(handle, fieldnames=fields).writeheader()

    def append(self, rows: list[dict]) -> None:
        with self.lock:
            with open(self.path, "a", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=self.fields,
                                        extrasaction="ignore")
                writer.writerows(rows)
                handle.flush()


def run_killable(command: list, timeout_s: float, cwd=None,
                 prefix: list | None = None) -> tuple[int, str, str, float]:
    """Run in a session of its own; on overrun kill the whole session.

    Returns (rc, stdout, stderr, wall_s), rc TIMEOUT_RC on a kill. The tools
    fork solvers, and killing the parent alone would leave those running.
    """
    t0 = time.monotonic()
    try:
        proc = subprocess.Popen([*(prefix or []), *command], cwd=cwd,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True,
                                start_new_session=True)
    except OSError as exc:
        return 127, "", str(exc), 0.0
    try:
        out, err = proc.communicate(timeout=timeout_s)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        out, err = proc.communicate()
        rc = TIMEOUT_RC
    return rc, out or "", err or "", round(time.monotonic() - t0, 3)


def binaries_for(pass_name: str) -> dict:
    return {"screen": {"realize": REALIZE_BIN, "ltlsynt": LTLSYNT_BIN},
            "frontier": {"maximal": MAXIMAL_BIN},
            "coverage": {"compare": COMPARE_BIN}}.get(pass_name, {})


def read_versions(pass_name: str) -> dict:
    """`--version` of each PEREDUR binary the pass runs. ltlsynt is SPOT's,
    so its first `--version` line stands in for a commit."""
    out = {}
    for name, path in binaries_for(pass_name).items():
        if name == "ltlsynt":
            try:
                text = subprocess.run([str(path), "--version"],
                                      capture_output=True, text=True,
                                      timeout=60).stdout
                out[name] = {"path": str(path),
                             "version": (text.splitlines() or ["?"])[0]}
            except (OSError, subprocess.SubprocessError):
                out[name] = {"path": str(path), "version": "?"}
            continue
        out[name] = {"path": str(path), **R.binary_version(path)}
    return out


def enforce_freshness(versions: dict, head, allow_stale: bool) -> None:
    peredur = {k: v for k, v in versions.items() if "commit" in v}
    problems = R.staleness_problems(peredur, head)
    if not problems:
        return
    for problem in problems:
        print(f"STALE BINARY: {problem}", file=sys.stderr)
    if not allow_stale:
        sys.exit("Refusing to score with a stale binary; rebuild "
                 "build-release or pass --allow-stale-binary.")
    print("Continuing anyway (--allow-stale-binary).", file=sys.stderr)


class Manifest:
    def __init__(self, args, out: Path, versions: dict, budgets: dict):
        self.path = out / f"{MANIFEST_STEM}-{host_name()}.json"
        self.lock = threading.Lock()
        self.data = {
            "written_by": "scripts/maoz_score.py",
            "kind": "maoz-score",
            "pass": args.pass_name,
            "hostname": socket.gethostname(),
            "git": {"branch": R.git_branch(),
                    "head": R.working_tree_head() or R.LEGACY_COMMIT},
            "argv": sys.argv,
            "out": str(args.out),
            "out_resolved": str(out),
            "results": str(args.results),
            "pool": str(args.pool),
            "specs": list(args.specs),
            "tools": list(args.tools),
            "seeds": list(args.seeds),
            "jobs": args.jobs,
            "budgets": budgets,
            "binaries": versions,
            "allow_stale_binary": bool(args.allow_stale_binary),
            "python_version": platform.python_version(),
            "started": now(),
            "finished": None,
            "wall_s": None,
            "counts": {"planned": 0, "done": 0},
        }
        self.t0 = time.monotonic()

    def write(self) -> None:
        with self.lock:
            part = self.path.with_name(self.path.name + ".part")
            part.write_text(json.dumps(self.data, indent=2) + "\n")
            os.replace(part, self.path)

    def bump(self, key: str = "done", by: int = 1) -> None:
        with self.lock:
            self.data["counts"][key] = self.data["counts"].get(key, 0) + by
        self.write()

    def finish(self, **counts) -> None:
        self.data["counts"].update(counts)
        self.data["finished"] = now()
        self.data["wall_s"] = round(time.monotonic() - self.t0, 1)
        self.write()


# -- pool -----------------------------------------------------------------


def frontier_files(members: Path) -> tuple[float | None, list[str]]:
    """The files a run's curve holds at its last cut, and that cut."""
    rows = []
    with open(members, newline="") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            rows.append((float(row["cut_s"]), row["file"]))
    if not rows:
        return None, []
    last = max(cut for cut, _ in rows)
    return last, sorted({f for cut, f in rows if cut == last})


def pass_pool(args, out: Path, manifest: Manifest) -> int:
    curves = resolve(args.curves)
    rerun = resolve(args.rerun)
    pool_rows, run_rows = [], []
    written: dict = {}
    manifest.data.update({"curves": str(curves), "rerun": str(rerun),
                          "arm": ARM})
    manifest.data["counts"]["planned"] = len(args.specs) * N_RUNS
    manifest.write()
    for spec in args.specs:
        for seed in range(N_RUNS):
            run = f"{ARM}_{spec}_seed{seed:02d}"
            found = [(h, curves / h / f"{run}.members.tsv")
                     for h in ("av2", "av3")]
            found = [(h, p) for h, p in found if p.exists()]
            if not found:
                sys.exit(f"no sidecar for {run} under {curves}/{{av2,av3}}")
            sides = {h: frontier_files(p) for h, p in found}
            if len({json.dumps(v) for v in sides.values()}) > 1:
                sys.exit(f"{run}: the two hosts' sidecars disagree")
            host = found[0][0]
            cut, files = sides[host]
            run_rows.append({"spec": spec, "run": run, "seed": seed,
                             "host": host, "last_cut_s": cut,
                             "n_frontier": len(files)})
            for name in files:
                source = rerun / run / "accumulated" / name
                text = source.read_text()
                md5 = normalised_md5(text)
                target = out / spec / f"{md5}.tlsf"
                if (spec, md5) not in written:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_text(text)
                    written[(spec, md5)] = str(source)
                pool_rows.append({"spec": spec, "run": run, "seed": seed,
                                  "file": name, "md5": md5})
            manifest.bump()
    with open(out / POOL_TSV, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["spec", "run", "seed",
                                                    "file", "md5"],
                                delimiter="\t")
        writer.writeheader()
        writer.writerows(pool_rows)
    with open(out / RUNS_TSV, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["spec", "run", "seed",
                                                    "host", "last_cut_s",
                                                    "n_frontier"],
                                delimiter="\t")
        writer.writeheader()
        writer.writerows(run_rows)
    per_spec = {s: sum(1 for (sp, _) in written if sp == s)
                for s in args.specs}
    manifest.finish(frontier_rows=len(pool_rows), unique=len(written),
                    unique_per_spec=per_spec)
    print(f"pool: {len(pool_rows)} frontier rows over {len(run_rows)} runs, "
          f"{len(written)} unique repairs")
    for spec, n in per_spec.items():
        print(f"  {spec:<14} {n}")
    return 0


# -- screen ---------------------------------------------------------------


def tool_repairs(results: Path, tools, specs) -> list[dict]:
    items = []
    for tool in tools:
        for spec in specs:
            acc = tool_dir(results, tool, spec)
            if not acc.is_dir():
                continue
            for path in sorted(acc.glob("spec*.tlsf")):
                match = SPEC_FILE.match(path.name)
                if match is None:
                    continue
                items.append({"tool": tool, "spec": spec,
                              "index": int(match.group(1)),
                              "file": path.name, "path": path,
                              "bytes": path.stat().st_size})
    return items


def realize_one(path: Path, timeout_s: int) -> tuple[str, float, str]:
    rc, out, err, wall = run_killable([str(REALIZE_BIN), str(path)],
                                      timeout_s)
    if rc == TIMEOUT_RC:
        return "undecided", wall, f"killed at {timeout_s}s"
    word = out.strip()
    if rc == 0 and word == "REALIZABLE":
        return "yes", wall, ""
    if rc == 0 and word == "UNREALIZABLE":
        return "no", wall, ""
    detail = (err.strip() or out.strip())[-200:].replace("\n", " ")
    return "undecided", wall, f"rc={rc}: {detail}"


def wellsep_one(path: Path, timeout_s: int) -> tuple[str, float, str]:
    result = W.check_one(path, LTLSYNT_BIN, float(timeout_s), False)
    verdict = {W.VERDICT_WELL_SEPARATED: "yes",
               W.VERDICT_NOT_WELL_SEPARATED: "no"}.get(result.verdict,
                                                       "undecided")
    return verdict, round(result.elapsed_s, 3), result.detail


def admissible(realizable: str, well_separated: str) -> str:
    if "no" in (realizable, well_separated):
        return "no"
    if realizable == well_separated == "yes":
        return "yes"
    return "undecided"


def pass_screen(args, out: Path, manifest: Manifest) -> int:
    results = resolve(args.results)
    items = tool_repairs(results, args.tools, args.specs)
    if not items:
        print(f"no tool repairs under {results}", file=sys.stderr)
        return 2
    csv_path = out / SCREEN_CSV
    done = {(r["tool"], r["spec"], r["file"]) for r in read_csv(csv_path)}
    todo = [i for i in items if (i["tool"], i["spec"], i["file"]) not in done]
    # Smallest first: a hard repair is usually a long one, and a status poll
    # then sees most of the rows early.
    todo.sort(key=lambda i: (i["bytes"], i["tool"], i["spec"], i["index"]))
    manifest.data["counts"].update({"planned": len(items),
                                    "done": len(items) - len(todo),
                                    "already": len(items) - len(todo)})
    manifest.write()
    print(f"screen: {len(items)} tool repairs, {len(todo)} to screen, "
          f"jobs {args.jobs}, realize {args.realize_timeout}s, "
          f"well-separation {args.wellsep_timeout}s")
    sink = Appender(csv_path, SCREEN_FIELDS)

    def work(item: dict) -> None:
        real, real_s, real_d = realize_one(item["path"], args.realize_timeout)
        sep, sep_s, sep_d = wellsep_one(item["path"], args.wellsep_timeout)
        row = {**item, "realizable": real, "realize_s": real_s,
               "realize_detail": real_d, "well_separated": sep,
               "wellsep_s": sep_s, "wellsep_detail": sep_d,
               "admissible": admissible(real, sep)}
        sink.append([row])
        manifest.bump()

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(work, todo))
    rows = read_csv(csv_path)
    rows.sort(key=lambda r: (r["tool"], r["spec"], int(r["index"])))
    write_csv(csv_path, SCREEN_FIELDS, rows)
    tally: dict = {}
    for r in rows:
        tally[r["admissible"]] = tally.get(r["admissible"], 0) + 1
    manifest.finish(admissible=tally)
    print(f"screen: done, admissible {tally}")
    missing = len(items) - len(rows)
    return 0 if missing == 0 else 1


# -- frontier -------------------------------------------------------------


def parse_maximal(text: str) -> tuple[dict, list[dict]]:
    head, members = {}, []
    for line in text.splitlines():
        line = line.strip()
        match = MAXIMAL_CLASS.match(line)
        if match:
            members.append({"class": int(match.group(1)),
                            "file": Path(match.group(2)).name,
                            "n_identical": int(match.group(3) or 0)})
            continue
        words = line.split()
        if len(words) == 2 and words[1].isdigit():
            head[words[0]] = int(words[1])
    return head, members


def pass_frontier(args, out: Path, manifest: Manifest) -> int:
    screen = read_csv(resolve(args.screen) / SCREEN_CSV)
    if not screen:
        print(f"no {SCREEN_CSV} under {args.screen}", file=sys.stderr)
        return 2
    results = resolve(args.results)
    groups: dict = {}
    for row in screen:
        if row["tool"] in args.tools and row["spec"] in args.specs:
            key = (row["tool"], row["spec"])
            groups.setdefault(key, [])
            if row["admissible"] == "yes":
                groups[key].append(row["file"])
    # Every (tool, spec) with a tool directory, so an empty one is reported
    # as empty rather than missing.
    for tool in args.tools:
        for spec in args.specs:
            if tool_dir(results, tool, spec).is_dir():
                groups.setdefault((tool, spec), [])
    order = sorted(groups, key=lambda k: (len(groups[k]), k))
    manifest.data["counts"]["planned"] = len(order)
    manifest.write()
    members, summary = [], []
    failed = 0
    tmp_root = out / "tmp"
    tmp_root.mkdir(parents=True, exist_ok=True)
    for tool, spec in order:
        files = sorted(groups[(tool, spec)],
                       key=lambda f: int(SPEC_FILE.match(f).group(1)))
        row = {"tool": tool, "spec": spec, "n_admissible": len(files),
               "files": len(files), "detail": ""}
        if len(files) <= 1:
            row.update({"status": "empty" if not files else "single",
                        "distinct": len(files), "maximal": len(files),
                        "classes": len(files), "unparsed": 0, "wall_s": 0})
            for f in files:
                members.append({"tool": tool, "spec": spec, "file": f,
                                "class": 0, "representative": 1,
                                "n_identical": 0})
        else:
            with tempfile.TemporaryDirectory(dir=tmp_root) as tmp:
                for f in files:
                    shutil.copy2(tool_dir(results, tool, spec) / f,
                                 Path(tmp) / f)
                rc, text, err, wall = run_killable(
                    [str(MAXIMAL_BIN), tmp, "--jobs", str(args.jobs),
                     "--timeout", str(args.maximal_timeout)],
                    args.maximal_wall_s)
            head, found = parse_maximal(text)
            row.update({"distinct": head.get("distinct"),
                        "maximal": head.get("maximal"),
                        "classes": head.get("classes"),
                        "unparsed": head.get("unparsed", 0), "wall_s": wall})
            if rc != 0 or not found:
                row["status"] = "timeout" if rc == TIMEOUT_RC else "error"
                row["detail"] = f"rc={rc}: {err.strip()[-300:]}"
                failed += 1
            else:
                row["status"] = "ok"
                seen = set()
                for m in found:
                    rep = m["class"] not in seen
                    seen.add(m["class"])
                    members.append({"tool": tool, "spec": spec,
                                    "file": m["file"], "class": m["class"],
                                    "representative": int(rep),
                                    "n_identical": m["n_identical"]})
        summary.append(row)
        print(f"frontier: {tool}/{spec} {row['n_admissible']} admissible -> "
              f"{row['maximal']} maximal, {row['classes']} classes "
              f"[{row['status']}, {row['wall_s']}s]", flush=True)
        manifest.bump()
    members.sort(key=lambda m: (m["tool"], m["spec"], m["class"], m["file"]))
    summary.sort(key=lambda r: (r["tool"], r["spec"]))
    write_csv(out / FRONTIER_CSV, FRONTIER_FIELDS, members)
    write_csv(out / FRONTIER_SUMMARY_CSV, SUMMARY_FIELDS, summary)
    shutil.rmtree(tmp_root, ignore_errors=True)
    manifest.finish(failed=failed,
                    representatives=sum(m["representative"] for m in members))
    return 0 if failed == 0 else 1


# -- coverage -------------------------------------------------------------


def parse_compare(text: str) -> dict:
    """{repair file name: relation} from compare's per-repair lines."""
    out = {}
    for line in text.splitlines():
        if line.startswith("Summary:"):
            break
        match = COMPARE_LINE.match(line.rstrip())
        if match is None:
            continue
        detail = match.group(2)
        for word, relation in COMPARE_WORDS:
            if detail.startswith(word):
                out[match.group(1)] = relation
                break
    return out


def compare_unit(pool_dir: Path, md5s: list, tool_path: Path, timeout_s,
                 tmp_root: Path, prefix: list) -> tuple[int, dict, float]:
    with tempfile.TemporaryDirectory(dir=tmp_root) as tmp:
        repairs, ideals = Path(tmp) / "repairs", Path(tmp) / "ideals"
        repairs.mkdir()
        ideals.mkdir()
        for md5 in md5s:
            os.symlink(pool_dir / f"{md5}.tlsf", repairs / f"{md5}.tlsf")
        os.symlink(tool_path, ideals / tool_path.name)
        rc, out, _err, wall = run_killable(
            [str(COMPARE_BIN), "--repairs", str(repairs),
             "--ideals", str(ideals)], timeout_s, prefix=prefix)
    found = parse_compare(out) if rc == 0 else {}
    return rc, {name[:-len(".tlsf")]: rel for name, rel in found.items()}, wall


def load_pool(pool: Path, specs) -> dict:
    """{spec: sorted md5s} from the pool directory."""
    return {spec: sorted(p.stem for p in (pool / spec).glob("*.tlsf"))
            for spec in specs if (pool / spec).is_dir()}


def pin_prefix(slot: int, cores: int, pinned: bool) -> list:
    if not pinned:
        return []
    lo = slot * cores
    return ["taskset", "-c", f"{lo}-{lo + cores - 1}"]


def pass_coverage(args, out: Path, manifest: Manifest) -> int:
    results = resolve(args.results)
    pool = resolve(args.pool)
    reps = [m for m in read_csv(resolve(args.frontier) / FRONTIER_CSV)
            if m["representative"] == "1" and m["tool"] in args.tools
            and m["spec"] in args.specs]
    summary = read_csv(resolve(args.frontier) / FRONTIER_SUMMARY_CSV)
    if not summary:
        print(f"no {FRONTIER_SUMMARY_CSV} under {args.frontier}",
              file=sys.stderr)
        return 2
    md5s = load_pool(pool, args.specs)
    missing = sorted({m["spec"] for m in reps} - set(md5s))
    if missing:
        print(f"no pool for {', '.join(missing)} under {pool}",
              file=sys.stderr)
        return 2
    cpus = os.cpu_count() or 1
    if args.jobs * args.cores > cpus:
        print(f"{args.jobs} jobs x {args.cores} cores exceeds this host's "
              f"{cpus}", file=sys.stderr)
        return 2
    relations_path = out / RELATIONS_CSV
    done = {(r["tool"], r["spec"], r["tool_file"], r["peredur_md5"])
            for r in read_csv(relations_path)}
    units = []
    for m in reps:
        pool_md5 = md5s[m["spec"]]
        tool_path = tool_dir(results, m["tool"], m["spec"]) / m["file"]
        for start in range(0, len(pool_md5), args.chunk_size):
            chunk = pool_md5[start:start + args.chunk_size]
            todo = [h for h in chunk
                    if (m["tool"], m["spec"], m["file"], h) not in done]
            if todo:
                units.append({"tool": m["tool"], "spec": m["spec"],
                              "file": m["file"], "path": tool_path,
                              "chunk": start // args.chunk_size,
                              "md5s": todo,
                              "bytes": tool_path.stat().st_size})
    units.sort(key=lambda u: (u["bytes"], u["tool"], u["spec"], u["file"],
                              u["chunk"]))
    planned = sum(len(md5s[m["spec"]]) for m in reps)
    manifest.data["counts"].update({"planned": planned,
                                    "done": planned - sum(len(u["md5s"])
                                                          for u in units),
                                    "units": len(units)})
    manifest.write()
    print(f"coverage: {len(reps)} tool frontier repairs, {planned} pairs, "
          f"{len(units)} units to run, jobs {args.jobs} x {args.cores} "
          f"core(s), chunk {args.chunk_size} under {args.chunk_timeout}s, "
          f"pair {args.pair_timeout}s", flush=True)
    sink = Appender(relations_path, RELATION_FIELDS)
    timings = Appender(out / TIMINGS_CSV, TIMING_FIELDS)
    tmp_root = out / "tmp"
    tmp_root.mkdir(parents=True, exist_ok=True)
    pinned = shutil.which("taskset") is not None and args.cores > 0
    slots = list(range(args.jobs))
    slot_lock = threading.Lock()

    def work(unit: dict) -> None:
        with slot_lock:
            slot = slots.pop()
        try:
            prefix = pin_prefix(slot, args.cores, pinned)
            pool_dir = pool / unit["spec"]
            base = {"tool": unit["tool"], "spec": unit["spec"],
                    "tool_file": unit["file"]}
            rc, found, wall = compare_unit(pool_dir, unit["md5s"],
                                           unit["path"], args.chunk_timeout,
                                           tmp_root, prefix)
            timings.append([{**base, "chunk": unit["chunk"],
                             "n_pairs": len(unit["md5s"]), "mode": "chunk",
                             "rc": rc, "wall_s": wall}])
            rows = []
            if rc == 0 and set(found) >= set(unit["md5s"]):
                rows = [{**base, "peredur_md5": h, "relation": found[h],
                         "source": "chunk"} for h in unit["md5s"]]
            else:
                # One hard pair must not cost the chunk: re-run each pair
                # alone under the smaller cap.
                for h in unit["md5s"]:
                    rc1, one, wall1 = compare_unit(
                        pool_dir, [h], unit["path"], args.pair_timeout,
                        tmp_root, prefix)
                    timings.append([{**base, "chunk": unit["chunk"],
                                     "n_pairs": 1, "mode": "single",
                                     "rc": rc1, "wall_s": wall1}])
                    if rc1 == 0 and h in one:
                        rows.append({**base, "peredur_md5": h,
                                     "relation": one[h], "source": "single"})
                    else:
                        rows.append({**base, "peredur_md5": h,
                                     "relation": "undecided",
                                     "source": ("single-timeout"
                                                if rc1 == TIMEOUT_RC
                                                else f"single-rc{rc1}")})
            sink.append(rows)
            manifest.bump(by=len(rows))
        finally:
            with slot_lock:
                slots.append(slot)

    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        list(executor.map(work, units))
    shutil.rmtree(tmp_root, ignore_errors=True)
    rows = read_csv(relations_path)
    rows.sort(key=lambda r: (r["tool"], r["spec"], r["tool_file"],
                             r["peredur_md5"]))
    write_csv(relations_path, RELATION_FIELDS, rows)
    tally: dict = {}
    for r in rows:
        tally[r["relation"]] = tally.get(r["relation"], 0) + 1
    manifest.finish(relations=tally)
    print(f"coverage: done, {tally}")
    return 0 if len(rows) >= planned else 1


# -- report ---------------------------------------------------------------


def guarantees_of(path: Path) -> tuple:
    """The GUARANTEES statements with whitespace removed, as a sorted tuple,
    so a reordering alone is not a change."""
    spec = W.parse_tlsf(path)
    return tuple(sorted(re.sub(r"\s+", "", g)
                        for g in spec.sections.get("guarantee", [])))


def crash_reason(maoz_out: Path, tool: str, spec: str) -> str | None:
    """Why the tool printed nothing, from its result.json, or None if it
    printed something."""
    path = maoz_out / TOOLS[tool] / spec / "result.json"
    if not path.exists():
        return "no result.json"
    record = json.loads(path.read_text())
    if record.get("n_repairs"):
        return None
    if record.get("killed"):
        return "killed with none printed"
    if record.get("exit_code") not in (0, None):
        return f"crashed with none printed (exit {record.get('exit_code')})"
    return "no repair found"


def share(numerator: int, denominator: int):
    return round(numerator / denominator, 4) if denominator else ""


def pass_report(args, out: Path, manifest: Manifest) -> int:
    results = resolve(args.results)
    pool = resolve(args.pool)
    maoz_out = resolve(args.maoz_out)
    screen = read_csv(resolve(args.screen) / SCREEN_CSV)
    members = read_csv(resolve(args.frontier) / FRONTIER_CSV)
    frontier_status = {(r["tool"], r["spec"]): r["status"] for r in
                       read_csv(resolve(args.frontier) / FRONTIER_SUMMARY_CSV)}
    relations = read_csv(resolve(args.coverage) / RELATIONS_CSV)
    with open(pool / POOL_TSV, newline="") as handle:
        pool_rows = list(csv.DictReader(handle, delimiter="\t"))
    with open(pool / RUNS_TSV, newline="") as handle:
        run_rows = list(csv.DictReader(handle, delimiter="\t"))
    rel = {(r["tool"], r["spec"], r["tool_file"], r["peredur_md5"]):
           r["relation"] for r in relations}
    runs: dict = {}
    for r in run_rows:
        runs.setdefault(r["spec"], {})[r["run"]] = set()
    for r in pool_rows:
        runs[r["spec"]][r["run"]].add(r["md5"])
    original = {spec: guarantees_of(REPO_ROOT / "examples" / spec /
                                    "spec.tlsf") for spec in args.specs}
    changed = {}
    for spec in args.specs:
        for md5 in set().union(*runs.get(spec, {}).values()):
            changed[(spec, md5)] = (
                guarantees_of(pool / spec / f"{md5}.tlsf") != original[spec])
    adapt = {}
    adapt_path = results / "maoz-adapt.json"
    if adapt_path.exists():
        for rec in json.loads(adapt_path.read_text()):
            adapt[(rec["algorithm"], rec["spec"])] = rec

    rows = []
    for tool in args.tools:
        for spec in args.specs:
            scr = [r for r in screen if r["tool"] == tool
                   and r["spec"] == spec]
            reps = sorted(m["file"] for m in members
                          if m["tool"] == tool and m["spec"] == spec
                          and m["representative"] == "1")
            spec_runs = runs.get(spec, {})
            pool_md5 = sorted(set().union(*spec_runs.values())) \
                if spec_runs else []
            row = {"tool": tool, "spec": spec,
                   "n_repairs": len(scr),
                   "n_realizable": sum(r["realizable"] == "yes" for r in scr),
                   "n_unrealizable": sum(r["realizable"] == "no"
                                         for r in scr),
                   "n_realize_undecided": sum(r["realizable"] == "undecided"
                                              for r in scr),
                   "n_wellsep_no": sum(r["well_separated"] == "no"
                                       for r in scr),
                   "n_wellsep_undecided": sum(r["well_separated"] ==
                                              "undecided" for r in scr),
                   "n_admissible": sum(r["admissible"] == "yes"
                                       for r in scr),
                   "n_admissible_undecided": sum(r["admissible"] ==
                                                 "undecided" for r in scr),
                   "frontier_status": frontier_status.get((tool, spec),
                                                          "missing"),
                   "n_frontier": len(reps), "n_pool": len(pool_md5),
                   "n_runs": len(spec_runs)}
            pairs = [(t, p) for t in reps for p in pool_md5]
            got = {pair: rel.get((tool, spec, pair[0], pair[1]))
                   for pair in pairs}
            n_missing = sum(v is None for v in got.values())
            n_undecided = sum(v == "undecided" for v in got.values())
            row.update({"n_pairs": len(pairs), "n_pairs_missing": n_missing,
                        "undecided_share": share(n_undecided, len(pairs))})
            for reading, generous in (("strict", False), ("lenient", True)):
                def covers(t, p, which):
                    value = got[(t, p)]
                    if value in (None, "undecided"):
                        return generous
                    return value in (("equivalent", "stronger")
                                     if which == "peredur"
                                     else ("equivalent", "weaker"))
                covered_by_pool = [t for t in reps
                                   if any(covers(t, p, "peredur")
                                          for p in pool_md5)]
                per_run = [share(sum(any(covers(t, p, "peredur")
                                         for p in frontier) for t in reps),
                                 len(reps))
                           for frontier in spec_runs.values()]
                per_run = [x for x in per_run if x != ""]
                converse, weakening = [], set()
                for frontier in spec_runs.values():
                    if not frontier:
                        continue
                    uncovered = [p for p in frontier
                                 if not any(covers(t, p, "tool")
                                            for t in reps)]
                    converse.append(len(uncovered) / len(frontier))
                    weakening |= {p for p in uncovered
                                  if changed[(spec, p)]}
                row[f"coverage_per_run_{reading}"] = (
                    round(sum(per_run) / len(per_run), 4) if per_run else "")
                row[f"coverage_by_pool_{reading}"] = share(
                    len(covered_by_pool), len(reps))
                row[f"uncovered_tool_{reading}"] = " ".join(
                    t for t in reps if t not in covered_by_pool)
                row[f"converse_per_run_{reading}"] = (
                    round(sum(converse) / len(converse), 4)
                    if converse and reps else "")
                row[f"converse_guarantee_weakening_{reading}"] = (
                    len(weakening) if reps else "")
            row["outcome"], row["reason"] = verdict(row, tool, spec,
                                                    maoz_out, adapt)
            rows.append(row)
    fields = list(rows[0].keys()) if rows else []
    write_csv(out / REPORT_CSV, fields, rows)
    text = report_text(rows)
    (out / REPORT_TXT).write_text(text)
    print(text)
    manifest.data["counts"]["planned"] = len(rows)
    manifest.finish(done=len(rows))
    return 0


def verdict(row: dict, tool: str, spec: str, maoz_out: Path,
            adapt: dict) -> tuple[str, str]:
    """Section 11.5, per (tool, subject). Outcome 1 and covered are stated
    only where both readings of an undecided pair agree."""
    if row["frontier_status"] not in ("ok", "single", "empty"):
        return "incomplete", f"frontier {row['frontier_status']}"
    if row["n_frontier"] == 0:
        crash = crash_reason(maoz_out, tool, spec)
        if crash is not None:
            return "3", crash
        rec = adapt.get((TOOLS[tool], spec)) or {}
        if row["n_repairs"] == 0:
            if rec.get("n_untranslatable"):
                return "3", "untranslatable"
            return "3", "no repair found"
        parts = []
        if row["n_unrealizable"]:
            parts.append(f"unrealisable {row['n_unrealizable']}")
        if row["n_wellsep_no"]:
            parts.append(f"not well-separated {row['n_wellsep_no']}")
        if row["n_admissible_undecided"]:
            parts.append(f"undecided {row['n_admissible_undecided']}")
        return "3", ", ".join(parts) or "no admissible repair"
    if row["n_pairs_missing"]:
        return "incomplete", f"{row['n_pairs_missing']} pairs not compared"
    if row["coverage_by_pool_lenient"] != "" and \
            row["coverage_by_pool_lenient"] < 1:
        return "1", "covered by no run: " + row["uncovered_tool_lenient"]
    if row["coverage_by_pool_strict"] == 1:
        return "covered", "every frontier repair covered by the pool"
    return "undetermined", ("covered only if undecided pairs cover: "
                            + row["uncovered_tool_strict"])


def report_text(rows: list[dict]) -> str:
    lines = ["Maoz baselines against PEREDUR: coverage by implication",
             "(PLAN.md section 11 of 2026-10-02-maoz-baselines; strict reads "
             "an undecided pair as non-coverage, lenient as coverage)", ""]
    head = (f"{'tool':<12}{'spec':<14}{'adm':>5}{'front':>6}{'pool':>6}"
            f"{'cov/run':>16}{'cov/pool':>16}{'converse':>16}{'undec':>7}"
            f"  outcome")
    lines.append(head)

    def pair(row, key):
        a, b = row[f"{key}_strict"], row[f"{key}_lenient"]
        return f"{a if a != '' else '-'}/{b if b != '' else '-'}"

    for r in rows:
        lines.append(
            f"{r['tool']:<12}{r['spec']:<14}{r['n_admissible']:>5}"
            f"{r['n_frontier']:>6}{r['n_pool']:>6}"
            f"{pair(r, 'coverage_per_run'):>16}"
            f"{pair(r, 'coverage_by_pool'):>16}"
            f"{pair(r, 'converse_per_run'):>16}"
            f"{str(r['undecided_share'] or '-'):>7}  {r['outcome']}")
    lines.append("")
    ones = [r for r in rows if r["outcome"] == "1"]
    threes = [r for r in rows if r["outcome"] == "3"]
    open_ = [r for r in rows if r["outcome"] in ("undetermined",
                                                 "incomplete")]
    if ones:
        lines.append("Outcome 1: tool frontier repairs no PEREDUR run covers")
        lines += [f"  {r['tool']}/{r['spec']}: {r['reason']}" for r in ones]
    elif not open_:
        lines.append("Outcome 2: the pool covers every tool frontier repair "
                     "under both readings")
    else:
        lines.append("No Outcome 1 under the lenient reading, but Outcome 2 "
                     "is not stated: some (tool, subject) depends on "
                     "undecided or missing pairs")
    for r in open_:
        lines.append(f"  {r['tool']}/{r['spec']}: {r['outcome']}, "
                     f"{r['reason']}")
    if threes:
        lines.append("Outcome 3: no admissible repair")
        lines += [f"  {r['tool']}/{r['spec']}: {r['reason']}" for r in threes]
    weak = [(r["tool"], r["spec"], r["converse_guarantee_weakening_strict"],
             r["converse_guarantee_weakening_lenient"]) for r in rows
            if r["n_frontier"]]
    lines.append("")
    lines.append("Uncovered PEREDUR frontier repairs that change GUARANTEES "
                 "(distinct, strict/lenient):")
    lines += [f"  {t}/{s}: {a}/{b}" for t, s, a, b in weak]
    return "\n".join(lines) + "\n"


# -- driver ---------------------------------------------------------------


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pass", dest="pass_name", required=True,
                        choices=PASSES)
    parser.add_argument("--out", required=True, metavar="DIR")
    parser.add_argument("--results", default=INPUT_DEFAULTS["results"],
                        metavar="DIR", help="the tool repairs maoz_adapt.py "
                        "wrote (default: %(default)s)")
    parser.add_argument("--pool", default=INPUT_DEFAULTS["pool"],
                        metavar="DIR", help="the PEREDUR pool the pool pass "
                        "wrote (default: %(default)s)")
    parser.add_argument("--screen", metavar="DIR",
                        help="the screen pass's out directory")
    parser.add_argument("--frontier", metavar="DIR",
                        help="the frontier pass's out directory")
    parser.add_argument("--coverage", metavar="DIR",
                        help="the coverage pass's out directory")
    parser.add_argument("--maoz-out", default=INPUT_DEFAULTS["maoz_out"],
                        metavar="DIR", help="the maoz phase's run tree, for "
                        "Outcome 3 reasons (default: %(default)s)")
    parser.add_argument("--curves", default=str(DEFAULT_CURVES),
                        metavar="DIR", help="pool: the paper re-run's curves "
                        "directory, holding av2/ and av3/")
    parser.add_argument("--rerun", default=str(DEFAULT_RERUN), metavar="DIR",
                        help="pool: the paper re-run's results directory")
    parser.add_argument("--specs", nargs="+", default=list(SUBJECTS),
                        choices=SUBJECTS, metavar="SPEC")
    parser.add_argument("--tools", nargs="+", default=list(TOOLS),
                        choices=list(TOOLS), metavar="TOOL")
    parser.add_argument("--seeds", nargs="+", type=int, default=[0],
                        metavar="N", help="accepted from campaign.py; the "
                        "tools are deterministic, so only seed 0")
    for key, value in DEFAULTS.items():
        parser.add_argument(f"--{key.replace('_', '-')}", type=int,
                            default=value, metavar="N",
                            help=f"(default: {value})")
    parser.add_argument("--allow-stale-binary", action="store_true")
    args = parser.parse_args(argv)
    if args.seeds != [0]:
        parser.error("the tools are deterministic: declare seed 0 only")
    if min(getattr(args, k) for k in DEFAULTS) < 1:
        parser.error("every budget must be a positive integer")
    need = {"frontier": ("screen",), "coverage": ("frontier",),
            "report": ("screen", "frontier", "coverage")}
    for key in need.get(args.pass_name, ()):
        if getattr(args, key) is None:
            parser.error(f"--pass {args.pass_name} needs --{key}")
    return args


def main(argv=None) -> int:
    args = parse_args(argv)
    out = resolve(args.out)
    out.mkdir(parents=True, exist_ok=True)
    versions = read_versions(args.pass_name)
    enforce_freshness(versions, R.working_tree_head(),
                      args.allow_stale_binary)
    budgets = {k: getattr(args, k) for k in DEFAULTS}
    manifest = Manifest(args, out, versions, budgets)
    manifest.write()
    handler = {"pool": pass_pool, "screen": pass_screen,
               "frontier": pass_frontier, "coverage": pass_coverage,
               "report": pass_report}[args.pass_name]
    return handler(args, out, manifest)


if __name__ == "__main__":
    sys.exit(main())
