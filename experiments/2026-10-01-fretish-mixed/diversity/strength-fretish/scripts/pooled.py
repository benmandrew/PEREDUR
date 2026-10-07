"""Pooled-frontier strength of FRETISH directed against uniform mutation.

usage: python3 pooled.py prep <subject>...    pool, dedup, fingerprint, count candidate pairs
       python3 pooled.py run <subject>...     within-arm frontiers, then the cross-arm matrix
       python3 pooled.py check <subject> <n>  fingerprint soundness spot check
       python3 pooled.py summary              per-subject CSV, relations CSV, Wilcoxon

Pool: for each arm, every member of every run's accumulated/maximal.tsv over
the 30 seeds of results-fretish-ablation (seeds 0-14 copied from av2 into
av2runs/, seeds 15-29 from av3), the input frontier_triple.py and hybrid3.py
read. Members with identical normalised JSON are one node, and a node holds
the arms it came from.

Each node is evaluated on the fmix-front word set of its subject (fpdraw
eval, FPDRAW_UNGLUE=1 on rad-core-* and mode-arbiter as in hybrid_go.sh). A
word x accepts and y rejects refutes x => y. Every direction that no word
refutes goes to `compare` (5dc3472 plus compare-directions.patch, which skips
the refuted direction and prints each direction's verdict, 1/0/?). Words
decide cost, never a verdict.

Within each arm a node is dominated when another node of that arm strictly
implies it (a timed-out direction reads as no implication, as in `maximal`);
the undominated nodes, merged by equivalence, are the arm's frontier classes.
Then every directed class meets every uniform class in both directions.
"""
import collections, csv, json, os, re, signal, subprocess, sys, tempfile, threading, time, random, resource
from concurrent.futures import ThreadPoolExecutor
import numpy as np

H = os.path.expanduser("~/fret-pooled")
CMP = os.environ.get("COMPARE", f"{H}/bin/compare")
FPDRAW = os.path.expanduser("~/fmix-front/fpdraw")
WORDS = os.path.expanduser("~/fmix-front/words")
TP = os.path.expanduser("~/projects/counter/build-release/third_party")
A3 = os.path.expanduser("~/projects/counter/experiments/results-fretish-ablation")
A2 = f"{H}/av2runs"
WORKERS = int(os.environ.get("WORKERS", "8"))
CPUS = int(os.environ.get("CPUS", "2"))       # affinity per compare call = its thread pool size
CPU0 = int(os.environ.get("CPU0", "16"))
VMEM_KB = int(os.environ.get("VMEM_KB", str(8 * 1024 * 1024)))
BATCH = int(os.environ.get("BATCH", "48"))
QUERY_S = 20                                   # compare's fixed per-direction budget at 5dc3472
ARMS = ("directed", "uniform")
RUN = re.compile(r"^sweep_O_(directed|uniform)_nsga2-apportion_wkoff_log_(.+)_seed(\d+)$")
ENV = dict(os.environ, PEREDUR_BLACK_PATH=f"{TP}/black/black",
           PEREDUR_SPOT_BIN_DIR=f"{TP}/spot/bin", PEREDUR_GANAK_PATH=f"{TP}/ganak/bin/ganak")


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def members(subject):
    out = []
    for root, lo, hi in ((A2, 0, 14), (A3, 15, 29)):
        for name in sorted(os.listdir(root)):
            m = RUN.match(name)
            if not m or m.group(2) != subject or not lo <= int(m.group(3)) <= hi:
                continue
            acc = os.path.join(root, name, "accumulated")
            tsv = os.path.join(acc, "maximal.tsv")
            if not os.path.exists(tsv):
                continue
            for n in list(open(tsv))[1:]:
                if n.strip():
                    out.append((m.group(1), int(m.group(3)), os.path.join(acc, n.strip())))
    return out


def sdir(subject):
    d = f"{H}/subjects/{subject}"; os.makedirs(d, exist_ok=True); return d


