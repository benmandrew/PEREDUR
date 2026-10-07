import sys, random, time, threading, queue, collections
sys.argv = [sys.argv[0]] + sys.argv[1:]
import importlib.util
spec = importlib.util.spec_from_file_location("p", "/home/benandrew/fret-pooled/pooled.py"); p = importlib.util.module_from_spec(spec); spec.loader.exec_module(p)
s, n = sys.argv[1], int(sys.argv[2])
p._slots = queue.Queue(); [p._slots.put(k) for k in range(p.WORKERS)]
nodes = p.load_nodes(s); S = p.subsets(s, nodes)
import numpy as np
c = np.argwhere(np.triu(S | S.T, 1)); rng = random.Random(3)
picks = [tuple(map(int, c[k])) for k in rng.sample(range(len(c)), min(n, len(c)))]
st = {"lock": threading.Lock(), "calls": 0, "queries": 0, "cap": 0, "missing": 0, "rc": 0}
t = time.time(); cnt = collections.Counter()
from concurrent.futures import ThreadPoolExecutor
def one(ij):
    i, j = ij; d = "both" if S[i, j] and S[j, i] else ("fwd" if S[j, i] else "rev")
    return p.compare_batch(nodes, i, [j], d, st)
with ThreadPoolExecutor(p.WORKERS) as ex:
    for g in ex.map(one, picks):
        cnt.update(g.values())
print(s, len(picks), "pairs", dict(cnt), f"{time.time()-t:.0f}s", st)
