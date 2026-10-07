"""Pooled net-subsumption pass for RQ2: selection (Pareto against scalarised)
and grading (MRS against the ladder), in the design of PLAN.md and of the
pooled RQ3 and FRETISH passes (counter experiments/2026-09-25-overlap,
../strength-fretish).

A side's pool is the union of its 30 runs' last-cut frontiers, from the
rematch separation-recount-rematch-s0 sidecars. Members whose bodies match
once comments and whitespace are removed are one node, represented by the
lowest (seed, file). Stage 1 compares, within each side, every pair of nodes
that survives the fingerprint prefilter. A node strictly implied by another
node of its side is dominated; the undominated nodes merge into frontier
classes by equivalence. An undecided pair reads as no implication here, as in
`maximal`. Stage 2 compares every Pareto class with every scalarised and
every ladder class that survives the prefilter.

sub(A by B) is the share of A's classes strictly implied by some B class, and
net = sub(B by A) - sub(A by B), positive when A (Pareto, or MRS) is the
stronger. Equivalent classes are ties. Undecided cross pairs are read both as
implications both ways (imp) and as non-implications (non), as in score.py.
The test is the exact Wilcoxon signed-rank over families.

Pair ids are spec|label@seed|file|label@seed|file, as in plan.py, so the
per-run pass's results answer any pair they share. Files are copied to every
host under ~/subsum/pool/ at their rematch-relative path.

usage:
  pooled.py pool <out>                   members.csv, fetch-<host>.txt
  pooled.py nodes <out> <hashes.csv>...  nodes.csv (body-hash merge)
  pooled.py within <out> <results>...    pairs-within.csv, minus answered
  pooled.py cross <out> <results>...     classes.csv, pairs-cross.csv
  pooled.py cross-pools <out> <results>... pairs-crosspool.csv (stage 2 over pools)
  pooled.py score <out> <results>...     families.csv, report on stdout
  pooled.py sample-plan <out> <k> <per> <results>...   sample/pairs-<k>.csv
  pooled.py sample-score <out> <draws> <results>...    sample/estimates.csv
"""
import collections, csv, glob, os, sys
import numpy as np

BR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../ball-radii")
sys.path.insert(0, BR)
import tlsf_prc as T

EXP = os.path.expanduser("~/projects/counter-wt-fingerprint/experiments/separation-recount-rematch-s0")
P, W, L = "mrs-nsga2-apportion", "mrs-weighted", "aurus-nsga2-apportion"
SIDES = (P, W, L)
COMPS = {"selection": (P, W), "grading": (P, L)}
POOL = "/home/benandrew/subsum/pool"
FLIP = {"weaker": "stronger", "stronger": "weaker"}
SAMPLED = {"humanoid-531", "humanoid-742"}
HALF = 0.10   # stop when every sampled share's 95% half-width is at most this


def rel_path(label, spec, seed, f):
    g, s = label.split("-", 1)
    return f"sweep_G_{g}_{s}_wkoff_log_{spec}_seed{seed:02d}/accumulated/{f}"


def runs():
    out = {}
    for p in glob.glob(f"{EXP}/av*/*.members.tsv"):
        m = T.NAME.match(os.path.basename(p))
        if m and not m.group(1) and f"{m.group(2)}-{m.group(3)}" in SIDES:
            out[(f"{m.group(2)}-{m.group(3)}", m.group(4), int(m.group(5)))] = p
    return out


def pool(out):
    os.makedirs(out, exist_ok=True)
    w = csv.writer(open(f"{out}/members.csv", "w", newline=""))
    w.writerow(["label", "spec", "seed", "file", "path"])
    fetch = {"av2": set(), "av3": set()}
    for (label, spec, seed), p in sorted(runs().items()):
        fp = T.load_prints(p.replace(".members.tsv", ".fingerprints.tsv"), names := T.frontier(p))
        for f in names:
            if f in fp:
                r = rel_path(label, spec, seed, f)
                w.writerow([label, spec, seed, f, r])
                fetch["av2" if seed < 15 else "av3"].add(r)
    for h, s in fetch.items():
        open(f"{out}/fetch-{h}.txt", "w").write("".join(f"{x}\n" for x in sorted(s)))
        print(h, len(s), "files", file=sys.stderr)


