#!/usr/bin/env python3
"""Score a campaign's maximality curves on one host, over that host's seeds.

    python3 scripts/score_campaign.py --results experiments/results-rematch \\
        --out experiments/curves-rematch --seeds 0 1 2 3 [--workers 8] \\
        [--cores 4] [--cuts 20] [--maximal-timeout 900] \\
        [--compare-timeout 600] [--deadline-s 4500] [--wall-cap-s 5400] \\
        [--allow-stale-binary] [--dry-run]

The scoring twin of run_experiments.py, and what a `kind = "score"` phase in
campaign.toml runs. The search writes one run directory per (spec, seed) under
a results directory; this walks those directories and runs score_curves.py
--maximality over each, one invocation per run, writing <out>/<run>.csv.

Two campaigns (2026-08-29-aurus-matched and 2026-09-04-aurus-rematch) needed
this pass, at 716 and roughly 325 worker-hours, and both times it was a pair
of hand-rolled bash scripts on the hosts, a budget file edited over ssh, and
provenance assembled by hand afterwards. This file reproduces those scripts'
mechanics -- a smallest-first queue by accumulated/index.tsv line count, N
workers each pinned to a block of cores, one score_curves.py per run under an
outer wall cap, the per-run CSV moved into place through a .part file, a
timings line per run naming the budgets it ran under, a failures list -- and
adds what they lacked: a manifest recording every budget and both scoring
binaries' commits, the same freshness gate run_experiments.py runs, and a
resume, so a requeued phase re-scores only what failed.

A host scores only the seeds it is given. The seed split is enforced here, in
the same place the runner enforces it, so two hosts sharing a results
directory layout never score one run twice.

Stdlib only, on python 3.10: this runs on the lab hosts from a cron tick.
"""

import argparse
import json
import os
import platform
import queue
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import run_experiments as R  # noqa: E402

REPO_ROOT = Path(__file__).parent.parent

# The binaries the pass runs, and the ones the freshness gate reads. The
# per-binary overrides are the ones score_curves.py itself honours, so the gate
# checks the binary the children will actually run; COUNTER_BIN_DIR moves both
# at once, for a worktree pointed at another checkout's build, and is exported
# to the children as the two per-binary overrides so they cannot diverge.
BIN_DIR = Path(os.environ.get("COUNTER_BIN_DIR", REPO_ROOT / "build-release"))
MAXIMAL_BIN = Path(os.environ.get("MAXIMAL_BIN", BIN_DIR / "maximal"))
COMPARE_BIN = Path(os.environ.get("COMPARE_BIN", BIN_DIR / "compare"))

# The scorer, as a command line rather than an import: one subprocess per run
# is what the outer wall cap and the core pinning attach to. COUNTER_SCORE_CURVES_CMD
# points it at a stub so the pool can be tested without a solver.
SCORE_CURVES_CMD = os.environ.get("COUNTER_SCORE_CURVES_CMD",
                                  "python3 scripts/score_curves.py")

# The values a score phase takes when campaign.toml omits a key. campaign.py
# reads them from here, so the declaration and the CLI cannot disagree.
DEFAULTS = {
    "workers": 8,
    "cores": 4,
    "cuts": 20,
    "maximal_timeout": 900,
    "compare_timeout": 600,
    "deadline_s": 4500,
}
# The outer `timeout` sits this far past score_curves.py's own deadline: the
# deadline stops new cuts being started, and one cut's maximal call may still
# be inside its --maximal-timeout when it lands.
WALL_CAP_MARGIN_S = 900

SEED_SUFFIX = re.compile(r"_seed(\d+)$")
INDEX_PATH = Path("accumulated") / "index.tsv"
MANIFEST_STEM = "score-manifest"
TIMINGS_NAME = "timings.txt"
FAILURES_NAME = "failures.txt"
WARNINGS_NAME = "warnings.log"
# coreutils timeout's own exit status for a command it killed, used for the
# fallback too so a timings line reads the same whichever mechanism capped it.
TIMEOUT_RC = 124


