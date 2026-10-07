"""Sample estimate of the pooled strength shares for one subject too dense to run whole.
usage: python3 sample_pooled.py <subject> [max_per_arm]
Draws nodes of each arm in a fixed random order (seed 7), interleaving the arms.
For a drawn node x of arm A, against the other arm B:
  on_front  no node of A strictly implies x (a timed-out direction reads as no
            implication, as in pooled.py and maximal)
  subsumed  on_front, and some node of B strictly implies x. A node of B that
            strictly implies x has a frontier dominator that does too, so this
            matches pooled.py's test against B's frontier classes.
  shared    on_front, not subsumed, and some node of B is equivalent to x
  undecided on_front, not subsumed or shared, and some B => x direction timed out
The estimate of sub_A is subsumed / on_front over the drawn nodes, a ratio
estimator with a normal 95% half-width. It weights frontier classes by their
node count, where pooled.py counts each class once. Any prefix of rows.tsv is a
random sample, so the run can stop at any row.
"""
import os, queue, random, sys, threading, time, math
from concurrent.futures import ThreadPoolExecutor
import pooled as P

subject = sys.argv[1]
max_per_arm = int(sys.argv[2]) if len(sys.argv) > 2 else 400
HALF = float(os.environ.get("HALF", "0.10"))
MIN_FRONT = int(os.environ.get("MIN_FRONT", "30"))
P._slots = queue.Queue()
for k in range(P.WORKERS):
    P._slots.put(k)
nodes = P.load_nodes(subject)
S = P.subsets(subject, nodes)
V = P.Verdicts(subject)
held = {a: [i for i, (_, arms, *_) in enumerate(nodes) if a in arms] for a in P.ARMS}
out_path = f"{P.sdir(subject)}/sample.tsv"
done = set()
if os.path.exists(out_path):
    for line in list(open(out_path))[1:]:
        f = line.split("\t"); done.add((f[0], int(f[1])))
out = open(out_path, "a")
if not done:
    out.write("arm\tnode\ton_front\tsubsumed\tshared\tundecided\tqueries\twall_s\n"); out.flush()
lock = threading.Lock()
rows = {a: [] for a in P.ARMS}
if done:
    for line in list(open(out_path))[1:]:
        f = line.rstrip("\n").split("\t")
        rows[f[0]].append(tuple(int(v) for v in f[2:6]))


def ask(x, ys, dirs, stats):
    """Directions y => x (fwd) or x => y (rev) for ys, cached in V."""
    key = (lambda y: (y, x)) if dirs == "fwd" else (lambda y: (x, y))
    need = [y for y in ys if key(y) not in V.v]
    for k in range(0, len(need), P.BATCH):
        V.put(P.compare_batch(nodes, x, need[k:k + P.BATCH], dirs, stats).items())
    return {y: V.v[key(y)] for y in ys}


def strict_implier(x, cands, stats):
    """Returns ('strict'|'equiv'|'undecided'|None) for the strongest relation found,
    stopping at the first strict implier. Batches so the stop saves work."""
    found = None
    for k in range(0, len(cands), P.BATCH):
        part = cands[k:k + P.BATCH]
        fwd = ask(x, part, "fwd", stats)
        hits = [y for y in part if fwd[y] == "1"]
        if any(fwd[y] == "?" for y in part) and found is None:
            found = "undecided"
        if hits:
            rev = ask(x, hits, "rev", stats)
            if any(rev[y] != "1" for y in hits):
                return "strict"
            found = "equiv"
    return found


def one(arm, x):
    t = time.time(); other = "uniform" if arm == "directed" else "directed"
    stats = {"lock": threading.Lock(), "calls": 0, "queries": 0, "cap": 0, "missing": 0, "rc": 0}
    own = [y for y in held[arm] if y != x and S[y, x]]
    random.Random(x).shuffle(own)
    on_front = strict_implier(x, own, stats) != "strict"
    sub = sh = und = 0
    if on_front:
        cross = [y for y in held[other] if y != x and S[y, x]]
        random.Random(x + 1).shuffle(cross)
        r = strict_implier(x, cross, stats)
        sub = r == "strict"; sh = not sub and (r == "equiv" or x in held[other])
        und = not sub and not sh and r == "undecided"
    row = (int(on_front), int(sub), int(sh), int(und))
    with lock:
        rows[arm].append(row)
        out.write(f"{arm}\t{x}\t" + "\t".join(map(str, row)) + f"\t{stats['queries']}\t{time.time()-t:.0f}\n"); out.flush()
    return arm


def est(arm):
    r = rows[arm]; f = [v for v in r if v[0]]
    if not f:
        return float("nan"), float("inf"), 0, len(r)
    p = sum(v[1] for v in f) / len(f)
    return p, 1.96 * math.sqrt(max(p * (1 - p), 1e-9) / len(f)), len(f), len(r)


order = {a: [x for x in held[a]] for a in P.ARMS}
for a in P.ARMS:
    random.Random(7).shuffle(order[a])
    order[a] = [x for x in order[a][:max_per_arm] if (a, x) not in done]
jobs = [j for pair in zip([("directed", x) for x in order["directed"]],
                          [("uniform", x) for x in order["uniform"]]) for j in pair]
P.log(f"SAMPLE {subject} start, {len(done)} rows cached, {len(jobs)} to draw")
stop = threading.Event()


def gated(job):
    if stop.is_set():
        return
    one(*job)
    with lock:
        e = {a: est(a) for a in P.ARMS}
        if all(e[a][1] <= HALF and e[a][2] >= MIN_FRONT for a in P.ARMS):
            stop.set()
        if sum(len(v) for v in rows.values()) % 10 == 0:
            P.log("SAMPLE " + " ".join(f"{a}: sub={e[a][0]:.3f}±{e[a][1]:.3f} front={e[a][2]}/{e[a][3]}" for a in P.ARMS))


with ThreadPoolExecutor(int(os.environ.get("XPAR", "4"))) as ex:
    list(ex.map(gated, jobs))
e = {a: est(a) for a in P.ARMS}
P.log(f"SAMPLE_DONE {subject} " + " ".join(f"{a}: sub={e[a][0]:.4f}±{e[a][1]:.4f} front={e[a][2]}/{e[a][3]}" for a in P.ARMS))
