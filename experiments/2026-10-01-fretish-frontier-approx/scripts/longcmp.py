"""Sample fingerprint-surviving pairs of one (subject, seed) and ask compare at a long budget."""
import os, random, sys
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault("MAXIMAL", "unused")
import hybrid
from calib import runs_in, frontier, load_fp, sub
fp_dir, subject, seed, n, dirs = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5:]
runs = runs_in(dirs); p = load_fp(fp_dir, subject)
m = frontier(runs[(subject, seed, "directed")]) + frontier(runs[(subject, seed, "uniform")])
pairs = [(x, y) for i, x in enumerate(m) for y in m[i + 1:] if sub(p[x], p[y]) or sub(p[y], p[x])]
random.Random(0).shuffle(pairs); pairs = pairs[:n]
with ThreadPoolExecutor(int(os.environ.get("WORKERS", "6"))) as ex:
    got = list(ex.map(lambda xy: hybrid.query(xy[0], [xy[1]])[xy[1]], pairs))
from collections import Counter
print(subject, seed, f"{len(pairs)} sampled pairs at T={hybrid.T}s:", dict(Counter(got)))
