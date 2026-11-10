#!/usr/bin/env python3
"""Reduce a pooled pass's collected fingerprints and verdicts to strength and
ball-radii tables, one row per subject and contrast.

    python3 scripts/pooled_reduce.py DESIGN.toml MEMBERS_DIR COLLECTED_DIR OUT_DIR \\
        [--subjects SUBJECT...] [--ball-k 1 3 5] [--no-balls] [--match SEED] \\
        [--pooled-balls | --pooled-only] [--pooled-draws N] [--jobs N]

Run locally after `campaign.py collect --outputs NAME`. MEMBERS_DIR is the
committed output of scripts/pool_members.py. COLLECTED_DIR is
`experiments/NAME/`, holding `<host>/fp/<subject>/` (fp.tsv, and the draw
orders of a sampled subject) and `<host>/subsumption-<host>.csv`. A subject's
files may sit under any one host.

Strength. Verdicts are per direction: a pair id `subject:i:j` carries i => j
as a_implies_b and j => i as b_implies_a, each 1, 0 or ? (undecided). A pair
the plan left out was refuted both ways by a word and reads 0. Before any
figure, each subject's rows are checked against the plan re-derived from the
design and the fingerprints: every planned pair must have a row, and no other
pair may.

Within each arm, a node is dominated when another node of the arm implies it
and is not implied back, an undecided direction reading as no implication, as
in `maximal`. The undominated nodes, merged by equivalence, are the arm's
frontier classes. sub_a is the share of arm a's classes that some class of arm
b strictly implies, sub_b the converse, and net = sub_b - sub_a, positive when
a is the stronger. Three readings of an undecided direction are reported:
`decided` drops a pair with one, `imp` reads it as an implication, `non` as
none (the unsuffixed columns are `decided`).

An exhaustive subject reads every class pair (the 2026-10-08 FRETISH
reduce.py). A sampled subject estimates each share from its draws (the
2026-10-06 pooled RQ2 pooled.py `sample-score`): a drawn node's frontier
membership and class size come from its own arm, and whether a partner repair
strictly implies it from the partner's whole pool. A frontier draw is
weighted 1/class size, so the ratio estimate is a share of classes, with a
linearised 95% half-width. Sampled rows report `imp` and `non` only.

Ball radii. Each member takes its node's fingerprint, and each run's members
form one group. For every contrast, `triple` units pair a's and b's groups at
one seed, and `xtriple` units pair them across seeds of one block of three
consecutive seeds (a at the r-th, b at the next), so no pair shares an RNG
prefix. `null` units hold three seeds of one arm, the same-arm control. A
group's region is the union of balls around its members of radius the Jaccard
distance to the K-th nearest other member, as in the 2026-10-08 fp_prc.py.

Pooled ball radii (--pooled-balls, or --pooled-only alone). A run with K or
fewer members has no radius, so an arm whose runs are small loses most of its
per-run units. Here each arm's seeds split by parity into two half-pools
of distinct nodes, all four half-pools of a contrast are subsampled to the
smallest one's size, and a's odd half meets b's even half and the reverse, so
no pair shares a seed's RNG stream. An arm's control pairs its own two halves
at the same size. Shares are averaged over --pooled-draws draws. Writes
OUT_DIR/ball-radii-pooled/pooled.csv and report-k<K>.txt.

Writes OUT_DIR/strength/{contrasts,relations}.csv and summary.txt, and, unless
--no-balls, OUT_DIR/ball-radii/prc-k<K>.csv and report-k<K>.txt. The summary
gives, per contrast, the exact Wilcoxon signed-rank test over subjects, with a
Holm correction within each contrast family.

Stdlib only.
"""

import argparse
import array
import collections
import csv
import glob
import gzip
import json
import math
import multiprocessing
import os
import random
import statistics
import sys

import pooled_design as D

MODES = ("decided", "imp", "non")


# ── inputs ──────────────────────────────────────────────────────────────────

def load_nodes(members_dir: str, subject: str) -> list:
    with gzip.open(os.path.join(members_dir, f"{subject}.jsonl.gz"), "rt") as h:
        nodes = [json.loads(line) for line in h if line.strip()]
    if [n["node"] for n in nodes] != list(range(len(nodes))):
        sys.exit(f"pooled_reduce: {subject}: nodes are not numbered 0..n-1")
    return nodes


def one(paths: list, what: str) -> str:
    if len(paths) != 1:
        sys.exit(f"pooled_reduce: expected one {what}, found {len(paths)}: "
                 f"{paths}")
    return paths[0]


