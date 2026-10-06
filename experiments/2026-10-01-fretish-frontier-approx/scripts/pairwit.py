"""Ask black for a counterexample word to each fingerprint-surviving implication.

usage: pairwit.py <ltl-with-paths> <fp-dir> <subject> <out.words> <n|all> <seed>... -- <results-dir>...
For each surviving direction x => y (fingerprints do not refute it), solve
(A_x -> G_x) & !(A_y -> G_y) with black. SAT gives a witness word (written in
`fpdraw eval` format, undef filled at random); UNSAT proves the implication;
anything else is undecided. Prints the tally and timing.
"""
import os, random, sys, time, collections
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from calib import runs_in, frontier, load_fp, sub
import draw_black

ltl, fp_dir, subject, out = sys.argv[1:5]
n = sys.argv[5]
rest = sys.argv[6:]; k = rest.index("--"); seeds = [int(s) for s in rest[:k]]; dirs = rest[k + 1:]
lines = open(ltl).read().splitlines(); signals = lines[0].split("\t")[1].split()
spec = {r.split("\t")[0]: r.split("\t")[1:3] for r in lines[1:]}
runs = runs_in(dirs); p = load_fp(fp_dir, subject)
todo = []
for seed in seeds:
    m = frontier(runs[(subject, seed, "directed")]) + frontier(runs[(subject, seed, "uniform")])
    for i, x in enumerate(m):
        for y in m[i + 1:]:
            if sub(p[x], p[y]): todo.append((x, y))
            if sub(p[y], p[x]): todo.append((y, x))
if n != "all":
    random.Random(0).shuffle(todo); todo = todo[:int(n)]
def f(path):
    a, g = spec[path]; return f"(({a}) -> ({g}))"
def one(i_xy):
    i, (x, y) = i_xy; rng = random.Random(i); t = time.time()
    result, model = draw_black.solve(f"{f(x)} & !{f(y)}")
    word = None
    if result == "SAT" and model and model["size"] <= 64 and 0 <= model["loop"] < model["size"]:
        masks = {}
        for s in signals:
            mask = 0
            for t_, st in enumerate(model["states"]):
                v = st.get(s, "undef"); mask |= int(v == "true" if v != "undef" else rng.random() < 0.5) << t_
            masks[s] = mask
        word = f"{model['loop']} {model['size']} " + " ".join(f"{s}={v:x}" for s, v in masks.items())
    return result, time.time() - t, word
with ThreadPoolExecutor(int(os.environ.get("WORKERS", "6"))) as ex:
    got = list(ex.map(one, enumerate(todo)))
c = collections.Counter(r for r, _, _ in got)
ts = sorted(t for _, t, _ in got)
print(f"{len(todo)} directions: {dict(c)}; time median {ts[len(ts)//2]:.2f}s p90 {ts[int(len(ts)*.9)]:.2f}s total {sum(ts):.0f}s", flush=True)
with open(out, "w") as fh:
    fh.writelines(w + "\n" for _, _, w in got if w)
