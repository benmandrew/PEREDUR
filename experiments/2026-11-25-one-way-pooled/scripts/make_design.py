#!/usr/bin/env python3
"""Write the design and subject lists of the one-way pooled passes.

    python3 experiments/2026-11-25-one-way-pooled/scripts/make_design.py specs
    python3 experiments/2026-11-25-one-way-pooled/scripts/make_design.py seeded \
        [--drop SUBJECT ...] [--sampled SUBJECT ...] [--av2 SUBJECT ...]
    python3 experiments/2026-11-25-one-way-pooled/scripts/make_design.py random
    python3 experiments/2026-11-25-one-way-pooled/scripts/make_design.py seeded-random \
        [--drop SUBJECT ...]

Two-way mutation (gate) was dropped from the paper on 2026-11-24
(2026-11-24-one-way PLAN.md). These passes repeat 2026-11-20-final-engine-
pooled and -seeded-pooled on the paper's 8 nsga2-apportion arms:

- base and only-<f> from sweep F (2026-11-20-final-engine, -large's seeded);
- full from sweep F's no-gate (a source with a literal run_dir);
- no-<f> from sweep T (2026-11-24-one-way), which is no-gate with f off.

`specs` writes experiments/2026-11-25-one-way-pooled/{design.toml,
subjects-av2.txt,subjects-av3.txt}; `seeded` the same under
experiments/2026-11-25-one-way-seeded-pooled/; `random` under
experiments/2026-11-25-one-way-pooled-random/, which pools sweep F's
random/base against base on the specs pass's subjects, to be scored on that
pass's word sample (copied into its fp directory before enqueue);
`seeded-random` under experiments/2026-11-27-seeded-random-pooled/, which
does the same on the seeded pass's subjects and host split (its
subjects-av2.txt and subjects-av3.txt, which must be present), with random/base
from 2026-11-27-seeded-random and base from sweep F. All are
untracked, like every pooled pass's inputs; this script is the record.

Families: "base" holds full, every only-<f> and no-climb against base;
"ablation" each no-<f> against full; every other pair of the 8 arms is in
"joint", so joint_measures.py can decide the joint front over all 8.
"""

import argparse
import itertools
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
FEATURES = ["climb", "remove", "novelty"]
ARMS = (["base", "full"] + [f"no-{f}" for f in FEATURES]
        + [f"only-{f}" for f in FEATURES])
RUN_DIR = "sweep_F_{level}_{scheme}_wkoff_log_{subject}_seed{seed:02d}"
FULL_RUN_DIR = "sweep_F_no-gate_{scheme}_wkoff_log_{subject}_seed{seed:02d}"
SWEEP_T_RUN_DIR = "sweep_T_{level}_{scheme}_wkoff_log_{subject}_seed{seed:02d}"

# The 18 subjects and host split of 2026-11-20-final-engine-pooled.
SPEC_AV2 = ["fsm", "fsm-timing", "fsm-combined", "mode-arbiter", "takeoff",
            "liquid-mixer", "rad-core-1-18", "rad-core-10", "rad-core-12-18",
            "rad-core-17-18", "rad-core-45", "rad-core-55", "rad-core-61",
            "rad-core-65", "rad-core-68"]
SPEC_AV3 = ["rad-core-33", "valu3s-uc6", "lift-plus-cruise-mini"]
SPEC_SAMPLED = ["valu3s-uc6", "lift-plus-cruise-mini"]


def arm(level: str) -> str:
    return level if "/" in level else f"nsga2-apportion/{level}"


def contrasts(which: str = "specs") -> list:
    if which in ("random", "seeded-random"):
        return [("random", "random/base", "base", "base")]
    out = [("full", "full", "base", "base")]
    out += [(f"only-{f}", f"only-{f}", "base", "base") for f in FEATURES]
    out += [("no-climb-over-base", "no-climb", "base", "base")]
    out += [(f"no-{f}", f"no-{f}", "full", "ablation") for f in FEATURES]
    named = {frozenset((a, b)) for _, a, b, _ in out}
    for a, b in itertools.combinations(ARMS, 2):
        if frozenset((a, b)) not in named:
            out.append((f"{a}-over-{b}", a, b, "joint"))
    return out


def design(header: str, results: str, results_t: str, seeds: str,
           subjects: list, sampled: list, which: str = "specs") -> str:
    lines = [f"# {line}".rstrip() for line in header.splitlines()]
    lines += ["", "draws = 100", "", "[[groups]]", f'results = "{results}"',
              f'run_dir = "{RUN_DIR}"', f'seeds = "{seeds}"',
              'members = "output"', "subjects = ["]
    lines += [f'  "{s}",' for s in subjects]
    lines += ["]", "sampled = [" + ", ".join(f'"{s}"' for s in sampled) + "]",
              "contrasts = ["]
    lines += [f'  {{ name = "{n}", a = "{arm(a)}", b = "{arm(b)}", '
              f'family = "{fam}" }},' for n, a, b, fam in contrasts(which)]
    lines += ["]"]
    if which == "seeded-random":
        lines += ["", "[groups.sources]",
                  f'"{arm("random/base")}" = {{ results = "{results_t}", '
                  f'run_dir = "{RUN_DIR}", seeds = "{seeds}", members = "output" }}']
    elif which != "random":
        lines += ["", "[groups.sources]",
                  f'"{arm("full")}" = {{ results = "{results}", '
                  f'run_dir = "{FULL_RUN_DIR}", seeds = "{seeds}", members = "output" }}']
        lines += [f'"{arm(f"no-{f}")}" = {{ results = "{results_t}", '
                  f'run_dir = "{SWEEP_T_RUN_DIR}", seeds = "{seeds}", members = "output" }}'
                  for f in FEATURES]
    lines += [""]
    return "\n".join(lines)