def subject_dir(collected: str, subject: str) -> str:
    fp = one(glob.glob(os.path.join(collected, "*", "fp", subject, "fp.tsv")),
             f"fp.tsv for {subject}")
    return os.path.dirname(fp)


def load_bits(sdir: str, n: int) -> list:
    bits = [None] * n
    with open(os.path.join(sdir, "fp.tsv")) as h:
        for line in h:
            path, _, digits = line.rstrip("\n").partition("\t")
            bits[int(os.path.basename(path).split(".")[0])] = int(digits, 16)
    missing = [i for i, b in enumerate(bits) if b is None]
    if missing:
        sys.exit(f"pooled_reduce: {sdir}/fp.tsv lacks nodes {missing[:5]}")
    return bits


def load_orders(sdir: str, rec: dict) -> dict:
    out = {}
    for arm in rec["arms"]:
        path = os.path.join(sdir, f"order-{D.arm_slug(arm)}.txt")
        with open(path) as h:
            out[arm] = [int(x) for x in h.read().split()]
    return out


def load_rows(collected: str) -> dict:
    by = collections.defaultdict(dict)
    for path in sorted(glob.glob(os.path.join(collected, "*",
                                              "subsumption-*.csv"))):
        with open(path, newline="") as h:
            for r in csv.DictReader(h):
                subject, i, j = r["id"].rsplit(":", 2)
                by[subject][(int(i), int(j))] = r
    return by


def verdicts(rows: dict) -> tuple:
    V, secs = {}, 0.0
    for (i, j), r in rows.items():
        V[(i, j)] = r.get("a_implies_b") or "?"
        V[(j, i)] = r.get("b_implies_a") or "?"
        secs += float(r.get("secs") or 0)
    return V, secs


def check_plan(subject: str, planned: dict, rows: dict) -> int:
    extra = sum(1 for k in rows if k not in planned)
    lost = sum(1 for k in planned if k not in rows)
    if extra or lost:
        sys.exit(f"pooled_reduce: {subject}: {lost} planned pair(s) have no "
                 f"row and {extra} row(s) were never planned; collect again "
                 f"or rerun the compare phase")
    return sum(2 if d == "both" else 1 for d in planned.values())


# ── statistics ──────────────────────────────────────────────────────────────

def wilcoxon_p(diffs: list) -> float:
    """Exact two-sided signed-rank p, zeros dropped, ties at average rank.

    Ranks are doubled so a tie's average stays an integer, and the null
    distribution of the positive rank sum is counted exactly.
    """
    d = [x for x in diffs if x != 0]
    if not d:
        return 1.0
    order = sorted(range(len(d)), key=lambda k: abs(d[k]))
    r2 = [0] * len(d)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and abs(d[order[j + 1]]) == abs(d[order[i]]):
            j += 1
        for k in range(i, j + 1):
            r2[order[k]] = i + j + 2          # twice the average of ranks i+1..j+1
        i = j + 1
    w = sum(r for r, x in zip(r2, d) if x > 0)
    total = sum(r2)
    counts = [0] * (total + 1)
    counts[0] = 1
    for r in r2:
        for s in range(total, r - 1, -1):
            counts[s] += counts[s - r]
    n_all = 2 ** len(d)
    low = sum(counts[:w + 1]) / n_all
    high = sum(counts[w:]) / n_all
    return min(1.0, 2 * min(low, high))


def holm(ps: dict) -> dict:
    """Holm-adjusted p for each key of {key: p}."""
    items = sorted(ps.items(), key=lambda kv: kv[1])
    m, out, running = len(items), {}, 0.0
    for k, (key, p) in enumerate(items):
        running = max(running, min(1.0, (m - k) * p))
        out[key] = running
    return out


def ratio(y: list, wt: list) -> tuple:
    """Ratio estimate sum(y)/sum(wt) and its linearised 95% half-width."""
    if sum(wt) == 0:
        return float("nan"), float("inf")
    r = sum(y) / sum(wt)
    if len(wt) < 2:
        return r, float("inf")
    resid = [a - r * b for a, b in zip(y, wt)]
    var = statistics.variance(resid)
    return r, 1.96 * math.sqrt(var / len(wt)) / statistics.mean(wt)


# ── strength: exhaustive ────────────────────────────────────────────────────

def implies(V: dict, x: int, y: int, mode: str) -> bool:
    v = V.get((x, y), "0")
    return v == "1" or (v == "?" and mode == "imp")


