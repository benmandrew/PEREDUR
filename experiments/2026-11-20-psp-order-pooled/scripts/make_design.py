#!/usr/bin/env python3
"""Write the design and subject lists of the PSP campaign's pooled pass.

    python3 experiments/2026-11-20-psp-order-pooled/scripts/make_design.py \
        [--drop SUBJECT ...] [--sampled SUBJECT ...] [--av2 SUBJECT ...]

Writes experiments/2026-11-20-psp-order-pooled/{design.toml,subjects-av2.txt,
subjects-av3.txt}. All three are untracked, like every pooled pass's inputs,
and are copied to each host by rsync; this script is the tracked record of
what they hold.

The design has two groups, one per profile of 2026-11-20-psp-order: the
specifications (psp-order: the hand-written subjects and the unrealisable
SYNTECH17 imports, seeds 0-29) and the seeded corpus (psp-order-seeded,
seeds 0-4). Subjects come from the registries in scripts/run_experiments.py,
so a subject added there joins the pass. Arms are sweep F's base, full and
every no-<f>. Families: "base" holds full and no-climb against base;
"ablation" holds each no-<f> against full; every other pair of the six arms
is in "joint", so every cross-arm pair is planned and joint_measures.py can
decide the joint front over all six arms.

After pooling, rerun with --drop for any subject where an arm holds 3 or
fewer members (or none: a SYNTECH17 import where no arm finds a repair), and
with --sampled for any pool too large to compare exhaustively.
"""

import argparse
import itertools
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "experiments" / "2026-11-20-psp-order-pooled"
sys.path.insert(0, str(ROOT / "scripts"))
import run_experiments as R  # noqa: E402

FEATURES = ["climb", "gate", "remove", "novelty"]
ARMS = list(R.PSP_ORDER_ARMS)
RUN_DIR = "sweep_F_{level}_{scheme}_wkoff_log_{subject}_seed{seed:02d}"


def arm(level: str) -> str:
    return f"nsga2-apportion/{level}"


def contrasts() -> list:
    out = [("full", "full", "base", "base"),
           ("no-climb-over-base", "no-climb", "base", "base")]
    out += [(f"no-{f}", f"no-{f}", "full", "ablation") for f in FEATURES]
    named = {frozenset((a, b)) for _, a, b, _ in out}
    for a, b in itertools.combinations(ARMS, 2):
        if frozenset((a, b)) not in named:
            out.append((f"{a}-over-{b}", a, b, "joint"))
    return out


def group(profile: str, subjects: list, sampled: list) -> list:
    prof = R.PROFILES[profile]
    seeds = prof["seeds"]
    assert seeds == list(range(seeds[0], seeds[-1] + 1)), profile
    results = prof["results_dir"].relative_to(ROOT).as_posix()
    lines = ["", f"# Profile {profile}.", "[[groups]]", f'results = "{results}"',
             f'run_dir = "{RUN_DIR}"', f'seeds = "{seeds[0]}-{seeds[-1]}"',
             'members = "output"', "subjects = ["]
    lines += [f'  "{s}",' for s in subjects]
    lines += ["]", "sampled = [" + ", ".join(f'"{s}"' for s in sampled) + "]",
              "contrasts = ["]
    lines += [f'  {{ name = "{n}", a = "{arm(a)}", b = "{arm(b)}", '
              f'family = "{fam}" }},' for n, a, b, fam in contrasts()]
    lines += ["]"]
    return lines


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--drop", nargs="*", default=[],
                    help="subjects to leave out, such as those where an arm "
                         "holds 3 or fewer members")
    ap.add_argument("--sampled", nargs="*", default=[],
                    help="subjects to sample at 100 draws an arm")
    ap.add_argument("--av2", nargs="*", default=None,
                    help="subjects for av2; the rest go to av3 (default: "
                         "alternate subjects in sorted order, per group)")
    args = ap.parse_args(argv)
    groups = {"psp-order": list(R.PROFILES["psp-order"]["specs"]),
              "psp-order-seeded": list(R.PROFILES["psp-order-seeded"]["specs"])}
    every = {s for names in groups.values() for s in names}
    unknown = set(args.drop + args.sampled + (args.av2 or [])) - every
    if unknown:
        sys.exit(f"make_design: not a subject of the campaign: {sorted(unknown)}")
    lines = ["# Pooled pass over 2026-11-20-psp-order: sweep F's base, full and",
             "# every no-<f> under nsga2-apportion. Written by",
             "# experiments/2026-11-20-psp-order-pooled/scripts/make_design.py;",
             "# see 2026-11-20-psp-order/PLAN.md.", "", "draws = 100"]
    av2, av3 = [], []
    for profile, names in groups.items():
        names = sorted(n for n in names if n not in set(args.drop))
        if not names:
            continue
        lines += group(profile, names, sorted(set(args.sampled) & set(names)))
        mine = (sorted(set(args.av2) & set(names)) if args.av2 is not None
                else names[0::2])
        av2 += mine
        av3 += [n for n in names if n not in set(mine)]
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "design.toml").write_text("\n".join(lines) + "\n")
    (OUT / "subjects-av2.txt").write_text("".join(f"{s}\n" for s in av2))
    (OUT / "subjects-av3.txt").write_text("".join(f"{s}\n" for s in av3))
    print(f"{OUT}: {len(av2)} + {len(av3)} subjects, "
          f"{len(contrasts())} contrasts a subject")
    return 0


if __name__ == "__main__":
    sys.exit(main())