def load_members(out):
    m = collections.defaultdict(list)
    for r in csv.DictReader(open(f"{out}/members.csv")):
        m[(r["label"], r["spec"])].append((int(r["seed"]), r["file"], r["path"]))
    return m


def nodes(out, hashfiles):
    h = {}
    for f in hashfiles:
        for r in csv.DictReader(open(f)):
            h[r["path"]] = r["sha"]
    w = csv.writer(open(f"{out}/nodes.csv", "w", newline=""))
    w.writerow(["label", "spec", "seed", "file", "path", "members"])
    for (label, spec), ms in sorted(load_members(out).items()):
        by = collections.defaultdict(list)
        for m in sorted(ms):
            by[h[m[2]]].append(m)
        for g in by.values():
            w.writerow([label, spec, g[0][0], g[0][1], g[0][2], len(g)])


def load_nodes(out):
    n = collections.defaultdict(list)
    for r in csv.DictReader(open(f"{out}/nodes.csv")):
        n[(r["label"], r["spec"])].append((int(r["seed"]), r["file"], r["path"]))
    return n


def prints(label, spec, seed, names, cache={}):
    k = (label, spec, seed)
    if k not in cache:
        p = runs_cache()[k]
        cache[k] = T.load_prints(p.replace(".members.tsv", ".fingerprints.tsv"), T.frontier(p))
    return np.stack([cache[k][f] for f in names]).view(np.uint64)


_runs = None


def runs_cache():
    global _runs
    if _runs is None:
        _runs = runs()
    return _runs


def fparr(label, spec, ns):
    by = collections.defaultdict(list)
    for i, (s, f, _) in enumerate(ns):
        by[s].append((i, f))
    a = np.zeros((len(ns), 1024), np.uint64)
    for s, v in by.items():
        a[[i for i, _ in v]] = prints(label, spec, s, [f for _, f in v])
    return a


def pid(spec, la, a, lb, b):
    return f"{spec}|{la}@{a[0]}|{a[1]}|{lb}@{b[0]}|{b[1]}"


def load_rel(results):
    rel = {}
    for f in results:
        for r in csv.DictReader(open(f)):
            rel[r["id"]] = r["relation"]
    return rel


def lookup(rel, spec, la, a, lb, b):
    r = rel.get(pid(spec, la, a, lb, b))
    if r is not None:
        return r
    r = rel.get(pid(spec, lb, b, la, a))
    return FLIP.get(r, r) if r is not None else None


def emit(w, rel, spec, la, A, fa, lb, B, fb, same):
    """Write the prefilter survivors of A x B not answered by rel."""
    n = known = 0
    for i in range(len(A)):
        x = fa[i][None, :]
        ok = ~((x & ~fb).any(1)) | ~((fb & ~x).any(1))
        for t in np.nonzero(ok)[0]:
            if same and t <= i:
                continue
            if lookup(rel, spec, la, A[i], lb, B[t]) is not None:
                known += 1
                continue
            w.writerow([pid(spec, la, A[i], lb, B[t]), f"{POOL}/{A[i][2]}", f"{POOL}/{B[t][2]}"])
            n += 1
    return n, known


def within(out, results):
    """env SPEC limits the plan to one family and writes pairs-within-<SPEC>.csv."""
    only = os.environ.get("SPEC")
    rel = load_rel(results)
    w = csv.writer(open(f"{out}/pairs-within{'-' + only if only else ''}.csv", "w", newline=""))
    w.writerow(["id", "a_path", "b_path"])
    for (label, spec), ns in sorted(load_nodes(out).items()):
        if only and spec != only:
            continue
        f = fparr(label, spec, ns)
        n, known = emit(w, rel, spec, label, ns, f, label, ns, f, True)
        print(f"{spec}\t{label}\tnodes {len(ns)}\tpairs {n}\tanswered {known}", file=sys.stderr, flush=True)


