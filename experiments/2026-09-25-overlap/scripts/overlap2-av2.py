"""Two-stage AuRUS frontier for families whose pooled maximal pass hit the
cap: reduce each run's solutions to its own frontier, pool the survivors and
reduce again, then build the matrix as overlap.py does. The pooled frontier
is the same set, since a solution dominated within its run is dominated in
the pool."""
import os, re, shutil, subprocess, sys, time, tempfile, collections
H = os.path.expanduser("~/overlap"); B = os.path.expanduser("~/projects/counter/build-release")
SURV = re.compile(r"^class\s+\d+\s+(.+?)(?:\s+\(\+\d+ identical\))?$")
REL = re.compile(r"^(\S+)\s+:\s+(equivalent|strictly stronger|strictly weaker|incomparable|timeout)")
CAP = int(os.environ.get("CAP", "7200")); log = open(f"{H}/out/log.tsv", "a")
def maximal(files, jobs):
    with tempfile.TemporaryDirectory() as d:
        for f in files: os.symlink(f, f"{d}/{os.path.basename(f)}")
        try:
            r = subprocess.run([f"{B}/maximal", d, "--jobs", str(jobs)], capture_output=True, text=True, timeout=CAP)
        except subprocess.TimeoutExpired:
            return None
        return [os.path.realpath(m.group(1)) if os.path.islink(m.group(1)) else m.group(1)
                for m in map(SURV.match, (l.strip() for l in r.stdout.splitlines())) if m]
for fam in sys.argv[1:]:
    o = f"{H}/out/{fam}"; shutil.rmtree(o, ignore_errors=True); os.makedirs(o)
    pool = f"{H}/pool/{fam}/A"; by = collections.defaultdict(list)
    for f in sorted(os.listdir(pool)): by[f.split("_")[0]].append(f"{pool}/{f}")
    t = time.time(); surv = []; capped = 0
    for seed, files in sorted(by.items()):
        s = maximal(files, 30)
        if s is None: capped += 1; surv += files
        else: surv += [f"{pool}/{os.path.basename(x)}" for x in s]
    open(f"{o}/runs-survivors-A.txt", "w").write("\n".join(os.path.basename(x) for x in surv) + "\n")
    log.write(f"{fam}\tmaximal-A-runs\t{len(surv)} capped={capped}\t{time.time()-t:.1f}\n"); log.flush()
    t = time.time(); front = maximal(surv, 30)
    if front is not None: open(f"{o}/maximal-A-pooled.txt", "w").write("\n".join(os.path.basename(x) for x in front) + "\n")
    log.write(f"{fam}\tmaximal-A-pooled\t{'cap' if front is None else len(front)}\t{time.time()-t:.1f}\n"); log.flush()
    if front is None: continue
    d = f"{o}/front-A"; os.makedirs(d)
    for x in front: os.symlink(f"{pool}/{os.path.basename(x)}", f"{d}/{os.path.basename(x)}")
    os.symlink(f"{H}/pool/{fam}/P", f"{o}/front-P")
    t = time.time(); rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for a in sorted(os.listdir(d)):
            one = f"{tmp}/one"; shutil.rmtree(one, ignore_errors=True); os.makedirs(one)
            os.symlink(os.path.realpath(f"{d}/{a}"), f"{one}/{a}")
            r = subprocess.run([f"{B}/compare", "--repairs", f"{o}/front-P", "--ideals", one], capture_output=True, text=True)
            rows += [(m.group(1), a, m.group(2)) for m in map(REL.match, (l.strip() for l in r.stdout.splitlines())) if m]
    with open(f"{o}/matrix.tsv", "w") as f:
        for r in rows: f.write("\t".join(r) + "\n")
    log.write(f"{fam}\tmatrix\t{len(rows)}\t{time.time()-t:.1f}\n"); log.flush()