def wall_cap_default(deadline_s: int) -> int:
    return deadline_s + WALL_CAP_MARGIN_S


def index_lines(run_dir: Path) -> int:
    """Lines in accumulated/index.tsv, header included; 0 where absent."""
    try:
        with open(run_dir / INDEX_PATH, "rb") as handle:
            return sum(1 for _ in handle)
    except OSError:
        return 0


def seed_of(run_dir: Path):
    match = SEED_SUFFIX.search(run_dir.name)
    return int(match.group(1)) if match else None


def queue_runs(results: Path, seeds) -> list:
    """The run directories to score, smallest first.

    Directly under the results directory, following symlinks as `find -L`
    did; named `..._seed<N>` with N in this host's seeds, and nothing else. A
    directory that is not a run -- `accumulated`, a stray file -- carries no
    seed suffix and is left alone. Smallest first by index line count so the
    heavy families land last, where the budgets can still be raised for them
    before the queue reaches them; ties break on the name so the order is a
    function of the tree alone.
    """
    wanted = set(seeds)
    found = []
    for entry in sorted(results.iterdir()):
        if not entry.is_dir():
            continue
        seed = seed_of(entry)
        if seed is None or seed not in wanted:
            continue
        found.append((index_lines(entry), entry.name, entry))
    found.sort()
    return [entry for _, _, entry in found]


def csv_path(out: Path, run_dir: Path) -> Path:
    return out / f"{run_dir.name}.csv"


def is_scored(path: Path) -> bool:
    """A curve is present once its CSV exists and holds something.

    A zero-length file is what an interrupted move or a scorer killed before
    its header leaves, and counting it would make the resume skip a run that
    has no curve.
    """
    try:
        return path.stat().st_size > 0
    except OSError:
        return False


def read_versions() -> dict:
    return {"maximal": R.binary_version(MAXIMAL_BIN),
            "compare": R.binary_version(COMPARE_BIN)}


def enforce_freshness(versions: dict, head, allow_stale: bool) -> None:
    """The runner's own gate, over the two binaries this pass runs.

    A curve scored by a maximal built from another commit names that commit
    nowhere in its rows; the manifest is the only record, so the gate is what
    keeps the manifest honest.
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


def pin_prefix(slot: int, cores: int, pinned: bool) -> list:
    """Worker `slot` owns cores [slot*cores, slot*cores + cores - 1]."""
    if not pinned:
        return []
    lo = slot * cores
    return ["taskset", "-c", f"{lo}-{lo + cores - 1}"]


def scorer_args(args, part: str, run_dir: str) -> list:
    return [*shlex.split(SCORE_CURVES_CMD), "--maximality",
            "--cuts", str(args.cuts), "--jobs", str(args.cores),
            "--deadline-s", str(args.deadline_s),
            "--maximal-timeout", str(args.maximal_timeout),
            "--compare-timeout", str(args.compare_timeout),
            "--out", part, run_dir]


def invocation_template(args) -> str:
    """The command line each run gets, with the two paths left as slots.

    Recorded in the manifest so an archive's `maximality_pass.invocation` is
    copied rather than reconstructed from memory.
    """
    return " ".join(scorer_args(args, "<out>/<run>.csv.part", "<run-dir>"))


class Ledger:
    """The three per-run records, appended under one lock.

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


# With coreutils timeout in front the cap is its own and the Python one is a
# backstop this far behind it, for a timeout that itself failed to kill.
BACKSTOP_S = 60