def frontier(rel, label, spec, ns):
    """Classes of the undominated nodes; undecided reads as no implication."""
    dom = [False] * len(ns)
    eq = collections.defaultdict(set)
    for i in range(len(ns)):
        for t in range(i + 1, len(ns)):
            r = lookup(rel, spec, label, ns[i], label, ns[t])
            if r == "weaker":
                dom[i] = True
            elif r == "stronger":
                dom[t] = True
            elif r == "equivalent":
                eq[i].add(t); eq[t].add(i)
    seen, classes = set(), []
    for i in range(len(ns)):
        if dom[i] or i in seen:
            continue
        comp, stack = [], [i]
        while stack:
            k = stack.pop()
            if k in seen:
                continue
            seen.add(k); comp.append(k); stack.extend(eq[k])
        classes.append(sorted(comp))
    return classes


def all_classes(out, rel):
    """(label, spec) -> list of class representatives, from stage 1."""
    res = {}
    for (label, spec), ns in sorted(load_nodes(out).items()):
        if spec in SAMPLED:
            continue
        res[(label, spec)] = [ns[c[0]] for c in frontier(rel, label, spec, ns)]
    return res


def cross(out, results):
    rel = load_rel(results)
    cl = all_classes(out, rel)
    c = csv.writer(open(f"{out}/classes.csv", "w", newline=""))
    c.writerow(["label", "spec", "seed", "file", "path"])
    for (label, spec), reps in sorted(cl.items()):
        for s, f, p in reps:
            c.writerow([label, spec, s, f, p])
    w = csv.writer(open(f"{out}/pairs-cross.csv", "w", newline=""))
    w.writerow(["id", "a_path", "b_path"])
    for spec in sorted({k[1] for k in cl} - SAMPLED):
        for comp, (a, b) in COMPS.items():
            if (a, spec) not in cl or (b, spec) not in cl:
                continue
            A, B = cl[(a, spec)], cl[(b, spec)]
            n, known = emit(w, rel, spec, a, A, fparr(a, spec, A), b, B, fparr(b, spec, B), False)
            print(f"{spec}\t{comp}\tclasses {len(A)} x {len(B)}\tpairs {n}\tanswered {known}", file=sys.stderr, flush=True)


def cross_pools(out, results):
    """Stage 2 over whole pools: every Pareto node against every scalarised and
    every ladder node that survives the prefilter, for the exact families. The
    scorer restricts these to the frontier classes, so stage 2 needs nothing
    from stage 1 and the whole pass is one pair list."""
    rel = load_rel(results)
    ns = load_nodes(out)
    w = csv.writer(open(f"{out}/pairs-crosspool.csv", "w", newline=""))
    w.writerow(["id", "a_path", "b_path"])
    for spec in sorted({k[1] for k in ns} - SAMPLED):
        for comp, (a, b) in COMPS.items():
            if (a, spec) not in ns or (b, spec) not in ns:
                continue
            A, B = ns[(a, spec)], ns[(b, spec)]
            n, known = emit(w, rel, spec, a, A, fparr(a, spec, A), b, B, fparr(b, spec, B), False)
            print(f"{spec}\t{comp}\tnodes {len(A)} x {len(B)}\tpairs {n}\tanswered {known}", file=sys.stderr, flush=True)


