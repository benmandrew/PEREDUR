"""Sampled frontier placement for families whose pooled maximal pass did not
finish. Draws a fixed random order over one side's pool and, for each drawn
member x, runs `compare --repairs <PEREDUR pool> --ideals {x}`, so every PEREDUR
repair's own relation to x is printed (compare keeps one best relation per
repair, and with one ideal that is the pair's relation). Any prefix of the
order is a simple random sample, so the run can stop at any row.

usage: sample.py <family> <side P|A> <n> [seed]
  side P: x drawn from PEREDUR's pool; x is off its frontier when some pool
          repair is strictly stronger than x.
  side A: x drawn from AuRUS's frontier (out/<family>/front-A); x is dominated
          by PEREDUR when some pool repair is strictly stronger than x.
Writes out/<family>/sample-<side>.tsv (one row per x) and the raw compare
output to out/<family>/sample-<side>/<x>.txt."""
import os, random, re, subprocess, sys, tempfile, time
H = os.path.expanduser("~/overlap"); B = os.path.expanduser("~/projects/counter/build-release")
CMP = os.environ.get("COMPARE", f"{B}/compare")
# Rows to write before stopping. The draw order stays that of n, so the rows are
# a prefix of one order and a simple random sample at any length.
STOP = int(os.environ.get("STOP", "0")) or None
fam, side, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
seed = int(sys.argv[4]) if len(sys.argv) > 4 else 20260929
pool = f"{H}/pool/{fam}/P"
if side == "PA":
    # The P sample's first n members against every AuRUS frontier class, for a
    # family with no matrix: one compare per class, the sample as the repairs.
    pick = random.Random(seed).sample(sorted(os.listdir(pool)), min(n, len(os.listdir(pool))))[:STOP]
    front = f"{H}/out/{fam}/front-A"; out = f"{H}/out/{fam}/sample-PA.tsv"
    done = {l.split("\t")[1] for l in open(out)} if os.path.exists(out) else set()
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(f"{d}/p"); os.makedirs(f"{d}/a")
        for x in pick: os.symlink(f"{pool}/{x}", f"{d}/p/{x}")
        for a in sorted(os.listdir(front)):
            if a in done: continue
            for y in os.listdir(f"{d}/a"): os.remove(f"{d}/a/{y}")
            os.symlink(os.path.realpath(f"{front}/{a}"), f"{d}/a/{a}")
            r = subprocess.run([CMP, "--repairs", f"{d}/p", "--ideals", f"{d}/a"], capture_output=True, text=True)
            rows = re.findall(r"^(\S+)\s+:\s+(equivalent|strictly stronger|strictly weaker|incomparable|timeout)", r.stdout, re.M)
            with open(out, "a") as f:
                for p, rel in rows: f.write(f"{p}\t{a}\t{rel}\n")
    sys.exit(0)
src = pool if side == "P" else f"{H}/out/{fam}/front-A"
order = random.Random(seed).sample(sorted(os.listdir(src)), min(n, len(os.listdir(src))))
REL = re.compile(r"^(\S+)\s+:\s+(equivalent|strictly stronger|strictly weaker|incomparable|timeout)(?: than| to)?\s*(\S*)")
raw = f"{H}/out/{fam}/sample-{side}"; os.makedirs(raw, exist_ok=True)
tsv = f"{H}/out/{fam}/sample-{side}.tsv"
done = set()
if os.path.exists(tsv):
    done = {l.split("\t")[1] for l in open(tsv) if not l.startswith("rank")}
else:
    open(tsv, "w").write("rank\tname\tstronger\tweaker\tequivalent\ttimeout\tincomparable\twall_s\n")
for rank, x in enumerate(order[:STOP]):
    if x in done: continue
    t = time.time()
    with tempfile.TemporaryDirectory() as d:
        os.symlink(os.path.realpath(f"{src}/{x}"), f"{d}/{x}")
        r = subprocess.run([CMP, "--repairs", pool, "--ideals", d], capture_output=True, text=True)
    open(f"{raw}/{x}.txt", "w").write(r.stdout)
    c = {"strictly stronger": 0, "strictly weaker": 0, "equivalent": 0, "timeout": 0, "incomparable": 0}
    for m in map(REL.match, (l.strip() for l in r.stdout.splitlines())):
        if m and not (side == "P" and m.group(1) == x): c[m.group(2)] += 1
    with open(tsv, "a") as f:
        f.write(f"{rank}\t{x}\t{c['strictly stronger']}\t{c['strictly weaker']}\t{c['equivalent']}"
                f"\t{c['timeout']}\t{c['incomparable']}\t{time.time()-t:.1f}\n")
