#!/usr/bin/env python3
"""Reduce the collected fingerprint and compare outputs to the archive's tables.

    python3 reduce.py MEMBERS_DIR COLLECTED_DIR OUT_DIR \\
        --strength SUBJECT... --ball SUBJECT...

MEMBERS_DIR is this campaign's members/ (<subject>.jsonl.gz, node numbering).
COLLECTED_DIR is what `campaign.py collect --outputs analysis-fretish-rerun`
leaves: <host>/fp/<subject>/fp.tsv and <host>/subsumption-<host>.csv.

Strength (the 2026-10-01 strength-fretish pooled.py `run` and `summary`,
ported): a pair id `subject:i:j` carries i => j as a_implies_b and j => i as
b_implies_a. A direction a word refutes was never queried and reads 0, as in
pooled.py; so does a pair no row names, since the planner leaves out exactly
the pairs words refute both ways. Before any figure, each subject's planned
set is checked against its fingerprints: every pair with a direction no word
refutes must have a row, and nothing else. Within each arm, a node is
dominated when another node of that arm implies it and is not implied back
(an undecided direction reads as no implication, as in `maximal`); the
undominated nodes, merged by equivalence, are the frontier classes. Every
directed class then meets every uniform class. Writes OUT_DIR/subjects.csv,
relations.csv and summary.txt in the archive's format, except that `queries`
counts every queried direction of the non-adaptive plan and `call_s_sum` (the
summed per-pair seconds) replaces `query_wall_s` and `wall_s`.

Ball radii: writes OUT_DIR/ball/<subject>.tsv, one row per member, the run
path fp_prc.py parses and the node's fingerprint, for fp_prc.py to read.

Needs numpy and scipy (the hosts' and this box's /usr/bin/python3 have both).
"""
import argparse
import collections
import csv
import glob
import gzip
import json
import os
import sys

import numpy as np

ARMS = ("directed", "uniform")
RUN_PATH = ("results-fretish-rerun/sweep_O_{arm}_nsga2-apportion_wkoff_log_"
            "{subject}_seed{seed:02d}/accumulated/{file}")


def load_nodes(members_dir, subject):
    with gzip.open(os.path.join(members_dir, f"{subject}.jsonl.gz"), "rt") as h:
        nodes = [json.loads(line) for line in h]
    assert [n["node"] for n in nodes] == list(range(len(nodes))), subject
    return nodes


def one(paths, what):
    if len(paths) != 1:
        sys.exit(f"reduce: expected one {what}, found {len(paths)}: {paths}")
    return paths[0]


def load_fp(collected, subject, n):
    path = one(glob.glob(os.path.join(collected, "*", "fp", subject, "fp.tsv")),
               f"fp.tsv for {subject}")
    hexes = [None] * n
    for line in open(path):
        p, _, digits = line.rstrip("\n").partition("\t")
        hexes[int(os.path.basename(p).split(".")[0])] = digits
    missing = [i for i, h in enumerate(hexes) if h is None]
    if missing:
        sys.exit(f"reduce: {path} lacks nodes {missing[:5]}...")
    return hexes


def subsets(hexes):
    """S[i, j]: no word refutes i => j."""
    rows = [bytes.fromhex(h) for h in hexes]
    pad = (-len(rows[0])) % 8
    x = np.frombuffer(b"".join(r + b"\0" * pad for r in rows),
                      dtype=np.uint64).reshape(len(rows), -1)
    nx = ~x
    S = np.zeros((len(rows), len(rows)), dtype=bool)
    for i in range(len(rows)):
        S[i] = ~((x[i][None, :] & nx).any(axis=1))
    return S


def load_rows(collected):
    by = collections.defaultdict(dict)
    for path in sorted(glob.glob(os.path.join(collected, "*", "subsumption-*.csv"))):
        for r in csv.DictReader(open(path, newline="")):
            subject, i, j = r["id"].rsplit(":", 2)
            by[subject][(int(i), int(j))] = r
    return by