def frontier(nodes: list, V: dict, arm: str) -> list:
    """Frontier classes of one arm, from the planned pairs within it."""
    ids = [n["node"] for n in nodes if arm in n["arms"]]
    held = set(ids)
    parent = {i: i for i in ids}

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    dominated = set()
    for (a, b), v in V.items():
        if a >= b or a not in held or b not in held:
            continue
        ab, ba = v == "1", V.get((b, a), "0") == "1"
        if ab and ba:
            parent[find(a)] = find(b)
        elif ab:
            dominated.add(b)
        elif ba:
            dominated.add(a)
    classes = collections.defaultdict(list)
    for i in ids:
        if i not in dominated:
            classes[find(i)].append(i)
    return sorted(sorted(v) for v in classes.values())


def classify(V: dict, x: int, y: int, mode: str) -> str:
    a, b = V.get((x, y), "0"), V.get((y, x), "0")
    if mode == "decided" and "?" in (a, b):
        return "undecided"
    t = (implies(V, x, y, mode), implies(V, y, x, mode))
    return {(True, True): "equivalent", (True, False): "a_stronger",
            (False, True): "b_stronger", (False, False): "incomparable"}[t]


def pool_counts(nodes: list, a: str, b: str) -> dict:
    return {"a_pool": sum(n["n"].get(a, 0) for n in nodes),
            "b_pool": sum(n["n"].get(b, 0) for n in nodes),
            "a_nodes": sum(a in n["arms"] for n in nodes),
            "b_nodes": sum(b in n["arms"] for n in nodes),
            "both_nodes": sum(a in n["arms"] and b in n["arms"] for n in nodes)}


def exhaustive(subject: str, nodes: list, bits: list, V: dict, contrast: dict,
               fronts: dict, rel_rows: list) -> dict:
    a, b = contrast["a"], contrast["b"]
    for arm in (a, b):
        if arm not in fronts:
            fronts[arm] = frontier(nodes, V, arm)
    ra = [c[0] for c in fronts[a]]
    rb = [c[0] for c in fronts[b]]
    brep = {m: c[0] for c in fronts[b] for m in c}
    same = {(c[0], brep[m]) for c in fronts[a] for m in c if m in brep}
    pairs = {(x, y) for x in ra for y in rb
             if D.subset(bits[x], bits[y]) or D.subset(bits[y], bits[x])} | same
    row = {"subject": subject, "contrast": contrast["name"], "a": a, "b": b,
           "plan": "exhaustive", **pool_counts(nodes, a, b),
           "a_front": len(ra), "b_front": len(rb)}
    for mode in MODES:
        sub_a, sub_b, counts = set(), set(), collections.Counter()
        for x, y in sorted(pairs):
            r = ("equivalent" if (x, y) in same or x == y
                 else classify(V, x, y, mode))
            counts[r] += 1
            if r == "b_stronger":
                sub_a.add(x)
            elif r == "a_stronger":
                sub_b.add(y)
            if mode == "decided" and r != "incomparable":
                rel_rows.append({
                    "subject": subject, "contrast": contrast["name"],
                    "a_class": x, "b_class": y,
                    "a_implies_b": "1" if (x, y) in same else V.get((x, y), "0"),
                    "b_implies_a": "1" if (x, y) in same else V.get((y, x), "0"),
                    "relation": r})
        counts["incomparable"] += len(ra) * len(rb) - len(pairs)
        sfx = "" if mode == "decided" else f"_{mode}"
        sa = len(sub_a) / len(ra) if ra else None
        sb = len(sub_b) / len(rb) if rb else None
        row[f"sub_a{sfx}"] = "" if sa is None else round(sa, 4)
        row[f"sub_b{sfx}"] = "" if sb is None else round(sb, 4)
        row[f"net{sfx}"] = "" if sa is None or sb is None else round(sb - sa, 4)
        if mode == "decided":
            row.update({"pairs": len(ra) * len(rb),
                        "pairs_equivalent": counts["equivalent"],
                        "pairs_a_stronger": counts["a_stronger"],
                        "pairs_b_stronger": counts["b_stronger"],
                        "pairs_incomparable": counts["incomparable"],
                        "pairs_undecided": counts["undecided"]})
    return row


# ── strength: sampled ───────────────────────────────────────────────────────

def strictly(V: dict, y: int, x: int, mode: str) -> bool:
    """y strictly implies x under one reading of an undecided direction."""
    return implies(V, y, x, mode) and not implies(V, x, y, mode)


