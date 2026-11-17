#!/usr/bin/env python3
"""Accepted-trace share and joint-front reach for a pooled joint pass.

    python3 scripts/joint_measures.py DESIGN.toml MEMBERS_DIR COLLECTED_DIR OUT_DIR

Measures 1 and 3 of experiments/2026-11-16-fretish-order-pooled-joint/PLAN.md.
Inputs are those of pooled_reduce.py. Frontier classes are pooled_reduce's,
with an undecided direction read as no implication.

1. A node's accepted-trace share is the share of the subject's sampled words
   its fingerprint accepts. An arm's score on a subject is the median share
   over its frontier classes; lower is stronger. For a sampled subject the
   frontier is estimated by the drawn nodes that no node of their own arm
   strictly implies.
2. The joint front of a subject is the set of frontier classes of all arms,
   merged across arms by equivalence, that no class of another arm strictly
   implies. An arm's reach is the share of the joint front for which it holds
   an equivalent or stronger class. For a sampled subject, each drawn frontier
   node stands in for its class, weighted by one over its class size.

Pairs of arms are compared on the paired difference over subjects with the
exact Wilcoxon signed-rank p, Holm over the pairs, and the Hodges-Lehmann
shift with its exact 95% interval. Exhaustive subjects make the main result;
sampled subjects are reported apart.
"""

import argparse
import collections
import csv
import itertools
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pooled_design as D  # noqa: E402
import pooled_reduce as R  # noqa: E402


def short(arm: str) -> str:
    return arm.split("/")[-1]


def words(sdir: str) -> int:
    with open(os.path.join(sdir, "words.meta")) as h:
        return sum(1 for line in h if line.strip())


def strictly(V, y, x):
    return R.implies(V, y, x, "non") and not R.implies(V, x, y, "non")


def reaches(V, z, g):
    return z == g or R.implies(V, z, g, "non")


