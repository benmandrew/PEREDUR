"""Hash each listed TLSF file's body once comments and whitespace are removed.

usage: hash_bodies.py <root> <list.txt> > hashes.csv   (path,sha; missing files are reported on stderr)
"""
import hashlib, os, re, sys
root, lst = sys.argv[1], sys.argv[2]
print("path,sha")
miss = 0
for rel in open(lst).read().split():
    try:
        s = open(os.path.join(root, rel)).read()
    except FileNotFoundError:
        miss += 1
        continue
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    body = re.sub(r"\s+", "", s)
    print(f"{rel},{hashlib.sha1(body.encode()).hexdigest()}")
print(f"missing {miss}", file=sys.stderr)
