import hashlib, os, re, shutil, glob
H = os.path.expanduser("~/overlap"); out = f"{H}/pool"; shutil.rmtree(out, ignore_errors=True)
def key(p):
    body = "".join(l for l in open(p) if not l.lstrip().startswith("//"))
    return hashlib.md5(re.sub(r"\s+", "", body).encode()).hexdigest()
seen = set(); cnt = {}
for src in ("pool-host", "pool-av2"):
    for p in sorted(glob.glob(f"{H}/{src}/*/*/*.tlsf")):
        side, fam = p.split("/")[-2], p.split("/")[-3]
        k = (fam, side, key(p)); c = cnt.setdefault((fam, side), [0, 0]); c[0] += 1
        if k in seen: continue
        seen.add(k); c[1] += 1
        d = f"{out}/{fam}/{side}"; os.makedirs(d, exist_ok=True); shutil.copyfile(p, f"{d}/{os.path.basename(p)}")
for (f, s), (a, b) in sorted(cnt.items()): print(f, s, a, b)
