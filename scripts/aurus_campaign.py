#!/usr/bin/env python3
"""Run the AuRUS baseline campaign and collect metrics to a results CSV.

The head-to-head arm of the ablation campaign (PLAN §3.3): for each spec x
repeat, invoke AuRUS's ./unreal-repair.sh from the AuRUS repo root with the
campaign parameters (BASE_FLAGS plus -GATO=<gato>, and -onlyInputsA for the
nine specs run-all-together.sh drives), one output directory per repeat.
AuRUS is not seedable (its RNG comes from Math.random() with no CLI
override), so independent repeats stand in for seeds and every out.txt is
archived.

`--aurus-root` must point at AuRUS as its authors left it, commit 3f6f01f,
which is the last upstream commit before this project's fork. The fork above
it changes the GA core, the model-counting fitness and the solver layer, and
would measure an optimised AuRUS rather than the published baseline. Only one
substitution is unavoidable: 3f6f01f tracks a macOS Mach-O Strix binary at
lib/new_strix/strix, so any Linux run needs a Linux Strix dropped in there.
Note the entry point lives at the repo root at that commit; the fork later
moved it under scripts/.

Per-run wall time is measured here, externally, rather than trusting the
JVM's self-report (recorded too, as aurus_time_s).

Each run's out.txt is parsed (`Num. of Solutions:`, `Time:`, `Settings{...}`)
into a row of <out-root>/aurus_results.csv. Resumable: a (spec, repeat) whose
out.txt already exists is not re-run — its CSV row is backfilled from the
existing out.txt if missing (wall_time_s blank, since the original wall clock
is gone). Repeat-major ordering, so killing the campaign at a wall-clock
deadline leaves a balanced design across specs.

Concurrency is bounded by a pool of worker threads, each owning one JVM
subprocess (--concurrency, default 10 — ~8 GB heap each via the script's
-Xmx8g stays far under the 125 GB av2/av3 machines). A run that outlives
GATO by 300 s is assumed wedged and its whole process group is killed.

Usage:
    python scripts/aurus_campaign.py --out-root ~/aurus-results \\
        --spot-bin ~/projects/counter/build-release/third_party/spot/bin
    python scripts/aurus_campaign.py --out-root out --specs minepump \\
        --repeats 1 --gato 30                      # smoke test
"""

import argparse
import csv
import os
import re
import signal
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