def verdicts(rows):
    V, queries, secs = {}, 0, 0.0
    for (i, j), r in rows.items():
        V[(i, j)] = r["a_implies_b"]
        V[(j, i)] = r["b_implies_a"]
        secs += float(r["secs"] or 0)
    return V, secs


def check_plan(subject, S, rows):
    n = len(S)
    want = np.triu(S | S.T, 1)
    got = np.zeros((n, n), dtype=bool)
    for i, j in rows:
        got[i, j] = True
    extra, lost = int((got & ~want).sum()), int((want & ~got).sum())
    if extra or lost:
        sys.exit(f"reduce: {subject}: {lost} planned pairs have no row and "
                 f"{extra} rows were never planned; collect again or rerun")
    queries = int(S[np.triu_indices(n, 1)].sum() + S.T[np.triu_indices(n, 1)].sum())
    return queries


def frontier(nodes, cand, V, arm):
    ids = [n["node"] for n in nodes if arm in n["arms"]]
    held = set(ids)
    dominated, parent = set(), {i: i for i in ids}

    def find(n):
        while parent[n] != n:
            parent[n] = parent[parent[n]]
            n = parent[n]
        return n
    for a, b in cand:
        if a not in held or b not in held:
            continue
        ab, ba = V.get((a, b), "0") == "1", V.get((b, a), "0") == "1"
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
    return [sorted(v) for v in classes.values()]


def classify(V, d, u, mode):
    a, b = V.get((d, u), "0"), V.get((u, d), "0")
    if mode == "decided" and "?" in (a, b):
        return "undecided"
    t = lambda r: r == "1" or (r == "?" and mode == "imp")  # noqa: E731
    return {(True, True): "equivalent", (True, False): "d_stronger",
            (False, True): "u_stronger", (False, False): "incomparable"}[(t(a), t(b))]


def strength(subject, nodes, S, rows, rel_rows):
    V, secs = verdicts(rows)
    queries = check_plan(subject, S, rows)
    cand = list(rows)
    F = {arm: frontier(nodes, cand, V, arm) for arm in ARMS}
    rep = {a: [c[0] for c in F[a]] for a in ARMS}
    arms = [set(n["arms"]) for n in nodes]
    row = {"subject": subject,
           "d_pool": sum(n["n_d"] for n in nodes), "u_pool": sum(n["n_u"] for n in nodes),
           "d_nodes": sum("directed" in a for a in arms),
           "u_nodes": sum("uniform" in a for a in arms),
           "both_nodes": sum(len(a) == 2 for a in arms),
           "d_front": len(rep["directed"]), "u_front": len(rep["uniform"])}
    for arm, col in zip(ARMS, ("within_undecided_d", "within_undecided_u")):
        row[col] = sum(1 for (x, y), r in V.items()
                       if r == "?" and arm in arms[x] and arm in arms[y])
    rd, ru = rep["directed"], rep["uniform"]
    urep_of = {m: c[0] for c in F["uniform"] for m in c}
    same = {(c[0], urep_of[m]) for c in F["directed"] for m in c if m in urep_of}
    rdi, rui = np.array(rd, dtype=int), np.array(ru, dtype=int)
    cmask = S[np.ix_(rdi, rui)] | S[np.ix_(rui, rdi)].T
    pairs = {(rd[i], ru[j]) for i, j in zip(*np.nonzero(cmask))} | same
    for mode in ("decided", "imp", "non"):
        subd, subu, shd, shu = set(), set(), set(), set()
        counts = collections.Counter()
        und_d, und_u = set(), set()
        for x, y in sorted(pairs):
            r = "equivalent" if (x, y) in same or x == y else classify(V, x, y, mode)
            counts[r] += 1
            if r == "u_stronger":
                subd.add(x)
            elif r == "d_stronger":
                subu.add(y)
            elif r == "equivalent":
                shd.add(x)
                shu.add(y)
            elif r == "undecided":
                und_d.add(x)
                und_u.add(y)
            if mode == "decided" and r != "incomparable":
                rel_rows.append({
                    "subject": subject, "d_class": x, "u_class": y,
                    "d_path": f"{subject}/nodes/{x:05d}.json",
                    "u_path": f"{subject}/nodes/{y:05d}.json",
                    "d_implies_u": "1" if (x, y) in same else V.get((x, y), "0"),
                    "u_implies_d": "1" if (x, y) in same else V.get((y, x), "0"),
                    "relation": r})
        counts["incomparable"] += len(rd) * len(ru) - len(pairs)
        nd, nu = len(rd), len(ru)
        sfx = "" if mode == "decided" else f"_{mode}"
        row[f"sub_d{sfx}"] = round(len(subd) / nd, 4) if nd else ""
        row[f"sub_u{sfx}"] = round(len(subu) / nu, 4) if nu else ""
        row[f"net{sfx}"] = (round(row[f"sub_u{sfx}"] - row[f"sub_d{sfx}"], 4)
                            if nd and nu else "")
        if mode == "decided":
            row.update({"n_sub_d": len(subd), "n_sub_u": len(subu),
                        "shared_d": len(shd), "shared_u": len(shu),
                        "pairs": nd * nu, "pairs_queried_or_identical": len(pairs),
                        "pairs_equivalent": counts["equivalent"],
                        "pairs_d_stronger": counts["d_stronger"],
                        "pairs_u_stronger": counts["u_stronger"],
                        "pairs_incomparable": counts["incomparable"],
                        "pairs_undecided": counts["undecided"],
                        "d_classes_with_undecided": len(und_d - subd - shd),
                        "u_classes_with_undecided": len(und_u - subu - shu)})
    row["method_d"] = row["method_u"] = "all-pairs"
    row["queries"] = queries
    row["call_s_sum"] = round(secs)
    return row