def draw_stats(nodes: list, V: dict, rec: dict, orders: dict) -> dict:
    """Per arm, per draw: (on the frontier, class size, {partner: {mode: sub}},
    {partner: undecided directions met against that partner's pool})."""
    pool = {arm: [n["node"] for n in nodes if arm in n["arms"]]
            for arm in rec["arms"]}
    out = {}
    for arm in rec["arms"]:
        stats = []
        for x in orders[arm][:rec["draws"]]:
            dom, size, undec = False, 1, {}
            for y in pool[arm]:
                if y == x:
                    continue
                xy, yx = V.get((x, y), "0") == "1", V.get((y, x), "0") == "1"
                if yx and not xy:
                    dom = True
                elif xy and yx:
                    size += 1
            sub = {}
            for other in D.partners(rec, arm):
                sub[other] = {"imp": False, "non": False}
                undec[other] = 0
                for y in pool[other]:
                    if y == x:
                        continue
                    if "?" in (V.get((x, y), "0"), V.get((y, x), "0")):
                        undec[other] += 1
                    for mode in ("imp", "non"):
                        if not sub[other][mode] and strictly(V, y, x, mode):
                            sub[other][mode] = True
            stats.append((not dom, size, sub, undec))
        out[arm] = stats
    return out


def sampled(subject: str, nodes: list, contrast: dict, stats: dict) -> dict:
    a, b = contrast["a"], contrast["b"]
    row = {"subject": subject, "contrast": contrast["name"], "a": a, "b": b,
           "plan": "sampled", **pool_counts(nodes, a, b),
           "draws_a": len(stats[a]), "draws_b": len(stats[b]),
           "frontier_draws_a": sum(f for f, *_ in stats[a]),
           "frontier_draws_b": sum(f for f, *_ in stats[b])}
    for mode in ("imp", "non"):
        est = {}
        for side, other in ((a, b), (b, a)):
            wt = [f / s for f, s, _, _ in stats[side]]
            y = [(f / s) * sub[other][mode] for f, s, sub, _ in stats[side]]
            est[side] = ratio(y, wt)
        (sa, ha), (sb, hb) = est[a], est[b]
        row.update({f"sub_a_{mode}": round(sa, 4), f"sub_a_{mode}_hw": round(ha, 4),
                    f"sub_b_{mode}": round(sb, 4), f"sub_b_{mode}_hw": round(hb, 4),
                    f"net_{mode}": round(sb - sa, 4)})
    row["undecided"] = (sum(u[b] for *_, u in stats[a])
                        + sum(u[a] for *_, u in stats[b]))
    return row


# ── ball radii ──────────────────────────────────────────────────────────────

CG = ("d", "u", "m")
PAIRS = [x + y for x in CG for y in CG if x != y]


def jaccard(x: int, y: int) -> float:
    union = (x | y).bit_count()
    return (x ^ y).bit_count() / union if union else 0.0


def unit_row(groups: dict, prints: list, k: int, match) -> list:
    idx = {g: list(v) for g, v in groups.items()}
    present = [g for g in CG if g in idx]
    if match is not None:
        rng = random.Random(f"{match}|" + "|".join(str(idx[g][0]) for g in present))
        n = min(len(v) for v in idx.values())
        idx = {g: sorted(rng.sample(v, n)) for g, v in idx.items()}
    radius = {}
    for g in present:
        if len(idx[g]) > k:
            radius[g] = []
            for i in idx[g]:
                ds = sorted(jaccard(prints[i], prints[j]) for j in idx[g])
                radius[g].append(ds[k])      # ds[0] is the member itself
    prec, cov = [], []
    for p in PAIRS:
        a, b = p
        if a not in radius or b not in idx or len(idx[b]) <= k:
            prec.append("")
            cov.append("")
            continue
        inside = [[jaccard(prints[i], prints[j]) <= r for j in idx[b]]
                  for i, r in zip(idx[a], radius[a])]
        prec.append(round(sum(any(col) for col in zip(*inside)) / len(idx[b]), 4))
        cov.append(round(sum(any(row) for row in inside) / len(idx[a]), 4))
    return [len(idx[g]) if g in idx else "" for g in CG] + prec + cov