def run_capped(command: list, log, env: dict, wall_cap_s: int,
               has_timeout: bool) -> int:
    """Run one scorer in a session of its own, and kill the whole session if
    it overruns the cap.

    A scorer forks maximal and compare, and killing score_curves.py alone
    leaves those running to the end of their own budgets on cores the next
    item is about to be pinned to. coreutils timeout kills the group it
    started; the fallback used to kill the child alone, so both paths now
    put the scorer in a new session and kill that with os.killpg on expiry.
    """
    try:
        proc = subprocess.Popen(command, cwd=str(REPO_ROOT), stdout=log,
                                stderr=subprocess.STDOUT, env=env,
                                start_new_session=True)
    except OSError as exc:
        log.write(f"cannot run {command[0]}: {exc}\n")
        return 127
    try:
        return proc.wait(timeout=wall_cap_s + (BACKSTOP_S if has_timeout
                                               else 0))
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        proc.wait()
        return TIMEOUT_RC


def score_one(run_dir: Path, slot: int, args, out: Path, ledger: Ledger,
              pinned: bool, timeout_bin, index: int, total: int,
              child_env: dict) -> int:
    """One run: score under the caps, move the CSV into place or record why not.

    The CSV is written to `<run>.csv.part` and renamed only on a zero exit
    with a non-empty file, so a reader listing `*.csv` never sees a curve that
    is still being written or one a killed scorer left half done. The timings
    line carries the three budgets the run was scored under, because they are
    what a partial curve has to be read against.
    """
    final = csv_path(out, run_dir)
    part = final.with_suffix(".csv.part")
    command = (pin_prefix(slot, args.cores, pinned)
               + scorer_args(args, str(part), str(run_dir)))
    if timeout_bin:
        command = [timeout_bin, str(args.wall_cap_s)] + command
    accumulated = max(index_lines(run_dir) - 1, 0)
    start = time.time()
    with open(out / WARNINGS_NAME, "a") as log:
        log.write(f"=== {time.strftime('%Y-%m-%dT%H:%M:%S')} {run_dir.name} "
                  f"(worker {slot})\n")
        log.flush()
        rc = run_capped(command, log, child_env, args.wall_cap_s,
                        bool(timeout_bin))
    elapsed = int(time.time() - start)
    ledger.append(TIMINGS_NAME,
                  f"{accumulated} {elapsed} {rc} {run_dir.name} "
                  f"{args.maximal_timeout}/{args.compare_timeout}/"
                  f"{args.deadline_s}")
    if rc == 0 and is_scored(part):
        os.replace(part, final)
        with ledger.lock:
            ledger.scored += 1
        print(f"[{index}/{total}] {run_dir.name}: scored in {elapsed}s")
    else:
        part.unlink(missing_ok=True)
        ledger.append(FAILURES_NAME, f"{rc} {run_dir}")
        with ledger.lock:
            ledger.failed += 1
        print(f"[{index}/{total}] {run_dir.name}: FAILED rc={rc} "
              f"after {elapsed}s")
    sys.stdout.flush()
    return rc


def run_pool(runs: list, args, out: Path, ledger: Ledger, pinned: bool,
             timeout_bin, child_env: dict) -> None:
    """`--workers` threads, each owning one core block, over one shared queue.

    Threads rather than processes: every worker spends its life blocked in a
    subprocess wait, so the interpreter lock costs nothing, and one process
    keeps the ledger's lock an ordinary one.
    """
    pending: queue.Queue = queue.Queue()
    for index, run_dir in enumerate(runs, 1):
        pending.put((index, run_dir))
    total = len(runs)

    def worker(slot: int) -> None:
        while True:
            try:
                index, run_dir = pending.get_nowait()
            except queue.Empty:
                return
            score_one(run_dir, slot, args, out, ledger, pinned, timeout_bin,
                      index, total, child_env)

    threads = [threading.Thread(target=worker, args=(slot,), daemon=True)
               for slot in range(max(1, min(args.workers, total)))]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()


def host_name() -> str:
    return socket.gethostname().split(".")[0]


def manifest_path(out: Path) -> Path:
    return out / f"{MANIFEST_STEM}-{host_name()}.json"