def ball(subject, nodes, hexes, out):
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, f"{subject}.tsv"), "w") as h:
        for n in nodes:
            for arm, seed, file in n["members"]:
                h.write(RUN_PATH.format(arm=arm, subject=subject, seed=seed, file=file)
                        + "\t" + hexes[n["node"]] + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("members")
    ap.add_argument("collected")
    ap.add_argument("out")
    ap.add_argument("--strength", nargs="*", default=[])
    ap.add_argument("--ball", nargs="*", default=[])
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    by = load_rows(args.collected)
    rows, rel_rows = [], []
    for subject in args.strength:
        nodes = load_nodes(args.members, subject)
        hexes = load_fp(args.collected, subject, len(nodes))
        rows.append(strength(subject, nodes, subsets(hexes), by.get(subject, {}), rel_rows))
        print(f"strength {subject}: d_front={rows[-1]['d_front']} "
              f"u_front={rows[-1]['u_front']} net={rows[-1]['net']}", flush=True)
    for subject in args.ball:
        nodes = load_nodes(args.members, subject)
        ball(subject, nodes, load_fp(args.collected, subject, len(nodes)),
             os.path.join(args.out, "ball"))
    if not rows:
        return
    from scipy.stats import wilcoxon
    with open(os.path.join(args.out, "subjects.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    with open(os.path.join(args.out, "relations.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, list(rel_rows[0]) if rel_rows else ["subject"])
        w.writeheader()
        w.writerows(rel_rows)
    lines = []
    for sfx in ("", "_imp", "_non"):
        net = [r[f"net{sfx}"] for r in rows if r[f"net{sfx}"] != ""]
        pos, neg = sum(v > 0 for v in net), sum(v < 0 for v in net)
        try:
            p = wilcoxon(net).pvalue
        except ValueError:
            p = float("nan")
        lines.append(f"net{sfx or '_decided'}: n={len(net)} median={np.median(net):.4f} "
                     f"mean={np.mean(net):.4f} u>d {pos}, d>u {neg}, "
                     f"tie {len(net) - pos - neg}, Wilcoxon p={p:.4g}")
    open(os.path.join(args.out, "summary.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
