#!/usr/bin/env python3
"""Write the designs, subject lists and run-directory farms of the passes that
bring 2026-11-26-extra-seeds into the pooled measures.

    python3 experiments/2026-11-26-extra-pooled/scripts/make_design.py count
    python3 experiments/2026-11-26-extra-pooled/scripts/make_design.py write

`count` prints, for fsm-lmcps and each of the 4 seeded mutants, every arm's
members (repair_N.json files) and the seeds that hold them, and which subjects
pass the join rule of 2026-11-26-extra-seeds/PLAN.md: fsm-lmcps needs at least
4 members from at least 2 seeds in every arm, a seeded mutant at least 4
members in every arm.

`write` builds two farms of symbolic links, so one design source can read a
subject's seeds from two campaigns' results:

- experiments/results-extra-pooled-lmcps: fsm-lmcps seeds 0-29 from sweep F
  (base, only-<f>, no-gate, random/base) and sweep W (no-<f>, the runs the
  paper's per-run measures read), seeds 30-329 from results-extra-seeds;
- experiments/results-extra-pooled-seeded: the mutants' seeds 0-4 from sweep F
  and sweep T (the runs the seeded pooled pass read), 5-29 from
  results-extra-seeds-seeded.

It then writes three passes, each repeating the parameters of the
2026-11-25 pass it extends:

- 2026-11-26-extra-pooled (of 2026-11-25-one-way-pooled): fsm-lmcps, if it
  passes, on av2; lift-plus-cruise-mini and valu3s-uc6, sampled, on av3, with
  no-<f> from sweep W instead of sweep T, so the pooled and per-run measures
  read the same runs (user, 2026-11-26). Their words are copied from
  one-way-pooled/fp before enqueue, so the word sample matches the random
  pass's.
- 2026-11-26-extra-pooled-random (of 2026-11-25-one-way-pooled-random):
  random/base against base on fsm-lmcps, on av2, on this pass's words.
- 2026-11-26-extra-seeded-pooled (of 2026-11-25-one-way-seeded-pooled): the
  seeded mutants that pass, split over both hosts.

Every output is untracked, like every pooled pass's inputs; this script is
the record.
"""

import argparse
import importlib.util
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EXP = ROOT / "experiments"
_spec = importlib.util.spec_from_file_location(
    "one_way_design", EXP / "2026-11-25-one-way-pooled" / "scripts" / "make_design.py")
ONE_WAY = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ONE_WAY)
FEATURES = ONE_WAY.FEATURES
RUN_DIR, FULL_RUN_DIR, SWEEP_T_RUN_DIR = (
    ONE_WAY.RUN_DIR, ONE_WAY.FULL_RUN_DIR, ONE_WAY.SWEEP_T_RUN_DIR)

LMCPS = "fsm-lmcps"
SAMPLED = ["lift-plus-cruise-mini", "valu3s-uc6"]
MUTANTS = [f"sw-fsm-lmcps__paper-v2__m{m:02d}" for m in (1, 2, 6, 7)]
LMCPS_SEEDS = range(330)
SEEDED_SEEDS = range(30)

# arm -> (scheme, level as run_dir names it, run_dir template)
ARMS = {"nsga2-apportion/base": ("nsga2-apportion", "base", RUN_DIR),
        "nsga2-apportion/full": ("nsga2-apportion", "full", FULL_RUN_DIR)}
ARMS.update({f"nsga2-apportion/only-{f}": ("nsga2-apportion", f"only-{f}", RUN_DIR)
             for f in FEATURES})
ARMS.update({f"nsga2-apportion/no-{f}": ("nsga2-apportion", f"no-{f}", SWEEP_T_RUN_DIR)
             for f in FEATURES})
PARETO = list(ARMS)
RANDOM = "random/base"
ARMS[RANDOM] = ("random", "base", RUN_DIR)


def run_name(arm: str, subject: str, seed: int) -> str:
    scheme, level, template = ARMS[arm]
    return template.format(scheme=scheme, level=level, subject=subject, seed=seed)


def lmcps_source(arm: str, seed: int) -> Path:
    if seed >= 30:
        results = "results-extra-seeds"
    elif "/no-" in arm:
        results = "results-one-way-wall"
    else:
        results = "results-final-engine"
    return EXP / results / run_name(arm, LMCPS, seed)


def seeded_source(arm: str, subject: str, seed: int) -> Path:
    if seed >= 5:
        results = "results-extra-seeds-seeded"
    elif "/no-" in arm:
        results = "results-one-way-seeded"
    else:
        results = "results-final-engine-seeded"
    return EXP / results / run_name(arm, subject, seed)


def members(run: Path) -> int:
    if not (run / "run.json").is_file():
        sys.exit(f"make_design: {run} is missing or unfinished")
    return sum(1 for _ in run.glob("repair_*.json"))


def tally(subject: str, arms: list, seeds, source) -> dict:
    out = {}
    for arm in arms:
        counts = {s: members(source(arm, s) if subject == LMCPS
                              else source(arm, subject, s)) for s in seeds}
        out[arm] = (sum(counts.values()), sum(1 for c in counts.values() if c))
    return out


def joins(subject: str, counts: dict) -> bool:
    if subject == LMCPS:
        return all(n >= 4 and k >= 2 for n, k in counts.values())
    return all(n >= 4 for n, _ in counts.values())


