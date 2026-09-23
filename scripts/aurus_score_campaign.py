#!/usr/bin/env python3
"""Drive one AuRUS scoring pass on one host, over that host's seeds.

    python3 scripts/aurus_score_campaign.py --pass anytime \\
        --results experiments/aurus-rerun-out \\
        --out experiments/anytime-aurus-rerun --seeds 0 1 2 [--jobs 4] \\
        [--compare-timeout 3600] [--allow-stale-binary] [--dry-run]

    python3 scripts/aurus_score_campaign.py --pass wellsep \\
        --results experiments/results-aurus-rerun \\
        --out experiments/wellsep-aurus-rerun --seeds 0 1 2 [--jobs 4] \\
        [--ltlsynt-timeout 60] [--pattern '*.tlsf'] [--fast-path off]

What a `kind = "aurus-score"` phase in campaign.toml runs, and the twin of
score_campaign.py for the two passes that read an AuRUS tree rather than a
PEREDUR run:

  anytime   scripts/score_aurus_anytime.py -- one `compare` call per repeat,
            dating each solution from the run's own iteration series, one row
            per (spec, repeat, index).
  wellsep   scripts/check_well_separated.py -- one `ltlsynt` call per
            candidate, deciding well-separation from the TLSF text and never
            through peredur (see that file for why the in-run verdict is not
            usable here).

A sibling rather than a third stage inside score_campaign.py. That file is the
driver for one scorer: its vocabulary -- cuts, an antichain deadline, a
per-worker block of cores under `taskset`, the three sidecars renamed with the
curve -- is score_curves.py's, and every function in it would have to branch on
a pass name that shares none of it. These two passes have their own units of
work, their own binaries and their own budgets, and bound their own solver
calls, so they need no outer wall cap at all. What is shared is the shape, not
the code: a seed split enforced here, a smallest-first queue, a resume, a
manifest recording every budget and the binaries that decided the rows, a
failures list and the runner's own freshness gate.

The two passes read differently shaped trees, and the shape is checked rather
than assumed:

  anytime   the raw AuRUS tree, `<results>/<spec>/repeat-<NN>`, as
            aurus_campaign.py writes it. It has to be the raw one: a solution
            is dated from the iteration series in that repeat's run.log, and
            aurus_adapt.py's PEREDUR-shaped tree carries the dates it derived
            but not the log they came from.
  wellsep   a PEREDUR-shaped results directory, `<results>/<run>_seed<NN>`,
            as aurus_adapt.py leaves it, whose candidates are read out of
            `accumulated/`.

A repeat stands where a seed does, AuRUS being unseedable, so both shapes
split on the number in the directory name and two hosts sharing a tree never
score one repeat twice.

Stdlib only, on python 3.10: this runs on the lab hosts from a cron tick.
"""

import argparse
import json
import os
import platform
import queue
import re
import shlex
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import run_experiments as R  # noqa: E402

REPO_ROOT = Path(__file__).parent.parent

# The binaries the passes run. PEREDUR_BIN_DIR moves them together, for a
# worktree pointed at another checkout's build, and COMPARE_BIN is the same
# override score_campaign.py and score_curves.py honour, so the gate reads the
# binary the children will actually run.
BIN_DIR = Path(os.environ.get("PEREDUR_BIN_DIR", REPO_ROOT / "build-release"))
COMPARE_BIN = Path(os.environ.get("COMPARE_BIN", BIN_DIR / "compare"))
# ltlsynt comes out of the fetched Spot tree, which is where
# check_well_separated.py looks for it by default; named here so the manifest
# records the one this host ran.
LTLSYNT_BIN = Path(os.environ.get(
    "LTLSYNT_BIN", BIN_DIR / "third_party" / "spot" / "bin" / "ltlsynt"))
EXAMPLES_DIR = Path(os.environ.get("PEREDUR_EXAMPLES_DIR",
                                   REPO_ROOT / "examples"))

# The two scorers, as command lines rather than imports: one subprocess per
# unit of work is what a failure, a timing line and a resume attach to. The
# overrides point them at stubs so the pool can be tested without a solver.
ANYTIME_CMD = os.environ.get("PEREDUR_ANYTIME_CMD",
                             "python3 scripts/score_aurus_anytime.py")
WELLSEP_CMD = os.environ.get("PEREDUR_WELLSEP_CMD",
                             "python3 scripts/check_well_separated.py")

PASSES = ("anytime", "wellsep")