def score(out, results):
    from scipy import stats
    rel = load_rel(results)
    cl = collections.defaultdict(list)
    for r in csv.DictReader(open(f"{out}/classes.csv")):
        cl[(r["label"], r["spec"])].append((int(r["seed"]), r["file"], r["path"]))
    w = csv.writer(open(f"{out}/families.csv", "w", newline=""))
    w.writerow(["comp", "spec", "a_classes", "b_classes", "subA_non", "subB_non", "net_non", "subA_imp", "subB_imp", "net_imp", "undecided", "errors"])
    for comp, (a, b) in COMPS.items():
        nets = {"non": [], "imp": []}
        undec_all = 0
        est = {r["spec"]: r for r in csv.DictReader(open(f"{out}/sample/estimates.csv")) if r["comp"] == comp} \
            if os.path.exists(f"{out}/sample/estimates.csv") else {}
        for spec in sorted({k[1] for k in cl} | set(est)):
            if spec in est:
                e = est[spec]
                print(f"  {comp} {spec}: sampled, net non {float(e['net_non']):+.4f} imp {float(e['net_imp']):+.4f}")
                for k in ("non", "imp"):
                    nets[k].append(float(e[f"net_{k}"]))
                w.writerow([comp, spec, "sampled", "sampled", e["subA_non"], e["subB_non"], e["net_non"],
                            e["subA_imp"], e["subB_imp"], e["net_imp"], e["undecided"], 0])
                continue
            A, B = cl.get((a, spec), []), cl.get((b, spec), [])
            if not A or not B:
                print(f"  {comp} {spec}: skipped, empty frontier ({len(A)} x {len(B)})")
                continue
            sa = {k: np.zeros(len(A), bool) for k in ("non", "imp")}
            sb = {k: np.zeros(len(B), bool) for k in ("non", "imp")}
            undec = errs = 0
            for i, x in enumerate(A):
                for t, y in enumerate(B):
                    r = lookup(rel, spec, a, x, b, y) or "incomparable"
                    if r == "weaker":
                        for k in sa: sa[k][i] = True
                    elif r == "stronger":
                        for k in sb: sb[k][t] = True
                    elif r in ("undecided", "error"):
                        undec += r == "undecided"; errs += r == "error"
                        sa["imp"][i] = True; sb["imp"][t] = True
            row = [comp, spec, len(A), len(B)]
            for k in ("non", "imp"):
                net = sb[k].mean() - sa[k].mean()
                nets[k].append(net)
                row += [round(sa[k].mean(), 4), round(sb[k].mean(), 4), round(net, 4)]
            w.writerow(row + [undec, errs])
            undec_all += undec
        print(f"== {comp}: A = {a}, B = {b}")
        for k in ("non", "imp"):
            x = np.array(nets[k])
            nz = x[x != 0]
            p = stats.wilcoxon(nz, method="exact").pvalue if len(nz) > 0 else 1.0
            print(f"  [{k}] families {len(x)}: A stronger on {(x > 0).sum()}, B on {(x < 0).sum()}, tie {(x == 0).sum()}; "
                  f"median net {np.median(x):+.4f}, mean {x.mean():+.4f}; exact Wilcoxon p {p:.4g}")
        print(f"  undecided cross pairs {undec_all}")


# Sampled families. A side's draw order is a seeded permutation of its nodes,
# so any prefix is a simple random sample. A drawn node is checked against
# every node of its own side (frontier membership and class size) and every
# node of the other side's pool (whether some B repair strictly implies it;
# a B repair implying it means some B frontier class does). Only pairs where
# the other node's fingerprint is a subset of the drawn node's can imply it,
# so only those are compared. A frontier draw is weighted 1/class size, so the
# ratio estimate is a share of classes.

def order(out, spec, label, n):
    p = f"{out}/sample/{spec}-{label}-order.txt"
    if not os.path.exists(p):
        seed = sum(map(ord, f"{spec}|{label}")) + 20261006
        open(p, "w").write("".join(f"{i}\n" for i in np.random.default_rng(seed).permutation(n)))
    return [int(x) for x in open(p).read().split()]


def partners(label):
    return [b for c, (a, b) in COMPS.items() if a == label] or [a for c, (a, b) in COMPS.items() if b == label]


def sample_plan(out, k, per, results):
    """Batch k: draws k*per .. (k+1)*per - 1 of each side of each sampled family."""
    os.makedirs(f"{out}/sample", exist_ok=True)
    rel = load_rel(results)
    seen = set()
    for f in glob.glob(f"{out}/sample/pairs-*.csv"):
        for r in csv.DictReader(open(f)):
            seen.add(r["id"])
    nodes_ = load_nodes(out)
    w = csv.writer(open(f"{out}/sample/pairs-{k:03d}.csv", "w", newline=""))
    w.writerow(["id", "a_path", "b_path"])
    n = 0
    for spec in sorted(SAMPLED):
        fa = {lab: fparr(lab, spec, nodes_[(lab, spec)]) for lab in SIDES}
        for lab in SIDES:
            ns = nodes_[(lab, spec)]
            for i in order(out, spec, lab, len(ns))[k * per:(k + 1) * per]:
                x = fa[lab][i][None, :]
                for other in [lab] + partners(lab):
                    ys = nodes_[(other, spec)]
                    ok = ~((fa[other] & ~x).any(1))   # fp(y) subset of fp(x)
                    for t in np.nonzero(ok)[0]:
                        if other == lab and t == i:
                            continue
                        a, b = ns[i], ys[t]
                        ida, idb = pid(spec, lab, a, other, b), pid(spec, other, b, lab, a)
                        if ida in seen or idb in seen or lookup(rel, spec, lab, a, other, b) is not None:
                            continue
                        seen.add(ida)
                        w.writerow([ida, f"{POOL}/{a[2]}", f"{POOL}/{b[2]}"])
                        n += 1
    print(f"batch {k}: {n} pairs", file=sys.stderr)
    return n