def exhaustive(nodes, bits, V, arms, nwords):
    fronts = {a: R.frontier(nodes, V, a) for a in arms}
    score = {a: (statistics.median(bits[c[0]].bit_count() / nwords
                                   for c in fronts[a]) if fronts[a] else None)
             for a in arms}
    reps = [(a, c[0]) for a in arms for c in fronts[a]]
    parent = list(range(len(reps)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for i, j in itertools.combinations(range(len(reps)), 2):
        (a, x), (b, y) = reps[i], reps[j]
        if a != b and (x == y or (R.implies(V, x, y, "non")
                                  and R.implies(V, y, x, "non"))):
            parent[find(i)] = find(j)
    groups = collections.defaultdict(list)
    for i in range(len(reps)):
        groups[find(i)].append(i)
    front = []
    for members in groups.values():
        hit = any(strictly(V, y, reps[m][1])
                  for m in members for (b, y) in reps if b != reps[m][0])
        if not hit:
            front.append([reps[m][1] for m in members])
    reach = {}
    for a in arms:
        held = [c[0] for c in fronts[a]]
        if not held or not front:
            reach[a] = None
            continue
        reach[a] = sum(any(reaches(V, z, g) for z in held for g in grp)
                       for grp in front) / len(front)
    return score, reach, len(front), {a: len(fronts[a]) for a in arms}


def sampled(nodes, bits, V, arms, rec, orders, nwords):
    pool = {a: [n["node"] for n in nodes if a in n["arms"]] for a in arms}
    draws = {}
    for a in arms:
        out = []
        for x in orders[a][:rec["draws"]]:
            dom = any(strictly(V, y, x) for y in pool[a] if y != x)
            if dom:
                continue
            size = 1 + sum(1 for y in pool[a] if y != x
                           and R.implies(V, x, y, "non")
                           and R.implies(V, y, x, "non"))
            out.append((x, size))
        draws[a] = out
    score = {a: (statistics.median(bits[x].bit_count() / nwords
                                   for x, _ in sorted(set(draws[a])))
                 if draws[a] else None) for a in arms}
    jf = []
    for a in arms:
        for x, size in draws[a]:
            if not any(strictly(V, y, x) for b in arms if b != a
                       for y in pool[b] if y != x):
                jf.append((x, 1 / size))
    reach = {}
    total = sum(w for _, w in jf)
    for a in arms:
        held = set(pool[a])
        reach[a] = (sum(w for x, w in jf
                        if x in held or any(R.implies(V, z, x, "non")
                                            for z in pool[a]))
                    / total) if total else None
    return score, reach, len(jf), {a: len(draws[a]) for a in arms}


def share_only(nodes, bits, V, rec, sdir, nwords):
    """Measure 1 alone: each arm's frontier from its own pairs, or from its
    drawn nodes on a sampled subject."""
    if rec["sampled"]:
        orders = R.load_orders(sdir, rec)
        pool = {a: [n["node"] for n in nodes if a in n["arms"]]
                for a in rec["arms"]}
        score, nf = {}, {}
        for a in rec["arms"]:
            front = sorted({x for x in orders[a][:rec["draws"]]
                            if not any(strictly(V, y, x)
                                       for y in pool[a] if y != x)})
            nf[a] = len(front)
            score[a] = (statistics.median(bits[x].bit_count() / nwords
                                          for x in front) if front else None)
        return score, nf
    fronts = {a: R.frontier(nodes, V, a) for a in rec["arms"]}
    return ({a: (statistics.median(bits[c[0]].bit_count() / nwords
                                   for c in f) if f else None)
             for a, f in fronts.items()},
            {a: len(f) for a, f in fronts.items()})


def hodges_lehmann(d: list) -> tuple:
    n = len(d)
    walsh = sorted((d[i] + d[j]) / 2 for i in range(n) for j in range(i, n))
    est = statistics.median(walsh)
    m = n * (n + 1) // 2
    counts = [0] * (m + 1)
    counts[0] = 1
    for r in range(1, n + 1):
        for s in range(m, r - 1, -1):
            counts[s] += counts[s - r]
    total, cum, k = 2 ** n, 0, 0
    for t in range(m + 1):
        cum += counts[t]
        if cum / total > 0.025:
            k = t
            break
    if k == 0:
        return est, float("nan"), float("nan")
    return est, walsh[k], walsh[m - 1 - k]


def compare(rows: list, arms: list, key: str, stronger_low: bool) -> list:
    pairs = list(itertools.combinations(arms, 2))
    out, ps = [], {}
    for a, b in pairs:
        diffs = []
        for r in rows:
            va, vb = r[key].get(a), r[key].get(b)
            if va is None or vb is None:
                continue
            diffs.append((vb - va) if stronger_low else (va - vb))
        p = R.wilcoxon_p(diffs) if diffs else 1.0
        ps[(a, b)] = p
        hl = hodges_lehmann(diffs) if len(diffs) >= 2 else (None, None, None)
        out.append({"a": short(a), "b": short(b), "n": len(diffs),
                    "a_wins": sum(x > 0 for x in diffs),
                    "b_wins": sum(x < 0 for x in diffs),
                    "ties": sum(x == 0 for x in diffs),
                    "hl": hl[0], "lo": hl[1], "hi": hl[2], "p": p})
    adj = R.holm(ps)
    for row, pair in zip(out, pairs):
        row["holm"] = adj[pair]
    return out


def fmt(rows: list, title: str, unit: str) -> str:
    lines = [f"== {title} (positive: a stronger; {unit})"]
    for r in rows:
        hl = ("     n/a" if r["hl"] is None else
              f"{r['hl']:+.4f} [{r['lo']:+.4f}, {r['hi']:+.4f}]")
        lines.append(f"  {r['a']:>10} vs {r['b']:<10} n={r['n']:2d} "
                     f"a>b {r['a_wins']:2d} b>a {r['b_wins']:2d} "
                     f"tie {r['ties']:2d}  HL {hl}  p={r['p']:.3g} "
                     f"Holm p={r['holm']:.3g}")
    return "\n".join(lines) + "\n"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("design")
    ap.add_argument("members")
    ap.add_argument("collected")
    ap.add_argument("out")
    ap.add_argument("--share-only", action="store_true",
                    help="measure 1 alone, for a pass whose design contrasts "
                         "only some pairs of arms, so the joint front is "
                         "not decided")
    args = ap.parse_args(argv)
    design = D.load_design(args.design)
    by = R.load_rows(args.collected)
    rows = []
    arms = None
    for name, rec in design["subjects"].items():
        nodes = R.load_nodes(args.members, name)
        sdir = R.subject_dir(args.collected, name)
        bits = R.load_bits(sdir, len(nodes))
        nwords = words(sdir)
        V, _ = R.verdicts(by.get(name, {}))
        arms = arms or list(rec["arms"])
        if args.share_only:
            score, nf = share_only(nodes, bits, V, rec, sdir, nwords)
            reach, nfront = dict.fromkeys(rec["arms"]), ""
        elif rec["sampled"]:
            orders = R.load_orders(sdir, rec)
            score, reach, nfront, nf = sampled(nodes, bits, V, rec["arms"],
                                               rec, orders, nwords)
        else:
            score, reach, nfront, nf = exhaustive(nodes, bits, V, rec["arms"],
                                                  nwords)
        rows.append({"subject": name, "sampled": rec["sampled"],
                     "words": nwords, "joint_front": nfront,
                     "score": score, "reach": reach, "front": nf})
        print(f"{name}: joint front {nfront}, "
              + " ".join(f"{short(a)}={score.get(a)}" for a in rec["arms"]),
              flush=True)
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "per-subject.csv"), "w", newline="") as h:
        w = csv.writer(h)
        w.writerow(["subject", "sampled", "words", "joint_front", "arm",
                    "frontier", "accepted_share", "reach"])
        for r in rows:
            for a in sorted(r["score"]):
                w.writerow([r["subject"], int(r["sampled"]), r["words"],
                            r["joint_front"], short(a), r["front"][a],
                            "" if r["score"][a] is None else round(r["score"][a], 6),
                            "" if r["reach"][a] is None else round(r["reach"][a], 4)])
    exh = [r for r in rows if not r["sampled"]]
    text = ""
    for label, sub in (("exhaustive subjects", exh), ("all subjects", rows)):
        text += fmt(compare(sub, arms, "score", True),
                    f"measure 1, accepted-trace share, {label}",
                    "HL in share of words")
        if args.share_only:
            continue
        text += fmt(compare(sub, arms, "reach", False),
                    f"measure 3, joint-front reach, {label}",
                    "HL in share of the joint front")
    with open(os.path.join(args.out, "summary.txt"), "w") as h:
        h.write(text)
    print(text, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