# The 26 specifications the AuRUS paper evaluates, keyed by the name their
# rows carry in this campaign's CSVs, valued by a path relative to
# <aurus-root>. The tree spans two roots — the case studies and the loose
# `examples/` one — so these are root-relative rather than rooted at
# `case-studies/`, which is what previously made the examples/ rows unnameable.
#
# Eleven of the keys are PEREDUR examples/ names, so those rows join directly
# against run_experiments.py's `aurus-h2h` rows; they are H2H_TLSF_SPECS there
# and are the only ones a repair-quality statistic can be computed for. The
# rest run here alone, PEREDUR having no family for them yet. Every out.txt is
# archived, so an import later re-scores an existing run rather than needing
# a new one.
#
# The six `-aurus` suffixes are deliberate. PEREDUR carries families named
# `detector`, `full-arbiter`, `load-balancer`, `prioritized-arbiter`,
# `round-robin-arbiter` and `simple-arbiter`, taken from SYNTCOMP at different
# parameter instances than these (PEREDUR's simple_arbiter_unreal2_3_basic
# against the paper's simple_arbiter_unreal2_2). Reusing the bare names would
# assert a correspondence that does not hold, which is the same reason AuRUS's
# arbiter is `arbiter-aurus` rather than `arbiter`.
#
# Two near-misses to leave alone: full_arbiter/ and syntcomp-unreal/lily02/
# each hold a second unrealizable variant the paper does not use
# (full_arbiter_unreal1_3_2_basic.tlsf, lilydemo01.tlsf), so these are picked
# by filename and never by directory.
#
# lily02 is the syntcomp-unreal copy, not the top-level case-studies/lily02
# one: the paper files Lily02 under SYNTCOMP and that copy carries the five
# references (lilydemo03..07) the row is scored against, where the top-level
# copy is a separate one-reference setup. The two lilydemo02.tlsf files are
# byte-identical, so this fixes provenance rather than behaviour.
SPEC_TLSF: dict[str, str] = {
    # Literature (5)
    "arbiter-aurus": "case-studies/arbiter/arbiter.tlsf",
    "minepump": "case-studies/minepump/minepump.tlsf",
    "rg1": "case-studies/RG1/RG1.tlsf",
    "rg2": "case-studies/RG2/RG2.tlsf",
    "lift": "case-studies/lift/Lift.tlsf",
    # SYNTCOMP (13)
    "detector-aurus":
        "case-studies/syntcomp-unreal/detector/detector_unreal_2.tlsf",
    "full-arbiter-aurus":
        "case-studies/syntcomp-unreal/full_arbiter/"
        "full_arbiter_unreal1_3_2.tlsf",
    "lily02": "case-studies/syntcomp-unreal/lily02/lilydemo02.tlsf",
    "lily11": "case-studies/syntcomp-unreal/lily11/lilydemo11.tlsf",
    "lily15": "case-studies/syntcomp-unreal/lily15/lilydemo15.tlsf",
    "lily16": "case-studies/syntcomp-unreal/lily16/lilydemo16.tlsf",
    "load-balancer-aurus":
        "case-studies/syntcomp-unreal/load_balancer/"
        "load_balancer_unreal1_2_2.tlsf",
    "ltl2dba-r-2":
        "case-studies/syntcomp-unreal/ltl2dba_R_2/ltl2dba_R_2.tlsf",
    "ltl2dba-theta-2":
        "case-studies/syntcomp-unreal/ltl2dba_theta_2/ltl2dba_theta_2.tlsf",
    "ltl2dba27": "case-studies/syntcomp-unreal/ltl2dba27/ltl2dba27.tlsf",
    "prioritized-arbiter-aurus":
        "case-studies/syntcomp-unreal/prioritized_arbiter/"
        "prioritized_arbiter_unreal1_3_2.tlsf",
    "round-robin-arbiter-aurus":
        "case-studies/syntcomp-unreal/round_robin/"
        "round_robin_arbiter_unreal1_2_3.tlsf",
    "simple-arbiter-aurus":
        "case-studies/syntcomp-unreal/simple_arbiter/"
        "simple_arbiter_unreal2_2.tlsf",
    # SYNTECH15 (8)
    "gyro-var1":
        "case-studies/GyroUnrealizable_Var1/"
        "GyroUnrealizable_Var1_710_GyroAspect_unrealizable.tlsf",
    "gyro-var2":
        "case-studies/GyroUnrealizable_Var2/"
        "GyroUnrealizable_Var2_710_GyroAspect_unrealizable.tlsf",
    "humanoid-458":
        "case-studies/HumanoidLTL_458/"
        "HumanoidLTL_458_Humanoid_fixed_unrealizable.tlsf",
    "humanoid-531":
        "case-studies/HumanoidLTL_531/"
        "HumanoidLTL_531_Humanoid_unrealizable.tlsf",
    "humanoid-503":
        "examples/icse2019/SYNTECH15/tlsf_specs/"
        "HumanoidLTL_503_Humanoid_fixed_unrealizable.tlsf",
    "humanoid-741":
        "examples/icse2019/SYNTECH15/tlsf_specs/"
        "HumanoidLTL_741_Humanoid_unrealizable.tlsf",
    "humanoid-742":
        "examples/icse2019/SYNTECH15/tlsf_specs/"
        "HumanoidLTL_742_Humanoid_unrealizable.tlsf",
    "pcar-v2-888":
        "examples/icse2019/SYNTECH15/tlsf_specs/"
        "PCarLTL_Unrealizable_V_2_unrealizable.0_888_PCar_fixed_unrealizable"
        ".tlsf",
}

