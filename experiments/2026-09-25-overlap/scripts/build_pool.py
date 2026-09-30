"""Pool one host's repairs per family: PEREDUR's returned repair_N.tlsf of the
shipping arm, AuRUS's well-separated solutions. Files are copied (symlinks
followed) as <out>/<family>/<side>/s<seed>_<name>, and a file whose body,
comments and whitespace aside, repeats one already pooled on this host is
skipped; the global dedup happens again after the two hosts are merged."""
import glob, hashlib, os, re, shutil, sys
out = os.path.expanduser(sys.argv[1]); E = os.path.expanduser("~/projects/counter/experiments")
def key(p):
    body = "".join(l for l in open(p) if not l.lstrip().startswith("//"))
    return hashlib.md5(re.sub(r"\s+", "", body).encode()).hexdigest()
seen = set(); n = {}
def put(fam, side, seed, p):
    k = (fam, side, key(p))
    n.setdefault((fam, side), [0, 0])[0] += 1
    if k in seen: return
    seen.add(k); n[(fam, side)][1] += 1
    d = os.path.join(out, fam, side); os.makedirs(d, exist_ok=True)
    shutil.copyfile(p, os.path.join(d, f"s{seed}_{os.path.basename(p)}"))
for d in sorted(glob.glob(f"{E}/results-paper-rerun/sweep_G_mrs_nsga2-apportion_wkoff_log_*_seed*")):
    m = re.search(r"_log_(.+)_seed(\d+)$", d)
    for p in sorted(glob.glob(f"{d}/repair_*.tlsf")): put(m.group(1), "P", m.group(2), p)
for p in open(os.path.expanduser("~/overlap-pilot/ws-paths.txt")):
    p = p.strip()
    if not os.path.exists(p): continue
    m = re.search(r"/aurus_(.+)_seed(\d+)/", p)
    if m.group(1) == "humanoid-741": continue
    put(m.group(1), "A", m.group(2), p)
for (fam, side), (a, b) in sorted(n.items()): print(fam, side, a, b)