# The values an aurus-score phase takes when campaign.toml omits a key.
# campaign.py reads them from here, so the declaration and the CLI cannot
# disagree.
DEFAULTS = {
    "jobs": 4,
    # anytime: the budget for one repeat's `compare` call, which is quadratic
    # in that repeat's solutions times the family's ideals.
    "compare_timeout": 3600,
    # wellsep: the budget for one candidate's ltlsynt call. A timeout is
    # `undecided`, never `well-separated`, so raising this buys verdicts and
    # lowering it buys none of the opposite kind.
    "ltlsynt_timeout": 60,
    # check_well_separated.py's own default is `repair_*.tlsf`, which is what
    # peredur names a repair. An adapted AuRUS run holds spec<i>.tlsf, so the
    # default here matches every candidate instead of none of them -- a
    # pattern matching nothing exits 2 per unit and drains the queue into
    # failures.txt.
    "pattern": "*.tlsf",
    # Off, as check_well_separated.py has it: on, a spec whose assumptions
    # reference no output short-circuits to well-separated without asking
    # ltlsynt, and the whole point of that script is that every verdict is
    # ltlsynt's.
    "fast_path": "off",
}
# Per pass, the budgets that reach its command line. A key belonging to the
# other pass is refused by campaign.py by name rather than carried along
# unread.
PASS_BUDGETS = {
    "anytime": ("compare_timeout",),
    "wellsep": ("ltlsynt_timeout", "pattern", "fast_path"),
}
CHOICE_BUDGETS = {"fast_path": ("on", "off")}
STRING_BUDGETS = ("pattern",)

MANIFEST_STEM = "aurus-score-manifest"
TIMINGS_NAME = "timings.txt"
FAILURES_NAME = "failures.txt"
WARNINGS_NAME = "warnings.log"

REPEAT_DIR = re.compile(r"^repeat-(\d+)$")
SEED_SUFFIX = re.compile(r"_seed(\d+)$")
SOLUTION_FILE = re.compile(r"^spec\d+\.tlsf$")
INDEX_PATH = Path("accumulated") / "index.tsv"

# check_well_separated.py exits 1 when any candidate came back `undecided`.
# That is a verdict the CSV carries, not a failed unit: an ltlsynt timeout is
# the answer the script exists to record honestly. Its 2 -- no input files, no
# ltlsynt -- is a failure.
WELLSEP_OK_RC = (0, 1)
ANYTIME_OK_RC = (0,)


def ok_returncodes(pass_name: str) -> tuple:
    return WELLSEP_OK_RC if pass_name == "wellsep" else ANYTIME_OK_RC


# -- the queue ----------------------------------------------------------------


class Unit:
    """One repeat: what is scored, what it is called, and how big it is.

    `name` is the output file's stem and the key the resume reads, so it has
    to be unique across the tree and stable between passes over it.
    """

    def __init__(self, name: str, path: Path, seed: int, size: int):
        self.name = name
        self.path = path
        self.seed = seed
        self.size = size


def count_solutions(repeat_dir: Path) -> int:
    """Solution files in a raw AuRUS repeat directory; 0 where unreadable."""
    try:
        return sum(1 for entry in os.listdir(repeat_dir)
                   if SOLUTION_FILE.match(entry))
    except OSError:
        return 0


def index_lines(run_dir: Path) -> int:
    """Lines in accumulated/index.tsv, header included; 0 where absent."""
    try:
        with open(run_dir / INDEX_PATH, "rb") as handle:
            return sum(1 for _ in handle)
    except OSError:
        return 0


def number_in(pattern, name: str):
    match = pattern.search(name)
    return int(match.group(1)) if match else None


def anytime_units(results: Path, wanted: set) -> list:
    """`<results>/<spec>/repeat-<NN>` for this host's repeats."""
    found = []
    for spec_dir in sorted(p for p in results.iterdir() if p.is_dir()):
        for repeat in sorted(p for p in spec_dir.iterdir() if p.is_dir()):
            seed = number_in(REPEAT_DIR, repeat.name)
            if seed is None or seed not in wanted:
                continue
            found.append(Unit(f"{spec_dir.name}_{repeat.name}", repeat, seed,
                              count_solutions(repeat)))
    return found


