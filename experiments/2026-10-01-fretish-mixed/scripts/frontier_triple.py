"""Three-way frontier share: per (subject, seed), the joint frontier of the
directed, uniform and mixed runs at that seed, and each arm's share of it.

Usage: MAXIMAL=... CAP=1800 JOBS=4 WORKERS=4 \
    python3 frontier_triple.py <out.csv> <results-dir>...
The results dirs are searched together, so the archived ablation runs
(results-fretish-ablation) and the mixed runs (results-fretish-mixed) can sit
in different places. Each run's frontier is its accumulated/maximal.tsv, so
censored runs count. One `maximal` call on the union gives the joint frontier,
counted in classes: a class is held by every arm with a member in it. The
share of an arm is held/joint. `maximal` names only the first of a
structurally identical group, so groups are rebuilt from normalised JSON.
Resumes: triples already in <out.csv> are skipped. Generalises the ablation's
frontier_paired.py from two arms to three.
"""
import collections, csv, json, os, re, subprocess, sys, tempfile, threading, time
from concurrent.futures import ThreadPoolExecutor

MAXIMAL = os.environ["MAXIMAL"]; CAP = int(os.environ.get("CAP", "1800"))
JOBS = os.environ.get("JOBS", "4"); WORKERS = int(os.environ.get("WORKERS", "4"))
ARMS = ("directed", "uniform", "mixed")
LINE = re.compile(r"^class\s+(\d+)\s+(\S+)")
RUN = re.compile(r"^sweep_O_(directed|uniform|mixed)_nsga2-apportion_wkoff_log_(.+)_seed(\d+)$")
COLS = (["subject", "seed"] + [f"{a[0]}_frontier" for a in ARMS] + ["joint"]
        + [f"{a[0]}_held" for a in ARMS] + [f"{a[0]}_only" for a in ARMS]
        + ["status", "wall_s"])
# Cheapest first, by the ablation curve pass's per-run cost on av3.
ORDER = ["rad-core-10", "fsm-lmcps", "rad-core-65", "rad-core-1-18",
         "rad-core-12-18", "rad-core-17-18", "rad-core-45", "rad-core-55",
         "rad-core-61", "rad-core-68", "fsm", "mode-arbiter", "fsm-timing",
         "takeoff", "fsm-combined", "rad-core-33", "lpc-full-core1",
         "liquid-mixer", "valu3s-uc6", "lpc-mini-core1"]

out_path = sys.argv[1]
runs = {}
for results in map(os.path.abspath, sys.argv[2:]):
    for name in os.listdir(results):
        m = RUN.match(name)
        if m:
            runs[(m.group(2), int(m.group(3)), m.group(1))] = os.path.join(results, name)
triples = sorted({(s, sd) for s, sd, _ in runs
                  if all((s, sd, a) in runs for a in ARMS)},
                 key=lambda p: (ORDER.index(p[0]) if p[0] in ORDER else 99, p[1]))
done = set()
if os.path.exists(out_path):
    done = {(r["subject"], int(r["seed"])) for r in csv.DictReader(open(out_path))}
out = open(out_path, "a", newline=""); w = csv.writer(out); lock = threading.Lock()
if not done:
    w.writerow(COLS); out.flush()


def frontier(run_dir):
    acc = os.path.join(run_dir, "accumulated")
    tsv = os.path.join(acc, "maximal.tsv")
    if not os.path.exists(tsv):
        return []
    names = [l.strip() for l in open(tsv)][1:]
    return [os.path.join(acc, n) for n in names if n]


def key(path):
    return json.dumps(json.load(open(path)), sort_keys=True)


def one(triple):
    subject, seed = triple; t = time.time()
    files = {a: frontier(runs[(subject, seed, a)]) for a in ARMS}
    groups = collections.defaultdict(set); arm_of = {}
    for a, fs in files.items():
        for f in fs:
            groups[key(f)].add(a)
    base = [subject, seed] + [len(files[a]) for a in ARMS]
    if not any(files.values()):
        return base + [0] * 7 + ["empty", 0.0]
    with tempfile.TemporaryDirectory(dir="/dev/shm") as d:
        for a, fs in files.items():
            for i, f in enumerate(fs):
                link = os.path.join(d, f"{a[0]}{i:05d}.json")
                os.symlink(f, link); arm_of[link] = f
        try:
            r = subprocess.run([MAXIMAL, d, "--jobs", JOBS], capture_output=True,
                               text=True, timeout=CAP)
        except subprocess.TimeoutExpired:
            return base + [""] * 7 + ["cap", round(time.time() - t, 1)]
        if r.returncode != 0:
            return base + [""] * 7 + [f"rc{r.returncode}", round(time.time() - t, 1)]
        joint = [(int(m.group(1)), arm_of[m.group(2)]) for m in
                 map(LINE.match, r.stdout.splitlines()) if m]
    cls = collections.defaultdict(set)
    for c, f in joint:
        cls[c] |= groups[key(f)]
    held = [sum(a in s for s in cls.values()) for a in ARMS]
    only = [sum(s == {a} for s in cls.values()) for a in ARMS]
    return base + [len(cls)] + held + only + ["ok", round(time.time() - t, 1)]


def job(triple):
    row = one(triple)
    with lock:
        w.writerow(row); out.flush()
        print(f"{row[0]} seed{row[1]} {row[-2]} {row[-1]}s", flush=True)


todo = [p for p in triples if p not in done]
print(f"{len(triples)} triples, {len(done)} done, {len(todo)} to run", flush=True)
with ThreadPoolExecutor(WORKERS) as ex:
    list(ex.map(job, todo))
