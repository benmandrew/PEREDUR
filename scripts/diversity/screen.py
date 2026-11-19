#!/usr/bin/env python3
"""Screen one arm against a reference arm on saved word banks, with no
satisfiability call.

    python3 scripts/diversity/screen.py OUT.json \\
        --arm NAME=GLOB --ref NAME=GLOB --subjects SUBJECT... \\
        [--bank experiments/2026-11-19-diversity-screen/wordbank] \\
        [--work DIR] [--k 3] [--draws 20] [--threads 4]

GLOB names a subject's run directories, with `{subject}` standing for the
subject, for example
`.../results-fretish-exchange-rerun/sweep_D_remove-exchange_nsga2-apportion_wkoff_log_{subject}_seed*`.
A run's seed is the number after its last `seed`. Every matched directory
must hold run.json.

Per subject:

1. Pool. Each run's repair_N.json files, fitness dropped, are pooled over
   both arms and every seed, identical sorted JSON being one node
   (scripts/pool_members.py's rule).
2. Fingerprint. Every node is evaluated on the subject's bank
   (scripts/diversity/wordbank.py) by `fpdraw eval`.
3. Frontier. Within each arm, `compare --rule-only` decides every ordered
   pair by rule. A node is dominated when another node of its arm is proved
   to imply it and not proved implied back; the rest, merged by proved
   equivalence, are the arm's frontier classes (pooled_reduce.frontier). An
   unproved pair reads as incomparable.
4. Strength: each arm's accepted-trace share, the median over its frontier
   classes of the share of bank words the class accepts. Lower is stronger.
5. Diversity: pooled_reduce.pooled_task at K, with a = the arm and b = the
   reference: odd-seed and even-seed half-pools subsampled to the smallest,
   b_in_a and a_in_b against each arm's own-halves control. Net
   containment = b_in_a - a_in_b, positive when the arm spreads wider. A
   subject whose smallest half-pool holds K or fewer nodes has no row, as in
   the pooled pass.
6. Descriptors: per run, the number of distinct (slots changed, kind of
   change) cells among its repairs (descriptors.py), and the run's own
   containment of the reference: the share of the reference's nodes from
   seeds of the other parity that fall in the union of the run's balls at K.

Over subjects, each paired difference gets the exact Wilcoxon signed-rank p
(pooled_reduce.wilcoxon_p) and the Hodges-Lehmann shift with its exact 95%
interval: net containment, b_in_a - ctrl_a, a_in_b - ctrl_b, and strength as
ref share - arm share (positive when the arm is stronger).

Writes OUT.json. Node files, fingerprints and verdicts go under --work
(default: OUT's stem beside it), and a rerun reuses fingerprints whose node
and bank are unchanged.
"""

import argparse
import collections
import glob
import hashlib
import json
import os
import random
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import descriptors  # noqa: E402
import fingerprint_members as F  # noqa: E402
import pool_members as P  # noqa: E402
import pooled_reduce as R  # noqa: E402

REPO = HERE.parent.parent


def binary(name: str) -> str:
    """build-release's copy where there is one, else the debug build's."""
    for build in ("build-release", "build"):
        path = REPO / build / name
        if path.exists():
            return str(path)
    return str(REPO / "build-release" / name)
DEFAULT_BANK = REPO / "experiments" / "2026-11-19-diversity-screen" / "wordbank"
SEED = re.compile(r"seed(\d+)(?!.*seed)")


# ── statistics ──────────────────────────────────────────────────────────────

def hodges_lehmann(d: list) -> tuple:
    """The median of the Walsh averages and its exact 95% interval from the
    signed-rank null (no ties assumed), as scripts/joint_measures.py on the
    pooled-pass branches computes it."""
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
        return est, None, None
    return est, walsh[k], walsh[m - 1 - k]


def paired(diffs: list) -> dict:
    if not diffs:
        return {"n": 0}
    est, lo, hi = hodges_lehmann(diffs) if len(diffs) >= 2 else (diffs[0], None, None)
    return {"n": len(diffs), "hl": round(est, 4),
            "ci95": [None if lo is None else round(lo, 4),
                     None if hi is None else round(hi, 4)],
            "positive": sum(x > 0 for x in diffs),
            "negative": sum(x < 0 for x in diffs),
            "p": R.wilcoxon_p(diffs)}