def ball_rows(subject: str, rec: dict, nodes: list, bits: list, k: int,
              match) -> list:
    prints, runs = [], collections.defaultdict(list)
    for n in nodes:
        for arm, seed, _ in n["members"]:
            runs[(arm, seed)].append(len(prints))
            prints.append(bits[n["node"]])
    seeds = rec["seeds"]
    blocks = [seeds[i:i + 3] for i in range(0, len(seeds) - 2, 3)]
    out = []
    for c in rec["contrasts"]:
        a, b = c["a"], c["b"]
        for s in seeds:
            if (a, s) in runs and (b, s) in runs:
                out.append(["triple", subject, c["name"], s] + unit_row(
                    {"d": runs[(a, s)], "u": runs[(b, s)]}, prints, k, match))
        for block in blocks:
            for r in range(3):
                sa, sb = block[r], block[(r + 1) % 3]
                if (a, sa) in runs and (b, sb) in runs:
                    out.append(["xtriple", subject, c["name"], sa] + unit_row(
                        {"d": runs[(a, sa)], "u": runs[(b, sb)]}, prints, k,
                        match))
    for arm in rec["arms"]:
        for block in blocks:
            if all((arm, s) in runs for s in block):
                out.append(["null", subject, arm, block[0]] + unit_row(
                    {g: runs[(arm, s)] for g, s in zip(CG, block)}, prints, k,
                    match))
    return out


BALL_HEADER = (["mode", "subject", "unit", "seed"] + [f"{g}_n" for g in CG]
               + [f"prec_{p}" for p in PAIRS] + [f"cov_{p}" for p in PAIRS])


def mean_of(rows: list, cols: list):
    v = [float(r[c]) for r in rows for c in cols if r[c] != ""]
    return statistics.mean(v) if v else None


def ball_report(rows: list, design: dict) -> str:
    """Per contrast, over subjects: each containment direction against the
    control of the arm whose region it is measured in, and the two directions
    against each other (the 2026-10-08 direction_ctrl.py and fp_prc_report.py
    on xtriple units)."""
    lines = []
    by = collections.defaultdict(list)
    for r in rows:
        by[(r["mode"], r["subject"], r["unit"])].append(r)
    contrasts = {}
    for rec in design["subjects"].values():
        for c in rec["contrasts"]:
            contrasts.setdefault(c["name"], c)
    all_pairs = [f"prec_{p}" for p in PAIRS]

    def line(label, pairs):
        if not pairs:
            return f"  {label:22} n= 0"
        d = [x - y for x, y in pairs]
        return (f"  {label:22} n={len(pairs):2d}  "
                f"{statistics.mean(x for x, _ in pairs):.3f} vs "
                f"{statistics.mean(y for _, y in pairs):.3f}  "
                f"below {sum(v < 0 for v in d)}/{sum(v != 0 for v in d)}  "
                f"p {wilcoxon_p(d):.3g}")
    for name, c in contrasts.items():
        b_in_a, a_in_b, asym = [], [], []
        for (mode, subject, unit), rs in sorted(by.items()):
            if mode != "xtriple" or unit != name:
                continue
            ctrl_a = mean_of(by.get(("null", subject, c["a"]), []), all_pairs)
            ctrl_b = mean_of(by.get(("null", subject, c["b"]), []), all_pairs)
            x, y = mean_of(rs, ["prec_du"]), mean_of(rs, ["prec_ud"])
            if x is not None and ctrl_a is not None:
                b_in_a.append((x, ctrl_a))
            if y is not None and ctrl_b is not None:
                a_in_b.append((y, ctrl_b))
            if x is not None and y is not None:
                asym.append((x, y))
        lines.append(f"{name}: a = {c['a']}, b = {c['b']} (xtriple)")
        lines.append(line("b in a vs control a", b_in_a))
        lines.append(line("a in b vs control b", a_in_b))
        lines.append(line("b in a vs a in b", asym))
    return "\n".join(lines) + "\n"


# ── pooled ball radii ───────────────────────────────────────────────────────

class Dist:
    """Jaccard distances among a fixed node set, each pair computed once."""

    def __init__(self, ids: list, bits: list):
        self.pos = {v: i for i, v in enumerate(ids)}
        pb = [bits[v] for v in ids]
        pc = [b.bit_count() for b in pb]
        self.rows = []
        for i, (x, cx) in enumerate(zip(pb, pc)):
            row = array.array("f", bytes(4 * len(ids)))
            for j in range(i):
                inter = (x & pb[j]).bit_count()
                union = cx + pc[j] - inter
                row[j] = (union - inter) / union if union else 0.0
            self.rows.append(row)
        for i in range(len(ids)):              # mirror the lower triangle
            ri = self.rows[i]
            for j in range(i + 1, len(ids)):
                ri[j] = self.rows[j][i]

    def radii(self, xs: list, ks: list) -> dict:
        """Per K, each member's distance to its K-th nearest other in xs."""
        cols = [self.pos[x] for x in xs]
        out = {k: [] for k in ks}
        for x in xs:
            row = self.rows[self.pos[x]]
            ds = sorted(row[c] for c in cols)  # ds[0] is the member itself
            for k in ks:
                out[k].append(ds[k])
        return out


