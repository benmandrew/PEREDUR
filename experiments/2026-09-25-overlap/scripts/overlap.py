"""Per family: reduce each tool's pool to its maximal classes, classify every
PEREDUR class against every AuRUS class, and each class against the ideals.
Writes <H>/out/<fam>/{maximal-P,maximal-A}.txt, matrix.tsv (p, a, relation of
p to a) and ideals-{P,A}.txt, and logs wall time per step to <H>/out/log.tsv."""
import os, re, subprocess, sys, time, tempfile, shutil
H = os.path.expanduser("~/overlap"); B = os.path.expanduser("~/projects/counter/build-release")
EX = os.path.expanduser("~/projects/counter/examples")
SURV = re.compile(r"^class\s+\d+\s+(.+?)(?:\s+\(\+\d+ identical\))?$")
REL = re.compile(r"^(\S+)\s+:\s+(equivalent|strictly stronger|strictly weaker|incomparable|timeout)")
CAP = int(os.environ.get("CAP", "7200"))
log = open(f"{H}/out/log.tsv", "a")
def step(fam, name, cmd, out):
    t = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=CAP)
        rc, text = r.returncode, r.stdout
    except subprocess.TimeoutExpired:
        rc, text = "cap", ""
    open(out, "w").write(text)
    log.write(f"{fam}\t{name}\t{rc}\t{time.time()-t:.1f}\n"); log.flush()
    return rc == 0, text
def rels(text): return [m.groups() for m in map(REL.match, (l.strip() for l in text.splitlines())) if m]
for fam in sys.argv[1:]:
    o = f"{H}/out/{fam}"; os.makedirs(o, exist_ok=True); reps = {}
    for side in "PA":
        pool = f"{H}/pool/{fam}/{side}"
        d = f"{o}/front-{side}"; shutil.rmtree(d, ignore_errors=True)
        if not os.path.isdir(pool): break
        if side == "P":
            # PEREDUR's repair_N.tlsf are already its per-run maximal output;
            # every distinct one is compared, no pooled pass.
            os.symlink(pool, d)
        else:
            ok, text = step(fam, f"maximal-{side}", [f"{B}/maximal", pool, "--jobs", "30"], f"{o}/maximal-{side}.txt")
            if not ok: break
            os.makedirs(d)
            for l in text.splitlines():
                m = SURV.match(l.strip())
                if m: os.symlink(m.group(1), f"{d}/{os.path.basename(m.group(1))}")
        reps[side] = d
    if len(reps) < 2: continue
    t = time.time(); rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for a in sorted(os.listdir(reps["A"])):
            one = f"{tmp}/one"; shutil.rmtree(one, ignore_errors=True); os.makedirs(one)
            os.symlink(os.path.realpath(f"{reps['A']}/{a}"), f"{one}/{a}")
            r = subprocess.run([f"{B}/compare", "--repairs", reps["P"], "--ideals", one], capture_output=True, text=True)
            rows += [(p, a, rel) for p, rel in rels(r.stdout)]
            if time.time() - t > CAP: rows.append(("CAP", a, "cap")); break
    with open(f"{o}/matrix.tsv", "w") as f:
        for r in rows: f.write("\t".join(r) + "\n")
    log.write(f"{fam}\tmatrix\t{len(rows)}\t{time.time()-t:.1f}\n"); log.flush()