def wellsep_units(results: Path, wanted: set) -> list:
    """`<results>/<run>_seed<NN>` for this host's seeds.

    Directly under the results directory and named the way the runner names a
    run, which is also what aurus_adapt.py emits. Anything else -- a stray
    file, an `accumulated` directory -- carries no seed suffix and is left
    alone.
    """
    found = []
    for entry in sorted(p for p in results.iterdir() if p.is_dir()):
        seed = number_in(SEED_SUFFIX, entry.name)
        if seed is None or seed not in wanted:
            continue
        found.append(Unit(entry.name, entry, seed, index_lines(entry)))
    return found


def queue_units(results: Path, seeds, pass_name: str) -> list:
    """The units to score, smallest first.

    Smallest first so the heavy families land last, where a budget can still
    be raised for them before the queue reaches them; ties break on the name,
    so the order is a function of the tree alone.
    """
    wanted = set(seeds)
    units = (anytime_units(results, wanted) if pass_name == "anytime"
             else wellsep_units(results, wanted))
    units.sort(key=lambda unit: (unit.size, unit.name))
    return units


def wrong_shape(results: Path, pass_name: str) -> str | None:
    """Why this tree cannot be the one this pass reads, or None.

    Checked rather than assumed, and before the gate, because the two trees
    sit beside each other under experiments/ and a phase pointed at the wrong
    one has no unit of work at all -- which is indistinguishable from a host
    that has not run its search yet.
    """
    raw = any(REPEAT_DIR.match(child.name)
              for entry in results.iterdir() if entry.is_dir()
              for child in entry.iterdir() if child.is_dir())
    adapted = any(SEED_SUFFIX.search(entry.name)
                  for entry in results.iterdir() if entry.is_dir())
    if pass_name == "anytime" and not raw:
        return (f"{results} holds no <spec>/repeat-<NN> directory. The "
                f"anytime pass reads the raw AuRUS tree, not the adapted "
                f"one: a solution is dated from that repeat's run.log, which "
                f"aurus_adapt.py does not copy")
    if pass_name == "wellsep" and not adapted and raw:
        return (f"{results} is a raw AuRUS tree. The wellsep pass reads the "
                f"PEREDUR-shaped tree aurus_adapt.py writes, whose "
                f"candidates sit under <run>_seed<NN>/accumulated")
    return None


def out_path(out: Path, unit: Unit) -> Path:
    return out / f"{unit.name}.csv"


def is_scored(path: Path) -> bool:
    """An output is present once its CSV exists and holds something.

    A zero-length file is what an interrupted move leaves, and counting it
    would make the resume skip a unit that has no rows.
    """
    try:
        return path.stat().st_size > 0
    except OSError:
        return False


# -- the binaries -------------------------------------------------------------


def read_versions(args) -> dict:
    """The PEREDUR binaries this pass runs, and no others.

    The wellsep pass runs none: ltlsynt comes out of the fetched Spot tree and
    carries no PEREDUR commit, so there is nothing for the gate to compare
    against. Its path and version string go in the manifest instead, which is
    the whole of what can be said about it.
    """
    if args.which_pass == "anytime":
        return {"compare": R.binary_version(COMPARE_BIN)}
    return {}


def ltlsynt_version() -> dict:
    """ltlsynt's own first line of `--version`, or why there is none."""
    try:
        proc = subprocess.run([str(LTLSYNT_BIN), "--version"],
                              capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError) as exc:
        return {"path": str(LTLSYNT_BIN), "version": f"unavailable: {exc}"}
    line = (proc.stdout or proc.stderr).splitlines()
    return {"path": str(LTLSYNT_BIN),
            "version": line[0].strip() if line else "?"}


def enforce_freshness(versions: dict, head, allow_stale: bool) -> None:
    """The runner's own gate, over the binaries this pass runs.

    Rows scored by a compare built from another commit name that commit
    nowhere; the manifest is the only record, so the gate is what keeps the
    manifest honest.
    """
    problems = R.staleness_problems(versions, head)
    if not problems:
        return
    bar = "!" * 72
    print(f"\n{bar}")
    print("STALE BINARY: the scoring binaries do not match this working tree.")
    for problem in problems:
        print(f"  - {problem}")
    print("Rebuild (cmake --build build-release) or pass --allow-stale-binary "
          "if the mismatch is deliberate.")
    print(f"{bar}\n")
    if not allow_stale:
        sys.exit("Refusing to score with a stale binary.")
    print("Continuing anyway (--allow-stale-binary).\n")


# -- the scorers --------------------------------------------------------------