def pooled_draw(dist: Dist, a: list, b: list, ks: list) -> dict:
    """One draw: a's and b's equal-size samples, each a union of balls of
    radius the distance to its K-th nearest other member; per K, the share of
    b inside a's region (b_in_a) and of a inside b's (a_in_b)."""
    ra, rb = dist.radii(a, ks), dist.radii(b, ks)
    cols_b = [dist.pos[y] for y in b]
    out = {}
    for k in ks:
        rad_a, rad_b = ra[k], rb[k]
        in_a = [False] * len(b)
        in_b = [False] * len(a)
        for i, x in enumerate(a):
            row, r = dist.rows[dist.pos[x]], rad_a[i]
            for j, c in enumerate(cols_b):
                d = row[c]
                if d <= r:
                    in_a[j] = True
                if d <= rad_b[j]:
                    in_b[i] = True
        out[k] = (sum(in_a) / len(b), sum(in_b) / len(a))
    return out


def half_nodes(nodes: list, arm: str, parity: int) -> list:
    return [n["node"] for n in nodes
            if any(m[0] == arm and m[1] % 2 == parity for m in n["members"])]


def pooled_task(task: tuple) -> list:
    """Rows for one (subject, contrast). Each arm's seeds split by parity
    into two half-pools of distinct nodes, and all four are subsampled to
    the smallest one's size, `draws` times. The contrast pairs a's odd half
    with b's even half and a's even with b's odd; an arm's control pairs its
    own odd and even halves. No pair shares a seed, so an arm that matches
    base run for run (one RNG stream, a feature that never fires) cannot
    look closer to base than base is to itself. Within a draw the contrast
    and both controls use the same samples."""
    subject, contrast, nodes, bits, ks, draws, seed = task
    a_arm, b_arm = contrast["a"], contrast["b"]
    A = [n["node"] for n in nodes if a_arm in n["arms"]]
    B = [n["node"] for n in nodes if b_arm in n["arms"]]
    (a0, a1), (b0, b1) = ((half_nodes(nodes, arm, 0), half_nodes(nodes, arm, 1))
                          for arm in (a_arm, b_arm))
    n = min(len(a0), len(a1), len(b0), len(b1))
    base = {"subject": subject, "contrast": contrast["name"],
            "n_a": len(A), "n_b": len(B), "n": n}
    cols = [c + sfx for c in ("b_in_a", "a_in_b", "ctrl_a", "ctrl_b")
            for sfx in ("", "_sd")]
    if n <= max(ks):
        return [dict(base, k=k, draws=0, **dict.fromkeys(cols, "")) for k in ks]
    dist = Dist(sorted(set(A) | set(B)), bits)
    acc = {k: collections.defaultdict(list) for k in ks}

    def take(rng, xs):
        return xs if len(xs) == n else sorted(rng.sample(xs, n))

    for r in range(draws):
        rng = random.Random(f"{seed}|{subject}|{contrast['name']}|{r}")
        sa0, sa1, sb0, sb1 = (take(rng, h) for h in (a0, a1, b0, b1))
        x1 = pooled_draw(dist, sa1, sb0, ks)
        x2 = pooled_draw(dist, sa0, sb1, ks)
        ca = pooled_draw(dist, sa0, sa1, ks)
        cb = pooled_draw(dist, sb0, sb1, ks)
        for k in ks:
            acc[k]["b_in_a"].append((x1[k][0] + x2[k][0]) / 2)
            acc[k]["a_in_b"].append((x1[k][1] + x2[k][1]) / 2)
            acc[k]["ctrl_a"].append(sum(ca[k]) / 2)
            acc[k]["ctrl_b"].append(sum(cb[k]) / 2)
    rows = []
    for k in ks:
        row = dict(base, k=k, draws=draws)
        for col in ("b_in_a", "a_in_b", "ctrl_a", "ctrl_b"):
            v = acc[k][col]
            row[col] = round(statistics.mean(v), 4) if v else ""
            row[f"{col}_sd"] = round(statistics.stdev(v), 4) if len(v) > 1 else ""
        rows.append(row)
    return rows