def spearman(x: list, y: list):
    def ranks(v):
        order = sorted(range(len(v)), key=lambda k: v[k])
        out, i = [0.0] * len(v), 0
        while i < len(order):
            j = i
            while j + 1 < len(order) and v[order[j + 1]] == v[order[i]]:
                j += 1
            for k in range(i, j + 1):
                out[order[k]] = (i + j) / 2 + 1
            i = j + 1
        return out
    if len(x) < 3:
        return None
    rx, ry = ranks(x), ranks(y)
    mx, my = statistics.mean(rx), statistics.mean(ry)
    sx = sum((a - mx) ** 2 for a in rx) ** 0.5
    sy = sum((b - my) ** 2 for b in ry) ** 0.5
    if sx == 0 or sy == 0:
        return None
    return sum((a - mx) * (b - my) for a, b in zip(rx, ry)) / (sx * sy)


# ── inputs ──────────────────────────────────────────────────────────────────

def runs_of(pattern: str, subject: str) -> list:
    out = []
    for run in sorted(glob.glob(pattern.replace("{subject}", subject))):
        if not os.path.isdir(run):
            continue
        if not os.path.isfile(os.path.join(run, "run.json")):
            sys.exit(f"screen: {run} has no run.json (unfinished)")
        m = SEED.search(os.path.basename(run))
        if not m:
            sys.exit(f"screen: no seed in {run}")
        out.append((int(m.group(1)), run))
    return out


def pool(arms: dict, subject: str) -> tuple:
    """Nodes in pool_members.py's shape, and each run's member node ids."""
    nodes, by_key, per_run = [], {}, {}
    for arm, pattern in arms.items():
        runs = runs_of(pattern, subject)
        if not runs:
            sys.exit(f"screen: {subject}: no run matches {pattern}")
        for seed, run in runs:
            ids = []
            for rel, spec in P.run_members(run, "output"):
                key = json.dumps(spec, sort_keys=True)
                node = by_key.get(key)
                if node is None:
                    node = by_key[key] = {"node": len(nodes), "arms": [],
                                          "n": {}, "members": [], "spec": spec}
                    nodes.append(node)
                if arm not in node["arms"]:
                    node["arms"].append(arm)
                    node["arms"].sort()
                node["n"][arm] = node["n"].get(arm, 0) + 1
                node["members"].append([arm, seed, rel])
                ids.append(node["node"])
            per_run[(arm, seed)] = (run, ids)
    return nodes, per_run


def node_digest(node: dict) -> str:
    return hashlib.sha256(json.dumps(node["spec"], sort_keys=True)
                          .encode()).hexdigest()[:20]


def fingerprints(nodes: list, paths: list, bank: str, wdir: str,
                 subject: str, args) -> list:
    """Each node's fingerprint on the bank, through a cache keyed on the
    node's JSON and the bank's checksum."""
    with open(os.path.join(bank, "words.meta.json")) as h:
        bank_sha = json.load(h)["sha256"]
    cache_path = os.path.join(wdir, f"fpcache-{bank_sha[:16]}.tsv")
    cache = {}
    if os.path.exists(cache_path):
        with open(cache_path) as h:
            for line in h:
                k, _, v = line.rstrip("\n").partition("\t")
                cache[k] = v
    digests = [node_digest(n) for n in nodes]
    todo = [i for i, d in enumerate(digests) if d not in cache]
    if todo:
        lst = os.path.join(wdir, "eval.list")
        F.write_whole(lst, "".join(paths[i] + "\n" for i in todo))
        out = os.path.join(wdir, "eval.tsv")
        F.run_to(out, [args.fpdraw, "eval", os.path.join(bank, "words"), lst,
                       str(args.threads)],
                 dict(os.environ, FPDRAW_UNGLUE=F.unglue(subject)),
                 os.path.join(wdir, "eval.err"))
        got = {}
        with open(out) as h:
            for line in h:
                p, _, digits = line.rstrip("\n").partition("\t")
                got[int(Path(p).stem)] = digits
        if len(got) != len(todo):
            sys.exit(f"screen: {subject}: fpdraw eval gave {len(got)} of "
                     f"{len(todo)} fingerprints; see {wdir}/eval.err")
        with open(cache_path, "a") as h:
            for i in todo:
                cache[digests[i]] = got[i]
                h.write(f"{digests[i]}\t{got[i]}\n")
    return [int(cache[d], 16) for d in digests]