def scorer_args(args, out_file: str, target: str) -> list:
    """The command line for one unit, budgets stated rather than defaulted.

    Both scorers get `--jobs 1`: `--jobs` on this side is the number of units
    in flight, so a scorer parallelising inside one of them would multiply the
    two. One knob, and what the manifest records is what ran.
    """
    if args.which_pass == "anytime":
        return [*shlex.split(ANYTIME_CMD),
                "--binary", str(COMPARE_BIN),
                "--examples", str(EXAMPLES_DIR),
                "--timeout", str(args.compare_timeout),
                "--jobs", "1",
                "--out", out_file,
                "--dirs", target]
    command = [*shlex.split(WELLSEP_CMD),
               "--ltlsynt", str(LTLSYNT_BIN),
               "--pattern", args.pattern,
               "--timeout", str(args.ltlsynt_timeout),
               "--jobs", "1",
               "--out", out_file]
    if args.fast_path == "on":
        command.append("--fast-path")
    return command + [target]


def target_of(args, unit: Unit) -> Path:
    """What the scorer is pointed at.

    The wellsep scorer walks a directory for `--pattern`, so it is given the
    candidates rather than the run: a run directory also holds run.json, and
    an adapted tree that ever grew a second .tlsf beside it would quietly add
    a row that is not a candidate.
    """
    if args.which_pass == "wellsep":
        accumulated = unit.path / "accumulated"
        if accumulated.is_dir():
            return accumulated
    return unit.path


def invocation_template(args) -> str:
    """The command line each unit gets, with the two paths left as slots.

    Recorded in the manifest so an archive's pass block is copied rather than
    reconstructed from memory.
    """
    return " ".join(scorer_args(args, "<out>/<unit>.csv.part", "<unit-dir>"))


class Ledger:
    """The per-unit records, appended under one lock.

    Several workers append to each file at once; the lock keeps a line whole,
    which O_APPEND alone does not promise for a line written in two calls.
    """

    def __init__(self, out: Path):
        self.out = out
        self.lock = threading.Lock()
        self.scored = 0
        self.failed = 0

    def append(self, name: str, line: str) -> None:
        with self.lock:
            with open(self.out / name, "a") as handle:
                handle.write(line + "\n")


def budget_note(args) -> str:
    """The budgets one timings line was produced under."""
    if args.which_pass == "anytime":
        return f"compare_timeout={args.compare_timeout}"
    return (f"ltlsynt_timeout={args.ltlsynt_timeout} "
            f"pattern={args.pattern} fast_path={args.fast_path}")


def score_one(unit: Unit, args, out: Path, ledger: Ledger, index: int,
              total: int, child_env: dict) -> int:
    """One unit: score it, move the CSV into place or record why not.

    The CSV is written to `<unit>.csv.part` and renamed only on an accepted
    exit with a non-empty file, so a reader listing `*.csv` never sees an
    output that is still being written or one a killed scorer left half done.
    """
    final = out_path(out, unit)
    part = final.with_suffix(".csv.part")
    # Removed first: score_aurus_anytime.py appends to --out and writes its
    # header only for a file that is not there, so a .part left by a killed
    # attempt would be continued rather than replaced.
    part.unlink(missing_ok=True)
    command = scorer_args(args, str(part), str(target_of(args, unit)))
    start = time.time()
    with open(out / WARNINGS_NAME, "a") as log:
        log.write(f"=== {time.strftime('%Y-%m-%dT%H:%M:%S')} {unit.name}\n")
        log.flush()
        try:
            proc = subprocess.run(command, cwd=str(REPO_ROOT), stdout=log,
                                  stderr=subprocess.STDOUT, env=child_env)
            rc = proc.returncode
        except OSError as exc:
            log.write(f"cannot run {command[0]}: {exc}\n")
            rc = 127
    elapsed = int(time.time() - start)
    ledger.append(TIMINGS_NAME, f"{unit.size} {elapsed} {rc} {unit.name} "
                                f"{budget_note(args)}")
    if rc in ok_returncodes(args.which_pass) and is_scored(part):
        os.replace(part, final)
        with ledger.lock:
            ledger.scored += 1
        print(f"[{index}/{total}] {unit.name}: scored in {elapsed}s")
    else:
        part.unlink(missing_ok=True)
        ledger.append(FAILURES_NAME, f"{rc} {unit.path}")
        with ledger.lock:
            ledger.failed += 1
        print(f"[{index}/{total}] {unit.name}: FAILED rc={rc} "
              f"after {elapsed}s")
    sys.stdout.flush()
    return rc


