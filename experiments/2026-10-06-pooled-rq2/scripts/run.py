"""Run `compare` on each planned pair, one pair per call, and append
id,relation,rc,secs to the output. relation is a's relative to b: weaker means
b strictly implies a. Resumable: ids already in the output are skipped.

usage: run.py <pairs.csv> <out.csv>
env: JOBS (28), CMP, PEREDUR_BLACK_PATH, PEREDUR_SPOT_BIN_DIR (default: the
     checkout's build-release/third_party), BLACK_T (300, compare --timeout per solver call),
     WALL_T (700, per pair), VMEM_KB (8000000)
"""
import csv, os, shutil, subprocess, sys, tempfile, threading, time
from concurrent.futures import ThreadPoolExecutor
HOME = os.path.expanduser("~")
CMP = os.environ.get("CMP", f"{HOME}/subsum/bin/compare")
JOBS = int(os.environ.get("JOBS", "28"))
BLACK_T = os.environ.get("BLACK_T", "300")
WALL_T = os.environ.get("WALL_T", "700")
VMEM_KB = os.environ.get("VMEM_KB", "8000000")
TP = f"{HOME}/projects/counter/build-release/third_party"
ENV = dict(os.environ)
ENV.setdefault("PEREDUR_BLACK_PATH", f"{TP}/black/install/bin/black")
ENV.setdefault("PEREDUR_SPOT_BIN_DIR", f"{TP}/spot/bin")
TMP = f"{HOME}/subsum/tmp"
KEYS = (("equivalent", "equivalent"), ("strictly weaker", "weaker"), ("strictly stronger", "stronger"),
        ("incomparable", "incomparable"), ("timeout", "undecided"))


def compare(a, b):
    d = tempfile.mkdtemp(dir=TMP)
    try:
        for sub, src in (("a", a), ("b", b)):
            os.mkdir(f"{d}/{sub}")
            os.symlink(src, f"{d}/{sub}/{os.path.basename(src)}")
        cmd = f"ulimit -v {VMEM_KB}; exec timeout {WALL_T} {CMP} --repairs {d}/a --ideals {d}/b --timeout {BLACK_T}"
        t = time.time()
        p = subprocess.run(["bash", "-c", cmd], env=ENV, capture_output=True, text=True)
        rel = "error"
        for line in p.stdout.splitlines():
            if " : " in line and not line.startswith("Summary"):
                s = line.split(" : ", 1)[1]
                rel = next((r for k, r in KEYS if s.startswith(k)), "error")
        if p.returncode == 124:
            rel = "undecided"
        return rel, p.returncode, round(time.time() - t, 2)
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(TMP, exist_ok=True)
    done = set()
    if os.path.exists(out):
        done = {r["id"] for r in csv.DictReader(open(out))}
    todo = [r for r in csv.DictReader(open(src)) if r["id"] not in done]
    new = not os.path.exists(out)
    fh = open(out, "a", newline="")
    w = csv.writer(fh)
    if new:
        w.writerow(["id", "relation", "rc", "secs"])
    lock, count, start = threading.Lock(), [0], time.time()
    print(f"{len(todo)} pairs to run, {len(done)} done", flush=True)

    def one(r):
        res = compare(r["a_path"], r["b_path"])
        with lock:
            w.writerow([r["id"], *res]); fh.flush()
            count[0] += 1
            if count[0] % 2000 == 0:
                print(f"{count[0]}/{len(todo)} {time.time() - start:.0f}s", flush=True)

    with ThreadPoolExecutor(JOBS) as ex:
        list(ex.map(one, todo))
    print(f"done {time.time() - start:.0f}s", flush=True)


if __name__ == "__main__":
    main()