def assess(which: str) -> tuple:
    rows = {}
    if which in ("all", "specs"):
        rows[LMCPS] = tally(LMCPS, PARETO + [RANDOM], LMCPS_SEEDS, lmcps_source)
    if which in ("all", "seeded"):
        for m in MUTANTS:
            rows[m] = tally(m, PARETO, SEEDED_SEEDS, seeded_source)
    passed = {s: joins(s, {a: c for a, c in rows[s].items() if a in PARETO})
              for s in rows}
    return rows, passed


def farm(name: str, links: dict) -> None:
    out = EXP / name
    out.mkdir(exist_ok=True)
    for entry in out.iterdir():
        if entry.is_symlink():
            entry.unlink()
    for run_dir, target in links.items():
        os.symlink(os.path.relpath(target, out), out / run_dir)
    print(f"{out}: {len(links)} links")


def group(results: str, run_dir_seeds: str, subjects: list, sampled: list,
          which: str, t_results: str) -> list:
    lines = ["", "[[groups]]", f'results = "{results}"', f'run_dir = "{RUN_DIR}"',
             f'seeds = "{run_dir_seeds}"', 'members = "output"', "subjects = ["]
    lines += [f'  "{s}",' for s in subjects]
    lines += ["]", "sampled = [" + ", ".join(f'"{s}"' for s in sampled) + "]",
              "contrasts = ["]
    lines += [f'  {{ name = "{n}", a = "{ONE_WAY.arm(a)}", b = "{ONE_WAY.arm(b)}", '
              f'family = "{fam}" }},' for n, a, b, fam in ONE_WAY.contrasts(which)]
    lines += ["]"]
    if which != "random":
        lines += ["", "[groups.sources]",
                  f'"nsga2-apportion/full" = {{ results = "{results}", '
                  f'run_dir = "{FULL_RUN_DIR}", seeds = "{run_dir_seeds}", members = "output" }}']
        lines += [f'"nsga2-apportion/no-{f}" = {{ results = "{t_results}", '
                  f'run_dir = "{SWEEP_T_RUN_DIR}", seeds = "{run_dir_seeds}", members = "output" }}'
                  for f in FEATURES]
    return lines


def write_pass(name: str, header: str, groups: list, av2: list, av3: list) -> None:
    out = EXP / name
    out.mkdir(parents=True, exist_ok=True)
    text = [f"# {line}".rstrip() for line in header.splitlines()] + ["", "draws = 100"]
    for g in groups:
        text += g
    (out / "design.toml").write_text("\n".join(text) + "\n")
    (out / "subjects-av2.txt").write_text("".join(f"{s}\n" for s in av2))
    (out / "subjects-av3.txt").write_text("".join(f"{s}\n" for s in av3))
    print(f"{out}: {len(av2)} + {len(av3)} subjects")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("which", choices=["count", "write"])
    ap.add_argument("--only", choices=["all", "specs", "seeded"], default="all",
                    help="the specification passes (fsm-lmcps, the sampled "
                         "subjects, random) or the seeded pass alone")
    args = ap.parse_args(argv)
    rows, passed = assess(args.only)
    for subject, counts in rows.items():
        print(f"{subject}: {'joins' if passed[subject] else 'stays out'}")
        for arm, (n, k) in counts.items():
            print(f"  {arm:30} {n:5} members from {k:3} seeds")
    if args.which == "count":
        return 0

    if args.only == "seeded":
        return write_seeded(passed)
    farm("results-extra-pooled-lmcps",
         {run_name(a, LMCPS, s): lmcps_source(a, s)
          for a in PARETO + [RANDOM] for s in LMCPS_SEEDS})

    lmcps = [LMCPS] if passed[LMCPS] else []
    groups = [group("experiments/results-final-engine", "0-29", SAMPLED, SAMPLED,
                    "specs", "experiments/results-one-way-wall")]
    if lmcps:
        groups.append(group("experiments/results-extra-pooled-lmcps", "0-329",
                            lmcps, [], "specs", "experiments/results-extra-pooled-lmcps"))
    write_pass("2026-11-26-extra-pooled",
               "Pooled pass on the paper's 8 arms (2026-11-26-extra-pooled make_design.py):\n"
               "lift-plus-cruise-mini and valu3s-uc6 at seeds 0-29 with no-<f> from\n"
               "sweep W, and fsm-lmcps at seeds 0-329 if it passes the join rule.",
               groups, lmcps, SAMPLED)
    if lmcps:
        write_pass("2026-11-26-extra-pooled-random",
                   "Pooled pass of random/base against nsga2-apportion/base on fsm-lmcps,\n"
                   "seeds 0-329, on the words of 2026-11-26-extra-pooled.",
                   [group("experiments/results-extra-pooled-lmcps", "0-329", lmcps, [],
                          "random", "")], lmcps, [])
    return write_seeded(passed) if args.only == "all" else 0


def write_seeded(passed: dict) -> int:
    farm("results-extra-pooled-seeded",
         {run_name(a, m, s): seeded_source(a, m, s)
          for a in PARETO for m in MUTANTS for s in SEEDED_SEEDS})
    mutants = [m for m in MUTANTS if passed[m]]
    if mutants:
        write_pass("2026-11-26-extra-seeded-pooled",
                   "Pooled pass over the 4 seeded fsm-lmcps mutants no arm repaired at\n"
                   "seeds 0-4, at seeds 0-29, on the paper's 8 arms, where they pass.",
                   [group("experiments/results-extra-pooled-seeded", "0-29", mutants, [],
                          "seeded", "experiments/results-extra-pooled-seeded")],
                   mutants[0::2], mutants[1::2])
    return 0


if __name__ == "__main__":
    sys.exit(main())