def load_nodes(subject):
    d = sdir(subject)
    rows = list(csv.DictReader(open(f"{d}/nodes.csv")))
    return [(r["path"], set(r["arms"].split("+")), int(r["n_d"]), int(r["n_u"])) for r in rows]


def prep(subject):
    d = sdir(subject); t = time.time()
    mem = members(subject)
    groups = collections.OrderedDict()
    for arm, seed, path in mem:
        k = json.dumps(json.load(open(path)), sort_keys=True)
        g = groups.setdefault(k, {"path": path, "n": collections.Counter()})
        g["n"][arm] += 1
    with open(f"{d}/nodes.csv", "w", newline="") as f:
        w = csv.writer(f); w.writerow(["node", "path", "arms", "n_d", "n_u"])
        for i, g in enumerate(groups.values()):
            w.writerow([i, g["path"], "+".join(a for a in ARMS if g["n"][a]),
                        g["n"]["directed"], g["n"]["uniform"]])
    nodes = load_nodes(subject)
    open(f"{d}/list", "w").write("".join(p + "\n" for p, *_ in nodes))
    if nodes:
        unglue = "1" if subject.startswith("rad-core-") or subject == "mode-arbiter" else "0"
        with open(f"{d}/fp.tsv", "w") as out, open(f"{d}/fp.err", "w") as err:
            subprocess.run([FPDRAW, "eval", f"{WORDS}/{subject}.words", f"{d}/list", "8"],
                           stdout=out, stderr=err, check=True,
                           env=dict(ENV, FPDRAW_UNGLUE=unglue))
    S = subsets(subject, nodes)
    held = {a: np.array([a in arms for _, arms, *_ in nodes], dtype=bool) for a in ARMS}
    cand = S | S.T
    np.fill_diagonal(cand, False)
    within = {a: int(np.triu(cand & np.outer(held[a], held[a])).sum()) for a in ARMS}
    cross = int((cand & np.outer(held["directed"], held["uniform"])).sum())
    nd = sum(1 for _, a, *_ in nodes if "directed" in a); nu = sum(1 for _, a, *_ in nodes if "uniform" in a)
    log(f"PREP {subject} members d={sum(1 for m in mem if m[0]=='directed')} "
        f"u={sum(1 for m in mem if m[0]=='uniform')} nodes={len(nodes)} d_nodes={nd} u_nodes={nu} "
        f"cand_within_d={within['directed']} cand_within_u={within['uniform']} "
        f"cand_cross_allnodes={cross} pairs_cross={nd*nu} {time.time()-t:.0f}s")


def subsets(subject, nodes):
    """S[i, j]: no word refutes i => j."""
    n = len(nodes)
    if n == 0:
        return np.zeros((0, 0), dtype=bool)
    cache = f"{sdir(subject)}/subset.npy"
    if os.path.exists(cache):
        return np.load(cache)
    fp = {}
    for line in open(f"{sdir(subject)}/fp.tsv"):
        p, _, digits = line.rstrip("\n").partition("\t")
        fp[p] = bytes.fromhex(digits)
    width = len(next(iter(fp.values()))); pad = (-width) % 8
    x = np.frombuffer(b"".join(fp[p] + b"\0" * pad for p, *_ in nodes), dtype=np.uint64).reshape(n, -1)
    nx = ~x
    S = np.zeros((n, n), dtype=bool)
    for i in range(n):
        S[i] = ~((x[i][None, :] & nx).any(axis=1))
    np.save(cache, S)
    return S


class Verdicts:
    """v[(a, b)] in '1', '0', '?': node a implies node b. Persisted per subject."""

    def __init__(self, subject):
        self.path = f"{sdir(subject)}/verdicts.tsv"; self.v = {}; self.lock = threading.Lock()
        if os.path.exists(self.path):
            for line in open(self.path):
                a, b, r = line.split()
                self.v[(int(a), int(b))] = r
        self.f = open(self.path, "a")

    def put(self, items):
        with self.lock:
            for (a, b), r in items:
                self.v[(a, b)] = r
                self.f.write(f"{a}\t{b}\t{r}\n")
            self.f.flush()


_slots = None