# GA parameters common to all 26 runs. These come from the AuRUS authors' own
# drivers under scripts/legacy/, which are the record of how the paper's
# numbers were produced; all three of them agree on every flag here.
#
# `-k=20` is the model-counter bound. The 2026-07-24 campaign ran 10, which
# counted traces to half the depth on AuRUS's side of a comparison where
# PEREDUR's own `model_counting.default_bound` is 20; the legacy drivers
# settle it independently of the paper's prose. `-GATO` is supplied per run
# from --gato and is 7200 in all three drivers too. `-geneNUM=0` restates the
# shipped default (GA_GENE_NUM_OF_MUTATIONS), so it is a no-op kept for
# fidelity to the record. See experiments/2026-07-24-ablation/REPORT.md.
#
# `-factors` is STATUS,SYN,SEMANTIC and is a DELIBERATE DEPARTURE from the
# drivers, which do not agree on it: run-all-together.sh omits it,
# run-spectra-icse2019.sh passes `0.7,0.1,0.2`, and
# run-all-sensitivity-syntcomp.sh passes `1,0,0` — status alone, with no
# similarity pressure whatever. Running the last of those would set PEREDUR,
# which weights syntactic and semantic similarity, against an AuRUS told to
# ignore both on a third of the corpus, and would flatter PEREDUR on repair
# quality for a reason unrelated to search.
#
# All 26 therefore run at `0.7,0.1,0.2`, which departs from the drivers only
# for the SYNTCOMP third: `Settings.setFactors` halves the semantic weight
# into LOST_MODELS and WON_MODELS, so the value is exactly the shipped default
# 0.7/0.1/0.1/0.1 that the other two drivers already run under. It is passed
# explicitly rather than left to the default so the archived settings string
# records the choice.
#
# `-removeGuarantees` appears in none of the drivers and is not passed, so
# AuRUS never deletes a guarantee. PEREDUR does, at p_remove_guarantee 0.05.
# That asymmetry in operator sets is a threat to validity, not a bug.
BASE_FLAGS = [
    "-Max=1000", "-Gen=1000", "-Pop=100", "-k=20", "-addA", "-geneNUM=0",
    "-factors=0.7,0.1,0.2",
]

# `-onlyInputsA` restricts generated assumptions to input variables. It is the
# one flag the drivers genuinely disagree on rather than merely spell
# differently, so it is matched per group: run-all-together.sh passes it for
# the five literature specs and the four SYNTECH15 ones it drives, and neither
# of the other two drivers passes it. Without it AuRUS may assume over its own
# outputs and return a repair the system satisfies by defeating its own
# assumptions — which PEREDUR's output gate rejects and AuRUS's does not.
ONLY_INPUTS_A = frozenset({
    "arbiter-aurus", "minepump", "rg1", "rg2", "lift",
    "gyro-var1", "gyro-var2", "humanoid-458", "humanoid-531",
})


def flags_for(spec: str) -> list[str]:
    """Return the AuRUS flag list for one specification."""
    flags = list(BASE_FLAGS)
    if spec in ONLY_INPUTS_A:
        flags.append("-onlyInputsA")
    return flags

# Grace beyond GATO before the process group is killed: AuRUS's own timeout is
# internal to the GA loop, so a wedged JVM (or a straggling model-counting
# child) can outlive it indefinitely.
KILL_GRACE_S = 300

CSV_FIELDS = [
    "spec", "repeat", "n_solutions", "aurus_time_s", "wall_time_s",
    "killed", "exit_code", "settings",
]

N_SOLUTIONS_RE = re.compile(r"Num\. of Solutions:\s*(\d+)")
# Anchored so "GA Time:" does not match.
TIME_RE = re.compile(r"^Time:\s*(\d+)", re.MULTILINE)
SETTINGS_RE = re.compile(r"Settings\{(.*)\}")


def parse_out_txt(out_txt: Path) -> dict:
    """Return the n_solutions / aurus_time_s / settings columns from out.txt.

    Missing fields stay blank rather than failing the row: an out.txt cut
    short by a kill still records whatever it got to.
    """
    row = {"n_solutions": "", "aurus_time_s": "", "settings": ""}
    try:
        text = out_txt.read_text(errors="replace")
    except OSError:
        return row
    if m := N_SOLUTIONS_RE.search(text):
        row["n_solutions"] = str(int(m.group(1)))
    if m := TIME_RE.search(text):
        row["aurus_time_s"] = str(int(m.group(1)))
    if m := SETTINGS_RE.search(text):
        row["settings"] = m.group(1)
    return row


