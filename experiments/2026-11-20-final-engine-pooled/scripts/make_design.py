#!/usr/bin/env python3
"""Write the design and subject lists of the final engine's two pooled passes.

    python3 experiments/2026-11-20-final-engine-pooled/scripts/make_design.py specs
    python3 experiments/2026-11-20-final-engine-pooled/scripts/make_design.py seeded \
        [--drop SUBJECT ...] [--sampled SUBJECT ...] [--av2 SUBJECT ...]
    python3 experiments/2026-11-20-final-engine-pooled/scripts/make_design.py random

`specs` writes experiments/2026-11-20-final-engine-pooled/{design.toml,
subjects-av2.txt,subjects-av3.txt}; `seeded` writes the same files under
experiments/2026-11-20-final-engine-seeded-pooled/; `random` writes them
under experiments/2026-11-23-final-engine-pooled-random/. All are untracked, like
every pooled pass's inputs, and copied to each host by rsync; this script is
the tracked record of what they hold.

Arms are sweep F's base, full, every no-<f> and every only-<f>. Families:
"base" holds full, every only-<f> and no-climb against base; "ablation" holds
each no-<f> against full; every other pair of the ten arms is in "joint", so
every cross-arm pair is planned and joint_measures.py can decide the joint
front over all ten arms.

`random` pools sweep F's random/base and nsga2-apportion/base alone, on the
18 subjects and host split of `specs`, with the one contrast random against
base. Strength and pooled balls read one pair of arms and share one arm's
frontier, so on the specs pass's word sample (copied into its fp directory
before enqueue) they equal what an eleventh arm in that pass would give.
Joint-front reach does not: its front is that of two arms.
"""

import argparse
import itertools
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
FEATURES = ["climb", "gate", "remove", "novelty"]
ARMS = (["base", "full"] + [f"no-{f}" for f in FEATURES]
        + [f"only-{f}" for f in FEATURES])
RUN_DIR = "sweep_F_{level}_{scheme}_wkoff_log_{subject}_seed{seed:02d}"

# The 18 subjects of 2026-11-19-fretish-exchange-pooled: the small set less
# fsm-lmcps (where base found no repair), and rad's 10 cores, with its host
# split and its two sampled subjects.
SPEC_AV2 = ["fsm", "fsm-timing", "fsm-combined", "mode-arbiter", "takeoff",
            "liquid-mixer", "rad-core-1-18", "rad-core-10", "rad-core-12-18",
            "rad-core-17-18", "rad-core-45", "rad-core-55", "rad-core-61",
            "rad-core-65", "rad-core-68"]
SPEC_AV3 = ["rad-core-33", "valu3s-uc6", "lift-plus-cruise-mini"]
SPEC_SAMPLED = ["valu3s-uc6", "lift-plus-cruise-mini"]


def arm(level: str) -> str:
    return level if "/" in level else f"nsga2-apportion/{level}"


def contrasts(which: str = "specs") -> list:
    if which == "random":
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


def design(header: str, results: str, seeds: str, subjects: list,
           sampled: list, which: str = "specs") -> str:
    lines = [f"# {line}".rstrip() for line in header.splitlines()]
    lines += ["", "draws = 100", "", "[[groups]]", f'results = "{results}"',
              f'run_dir = "{RUN_DIR}"', f'seeds = "{seeds}"',
              'members = "output"', "subjects = ["]
    lines += [f'  "{s}",' for s in subjects]
    lines += ["]", "sampled = [" + ", ".join(f'"{s}"' for s in sampled) + "]",
              "contrasts = ["]
    lines += [f'  {{ name = "{n}", a = "{arm(a)}", b = "{arm(b)}", '
              f'family = "{fam}" }},' for n, a, b, fam in contrasts(which)]
    lines += ["]", ""]
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
    ap.add_argument("which", choices=["specs", "seeded", "random"])
    ap.add_argument("--drop", nargs="*", default=[],
                    help="seeded subjects to leave out, such as those where "
                         "an arm holds 3 or fewer members")
    ap.add_argument("--sampled", nargs="*", default=[],
                    help="seeded subjects to sample at 100 draws an arm")
    ap.add_argument("--av2", nargs="*", default=None,
                    help="seeded subjects for av2; the rest go to av3 "
                         "(default: alternate subjects in sorted order)")
    args = ap.parse_args(argv)
    if args.which == "specs":
        text = design(
            "Pooled pass over 2026-11-20-final-engine (profile final-engine):\n"
            "sweep F's ten nsga2-apportion arms, 30 seeds, on the 18 subjects\n"
            "of 2026-11-19-fretish-exchange-pooled. Written by\n"
            "scripts/make_design.py specs; see PLAN.md.",
            "experiments/results-final-engine", "0-29",
            SPEC_AV2 + SPEC_AV3, SPEC_SAMPLED)
        write(ROOT / "experiments" / "2026-11-20-final-engine-pooled", text,
              SPEC_AV2, SPEC_AV3)
        return 0
    if args.which == "random":
        text = design(
            "Pooled pass over 2026-11-20-final-engine (profile final-engine):\n"
            "sweep F's random/base against nsga2-apportion/base, 30 seeds, on\n"
            "the 18 subjects of 2026-11-20-final-engine-pooled, on its word\n"
            "sample. Written by scripts/make_design.py random; see PLAN.md.",
            "experiments/results-final-engine", "0-29",
            SPEC_AV2 + SPEC_AV3, SPEC_SAMPLED, "random")
        write(ROOT / "experiments" / "2026-11-23-final-engine-pooled-random",
              text, SPEC_AV2, SPEC_AV3, "random")
        return 0
    listing = (ROOT / "experiments" / "2026-11-18-seeded-corpus-sideways"
               / "selected.txt")
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
        "Pooled pass over the sideways seeded corpus of 2026-11-20-final-engine\n"
        "(profile final-engine-seeded): sweep F's ten nsga2-apportion arms,\n"
        "seeds 0-4. Written by\n"
        "experiments/2026-11-20-final-engine-pooled/scripts/make_design.py\n"
        "seeded; see PLAN.md.",
        "experiments/results-final-engine-seeded", "0-4", names,
        sorted(set(args.sampled)))
    write(ROOT / "experiments" / "2026-11-20-final-engine-seeded-pooled", text,
          av2, av3)
    return 0


if __name__ == "__main__":
    sys.exit(main())
