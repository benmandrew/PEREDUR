"""Sampled paired frontier placement, for subjects whose paired `maximal`
pass is too slow. The population is every member of every run frontier of
one subject, both arms, all seeds. A fixed random order is drawn over it, and
each drawn member x of arm a at seed s runs
`compare --timeout T --repairs <other arm's frontier at seed s> --ideals {x}`.
Any prefix of the order is a simple random sample, so the run can stop at any
row. As in the RQ3 sampler, x is subsumed when some member of the other
frontier is strictly stronger than x, shared when one is equivalent, and
unknown when a query timed out and none of those held.

usage: COMPARE=... TIMEOUT=120 WORKERS=2 STOP=200 \
         python3 sample_paired.py <results-dir> <subject> <out-dir> [seed]
Writes <out-dir>/<subject>.tsv (one row per x, resumable) and the raw compare
output to <out-dir>/<subject>/<rank>.txt."""
import os, random, re, subprocess, sys, tempfile, threading, time
from concurrent.futures import ThreadPoolExecutor

CMP = os.environ["COMPARE"]; T = os.environ.get("TIMEOUT", "120")
WORKERS = int(os.environ.get("WORKERS", "2")); STOP = int(os.environ.get("STOP", "0")) or None
CAP = int(os.environ.get("CAP", "7200"))
results, subject, out = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3]
seed = int(sys.argv[4]) if len(sys.argv) > 4 else 20260930
RUN = re.compile(r"^sweep_O_(directed|uniform)_nsga2-apportion_wkoff_log_(.+)_seed(\d+)$")
REL = re.compile(r"^(\S+)\s+:\s+(equivalent|strictly stronger|strictly weaker|incomparable|timeout)")
OTHER = {"directed": "uniform", "uniform": "directed"}


def frontier(run_dir):
    acc = os.path.join(run_dir, "accumulated"); tsv = os.path.join(acc, "maximal.tsv")
    if not os.path.exists(tsv):
        return []
    return [os.path.join(acc, n) for n in (l.strip() for l in list(open(tsv))[1:]) if n]


runs = {}
for name in os.listdir(results):
    m = RUN.match(name)
    if m and m.group(2) == subject:
        runs[(m.group(1), int(m.group(3)))] = os.path.join(results, name)
fronts = {k: frontier(v) for k, v in runs.items()}
members = sorted((a, s, os.path.basename(f)) for (a, s), fs in fronts.items()
                 for f in fs if (OTHER[a], s) in runs)
order = random.Random(seed).sample(members, len(members))[:STOP]
os.makedirs(f"{out}/{subject}", exist_ok=True)
tsv = f"{out}/{subject}.tsv"
done = set()
if os.path.exists(tsv):
    done = {int(l.split("\t")[0]) for l in open(tsv) if not l.startswith("rank")}
else:
    open(tsv, "w").write("rank\tarm\tseed\tname\tother_frontier\tstronger\tweaker"
                         "\tequivalent\ttimeout\tincomparable\tstatus\twall_s\n")
lock = threading.Lock()
print(f"{subject}: {len(members)} members, {len(order)} drawn, {len(done)} done", flush=True)


def one(rank):
    a, s, name = order[rank]; t = time.time()
    other = fronts[(OTHER[a], s)]
    c = dict.fromkeys(("strictly stronger", "strictly weaker", "equivalent",
                       "timeout", "incomparable"), 0)
    status, text = "ok", ""
    if other:
        with tempfile.TemporaryDirectory(dir="/dev/shm") as d:
            os.makedirs(f"{d}/r"); os.makedirs(f"{d}/x")
            for i, f in enumerate(other):
                os.symlink(f, f"{d}/r/{i:05d}.json")
            os.symlink(os.path.join(runs[(a, s)], "accumulated", name), f"{d}/x/{name}")
            try:
                r = subprocess.run([CMP, "--timeout", T, "--repairs", f"{d}/r",
                                    "--ideals", f"{d}/x"], capture_output=True,
                                   text=True, timeout=CAP)
                text = r.stdout + r.stderr
                status = "ok" if r.returncode == 0 else f"rc{r.returncode}"
            except subprocess.TimeoutExpired:
                status = "cap"
        for m in map(REL.match, (l.strip() for l in text.splitlines())):
            if m:
                c[m.group(2)] += 1
    with lock:
        open(f"{out}/{subject}/{rank}.txt", "w").write(text)
        with open(tsv, "a") as f:
            f.write(f"{rank}\t{a}\t{s}\t{name}\t{len(other)}\t{c['strictly stronger']}"
                    f"\t{c['strictly weaker']}\t{c['equivalent']}\t{c['timeout']}"
                    f"\t{c['incomparable']}\t{status}\t{time.time() - t:.1f}\n")


with ThreadPoolExecutor(WORKERS) as ex:
    list(ex.map(one, [r for r in range(len(order)) if r not in done]))