def run_pool(units: list, args, out: Path, ledger: Ledger,
             child_env: dict) -> None:
    """`--jobs` threads over one shared queue.

    Threads rather than processes: every worker spends its life blocked in a
    subprocess wait, so the interpreter lock costs nothing, and one process
    keeps the ledger's lock an ordinary one.
    """
    pending: queue.Queue = queue.Queue()
    for index, unit in enumerate(units, 1):
        pending.put((index, unit))
    total = len(units)

    def worker() -> None:
        while True:
            try:
                index, unit = pending.get_nowait()
            except queue.Empty:
                return
            score_one(unit, args, out, ledger, index, total, child_env)

    threads = [threading.Thread(target=worker, daemon=True)
               for _ in range(max(1, min(args.jobs, total)))]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()


# -- the manifest -------------------------------------------------------------


def host_name() -> str:
    return socket.gethostname().split(".")[0]


def manifest_path(out: Path) -> Path:
    return out / f"{MANIFEST_STEM}-{host_name()}.json"


def budgets_of(args) -> dict:
    return {key: getattr(args, key) for key in PASS_BUDGETS[args.which_pass]}


def build_manifest(args, results: Path, out: Path, versions: dict, head,
                   counts: dict, started: str) -> dict:
    """Everything an archive's scoring block is assembled from."""
    binaries = {"compare": {"path": str(COMPARE_BIN), **versions["compare"]}
                } if "compare" in versions else {}
    if args.which_pass == "wellsep":
        binaries["ltlsynt"] = ltlsynt_version()
    return {
        "kind": "aurus-score",
        "pass": args.which_pass,
        "hostname": socket.gethostname(),
        "started": started,
        "finished": None,
        "results": str(args.results),
        "results_resolved": str(results),
        "out": str(args.out),
        "out_resolved": str(out),
        "seeds": list(args.seeds),
        "jobs": args.jobs,
        "budgets": budgets_of(args),
        **budgets_of(args),
        "invocation": invocation_template(args),
        "binaries": binaries,
        "git": {"branch": R.git_branch(), "head": head or R.LEGACY_COMMIT},
        "allow_stale_binary": bool(args.allow_stale_binary),
        "python_version": platform.python_version(),
        "counts": dict(counts),
        "files": {
            "rows": "<unit>.csv, one per repeat scored",
            "timings": f"{TIMINGS_NAME}: size elapsed_s rc unit budgets, one "
                       f"line per attempt",
            "failures": f"{FAILURES_NAME}: rc unit-dir, one line per failed "
                        f"attempt",
            "warnings": f"{WARNINGS_NAME}: every scorer's stdout and stderr",
        },
    }


def write_manifest(path: Path, manifest: dict) -> None:
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(manifest, indent=2) + "\n")
    os.replace(tmp, path)


# -- the command line ---------------------------------------------------------


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pass", dest="which_pass", required=True,
                        choices=PASSES,
                        help="anytime: date and grade every AuRUS solution; "
                             "wellsep: decide well-separation per candidate")
    parser.add_argument("--results", required=True, metavar="DIR",
                        help="the tree to read; relative to the checkout root")
    parser.add_argument("--out", required=True, metavar="DIR",
                        help="where the per-unit CSVs go; relative to the "
                             "checkout root")
    parser.add_argument("--seeds", required=True, nargs="+", type=int,
                        help="this host's seeds; a repeat whose number is not "
                             "listed is not scored here")
    parser.add_argument("--jobs", type=int, default=DEFAULTS["jobs"],
                        help=f"units of work in flight, each scorer given "
                             f"--jobs 1 (default: {DEFAULTS['jobs']})")
    parser.add_argument("--compare-timeout", type=int,
                        default=DEFAULTS["compare_timeout"],
                        help=f"anytime: seconds for one repeat's compare call "
                             f"(default: {DEFAULTS['compare_timeout']})")
    parser.add_argument("--ltlsynt-timeout", type=int,
                        default=DEFAULTS["ltlsynt_timeout"],
                        help=f"wellsep: seconds for one candidate's ltlsynt "
                             f"call; a timeout is undecided, never "
                             f"well-separated (default: "
                             f"{DEFAULTS['ltlsynt_timeout']})")
    parser.add_argument("--pattern", default=DEFAULTS["pattern"],
                        help=f"wellsep: glob for the candidate files "
                             f"(default: {DEFAULTS['pattern']})")
    parser.add_argument("--fast-path", choices=("on", "off"),
                        default=DEFAULTS["fast_path"],
                        help=f"wellsep: short-circuit specs whose assumptions "
                             f"reference no output, as the C++ filter does "
                             f"(default: {DEFAULTS['fast_path']})")
    parser.add_argument("--allow-stale-binary", action="store_true",
                        help="score with compare built from another commit or "
                             "a dirty tree; recorded in the manifest")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the queue and the command; score nothing")
    args = parser.parse_args(argv)
    for name in ("jobs", "compare_timeout", "ltlsynt_timeout"):
        if getattr(args, name) < 1:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if not args.pattern:
        parser.error("--pattern must be non-empty")
    return args