def rule_verdicts(ids: list, paths: list, bits: list, wdir: str, label: str,
                  compare: str) -> dict:
    """V for pooled_reduce.frontier over one arm's nodes: both directions of
    every pair with a proved direction, '1' or '0'. Only the directions no
    bank word refutes are asked (x => y needs bits[x] a subset of bits[y]),
    as fingerprint_members.plan narrows a pooled pass's pairs; a refuted
    direction cannot be proved, so the answer is the full grid's."""
    if len(ids) < 2:
        return {}
    adir = os.path.join(wdir, f"arm-{label}")
    os.makedirs(adir, exist_ok=True)
    for f in os.listdir(adir):
        os.unlink(os.path.join(adir, f))
    for i in ids:
        os.symlink(os.path.abspath(paths[i]), os.path.join(adir, f"{i:05d}.json"))
    plan = os.path.join(wdir, f"pairs-{label}.tsv")
    with open(plan + ".part", "w") as h:
        for k, x in enumerate(ids):
            fx = bits[x]
            for y in ids[k + 1:]:
                union = fx | bits[y]
                fwd, rev = union == bits[y], union == fx
                if fwd or rev:
                    h.write(f"{x:05d}.json\t{y:05d}.json\t"
                            f"{'both' if fwd and rev else 'fwd' if fwd else 'rev'}\n")
    os.replace(plan + ".part", plan)
    V = {}
    with subprocess.Popen([compare, "--rule-only", "--repairs", adir,
                           "--ideals", adir, "--pairs", plan],
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE, text=True) as proc:
        tail = []
        for line in proc.stderr:
            if not line.startswith("DIR\t"):
                tail = (tail + [line])[-20:]
                continue
            _, a, b, fwd, rev = line.rstrip("\n").split("\t")
            if fwd != "1" and rev != "1":
                continue
            x, y = int(a.split(".")[0]), int(b.split(".")[0])
            if x != y:
                V[(x, y)], V[(y, x)] = fwd, rev
    if proc.returncode != 0:
        sys.exit(f"screen: compare --rule-only failed on {adir}: "
                 f"{''.join(tail)}")
    return V


# ── per-run containment ─────────────────────────────────────────────────────

def run_containment(ids: list, ref: list, bits: list, k: int):
    """Share of `ref` inside the union of balls around a run's distinct
    members, each of radius the Jaccard distance to its K-th nearest other."""
    xs = sorted(set(ids))
    if len(xs) <= k or not ref:
        return None
    radii = []
    for x in xs:
        ds = sorted(R.jaccard(bits[x], bits[y]) for y in xs)
        radii.append(ds[k])
    held = sum(any(R.inside(R.jaccard(bits[x], bits[y]), r)
                   for x, r in zip(xs, radii)) for y in ref)
    return held / len(ref)


# ── driver ──────────────────────────────────────────────────────────────────