def pooled_report(rows: list, design: dict, k: int) -> str:
    """Per contrast, over subjects: each containment against its arm's
    half-pool control, and the two directions against each other."""
    families = collections.OrderedDict()
    for rec in design["subjects"].values():
        for c in rec["contrasts"]:
            families.setdefault(c["family"], [])
            if c["name"] not in families[c["family"]]:
                families[c["family"]].append(c["name"])
    tests = (("b in a vs control a", "b_in_a", "ctrl_a"),
             ("a in b vs control b", "a_in_b", "ctrl_b"),
             ("b in a vs a in b", "b_in_a", "a_in_b"))
    lines = [f"Pooled ball radii, K={k}: each arm's distinct nodes over its "
             "odd and over its even seeds, all four half-pools subsampled to "
             "the smallest; a contrast pairs halves of opposite parity, a "
             "control an arm's own two halves. Paired over subjects; p is the "
             "exact Wilcoxon signed-rank, Holm within a family and test."]
    for fam, names in families.items():
        for label, x, y in tests:
            pairs = {}
            for name in names:
                pairs[name] = [(float(r[x]), float(r[y])) for r in rows
                               if r["contrast"] == name and r["k"] == k
                               and r[x] != "" and r[y] != ""]
            ps = {nm: wilcoxon_p([u - v for u, v in p]) for nm, p in pairs.items() if p}
            adj = holm(ps)
            lines.append(f"== {fam}: {label}")
            for name in names:
                p = pairs[name]
                if not p:
                    lines.append(f"  {name:14} n= 0")
                    continue
                d = [u - v for u, v in p]
                lines.append(
                    f"  {name:14} n={len(p):2d}  "
                    f"{statistics.mean(u for u, _ in p):.3f} vs "
                    f"{statistics.mean(v for _, v in p):.3f}  "
                    f"median diff {statistics.median(d):+.3f}  "
                    f"below {sum(v < 0 for v in d)}/{sum(v != 0 for v in d)}  "
                    f"p {ps[name]:.3g}  Holm {adj[name]:.3g}")
    return "\n".join(lines) + "\n"


def pooled_balls(args, design: dict, names: list) -> None:
    tasks = []
    for name in names:
        rec = design["subjects"][name]
        nodes = load_nodes(args.members, name)
        bits = load_bits(subject_dir(args.collected, name), len(nodes))
        for c in rec["contrasts"]:
            tasks.append((name, c, nodes, bits, args.ball_k,
                          args.pooled_draws, args.pooled_seed))
    tasks.sort(key=lambda t: -sum(1 for n in t[2]
                                  if t[1]["a"] in n["arms"] or t[1]["b"] in n["arms"]))
    rows = []
    with multiprocessing.Pool(args.jobs) as pool:
        for got in pool.imap_unordered(pooled_task, tasks):
            rows += got
            print(f"pooled {got[0]['subject']} {got[0]['contrast']}: "
                  f"n={got[0]['n']}", flush=True)
    rows.sort(key=lambda r: (names.index(r["subject"]), r["contrast"], r["k"]))
    pdir = os.path.join(args.out, "ball-radii-pooled")
    os.makedirs(pdir, exist_ok=True)
    write_csv(os.path.join(pdir, "pooled.csv"), rows)
    for k in args.ball_k:
        with open(os.path.join(pdir, f"report-k{k}.txt"), "w") as h:
            h.write(pooled_report(rows, design, k))


# ── driver ──────────────────────────────────────────────────────────────────

def summary(rows: list, design: dict) -> str:
    lines = []
    families = collections.defaultdict(list)
    for rec in design["subjects"].values():
        for c in rec["contrasts"]:
            if c["name"] not in families[c["family"]]:
                families[c["family"]].append(c["name"])
    for sfx in ("", "_imp", "_non"):
        ps, stats = {}, {}
        for fam, names in families.items():
            for name in names:
                net = [r[f"net{sfx}"] for r in rows
                       if r["contrast"] == name and r.get(f"net{sfx}", "") != ""]
                if not net:
                    continue
                ps[name] = wilcoxon_p(net)
                stats[name] = net
        lines.append(f"== net{sfx or '_decided'} (a stronger when positive)")
        for fam, names in families.items():
            adj = holm({n: ps[n] for n in names if n in ps})
            for name in names:
                if name not in stats:
                    continue
                net = stats[name]
                pos, neg = sum(v > 0 for v in net), sum(v < 0 for v in net)
                lines.append(
                    f"  {name:14} n={len(net):2d} median={statistics.median(net):+.4f} "
                    f"mean={statistics.mean(net):+.4f} a>b {pos}, b>a {neg}, "
                    f"tie {len(net) - pos - neg}, Wilcoxon p={ps[name]:.4g}, "
                    f"Holm ({fam}) p={adj[name]:.4g}")
    return "\n".join(lines) + "\n"