def ratio(y, wt):
    """Ratio estimate sum(y)/sum(wt) and its 95% half-width (linearised)."""
    y, wt = np.asarray(y, float), np.asarray(wt, float)
    if wt.sum() == 0 or len(wt) < 2:
        return float("nan"), float("inf")
    r = y.sum() / wt.sum()
    d = y - r * wt
    return r, 1.96 * np.sqrt(d.var(ddof=1) / len(wt)) / wt.mean()


def sample_score(out, drawn, results):
    """Estimates from the first `drawn` draws of each side. Returns True when converged."""
    rel = load_rel(results)
    nodes_ = load_nodes(out)
    rows, done = [], True
    w = csv.writer(open(f"{out}/sample/estimates.csv", "w", newline=""))
    w.writerow(["comp", "spec", "draws", "frontier_draws", "subA_non", "subA_non_hw", "subB_non", "subB_non_hw",
                "net_non", "subA_imp", "subA_imp_hw", "subB_imp", "subB_imp_hw", "net_imp", "undecided"])
    for spec in sorted(SAMPLED):
        st = {}
        for lab in SIDES:
            ns = nodes_[(lab, spec)]
            for i in order(out, spec, lab, len(ns))[:drawn]:
                dom, size, undec = False, 1, 0
                sub = {o: {"non": False, "imp": False} for o in partners(lab)}
                for t, y in enumerate(ns):
                    if t == i:
                        continue
                    r = lookup(rel, spec, lab, ns[i], lab, y)
                    if r == "weaker":
                        dom = True
                    elif r == "equivalent":
                        size += 1
                for o in partners(lab):
                    for y in nodes_[(o, spec)]:
                        r = lookup(rel, spec, lab, ns[i], o, y)
                        if r == "weaker":
                            sub[o]["non"] = sub[o]["imp"] = True
                        elif r in ("undecided", "error"):
                            sub[o]["imp"] = True; undec += 1
                st.setdefault(lab, []).append((not dom, size, sub, undec))
        for comp, (a, b) in COMPS.items():
            row, nets = [comp, spec, drawn, sum(f for f, *_ in st[a])], {}
            est = {}
            for side, other in ((a, b), (b, a)):
                for k in ("non", "imp"):
                    wt = [f / s for f, s, _, _ in st[side]]
                    y = [(f / s) * sb[other][k] for f, s, sb, _ in st[side]]
                    est[(side, k)] = ratio(y, wt)
                    if est[(side, k)][1] > HALF:
                        done = False
            for k in ("non", "imp"):
                (ra, ha), (rb, hb) = est[(a, k)], est[(b, k)]
                row += [round(ra, 4), round(ha, 4), round(rb, 4), round(hb, 4), round(rb - ra, 4)]
            row.append(sum(u for *_, u in st[a]) + sum(u for *_, u in st[b]))
            w.writerow(row)
            print(f"  {comp} {spec}: draws {drawn}, sub A {est[(a, 'non')][0]:.3f} \u00b1{est[(a, 'non')][1]:.3f}, "
                  f"sub B {est[(b, 'non')][0]:.3f} \u00b1{est[(b, 'non')][1]:.3f}", file=sys.stderr)
    return done


if __name__ == "__main__":
    cmd, out, rest = sys.argv[1], sys.argv[2], sys.argv[3:]
    {"pool": lambda: pool(out), "nodes": lambda: nodes(out, rest), "within": lambda: within(out, rest),
     "cross": lambda: cross(out, rest), "cross-pools": lambda: cross_pools(out, rest), "score": lambda: score(out, rest),
     "sample-plan": lambda: sample_plan(out, int(rest[0]), int(rest[1]), rest[2:]),
     "sample-score": lambda: print("converged" if sample_score(out, int(rest[0]), rest[1:]) else "open")}[cmd]()