def cpu_slot():
    return _slots.get()


def limit():
    resource.setrlimit(resource.RLIMIT_AS, (VMEM_KB * 1024, VMEM_KB * 1024))


def compare_batch(nodes, x, ys, dirs, stats):
    """x and ys are node ids. fwd = y => x, rev = x => y. Returns {(a, b): r}."""
    slot = _slots.get()
    try:
        cpus = f"{CPU0 + slot * CPUS}-{CPU0 + slot * CPUS + CPUS - 1}"
        with tempfile.TemporaryDirectory(dir="/dev/shm") as d:
            os.makedirs(f"{d}/r"); os.makedirs(f"{d}/x")
            for i, y in enumerate(ys):
                os.symlink(nodes[y][0], f"{d}/r/{i:05d}.json")
            os.symlink(nodes[x][0], f"{d}/x/x.json")
            ndir = 2 if dirs == "both" else 1
            cap = 120 + (QUERY_S + 5) * len(ys) * ndir // CPUS
            p = subprocess.Popen(["taskset", "-c", cpus, CMP, "--repairs", f"{d}/r", "--ideals", f"{d}/x"],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                 env=dict(ENV, COMPARE_DIRECTIONS=dirs), preexec_fn=limit,
                                 start_new_session=True)
            try:
                out, err = p.communicate(timeout=cap); capped = False
            except subprocess.TimeoutExpired:
                os.killpg(p.pid, signal.SIGKILL); out, err = p.communicate(); capped = True
    finally:
        _slots.put(slot)
    got = {}
    for line in err.splitlines():
        if line.startswith("DIR\t"):
            _, rep, _ide, fwd, rev = line.split("\t")
            y = ys[int(rep[:5])]
            if dirs != "rev":
                got[(y, x)] = fwd
            if dirs != "fwd":
                got[(x, y)] = rev
    missing = 0
    for y in ys:
        for k in ([(y, x)] if dirs != "rev" else []) + ([(x, y)] if dirs != "fwd" else []):
            if k not in got:
                got[k] = "?"; missing += 1
    with stats["lock"]:
        stats["calls"] += 1; stats["queries"] += len(ys) * ndir
        stats["cap"] += capped; stats["missing"] += missing
        if p.returncode not in (0, -9) and not capped:
            stats["rc"] += 1
            if stats["rc"] < 5:
                log("compare rc", p.returncode, err[-400:])
    return got


def query_pairs(subject, nodes, S, V, pairs, label, quiet=False):
    """pairs: iterable of unordered (i, j). Queries every direction no word refutes
    and whose verdict is not cached; sets refuted directions to '0'."""
    todo = collections.defaultdict(list); refuted = []
    for i, j in pairs:
        if i > j:
            i, j = j, i
        need_f = S[j, i] and (j, i) not in V.v      # fwd: y=j => x=i
        need_r = S[i, j] and (i, j) not in V.v      # rev: x=i => y=j
        if not S[j, i] and (j, i) not in V.v:
            refuted.append(((j, i), "0"))
        if not S[i, j] and (i, j) not in V.v:
            refuted.append(((i, j), "0"))
        if need_f or need_r:
            todo[(i, "both" if need_f and need_r else "fwd" if need_f else "rev")].append(j)
    V.put(refuted)
    batches = [(x, ys[k:k + BATCH], dirs) for (x, dirs), ys in todo.items()
               for k in range(0, len(ys), BATCH)]
    nq = sum(len(b[1]) * (2 if b[2] == "both" else 1) for b in batches)
    if not quiet:
        log(f"{subject} {label}: {len(batches)} calls, {nq} directed queries")
    stats = {"lock": threading.Lock(), "calls": 0, "queries": 0, "cap": 0, "missing": 0, "rc": 0}
    t = time.time(); last = [t]

    def job(b):
        got = compare_batch(nodes, *b, stats)
        V.put(got.items())
        if time.time() - last[0] > 300:
            last[0] = time.time()
            log(f"{subject} {label}: {stats['calls']}/{len(batches)} calls, {stats['queries']}/{nq} q, "
                f"{time.time()-t:.0f}s")

    with ThreadPoolExecutor(WORKERS) as ex:
        list(ex.map(job, batches))
    (lambda *a: None if quiet and not stats['cap'] and not stats['rc'] else log(*a))(f"{subject} {label}: done {stats['calls']} calls {stats['queries']} q, cap={stats['cap']} "
        f"missing={stats['missing']} rc={stats['rc']} {time.time()-t:.0f}s")
    return nq, time.time() - t