def build_manifest(args, results: Path, out: Path, versions: dict, head,
                   pinned: bool, timeout_bin, counts: dict,
                   started: str) -> dict:
    """Everything an archive's `maximality_pass` block is assembled from."""
    return {
        "kind": "score",
        "hostname": socket.gethostname(),
        "started": started,
        "finished": None,
        "results": str(args.results),
        "results_resolved": str(results),
        "out": str(args.out),
        "out_resolved": str(out),
        "seeds": list(args.seeds),
        "workers": args.workers,
        "cores": args.cores,
        "pinned": pinned,
        "pinning": ("taskset -c <slot*cores>-<slot*cores+cores-1> per worker"
                    if pinned else "unpinned: no taskset on PATH"),
        "wall_cap": ("coreutils timeout, which kills the scorer's process "
                     "group" if timeout_bin
                     else "subprocess timeout on the scorer alone: no "
                          "coreutils timeout on PATH"),
        "cuts": args.cuts,
        "maximal_timeout": args.maximal_timeout,
        "compare_timeout": args.compare_timeout,
        "deadline_s": args.deadline_s,
        "wall_cap_s": args.wall_cap_s,
        "invocation": invocation_template(args),
        "binaries": {
            "maximal": {"path": str(MAXIMAL_BIN), **versions["maximal"]},
            "compare": {"path": str(COMPARE_BIN), **versions["compare"]},
        },
        "git": {"branch": R.git_branch(), "head": head or R.LEGACY_COMMIT},
        "allow_stale_binary": bool(args.allow_stale_binary),
        "python_version": platform.python_version(),
        "counts": dict(counts),
        "files": {
            "curves": "<run>.csv, one per run directory scored",
            "timings": f"{TIMINGS_NAME}: accumulated elapsed_s rc run "
                       f"maximal_timeout/compare_timeout/deadline_s, one "
                       f"line per attempt",
            "failures": f"{FAILURES_NAME}: rc run-dir, one line per failed "
                        f"attempt",
            "warnings": f"{WARNINGS_NAME}: every scorer's stdout and stderr",
        },
    }


