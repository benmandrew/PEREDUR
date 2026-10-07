"""Drive the pooled RQ2 pass through the hosts' q.sh queues.

usage: drive.py        (run from this directory; logs to pooled-rq2/drive.log)

Enqueues stage 1 (in chunks, so sampling batches can overtake it) and sample
batch 0 if not yet queued, then polls every 5 minutes. When a sample batch has
finished on every host, it fetches the results, scores the draws so far, and
either stops sampling (every share within pooled.HALF) or plans and enqueues
the next batch. When stage 1 has finished, it plans stage 2 with
`pooled.py cross` and enqueues it. When stage 2 and sampling are both done, it
scores the pass into pooled-rq2/report.txt and exits.
"""
import csv, glob, os, subprocess, sys, time

OUT = "pooled-rq2"
HOSTS = ("av1", "av2", "av3")
PER = 20          # draws per side per batch
MAX_BATCH = 15    # stop sampling after 300 draws per side regardless
CHUNKS = 12       # stage-1 and stage-2 chunks per host
PRIOR = sorted(glob.glob("results/results-*.csv"))
STATE = f"{OUT}/drive-state.txt"


def log(msg):
    with open(f"{OUT}/drive.log", "a") as f:
        f.write(f"{time.strftime('%H:%M:%S')} {msg}\n")


def sh(cmd, check=True):
    return subprocess.run(cmd, shell=True, check=check, capture_output=True, text=True).stdout


def state():
    return dict(l.split("=", 1) for l in open(STATE).read().split()) if os.path.exists(STATE) else {}


def save(st):
    open(STATE, "w").write("".join(f"{k}={v}\n" for k, v in st.items()))


def enqueue(src, prefix, chunks):
    """Deal src's rows round-robin to the hosts in `chunks` files each, then push."""
    rows = list(csv.reader(open(src)))
    hd, rows = rows[0], rows[1:]
    os.makedirs(f"{OUT}/q", exist_ok=True)
    names = []
    for j, h in enumerate(HOSTS):
        mine = rows[j::len(HOSTS)]
        for c in range(chunks):
            part = mine[c::chunks]
            if not part:
                continue
            n = f"{prefix}-{c:02d}-{h}.csv"
            w = csv.writer(open(f"{OUT}/q/{n}", "w", newline=""))
            w.writerow(hd); w.writerows(part)
            names.append((h, n))
    for h in HOSTS:
        files = [f"{OUT}/q/{n}" for hh, n in names if hh == h]
        if files:
            sh(f"rsync -a {' '.join(files)} {h}:/home/benandrew/subsum/q-rq2/")
    log(f"enqueued {len(rows)} pairs from {src} as {prefix}-*")
    return [n for _, n in names]


def finished(names):
    done = set()
    for h in HOSTS:
        for line in sh(f"ssh -o ConnectTimeout=20 {h} 'cat ~/subsum/q-rq2.log 2>/dev/null'", check=False).splitlines():
            p = line.split()
            if len(p) >= 3 and p[1] == "done":
                done.add(p[2])
    return all(n in done for n in names)


def fetch():
    for h in HOSTS:
        sh(f"rsync -a {h}:/home/benandrew/subsum/results-rq2.csv {OUT}/results-rq2-{h}.csv", check=False)
    return PRIOR + sorted(glob.glob(f"{OUT}/results-rq2-*.csv"))


def pooled(*args):
    return subprocess.run([sys.executable, "pooled.py", *args], capture_output=True, text=True, check=True)


def main():
    st = state()
    if "s1" not in st:
        st["s1"] = ",".join(enqueue(f"{OUT}/s1/all.csv", "1-s1", CHUNKS)); save(st)
    if "batch" not in st:
        st["batch"] = "0"
        st["b0"] = ",".join(enqueue(f"{OUT}/sample/pairs-000.csv", "0-sample-000", 1)); save(st)
    while True:
        st = state()
        if st.get("sampling") != "done":
            k = int(st["batch"])
            if finished(st[f"b{k}"].split(",")):
                res = fetch()
                r = pooled("sample-score", OUT, str((k + 1) * PER), *res)
                log(f"batch {k} scored: {r.stdout.strip()}\n{r.stderr.strip()}")
                if r.stdout.strip() == "converged" or k + 1 >= MAX_BATCH:
                    st["sampling"] = "done"
                    log(f"sampling done after {(k + 1) * PER} draws per side ({r.stdout.strip()})")
                else:
                    pooled("sample-plan", OUT, str(k + 1), str(PER), *res)
                    st["batch"] = str(k + 1)
                    st[f"b{k + 1}"] = ",".join(enqueue(f"{OUT}/sample/pairs-{k + 1:03d}.csv", f"0-sample-{k + 1:03d}", 1))
                save(st)
        if "s2" not in st and finished(st["s1"].split(",")):
            res = fetch()
            r = pooled("cross", OUT, *res)
            log(f"stage 1 done; cross planned\n{r.stderr.strip()}")
            st["s2"] = ",".join(enqueue(f"{OUT}/pairs-cross.csv", "2-s2", CHUNKS)); save(st)
        if "s2" in st and st.get("sampling") == "done" and finished(st["s2"].split(",")):
            res = fetch()
            r = pooled("score", OUT, *res)
            open(f"{OUT}/report.txt", "w").write(r.stdout)
            log("pass complete; report in report.txt")
            print(r.stdout)
            return
        time.sleep(300)


if __name__ == "__main__":
    main()