def screen_subject(subject: str, arm: str, ref: str, arms: dict, args) -> dict:
    t0 = time.time()
    wdir = os.path.join(args.work, subject)
    os.makedirs(wdir, exist_ok=True)
    nodes, per_run = pool(arms, subject)
    # Node files are numbered by this pool, so rewrite them each screen.
    paths = F.materialise(nodes, args.work, subject)
    bank = os.path.join(args.bank, subject)
    with open(os.path.join(bank, "words.meta.json")) as h:
        nwords = json.load(h)["words"]
    bits = fingerprints(nodes, paths, bank, wdir, subject, args)
    t_fp = time.time()
    row = {"subject": subject, "nodes": len(nodes)}
    fronts = {}
    for name in (arm, ref):
        ids = [n["node"] for n in nodes if name in n["arms"]]
        V = rule_verdicts(ids, paths, bits, wdir, name, args.compare)
        fronts[name] = R.frontier(nodes, V, name)
        f = fronts[name]
        row[name] = {
            "runs": sum(1 for a, _ in per_run if a == name),
            "members": sum(n["n"].get(name, 0) for n in nodes),
            "nodes": len(ids),
            "frontier_classes": len(f),
            "share": (statistics.median(bits[c[0]].bit_count() / nwords
                                        for c in f) if f else None)}
    t_front = time.time()
    contrast = {"name": f"{arm}-vs-{ref}", "a": arm, "b": ref}
    got = R.pooled_task((subject, contrast, nodes, bits, [args.k],
                         args.draws, args.seed))[0]
    row["containment"] = {c: got[c] for c in
                          ("n", "draws", "b_in_a", "a_in_b", "ctrl_a", "ctrl_b",
                           "b_in_a_sd", "a_in_b_sd", "ctrl_a_sd", "ctrl_b_sd")}
    if got["b_in_a"] != "":
        row["containment"]["net"] = round(got["b_in_a"] - got["a_in_b"], 4)
    with open(REPO / "examples" / subject / "spec.json") as h:
        orig = json.load(h)
    ref_half = {p: [n["node"] for n in nodes
                    if any(m[0] == ref and m[1] % 2 == p for m in n["members"])]
                for p in (0, 1)}
    runs = []
    for (name, seed), (run, ids) in sorted(per_run.items()):
        runs.append({"arm": name, "seed": seed,
                     "repairs": len(ids),
                     "descriptors": descriptors.descriptor_count(orig, run),
                     "contains_ref": run_containment(
                         ids, ref_half[1 - seed % 2], bits, args.k)})
    row["runs"] = runs
    pairs = [(r["descriptors"], r["contains_ref"]) for r in runs
             if r["contains_ref"] is not None]
    row["spearman_descriptors_containment"] = {
        "n_runs": len(pairs),
        "rho": spearman([a for a, _ in pairs], [b for _, b in pairs])}
    row["secs"] = {"pool_and_fingerprint": round(t_fp - t0, 1),
                   "frontier": round(t_front - t_fp, 1),
                   "total": round(time.time() - t0, 1)}
    print(f"{subject}: {len(nodes)} nodes, fronts "
          f"{row[arm]['frontier_classes']}/{row[ref]['frontier_classes']}, "
          f"net {row['containment'].get('net')}, {row['secs']['total']} s",
          flush=True)
    return row


def summarise(rows: list, arm: str, ref: str) -> dict:
    net, bin_ctrl, ain_ctrl, share = [], [], [], []
    for r in rows:
        c = r["containment"]
        if "net" in c:
            net.append(c["net"])
            bin_ctrl.append(c["b_in_a"] - c["ctrl_a"])
            ain_ctrl.append(c["a_in_b"] - c["ctrl_b"])
        if r[arm]["share"] is not None and r[ref]["share"] is not None:
            share.append(r[ref]["share"] - r[arm]["share"])
    return {"net_containment": paired(net),
            "ref_in_arm_minus_arm_control": paired(bin_ctrl),
            "arm_in_ref_minus_ref_control": paired(ain_ctrl),
            "strength_ref_minus_arm_share": paired(share),
            "screen_subjects": [r["subject"] for r in rows
                                if "net" in r["containment"]]}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--arm", required=True, help="NAME=GLOB")
    ap.add_argument("--ref", required=True, help="NAME=GLOB")
    ap.add_argument("--subjects", nargs="+", required=True)
    ap.add_argument("--bank", default=str(DEFAULT_BANK))
    ap.add_argument("--work", default=None)
    ap.add_argument("--k", type=int, default=3)
    ap.add_argument("--draws", type=int, default=20)
    ap.add_argument("--seed", default="0")
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--fpdraw", default=binary("fpdraw"))
    ap.add_argument("--compare", default=binary("compare"))
    args = ap.parse_args(argv)
    arm, arm_glob = args.arm.split("=", 1)
    ref, ref_glob = args.ref.split("=", 1)
    if arm == ref:
        sys.exit("screen: the arm and the reference need different names")
    args.work = args.work or os.path.splitext(args.out)[0] + "-work"
    arms = {arm: arm_glob, ref: ref_glob}
    t0 = time.time()
    rows = [screen_subject(s, arm, ref, arms, args) for s in args.subjects]
    out = {"arm": {"name": arm, "glob": arm_glob},
           "ref": {"name": ref, "glob": ref_glob},
           "k": args.k, "draws": args.draws, "seed": args.seed,
           "bank": os.path.abspath(args.bank),
           "summary": summarise(rows, arm, ref),
           "subjects": rows, "secs": round(time.time() - t0, 1)}
    F.write_whole(args.out, json.dumps(out, indent=1) + "\n")
    print(json.dumps(out["summary"], indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