def run_one(aurus_root: Path, tlsf: Path, out_dir: Path, gato: int,
            env: dict, flags: list[str]) -> tuple[int | str, int, float]:
    """Execute one AuRUS repair run; return (exit_code, killed, wall_time_s).

    The JVM runs in its own session so a timeout kill takes the whole process
    group (java plus any strix/relsat/ltl2tgba children) rather than just the
    wrapper shell.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(aurus_root / "unreal-repair.sh"),
           *flags, f"-GATO={gato}", f"-out={out_dir}", str(tlsf)]
    log_path = out_dir / "run.log"
    t_start = time.monotonic()
    killed = 0
    with open(log_path, "wb") as log_file:
        proc = subprocess.Popen(cmd, cwd=aurus_root, env=env,
                                stdout=log_file, stderr=subprocess.STDOUT,
                                start_new_session=True)
        try:
            proc.wait(timeout=gato + KILL_GRACE_S)
        except subprocess.TimeoutExpired:
            killed = 1
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            proc.wait()
    wall = round(time.monotonic() - t_start, 2)
    return proc.returncode, killed, wall


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--aurus-root", type=Path,
                        default=Path.home() / "projects" / "tools" / "aurus",
                        metavar="PATH",
                        help="AuRUS checkout (default: %(default)s); must be "
                             "built (bin/ populated by ant compile)")
    # Absolutised because AuRUS resolves a relative -out against its own repo
    # root (its scripts cd there), scattering outputs under the aurus tree
    # while the parser reads the harness-side path — bitten 2026-07-26.
    parser.add_argument("--out-root", type=lambda p: Path(p).resolve(),
                        required=True, metavar="PATH",
                        help="Directory for <spec>/repeat-NN/ run dirs and "
                             "aurus_results.csv")
    parser.add_argument("--specs", nargs="+", choices=list(SPEC_TLSF),
                        default=list(SPEC_TLSF), metavar="SPEC",
                        help="Specs to run, by PEREDUR family name "
                             "(default: the whole head-to-head set)")
    parser.add_argument("--repeats", type=int, default=30, metavar="N",
                        help="Independent repeats per spec (default: 30); "
                             "AuRUS is not seedable, so repeats stand in for "
                             "seeds")
    parser.add_argument("--repeat-offset", type=int, default=0, metavar="N",
                        help="First repeat index (default: 0), so two hosts "
                             "can split one repeat range: --repeats 15 on "
                             "each, with --repeat-offset 15 on the second. "
                             "Repeats are the AuRUS arm's only replicate "
                             "dimension and are numbered, not seeded, so two "
                             "hosts left at the default would both run 0..N-1 "
                             "and half the machine time would produce "
                             "duplicate indices that the merge discards")
    parser.add_argument("--gato", type=int, default=7200, metavar="S",
                        help="AuRUS GA execution timeout in seconds "
                             "(default: 7200, the published 2 h); the run is "
                             f"hard-killed at GATO + {KILL_GRACE_S} s if the "
                             "JVM wedges")
    parser.add_argument("--concurrency", type=int, default=10, metavar="N",
                        help="Concurrent AuRUS runs (default: 10)")
    parser.add_argument("--spot-bin", type=Path, default=None, metavar="PATH",
                        help="Directory prepended to PATH so AuRUS finds "
                             "ltl2tgba/autfilt (e.g. PEREDUR's "
                             "build-release/third_party/spot/bin). Omit if "
                             "SPOT is already on PATH")
    args = parser.parse_args()

    if args.concurrency < 1:
        sys.exit("--concurrency must be >= 1")
    repair_sh = args.aurus_root / "unreal-repair.sh"
    if not repair_sh.exists():
        sys.exit(f"Not an AuRUS checkout: {repair_sh} missing")
    if not (args.aurus_root / "bin" / "main" / "Main.class").exists():
        sys.exit(f"AuRUS is not built: run `ant compile` in {args.aurus_root}")

    env = os.environ.copy()
    if args.spot_bin is not None:
        env["PATH"] = f"{args.spot_bin.resolve()}{os.pathsep}{env['PATH']}"

    results_csv = args.out_root / "aurus_results.csv"
    done: set[tuple[str, int]] = set()
    if results_csv.exists():
        with open(results_csv, newline="") as f:
            for row in csv.DictReader(f):
                done.add((row["spec"], int(row["repeat"])))

    # Repeat-major, mirroring run_experiments.py's seed-major order: a
    # wall-clock kill leaves every spec at the same repeat depth.
    tasks = [(spec, rep)
             for rep in range(args.repeat_offset,
                              args.repeat_offset + args.repeats)
             for spec in args.specs]
    to_run = [(s, r) for s, r in tasks
              if not (args.out_root / s / f"repeat-{r:02d}" / "out.txt").exists()]
    backfill = [(s, r) for s, r in tasks
                if (s, r) not in done and (s, r) not in to_run]

    print("=" * 64)
    lo = args.repeat_offset
    hi = args.repeat_offset + args.repeats - 1
    print(f"  AuRUS baseline: {len(args.specs)} specs x {args.repeats} "
          f"repeats ({lo}-{hi})")
    print(f"    aurus:       {args.aurus_root}")
    print(f"    out:         {args.out_root}")
    print(f"    GATO:        {args.gato}s (kill at +{KILL_GRACE_S}s)")
    print(f"    concurrency: {args.concurrency}")
    print(f"    plan:        {len(to_run)} to run, {len(backfill)} rows to "
          f"backfill, {len(tasks) - len(to_run) - len(backfill)} already done")
    print("=" * 64)

    args.out_root.mkdir(parents=True, exist_ok=True)
    lock = threading.Lock()
    state = {"completed": 0}
    n_exec = len(to_run)
    t0 = time.monotonic()

    def append_row(row: dict) -> None:
        write_header = (not results_csv.exists()
                        or results_csv.stat().st_size == 0)
        with open(results_csv, "a", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
            if write_header:
                writer.writeheader()
            writer.writerow(row)

    # Backfilled rows come from a previous session's out.txt: the run itself
    # is done, only its CSV row is missing (e.g. the session died between the
    # run and the append). Wall time is unrecoverable and stays blank.
    for spec, rep in backfill:
        out_dir = args.out_root / spec / f"repeat-{rep:02d}"
        append_row({"spec": spec, "repeat": rep,
                    **parse_out_txt(out_dir / "out.txt"),
                    "wall_time_s": "", "killed": "", "exit_code": ""})
        done.add((spec, rep))

    def execute(task: tuple[str, int]) -> None:
        spec, rep = task
        run_id = f"{spec}/repeat-{rep:02d}"
        out_dir = args.out_root / spec / f"repeat-{rep:02d}"
        tlsf = args.aurus_root / SPEC_TLSF[spec]
        with lock:
            print(f"[start]      {run_id}", flush=True)
        exit_code, killed, wall = run_one(
            args.aurus_root, tlsf, out_dir, args.gato, env, flags_for(spec))
        row = {"spec": spec, "repeat": rep,
               **parse_out_txt(out_dir / "out.txt"),
               "wall_time_s": wall, "killed": killed, "exit_code": exit_code}
        with lock:
            state["completed"] += 1
            n = state["completed"]
            if (spec, rep) not in done:
                append_row(row)
                done.add((spec, rep))
            elapsed = time.monotonic() - t0
            eta = elapsed / n * (n_exec - n)
            note = "  KILLED" if killed else ""
            print(f"[{n}/{n_exec}]  {run_id}  done in {wall}s{note}"
                  f"  ETA {eta/60:.1f}min", flush=True)

    with ThreadPoolExecutor(max_workers=args.concurrency) as pool:
        list(pool.map(execute, to_run))

    elapsed_total = time.monotonic() - t0
    print(f"\nDone. {state['completed']} runs in {elapsed_total/60:.1f} min."
          f"\nResults: {results_csv}")


if __name__ == "__main__":
    main()
