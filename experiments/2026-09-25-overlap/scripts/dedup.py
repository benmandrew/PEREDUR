import hashlib, os, re, sys, collections
seen = collections.defaultdict(set); runs = collections.defaultdict(lambda: collections.defaultdict(set))
for p in open(os.path.expanduser("~/overlap-pilot/ws-paths.txt")):
    p = p.strip()
    if not os.path.exists(p): continue
    m = re.search(r"/aurus_(.+)_seed(\d+)/", p); fam, seed = m.group(1), m.group(2)
    body = "".join(l for l in open(p) if not l.startswith("//"))
    h = hashlib.md5(re.sub(r"\s+", "", body).encode()).hexdigest()[:16]
    print(fam, seed, h)