INC_THRESHOLD = int(os.environ.get("INC_THRESHOLD", "50000"))
WINDOW = int(os.environ.get("WINDOW", "64"))


def popcounts(subject, nodes):
    fp = {}
    for line in open(f"{sdir(subject)}/fp.tsv"):
        p, _, digits = line.rstrip("\n").partition("\t")
        fp[p] = bin(int(digits, 16)).count("1")
    return [fp[p] for p, *_ in nodes]


def frontier_incremental(subject, nodes, S, V, arm):
    """Antichain insertion for an arm whose candidate pairs are too many to
    query all. Nodes enter in ascending order of accepted words, so a node can
    only be implied by one entered before it or in its window. A node is
    checked against the current frontier representatives only: a node implied
    by a dominated node is implied by that node's dominator too, since the
    implication order is transitive (a timeout can break the chain, as it can
    in maximal). Returns classes, representative first."""
    pop = popcounts(subject, nodes)
    ids = sorted((i for i, (_, arms, *_) in enumerate(nodes) if arm in arms), key=lambda i: (pop[i], i))
    reps = []; members = {}
    t = time.time()
    for w0 in range(0, len(ids), WINDOW):
        win = ids[w0:w0 + WINDOW]
        pairs = [(f, n) for n in win for f in reps if S[f, n] or S[n, f]]
        pairs += [(a, b) for k, a in enumerate(win) for b in win[k + 1:] if S[a, b] or S[b, a]]
        if pairs:
            query_pairs(subject, nodes, S, V, pairs, f"inc-{arm}-{w0}", quiet=True)
        imp = lambda a, b: V.v.get((a, b), "0") == "1"
        for n in win:
            home = None; dominated = False
            for f in reps:
                if imp(f, n):
                    if imp(n, f):
                        home = f
                    else:
                        dominated = True
                    break
            if dominated:
                continue
            if home is not None:
                members[home].append(n); continue
            beaten = [f for f in reps if imp(n, f) and not imp(f, n)]
            for f in beaten:
                reps.remove(f); del members[f]
            reps.append(n); members[n] = [n]
        if (w0 // WINDOW) % 10 == 0:
            log(f"{subject} inc-{arm}: {w0 + len(win)}/{len(ids)} entered, {len(reps)} reps, {time.time()-t:.0f}s")
    return [members[f] for f in reps]


def implied(V, a, b, undecided):
    r = V.v.get((a, b), "0")
    return r == "1" or (r == "?" and undecided)


def frontier(nodes, S, V, arm):
    ids = [i for i, (_, arms, *_) in enumerate(nodes) if arm in arms]
    idx = np.array(ids, dtype=int)
    dominated = set(); parent = {i: i for i in ids}

    def find(n):
        while parent[n] != n:
            parent[n] = parent[parent[n]]; n = parent[n]
        return n
    sub = S[np.ix_(idx, idx)]
    for a_, b_ in zip(*np.nonzero(np.triu(sub | sub.T, 1))):
        a, b = ids[a_], ids[b_]
        ab, ba = V.v.get((a, b), "0") == "1", V.v.get((b, a), "0") == "1"
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


def run(subject):
    t0 = time.time(); d = sdir(subject)
    nodes = load_nodes(subject)
    if not nodes:
        json.dump({"subject": subject, "empty": True}, open(f"{d}/result.json", "w")); return
    S = subsets(subject, nodes); V = Verdicts(subject)
    cost = {}; F = {}; method = {}
    for arm in ARMS:
        ids = [i for i, (_, arms, *_) in enumerate(nodes) if arm in arms]
        idx = np.array(ids, dtype=int)
        sub = S[np.ix_(idx, idx)]
        pairs = [(ids[a], ids[b]) for a, b in zip(*np.nonzero(np.triu(sub | sub.T, 1)))]
        t = time.time(); q0 = len(V.v)
        if len(pairs) > INC_THRESHOLD:
            log(f"{subject} within-{arm}: {len(pairs)} candidate pairs, incremental antichain")
            F[arm] = frontier_incremental(subject, nodes, S, V, arm); method[arm] = "incremental"
            cost[arm] = (len(V.v) - q0, time.time() - t)
        else:
            cost[arm] = query_pairs(subject, nodes, S, V, pairs, f"within-{arm}")
            F[arm] = frontier(nodes, S, V, arm); method[arm] = "all-pairs"
    rep = {arm: [c[0] for c in F[arm]] for arm in ARMS}
    pairs = [(x, y) for x in rep["directed"] for y in rep["uniform"] if x != y and (S[x, y] or S[y, x])]
    cost["cross"] = query_pairs(subject, nodes, S, V, pairs, "cross")
    res = {"subject": subject, "frontier": F, "cost": cost, "method": method, "wall_s": time.time() - t0}
    json.dump(res, open(f"{d}/result.json", "w"))
    log(f"RUN {subject} d_front={len(F['directed'])} u_front={len(F['uniform'])} {time.time()-t0:.0f}s")


def check(subject, n):
    """Run compare on n random pairs that words refute in exactly one direction."""
    nodes = load_nodes(subject); S = subsets(subject, nodes)
    one = np.argwhere(S ^ S.T)
    rng = random.Random(7); picks = [tuple(one[k]) for k in rng.sample(range(len(one)), min(n, len(one)))]
    bad = 0; dec = 0
    for i, j in picks:
        # refuted direction is the one with S False
        a, b = (i, j) if not S[i, j] else (j, i)
        x, y = b, a  # compare: fwd = y => x = a => b, the refuted direction
        global _slots
        stats = {"lock": threading.Lock(), "calls": 0, "queries": 0, "cap": 0, "missing": 0, "rc": 0}
        got = compare_batch(nodes, x, [y], "fwd", stats)
        r = got[(y, x)]
        dec += r != "?"; bad += r == "1"
        if r == "1":
            log("UNSOUND", subject, nodes[a][0], "=>", nodes[b][0])
    log(f"CHECK {subject}: {len(picks)} refuted directions, {dec} decided, {bad} implications")


def classify(V, d, u, mode):
    """mode: 'decided', 'imp' (? as implication), 'non' (? as non-implication)."""
    a, b = V.v.get((d, u), "0"), V.v.get((u, d), "0")
    if mode == "decided" and "?" in (a, b):
        return "undecided"
    t = lambda r: r == "1" or (r == "?" and mode == "imp")
    return {(True, True): "equivalent", (True, False): "d_stronger",
            (False, True): "u_stronger", (False, False): "incomparable"}[(t(a), t(b))]


def summary(subjects):
    from scipy.stats import wilcoxon
    rows = []; rel_rows = []
    for s in subjects:
        d = sdir(s); p = f"{d}/result.json"
        if not os.path.exists(p):
            continue
        res = json.load(open(p))
        if res.get("empty"):
            continue
        nodes = load_nodes(s); V = Verdicts(s); F = res["frontier"]
        rep = {a: [c[0] for c in F[a]] for a in ARMS}
        row = {"subject": s,
               "d_pool": sum(n[2] for n in nodes), "u_pool": sum(n[3] for n in nodes),
               "d_nodes": sum("directed" in n[1] for n in nodes),
               "u_nodes": sum("uniform" in n[1] for n in nodes),
               "both_nodes": sum(len(n[1]) == 2 for n in nodes),
               "d_front": len(rep["directed"]), "u_front": len(rep["uniform"])}
        within_to = {a: sum(1 for (x, y), r in V.v.items() if r == "?" and a in nodes[x][1] and a in nodes[y][1]) for a in ARMS}
        row["within_undecided_d"], row["within_undecided_u"] = within_to["directed"], within_to["uniform"]
        S = subsets(s, nodes)
        rd, ru = rep["directed"], rep["uniform"]
        urep_of = {m: c[0] for c in F["uniform"] for m in c}
        same = set()
        for c in F["directed"]:
            for m in c:
                if m in urep_of:
                    same.add((c[0], urep_of[m]))
        rdi, rui = np.array(rd, dtype=int), np.array(ru, dtype=int)
        cand = S[np.ix_(rdi, rui)] | S[np.ix_(rui, rdi)].T
        pairs = {(rd[i], ru[j]) for i, j in zip(*np.nonzero(cand))} | same
        for mode in ("decided", "imp", "non"):
            subd, subu, shd, shu = set(), set(), set(), set(); counts = collections.Counter()
            und_d, und_u = set(), set()
            for x, y in pairs:
                r = "equivalent" if (x, y) in same or x == y else classify(V, x, y, mode)
                counts[r] += 1
                if r == "u_stronger": subd.add(x)
                elif r == "d_stronger": subu.add(y)
                elif r == "equivalent": shd.add(x); shu.add(y)
                elif r == "undecided": und_d.add(x); und_u.add(y)
                if mode == "decided" and r != "incomparable":
                    rel_rows.append({"subject": s, "d_class": x, "u_class": y,
                                     "d_path": nodes[x][0], "u_path": nodes[y][0],
                                     "d_implies_u": "1" if (x, y) in same else V.v.get((x, y), "0"),
                                     "u_implies_d": "1" if (x, y) in same else V.v.get((y, x), "0"),
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
                            "pairs_d_stronger": counts["d_stronger"], "pairs_u_stronger": counts["u_stronger"],
                            "pairs_incomparable": counts["incomparable"], "pairs_undecided": counts["undecided"],
                            "d_classes_with_undecided": len(und_d - subd - shd),
                            "u_classes_with_undecided": len(und_u - subu - shu)})
        row["method_d"], row["method_u"] = (res.get("method", {}).get(a, "all-pairs") for a in ARMS)
        c = res["cost"]
        row["queries"] = sum(v[0] for v in c.values()); row["query_wall_s"] = round(sum(v[1] for v in c.values()))
        row["wall_s"] = round(res["wall_s"])
        rows.append(row)
    cols = list(rows[0].keys())
    with open(f"{H}/subjects.csv", "w", newline="") as f:
        w = csv.DictWriter(f, cols); w.writeheader(); w.writerows(rows)
    with open(f"{H}/relations.csv", "w", newline="") as f:
        w = csv.DictWriter(f, list(rel_rows[0].keys()) if rel_rows else ["subject"]); w.writeheader(); w.writerows(rel_rows)
    for sfx in ("", "_imp", "_non"):
        net = [r[f"net{sfx}"] for r in rows if r[f"net{sfx}"] != ""]
        pos = sum(v > 0 for v in net); neg = sum(v < 0 for v in net)
        try:
            p = wilcoxon(net).pvalue
        except ValueError:
            p = float("nan")
        print(f"net{sfx or '_decided'}: n={len(net)} median={np.median(net):.4f} mean={np.mean(net):.4f} "
              f"u>d {pos}, d>u {neg}, tie {len(net)-pos-neg}, Wilcoxon p={p:.4g}")
    for r in rows:
        print(r["subject"], r["d_front"], r["u_front"], r["sub_d"], r["sub_u"], r["shared_d"], r["pairs_undecided"])


def main():
    global _slots
    import queue
    _slots = queue.Queue()
    for k in range(WORKERS):
        _slots.put(k)
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "prep":
        for s in args:
            prep(s)
    elif cmd == "run":
        for s in args:
            run(s)
    elif cmd == "check":
        check(args[0], int(args[1]))
    elif cmd == "summary":
        summary(args or sorted(os.listdir(f"{H}/subjects")))


if __name__ == "__main__":
    main()
