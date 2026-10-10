#!/usr/bin/env python3
"""Reduce the adaptive smoke test: search outcomes and cross-arm strength.

    python3 reduce.py RESULTS.csv [SUBSUMPTION.csv]

Search outcomes come from the runner's CSV. Strength comes from the
compare_pairs.py output over plan_pairs.py's plan: within one (subject, seed),
arm A *dominates* a repair b of arm B when some repair of A strictly implies
it. The unit's net is the share of B's repairs A dominates less the share of
A's that B dominates, so a positive net reads A stronger. A pair with an
undecided direction dominates nothing.

Stdlib only.
"""

import csv
import statistics
import sys
from collections import defaultdict
from math import comb

ARMS = ("directed", "uniform", "adaptive")


def sign_p(wins: int, losses: int) -> float:
    n, k = wins + losses, min(wins, losses)
    if n == 0:
        return 1.0
    return min(1.0, 2 * sum(comb(n, i) for i in range(k + 1)) / 2 ** n)


def search(path: str) -> None:
    rows = list(csv.DictReader(open(path, newline="")))
    by = {(r["spec"], int(r["seed"]), r["level_name"]): r for r in rows}
    subjects = sorted({r["spec"] for r in rows})
    stamps = sorted({(r["commit"], r["dirty"]) for r in rows})
    print(f"rows {len(rows)}; commit/dirty {stamps}")
    print("\nPer arm: found, median repairs a run, median and total wall, "
          "implies_ideal, killed")
    for arm in ARMS:
        mine = [r for r in rows if r["level_name"] == arm]
        if not mine:
            continue
        wall = [float(r["wall_time_s"]) for r in mine]
        print(f"  {arm:9s} found {sum(r['found_repair'] == '1' for r in mine)}"
              f"/{len(mine)}, repairs "
              f"{statistics.median(int(r['n_repairs']) for r in mine):g}, "
              f"wall median {statistics.median(wall):.1f} s total "
              f"{sum(wall) / 3600:.2f} h, implies_ideal "
              f"{sum(r['implies_ideal'] == '1' for r in mine)}, killed "
              f"{sum(r['timed_out'] == '1' for r in mine)}")
    print("\nPer subject: found / median repairs / median wall s / "
          "implies_ideal, by arm")
    for subject in subjects:
        cells = []
        for arm in ARMS:
            mine = [r for r in rows
                    if r["spec"] == subject and r["level_name"] == arm]
            if not mine:
                continue
            cells.append(
                f"{arm} {sum(r['found_repair'] == '1' for r in mine)}/"
                f"{len(mine)} "
                f"{statistics.median(int(r['n_repairs']) for r in mine):g} "
                f"{statistics.median(float(r['wall_time_s']) for r in mine):.0f} "
                f"{sum(r['implies_ideal'] == '1' for r in mine)}")
        print(f"  {subject:15s} " + " | ".join(cells))
    print("\nbest_relation to the ideals, by arm")
    for arm in ARMS:
        tally: dict = defaultdict(int)
        for r in rows:
            if r["level_name"] == arm:
                tally[r["best_relation"]] += 1
        print(f"  {arm:9s} {dict(sorted(tally.items()))}")
    print("\nPaired wall time, adaptive less the other arm, over (subject, "
          "seed)")
    for other in ("directed", "uniform"):
        diffs = [float(by[(s, d, "adaptive")]["wall_time_s"]) -
                 float(by[(s, d, other)]["wall_time_s"])
                 for (s, d, a) in by
                 if a == "adaptive" and (s, d, other) in by]
        if diffs:
            print(f"  vs {other}: n {len(diffs)}, median "
                  f"{statistics.median(diffs):+.2f} s, adaptive faster on "
                  f"{sum(x < 0 for x in diffs)}")


def strength(path: str) -> None:
    pairs: dict = defaultdict(list)
    undecided = errors = total = 0
    for r in csv.DictReader(open(path, newline="")):
        subject, seed, first, second, i, j = r["id"].split("|")
        fwd, rev = r["a_implies_b"], r["b_implies_a"]
        total += 1
        undecided += "?" in (fwd, rev)
        errors += r["relation"] == "error"
        pairs[(first, second, subject, int(seed))].append(
            (int(i), int(j), fwd, rev))
    print(f"\nStrength: {total} pairs, {undecided} with an undecided "
          f"direction, {errors} errors")
    contrasts = sorted({(a, b) for a, b, _, _ in pairs})
    for first, second in contrasts:
        per_subject: dict = defaultdict(list)
        for (a, b, subject, seed), cells in pairs.items():
            if (a, b) != (first, second):
                continue
            n_a = len({i for i, _, _, _ in cells})
            n_b = len({j for _, j, _, _ in cells})
            dominated_b = {j for _, j, fwd, rev in cells
                           if fwd == "1" and rev == "0"}
            dominated_a = {i for i, _, fwd, rev in cells
                           if rev == "1" and fwd == "0"}
            per_subject[subject].append(
                (len(dominated_b) / n_b, len(dominated_a) / n_a))
        print(f"\n  {first} against {second}: share of {second}'s repairs "
              f"{first} dominates, the reverse, and the net (positive reads "
              f"{first} stronger)")
        wins = losses = 0
        unit_wins = unit_losses = 0
        for subject in sorted(per_subject):
            units = per_subject[subject]
            fwd = statistics.mean(u[0] for u in units)
            rev = statistics.mean(u[1] for u in units)
            pos = sum(u[0] > u[1] for u in units)
            neg = sum(u[0] < u[1] for u in units)
            unit_wins += pos
            unit_losses += neg
            wins += fwd > rev
            losses += fwd < rev
            print(f"    {subject:15s} n {len(units):2d}  {fwd:.3f}  {rev:.3f}  "
                  f"net {fwd - rev:+.3f}  seeds +{pos}/-{neg}")
        print(f"    subjects: {first} stronger on {wins}, weaker on {losses}, "
              f"sign p = {sign_p(wins, losses):.4f}; units +{unit_wins}/"
              f"-{unit_losses}, sign p = {sign_p(unit_wins, unit_losses):.4g}")


if __name__ == "__main__":
    search(sys.argv[1])
    if len(sys.argv) > 2:
        strength(sys.argv[2])
