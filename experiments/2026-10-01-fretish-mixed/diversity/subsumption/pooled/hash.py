"""Hash each listed file's body with comments and whitespace removed, so
copies of one repair from different runs share a hash.
usage: hash.py < rel-paths > rel,md5   (paths relative to ~)"""
import hashlib, os, re, sys
H = os.path.expanduser("~")
for rel in sys.stdin.read().split():
    s = open(f"{H}/{rel}").read()
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    s = re.sub(r"\s+", "", s)
    print(f"{rel},{hashlib.md5(s.encode()).hexdigest()}")