def write_manifest(path: Path, manifest: dict) -> None:
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(manifest, indent=2) + "\n")
    os.replace(tmp, path)


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--results", required=True, metavar="DIR",
                        help="results directory holding the run directories; "
                             "relative to the checkout root")
    parser.add_argument("--out", required=True, metavar="DIR",
                        help="where the per-run curve CSVs go; relative to "
                             "the checkout root")
    parser.add_argument("--seeds", required=True, nargs="+", type=int,
                        help="this host's seeds; a run directory whose seed "
                             "is not listed is not scored here")
    parser.add_argument("--workers", type=int, default=DEFAULTS["workers"],
                        help=f"concurrent scorers (default: "
                             f"{DEFAULTS['workers']})")
    parser.add_argument("--cores", type=int, default=DEFAULTS["cores"],
                        help=f"cores per worker, also score_curves.py --jobs "
                             f"(default: {DEFAULTS['cores']})")
    parser.add_argument("--cuts", type=int, default=DEFAULTS["cuts"],
                        help=f"time cuts per curve (default: "
                             f"{DEFAULTS['cuts']})")
    parser.add_argument("--maximal-timeout", type=int,
                        default=DEFAULTS["maximal_timeout"],
                        help=f"seconds per maximal call, per cut (default: "
                             f"{DEFAULTS['maximal_timeout']})")
    parser.add_argument("--compare-timeout", type=int,
                        default=DEFAULTS["compare_timeout"],
                        help=f"seconds for the compare call (default: "
                             f"{DEFAULTS['compare_timeout']})")
    parser.add_argument("--deadline-s", type=int,
                        default=DEFAULTS["deadline_s"],
                        help=f"score_curves.py stops adding cuts after this "
                             f"(default: {DEFAULTS['deadline_s']})")
    parser.add_argument("--wall-cap-s", type=int, default=None,
                        help=f"outer timeout on one scorer (default: "
                             f"--deadline-s + {WALL_CAP_MARGIN_S})")
    parser.add_argument("--allow-stale-binary", action="store_true",
                        help="score with maximal/compare built from another "
                             "commit or a dirty tree; recorded in the manifest")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the queue and the command; score nothing")
    args = parser.parse_args(argv)
    for name in ("workers", "cores", "cuts", "maximal_timeout",
                 "compare_timeout", "deadline_s"):
        if getattr(args, name) < 1:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if args.wall_cap_s is None:
        args.wall_cap_s = wall_cap_default(args.deadline_s)
    elif args.wall_cap_s < 1:
        parser.error("--wall-cap-s must be positive")
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
    # Refused before the queue is built: an oversized pool is not a slow
    # pass but a fast one, `taskset -c` on a core the host does not have
    # exiting 1 at once and draining the whole queue into failures.txt.
    cpus = os.cpu_count()
    if cpus is not None and args.workers * args.cores > cpus:
        print(f"{args.workers} workers x {args.cores} cores is "
              f"{args.workers * args.cores} cores; this host has {cpus}",
              file=sys.stderr)
        return 2

    runs = queue_runs(results, args.seeds)
    if not runs:
        # An empty queue is never a finished pass. Exiting 0 here would let a
        # tick mark the phase done over nothing, and status read 0 of 0.
        print(f"nothing to score: no run directory under {results} is "
              f"named _seed<N> with N in {' '.join(map(str, args.seeds))}",
              file=sys.stderr)
        return 2
    already = [r for r in runs if is_scored(csv_path(out, r))]
    todo = [r for r in runs if not is_scored(csv_path(out, r))]
    versions = read_versions()
    head = R.working_tree_head()
    pinned = shutil.which("taskset") is not None
    timeout_bin = shutil.which("timeout")

    def label(name: str) -> str:
        version = versions[name]
        suffix = "-dirty" if version.get("dirty") == "1" else ""
        return f"{name} {version['commit_short']}{suffix}"

    print(f"Scoring {results}")
    print(f"    into:     {out}")
    print(f"    seeds:    {len(args.seeds)} "
          f"({min(args.seeds)}-{max(args.seeds)})")
    print(f"    queue:    {len(runs)} run(s), {len(already)} already scored, "
          f"{len(todo)} to score")
    print(f"    workers:  {args.workers} x {args.cores} core(s), "
          f"{'pinned' if pinned else 'unpinned (no taskset)'}")
    print(f"    budgets:  maximal {args.maximal_timeout}s, compare "
          f"{args.compare_timeout}s, deadline {args.deadline_s}s, wall cap "
          f"{args.wall_cap_s}s")
    print(f"    binaries: {label('maximal')}, {label('compare')}")
    print(f"    command:  {invocation_template(args)}")

    if args.dry_run:
        names = [run_dir.name for run_dir in todo]
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
    counts = {"queued": len(runs), "already_scored": len(already),
              "scored": 0, "failed": 0}
    started = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    manifest = build_manifest(args, results, out, versions, head, pinned,
                              timeout_bin, counts, started)
    write_manifest(manifest_path(out), manifest)

    child_env = dict(os.environ)
    child_env.setdefault("MAXIMAL_BIN", str(MAXIMAL_BIN))
    child_env.setdefault("COMPARE_BIN", str(COMPARE_BIN))
    ledger = Ledger(out)
    run_pool(todo, args, out, ledger, pinned, timeout_bin, child_env)

    counts.update({"scored": ledger.scored, "failed": ledger.failed})
    manifest["counts"] = dict(counts)
    manifest["finished"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    write_manifest(manifest_path(out), manifest)
    missing = [r for r in runs if not is_scored(csv_path(out, r))]
    print(f"\nDone: {counts['scored']} scored, {counts['failed']} failed, "
          f"{counts['already_scored']} already there; "
          f"{len(runs) - len(missing)}/{len(runs)} curves present.")
    if missing:
        print(f"{len(missing)} run(s) have no curve; see "
              f"{out / FAILURES_NAME}. A rerun scores only those.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
