#!/usr/bin/env python3
"""humanoid-742's strength row as a sampled estimate, as the archive read it.

    python3 sample_742.py plan                  # pair list, draws, bundle
    python3 sample_742.py score RESULTS.csv...  # estimates, sampled.json

The exact pass needs the pooled frontier of every side. av1 walked two of
them (data/humanoid-742-frontiers.json: Pareto, 1693 classes, and the ladder,
937), and the third, mrs-weighted's 9480-class pool, was cancelled 19 h into
its walk. This is the 2026-10-06-pooled-rq2 sampling (pooled.py sample-plan
and sample-score) over what is known:

- A side whose frontier is known is sampled from that frontier, so each draw
  is one class and carries weight 1.
- mrs-weighted is sampled from its pool. A draw is compared with every other
  pool class that can imply it, which says whether it is on the frontier and
  how many pool classes equal it; a frontier draw weighs 1 / class size, so
  the ratio estimate is a share of classes.
- A draw is subsumed when some class of the other side strictly implies it.
  Against a known frontier only frontier classes are asked, since a pool class
  implying the draw means a frontier class does.

Only a class whose print is a subset of the draw's can imply it, so only those
pairs are compared. An undecided pair within a side reads as no implication,
as in `maximal`; across sides it is read both ways (non, imp), as in
pooled_frontier.py score. Run from the checkout root.
"""
import csv
import hashlib
import io
import json
import os
import random
import sys
import tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), "data")
WORK = os.path.join("experiments", "analysis-tlsf-rerun-work")
SPEC = "humanoid-742"
FAM = f"{WORK}/strength/{SPEC}"
HOST = "av3"
P, W, L = "mrs-nsga2-apportion", "mrs-weighted", "aurus-nsga2-apportion"
COMPS = {"selection": (P, W), "grading": (P, L)}
# Draws per side. A known frontier's share is a plain proportion, whose 95%
# half-width at 100 draws is at most 0.0985. The pool side gets twice as many,
# since only its frontier draws count.
DRAWS = {P: 100, L: 100, W: 200}
HALF = 0.10
FLIP = {"weaker": "stronger", "stronger": "weaker"}


def load():
    sides, prints = {}, {}
    with open(f"{FAM}/classes.csv", newline="") as fh:
        for r in csv.DictReader(fh):
            sides[r["md5"]] = set(r["sides"].split(";"))
    with open(f"{FAM}/prints.tsv") as fh:
        for line in fh:
            md5, _, hexed = line.rstrip("\n").partition("\t")
            prints[md5] = int(hexed, 16)
    with open(f"{DATA}/humanoid-742-frontiers.json") as fh:
        fronts = {s: v["members"] for s, v in json.load(fh).items()}
    pool = {s: sorted(m for m, ss in sides.items() if s in ss) for s in (P, W, L)}
    for s, members in fronts.items():
        assert set(members) <= set(pool[s]), s
    return pool, fronts, prints


def draws_of(pool: dict, fronts: dict) -> dict:
    """A seeded permutation's prefix, so a longer sample extends this one."""
    out = {}
    for side, n in DRAWS.items():
        base = sorted(fronts.get(side, pool[side]))
        seed = sum(map(ord, f"{SPEC}|{side}")) + 20261010
        out[side] = random.Random(seed).sample(base, len(base))[:n]
    return out


def targets(pool: dict, fronts: dict) -> dict:
    """side -> [(role, classes a draw of that side is compared with)]."""
    return {P: [(W, pool[W]), (L, fronts[L])],
            L: [(P, fronts[P])],
            W: [(W, pool[W]), (P, fronts[P])]}


def path(md5: str) -> str:
    return f"{FAM}/classes/{md5}.tlsf"


def plan() -> int:
    pool, fronts, prints = load()
    draws = draws_of(pool, fronts)
    seen, rows, per = set(), [], {}
    for side, others in targets(pool, fronts).items():
        for x in draws[side]:
            px = prints[x]
            for role, ys in others:
                for y in ys:
                    if y == x or prints[y] & ~px:
                        continue
                    per[(side, role)] = per.get((side, role), 0) + 1
                    if (x, y) in seen or (y, x) in seen:
                        continue
                    seen.add((x, y))
                    rows.append([f"sample|{SPEC}|{x}|{y}", path(x), path(y)])
    os.makedirs(f"{WORK}/sample", exist_ok=True)
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(["id", "a_path", "b_path"])
    w.writerows(rows)
    with open(f"{WORK}/sample/pairs-{HOST}.csv", "w") as fh:
        fh.write(buf.getvalue())
    with open(f"{WORK}/sample/draws.json", "w") as fh:
        json.dump(draws, fh, indent=1)
    for (side, role), n in sorted(per.items()):
        print(f"{side} draws ({len(draws[side])}) against {role}: {n} pairs")
    print(f"{len(rows)} distinct pairs in {WORK}/sample/pairs-{HOST}.csv")

    bundle = f"{DATA}/sample.tar.xz"
    with tarfile.open(bundle, "w:xz", preset=6) as tar:
        for name in (f"sample/pairs-{HOST}.csv", "sample/draws.json"):
            with open(f"{WORK}/{name}", "rb") as fh:
                blob = fh.read()
            ti = tarfile.TarInfo(name)
            ti.size, ti.mode = len(blob), 0o644
            tar.addfile(ti, io.BytesIO(blob))
    with open(f"{DATA}/bundles.json") as fh:
        bundles = json.load(fh)
    with open(bundle, "rb") as fh:
        bundles["sample.tar.xz"] = hashlib.sha256(fh.read()).hexdigest()
    with open(f"{DATA}/bundles.json", "w") as fh:
        json.dump(bundles, fh, indent=1, sort_keys=True)
    return 0


