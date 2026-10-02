import os, re, subprocess, sys, tempfile, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from calib import runs_in, frontier, load_fp, sub
B = os.path.expanduser("~/projects/counter-wt/fpcal/build-release")
D, fpdir, subject, seed = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
runs = runs_in([f"{D}/results", f"{D}/av2/all"])
d = frontier(runs[(subject, seed, "directed")]); u = frontier(runs[(subject, seed, "uniform")])
members = d + u; p = load_fp(fpdir, subject)
with tempfile.TemporaryDirectory(dir="/dev/shm") as t:
    link = {}
    for i, f in enumerate(members):
        n = f"{t}/{'d' if f in d else 'u'}{i:05d}.json"; os.symlink(f, n); link[n] = f
    r = subprocess.run([f"{B}/maximal", t, "--jobs", "6"], capture_output=True, text=True)
    surv = {link[m.group(2)] for m in map(re.compile(r"^class\s+(\d+)\s+(\S+)").match, r.stdout.splitlines()) if m}
key = lambda f: json.dumps(json.load(open(f)), sort_keys=True)
sk = {key(f) for f in surv}
gone = [f for f in members if key(f) not in sk]
print(len(members), "members,", len(gone), "not in maximal output")
for g in gone:
    cands = [y for y in members if y != g and sub(p[y], p[g])]
    print(g.split("/")[-4][-25:], g.split("/")[-1], "fp-candidate dominators:", len(cands))
    if not cands:
        print("   NO fingerprint candidate -> who does maximal think dominates it?")
        with tempfile.TemporaryDirectory(dir="/dev/shm") as t:
            os.makedirs(f"{t}/r"); os.makedirs(f"{t}/x")
            for i, y in enumerate(members):
                if y != g: os.symlink(y, f"{t}/r/{i:05d}.json")
            os.symlink(g, f"{t}/x/x.json")
            out = subprocess.run([f"{B}/compare", "--timeout", "20", "--repairs", f"{t}/r", "--ideals", f"{t}/x"], capture_output=True, text=True).stdout
        for line in out.splitlines():
            if "strictly stronger" in line or "equivalent" in line or "timeout" in line:
                y = members[int(line.split(".json")[0].strip())]
                print("   ", line.strip()[:60], "->", y.split("/")[-4][-25:], y.split("/")[-1], "fp y=>g refuted:", not sub(p[y], p[g]))