def write_csv(path: str, rows: list) -> None:
    header: list = []
    for r in rows:
        for k in r:
            if k not in header:
                header.append(k)
    with open(path, "w", newline="") as h:
        w = csv.DictWriter(h, header or ["subject"], restval="")
        w.writeheader()
        w.writerows(rows)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("design")
    ap.add_argument("members")
    ap.add_argument("collected")
    ap.add_argument("out")
    ap.add_argument("--subjects", nargs="*", default=None)
    ap.add_argument("--ball-k", nargs="*", type=int, default=[1, 3, 5])
    ap.add_argument("--no-balls", action="store_true")
    ap.add_argument("--pooled-balls", action="store_true",
                    help="also write ball-radii-pooled/: each arm's nodes over "
                         "all seeds, subsampled to the smaller pool")
    ap.add_argument("--pooled-only", action="store_true",
                    help="write ball-radii-pooled/ alone; skip strength and "
                         "the per-run ball radii")
    ap.add_argument("--pooled-draws", type=int, default=20)
    ap.add_argument("--pooled-seed", default="0")
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--match", default="0",
                    help="seed for subsampling a unit's groups to its smallest "
                         "group; 'none' keeps every member")
    args = ap.parse_args(argv)
    design = D.load_design(args.design)
    match = None if args.match == "none" else args.match
    names = args.subjects or list(design["subjects"])
    if args.pooled_only:
        pooled_balls(args, design, names)
        return 0
    by = load_rows(args.collected)
    rows, rel_rows = [], []
    balls = collections.defaultdict(list)
    for name in names:
        rec = design["subjects"][name]
        nodes = load_nodes(args.members, name)
        sdir = subject_dir(args.collected, name)
        bits = load_bits(sdir, len(nodes))
        subject_rows = by.get(name, {})
        if rec["sampled"]:
            orders = load_orders(sdir, rec)
            plan = D.sampled_plan(rec, nodes, bits, orders)
        else:
            plan = D.exhaustive_plan(rec, nodes, bits)
        queries = check_plan(name, {(x, y): d for x, y, d in plan}, subject_rows)
        V, secs = verdicts(subject_rows)
        fronts: dict = {}
        stats = draw_stats(nodes, V, rec, orders) if rec["sampled"] else None
        for c in rec["contrasts"]:
            row = (sampled(name, nodes, c, stats) if rec["sampled"] else
                   exhaustive(name, nodes, bits, V, c, fronts, rel_rows))
            row["queries"] = queries
            row["call_s_sum"] = round(secs)
            rows.append(row)
            print(f"{name} {c['name']}: net_non={row['net_non']}", flush=True)
        if not args.no_balls:
            for k in args.ball_k:
                balls[k] += ball_rows(name, rec, nodes, bits, k, match)
    sdir = os.path.join(args.out, "strength")
    os.makedirs(sdir, exist_ok=True)
    write_csv(os.path.join(sdir, "contrasts.csv"), rows)
    write_csv(os.path.join(sdir, "relations.csv"), rel_rows)
    text = summary(rows, design)
    with open(os.path.join(sdir, "summary.txt"), "w") as h:
        h.write(text)
    print(text, end="")
    if not args.no_balls:
        bdir = os.path.join(args.out, "ball-radii")
        os.makedirs(bdir, exist_ok=True)
        for k, brows in balls.items():
            path = os.path.join(bdir, f"prc-k{k}.csv")
            with open(path, "w", newline="") as h:
                w = csv.writer(h)
                w.writerow(BALL_HEADER)
                w.writerows(brows)
            with open(path, newline="") as h:
                report = ball_report(list(csv.DictReader(h)), design)
            with open(os.path.join(bdir, f"report-k{k}.txt"), "w") as h:
                h.write(report)
    if args.pooled_balls:
        pooled_balls(args, design, names)
    return 0


if __name__ == "__main__":
    sys.exit(main())