def ratio(y: list, wt: list) -> tuple:
    """Ratio estimate sum(y)/sum(wt) and its 95% half-width (linearised)."""
    n, sw = len(wt), sum(wt)
    if sw == 0 or n < 2:
        return float("nan"), float("inf")
    r = sum(y) / sw
    d = [a - r * b for a, b in zip(y, wt)]
    mean = sum(d) / n
    var = sum((v - mean) ** 2 for v in d) / (n - 1)
    return r, 1.96 * (var / n) ** 0.5 / (sw / n)


def score(results: list) -> int:
    pool, fronts, _ = load()
    with open(f"{WORK}/sample/draws.json") as fh:
        draws = json.load(fh)
    rel = {}
    for res in results:
        with open(res, newline="") as fh:
            for r in csv.DictReader(fh):
                _, _, x, y = r["id"].split("|")
                rel[(x, y)] = r["relation"]
                rel[(y, x)] = FLIP.get(r["relation"], r["relation"])

    # The planned pairs are the candidates, not a second pass over prints.tsv:
    # the plan read the 65,536-word prints, and unpack.py leaves the bundle's
    # 4096-word copy in their place, which refutes a third as many pairs.
    planned = set()
    with open(f"{WORK}/sample/pairs-{HOST}.csv", newline="") as fh:
        for r in csv.DictReader(fh):
            _, _, x, y = r["id"].split("|")
            planned.update(((x, y), (y, x)))
    missing = 0

    def ask(x, ys):
        """Relations of draw x to each class of ys that can imply it."""
        nonlocal missing
        out = []
        for y in ys:
            if (x, y) not in planned:
                continue
            if (x, y) not in rel:
                missing += 1
                continue
            out.append(rel[(x, y)])
        return out

    # side -> [(on frontier, class size, {other: {non, imp}}, undecided)]
    st = {}
    tg = targets(pool, fronts)
    for side in (P, L, W):
        for x in draws[side]:
            front, size, undec, sub = True, 1, 0, {}
            for role, ys in tg[side]:
                rs = ask(x, ys)
                if role == side:
                    front = "weaker" not in rs
                    size += rs.count("equivalent")
                    continue
                open_ = sum(r in ("undecided", "error") for r in rs)
                undec += open_
                sub[role] = {"non": "weaker" in rs, "imp": "weaker" in rs or open_ > 0}
            st.setdefault(side, []).append((front, size, sub, undec))
    if missing:
        print(f"{missing} planned pair(s) have no result yet", file=sys.stderr)
        return 1

    out, converged = {}, True
    for comp, (a, b) in COMPS.items():
        est = {}
        for side, other in ((a, b), (b, a)):
            wt = [f / s for f, s, _, _ in st[side]]
            for k in ("non", "imp"):
                y = [(f / s) * sb[other][k] for f, s, sb, _ in st[side]]
                est[(side, k)] = ratio(y, wt)
                converged &= est[(side, k)][1] <= HALF
        row = {"A": a, "B": b, "draws_A": len(st[a]), "draws_B": len(st[b]),
               "frontier_draws_A": sum(f for f, *_ in st[a]), "frontier_draws_B": sum(f for f, *_ in st[b]),
               "undecided": sum(u for *_, u in st[a]) + sum(u for *_, u in st[b])}
        for k in ("non", "imp"):
            (ra, ha), (rb, hb) = est[(a, k)], est[(b, k)]
            row.update({f"subA_{k}": round(ra, 4), f"subA_{k}_hw": round(ha, 4),
                        f"subB_{k}": round(rb, 4), f"subB_{k}_hw": round(hb, 4),
                        f"net_{k}": round(rb - ra, 4)})
        out[comp] = row
        print(f"{comp} {SPEC}: sub A {row['subA_non']:.3f} ±{row['subA_non_hw']:.3f}, "
              f"sub B {row['subB_non']:.3f} ±{row['subB_non_hw']:.3f}, net non {row['net_non']:+.4f} "
              f"imp {row['net_imp']:+.4f}, undecided {row['undecided']}")
    doc = {"spec": SPEC, "pairs": len(rel) // 2, "half_width_bound": HALF, "converged": converged,
           "comparisons": out}
    with open(f"{FAM}/sampled.json", "w") as fh:
        json.dump(doc, fh, indent=1)
    print("converged" if converged else f"open: a half-width exceeds {HALF}; raise DRAWS and plan again")
    return 0


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "plan":
        sys.exit(plan())
    if len(sys.argv) >= 3 and sys.argv[1] == "score":
        sys.exit(score(sys.argv[2:]))
    sys.exit(__doc__)