def write(out: Path, text: str, av2: list, av3: list,
          which: str = "specs") -> None:
    out.mkdir(parents=True, exist_ok=True)
    (out / "design.toml").write_text(text)
    (out / "subjects-av2.txt").write_text("".join(f"{s}\n" for s in av2))
    (out / "subjects-av3.txt").write_text("".join(f"{s}\n" for s in av3))
    print(f"{out}: {len(av2)} + {len(av3)} subjects, "
          f"{len(contrasts(which))} contrasts")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("which", choices=["specs", "seeded", "random", "seeded-random"])
    ap.add_argument("--drop", nargs="*", default=[],
                    help="seeded subjects to leave out, such as those where "
                         "an arm holds 3 or fewer members")
    ap.add_argument("--sampled", nargs="*", default=[],
                    help="seeded subjects to sample at 100 draws an arm")
    ap.add_argument("--av2", nargs="*", default=None,
                    help="seeded subjects for av2; the rest go to av3 "
                         "(default: alternate subjects in sorted order)")
    args = ap.parse_args(argv)
    exp = ROOT / "experiments"
    if args.which in ("specs", "random"):
        header = ("Pooled pass on the paper's 8 arms (2026-11-25-one-way-pooled\n"
                  "make_design.py specs): base and only-<f> from sweep F, full from\n"
                  "sweep F's no-gate, no-<f> from sweep T; 30 seeds, on the 18\n"
                  "subjects of 2026-11-20-final-engine-pooled.")
        name = "2026-11-25-one-way-pooled"
        if args.which == "random":
            header = ("Pooled pass of sweep F's random/base against nsga2-apportion/base,\n"
                      "30 seeds, on the 18 subjects and word sample of\n"
                      "2026-11-25-one-way-pooled (make_design.py random).")
            name = "2026-11-25-one-way-pooled-random"
        text = design(header, "experiments/results-final-engine",
                      "experiments/results-one-way", "0-29",
                      SPEC_AV2 + SPEC_AV3, SPEC_SAMPLED, args.which)
        write(exp / name, text, SPEC_AV2, SPEC_AV3, args.which)
        return 0
    if args.which == "seeded-random":
        seeded = exp / "2026-11-25-one-way-seeded-pooled"
        lists = [seeded / f"subjects-{h}.txt" for h in ("av2", "av3")]
        if not all(f.is_file() for f in lists):
            sys.exit(f"make_design: no {lists[0]} or {lists[1]}; copy them from a host")
        av2, av3 = ([ln.strip() for ln in f.read_text().splitlines()
                     if ln.strip() and ln.strip() not in set(args.drop)]
                    for f in lists)
        unknown = set(args.drop) - set(av2 + av3) - set(
            ln.strip() for f in lists for ln in f.read_text().splitlines())
        if unknown:
            sys.exit(f"make_design: not in the seeded pass: {sorted(unknown)}")
        text = design(
            "Pooled pass of sweep F's random/base (2026-11-27-seeded-random) against\n"
            "nsga2-apportion/base (sweep F), seeds 0-4, on the subjects, host split\n"
            "and word sample of 2026-11-25-one-way-seeded-pooled\n"
            "(make_design.py seeded-random).",
            "experiments/results-final-engine-seeded",
            "experiments/results-final-engine-seeded-random", "0-4",
            sorted(av2 + av3), [], args.which)
        write(exp / "2026-11-27-seeded-random-pooled", text, av2, av3, args.which)
        return 0
    listing = exp / "2026-11-18-seeded-corpus-sideways" / "selected.txt"
    if not listing.is_file():
        sys.exit(f"make_design: no {listing}; copy the sideways corpus in")
    names = sorted("sw-" + ln.strip() for ln in listing.read_text().splitlines()
                   if ln.strip() and not ln.startswith("#"))
    unknown = set(args.drop + args.sampled + (args.av2 or [])) - set(names)
    if unknown:
        sys.exit(f"make_design: not in the corpus: {sorted(unknown)}")
    names = [n for n in names if n not in set(args.drop)]
    av2 = (sorted(set(args.av2) & set(names)) if args.av2 is not None
           else names[0::2])
    av3 = [n for n in names if n not in set(av2)]
    text = design(
        "Pooled pass over the sideways seeded corpus on the paper's 8 arms\n"
        "(2026-11-25-one-way-pooled make_design.py seeded): base and only-<f>\n"
        "from sweep F, full from sweep F's no-gate, no-<f> from sweep T;\n"
        "seeds 0-4.",
        "experiments/results-final-engine-seeded",
        "experiments/results-one-way-seeded", "0-4", names,
        sorted(set(args.sampled)))
    write(exp / "2026-11-25-one-way-seeded-pooled", text, av2, av3)
    return 0


if __name__ == "__main__":
    sys.exit(main())
