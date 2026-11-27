#!/usr/bin/env python3
"""Write the farms, designs and subject lists of the two passes that bring
2026-11-27-seeded-random-extra into the seeded pooled measures.

    python3 experiments/2026-11-27-seeded-random-extra/scripts/make_design.py count
    python3 experiments/2026-11-27-seeded-random-extra/scripts/make_design.py write

The seeded corpus's pooled passes differ in their subjects without these
runs: the 8 Pareto arms pool paper-v2 m01, m02 and m07 at seeds 0-29
(2026-11-26-extra-seeded-pooled), but random ran them at seeds 0-4 only, and
random holds 1 repair on m09 at seeds 0-4, which the Pareto arms pool there.
The user asked for no difference (2026-11-27): random runs at seeds 5-29 on
the four mutants, and every arm at seeds 5-29 on m09, so m09 is pooled at
seeds 0-29 in both passes, as the extra-seeds mutants are.

`count` prints every arm's members (repair_N.json files) over seeds 0-29 on
each mutant and the join rule's verdict: at least 4 members in every arm. A
mutant where random fails it leaves both passes (user, 2026-11-27).

`write` builds experiments/results-seeded-random-extra-farm, a farm of
symbolic links so one design source reads each run from its campaign:
seeds 0-4 from sweeps F and T as the seeded pass read them (random from
2026-11-27-seeded-random), seeds 5-29 from 2026-11-26-extra-seeds (the Pareto
arms on m01, m02, m07) or this campaign. It then writes two passes on av2:

- 2026-11-27-extra-m09-pooled (of 2026-11-25-one-way-seeded-pooled): m09 on
  the paper's 8 arms at seeds 0-29, drawing its own words. Its rows replace
  m09's in the seeded pass at import.
- 2026-11-27-extra-seeded-random-pooled (of 2026-11-27-seeded-random-pooled):
  random/base against base on the four mutants at seeds 0-29, on the 8-arm
  passes' words (m01, m02, m07 from extra-seeded-pooled/fp, m09 from
  extra-m09-pooled/fp), copied in before enqueue. Its rows join the seeded
  random pass at import.

Every output is untracked, like every pooled pass's inputs; this script is
the record.
"""

import argparse
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EXP = ROOT / "experiments"
_spec = importlib.util.spec_from_file_location(
    "extra_design", EXP / "2026-11-26-extra-pooled" / "scripts" / "make_design.py")
EXTRA = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(EXTRA)
PARETO, RANDOM, run_name = EXTRA.PARETO, EXTRA.RANDOM, EXTRA.run_name

M09 = "sw-fsm-lmcps__paper-v2__m09"
EXTRA_MUTANTS = [f"sw-fsm-lmcps__paper-v2__m{m:02d}" for m in (1, 2, 7)]
MUTANTS = EXTRA_MUTANTS + [M09]
SEEDS = range(30)
BASE = "nsga2-apportion/base"
FARM = "results-seeded-random-extra-farm"


def source(arm: str, subject: str, seed: int) -> Path:
    if arm == RANDOM:
        results = ("results-final-engine-seeded-random" if seed < 5
                   else "results-seeded-random-extra")
    elif seed >= 5:
        results = ("results-seeded-m09-extra" if subject == M09
                   else "results-extra-seeds-seeded")
    elif "/no-" in arm:
        results = "results-one-way-seeded"
    else:
        results = "results-final-engine-seeded"
    return EXP / results / run_name(arm, subject, seed)


def arms_of(subject: str) -> list:
    return PARETO + [RANDOM] if subject == M09 else [BASE, RANDOM]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("which", choices=["count", "write"])
    args = ap.parse_args(argv)
    passed = {}
    for m in MUTANTS:
        counts = {a: (sum(EXTRA.members(source(a, m, s)) for s in SEEDS),
                      sum(1 for s in SEEDS if EXTRA.members(source(a, m, s))))
                  for a in arms_of(m)}
        passed[m] = all(n >= 4 for n, _ in counts.values())
        print(f"{m}: {'joins' if passed[m] else 'stays out of both passes'}")
        for arm, (n, k) in counts.items():
            print(f"  {arm:30} {n:5} members from {k:3} seeds")
    if args.which == "count":
        return 0
    EXTRA.farm(FARM, {run_name(a, m, s): source(a, m, s)
                      for m in MUTANTS for a in arms_of(m) for s in SEEDS})
    results = f"experiments/{FARM}"
    if passed[M09]:
        EXTRA.write_pass(
            "2026-11-27-extra-m09-pooled",
            "Pooled pass over the seeded mutant paper-v2 m09 at seeds 0-29 on the\n"
            "paper's 8 arms (2026-11-27-seeded-random-extra make_design.py), so\n"
            "the Pareto arms and random pool it at the same seeds.",
            [EXTRA.group(results, "0-29", [M09], [], "seeded", results)], [M09], [])
    joined = [m for m in MUTANTS if passed[m]]
    if joined:
        EXTRA.write_pass(
            "2026-11-27-extra-seeded-random-pooled",
            "Pooled pass of random/base against nsga2-apportion/base on the seeded\n"
            "fsm-lmcps mutants paper-v2 m01, m02, m07 and m09 at seeds 0-29, on\n"
            "the words of the 8-arm passes (2026-11-27-seeded-random-extra\n"
            "make_design.py).",
            [EXTRA.group(results, "0-29", joined, [], "random", "")], joined, [])
    return 0


if __name__ == "__main__":
    sys.exit(main())