def resolve(path_text: str) -> Path:
    path = Path(path_text)
    return path if path.is_absolute() else REPO_ROOT / path


def main(argv=None) -> int:
    args = parse_args(argv)
    results = resolve(args.results)
    out = resolve(args.out)
    if not results.is_dir():
        print(f"no results directory at {results}", file=sys.stderr)
        return 2
    # Refused before the queue is built: oversubscribing a host does not make
    # a slow pass, it makes every solver call contend with the others and the
    # budgets stop meaning what the manifest says they mean.
    cpus = os.cpu_count()
    if cpus is not None and args.jobs > cpus:
        print(f"--jobs {args.jobs} on a host with {cpus} CPU(s)",
              file=sys.stderr)
        return 2
    shape = wrong_shape(results, args.which_pass)
    if shape is not None:
        print(shape, file=sys.stderr)
        return 2

    units = queue_units(results, args.seeds, args.which_pass)
    if not units:
        # An empty queue is never a finished pass. Exiting 0 here would let a
        # tick mark the phase done over nothing, and status read 0 of 0.
        print(f"nothing to score: no repeat under {results} carries a number "
              f"in {' '.join(map(str, args.seeds))}", file=sys.stderr)
        return 2
    already = [u for u in units if is_scored(out_path(out, u))]
    todo = [u for u in units if not is_scored(out_path(out, u))]
    versions = read_versions(args)
    head = R.working_tree_head()

    print(f"Scoring {results} ({args.which_pass})")
    print(f"    into:     {out}")
    print(f"    seeds:    {len(args.seeds)} "
          f"({min(args.seeds)}-{max(args.seeds)})")
    print(f"    queue:    {len(units)} unit(s), {len(already)} already "
          f"scored, {len(todo)} to score")
    print(f"    jobs:     {args.jobs}")
    print(f"    budgets:  {budget_note(args)}")
    for name in sorted(versions):
        version = versions[name]
        suffix = "-dirty" if version.get("dirty") == "1" else ""
        print(f"    binary:   {name} {version['commit_short']}{suffix}")
    if args.which_pass == "wellsep":
        print(f"    binary:   {LTLSYNT_BIN} (third party, not gated)")
    print(f"    command:  {invocation_template(args)}")

    if args.dry_run:
        names = [unit.name for unit in todo]
        if len(names) > 6:
            names = names[:3] + ["..."] + names[-3:]
        for name in names:
            print(f"    {name}")
        print("\nDry run: nothing scored, nothing written.")
        return 0

    enforce_freshness(versions, head, args.allow_stale_binary)
    out.mkdir(parents=True, exist_ok=True)
    for name in (TIMINGS_NAME, FAILURES_NAME, WARNINGS_NAME):
        (out / name).touch()
    counts = {"queued": len(units), "already_scored": len(already),
              "scored": 0, "failed": 0}
    started = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    manifest = build_manifest(args, results, out, versions, head, counts,
                              started)
    write_manifest(manifest_path(out), manifest)

    child_env = dict(os.environ)
    child_env.setdefault("COMPARE_BIN", str(COMPARE_BIN))
    ledger = Ledger(out)
    run_pool(todo, args, out, ledger, child_env)

    counts.update({"scored": ledger.scored, "failed": ledger.failed})
    manifest["counts"] = dict(counts)
    manifest["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    write_manifest(manifest_path(out), manifest)
    missing = [u for u in units if not is_scored(out_path(out, u))]
    print(f"\nDone: {counts['scored']} scored, {counts['failed']} failed, "
          f"{counts['already_scored']} already there; "
          f"{len(units) - len(missing)}/{len(units)} present.")
    if missing:
        print(f"{len(missing)} unit(s) have no rows; see "
              f"{out / FAILURES_NAME}. A rerun scores only those.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
