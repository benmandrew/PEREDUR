"""Calibrate fingerprint-only frontier placement against exact verdicts.

  calib.py fp <out> <setting> <subjects,...> <results-dir>...
      fingerprint every run-frontier member of each subject, one fpcal call a
      subject; setting is words/prefix/cycle, e.g. 65536/8/8
  calib.py paired <fp-dir> <exact.csv> <out.csv> <results-dir>...
      replay paired.py's placement with fingerprint relations, one row per
      (subject, seed) the exact csv placed, both placements side by side
  calib.py sample <fp-dir> <sample.tsv> <rank-dir> <out-prefix> <results-dir>...
      pair-level confusion of fingerprint relations against compare's
"""
import collections, csv, json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

FPCAL = os.environ.get("FPCAL", os.path.expanduser("~/fpcal-wt/build-release/fpcal"))
EXAMPLES = os.environ.get("EXAMPLES", os.path.expanduser("~/projects/counter/examples"))
WORKERS = int(os.environ.get("WORKERS", "8"))
RUN = re.compile(r"^sweep_O_(directed|uniform)_nsga2-apportion_wkoff_log_(.+)_seed(\d+)$")
REL = re.compile(r"^(\d+)\.json\s+:\s+(equivalent|strictly stronger|strictly weaker|incomparable|timeout)")


def runs_in(dirs):
    runs = {}
    for d in dirs:
        for name in os.listdir(d):
            m = RUN.match(name)
            if m:
                runs.setdefault((m.group(2), int(m.group(3)), m.group(1)),
                                os.path.realpath(os.path.join(d, name)))
    return runs


def frontier(run_dir):
    acc = os.path.join(run_dir, "accumulated")
    tsv = os.path.join(acc, "maximal.tsv")
    if not os.path.exists(tsv):
        return []
    return [os.path.join(acc, n) for n in (l.strip() for l in list(open(tsv))[1:]) if n]


def load_fp(fp_dir, subject):
    prints = {}
    for line in open(os.path.join(fp_dir, f"{subject}.tsv")):
        path, _, digits = line.rstrip("\n").partition("\t")
        value = 0
        for i in range(0, len(digits), 16):
            value |= int(digits[i:i + 16], 16) << (64 * (i // 16))
        prints[path] = value
    return prints


def sub(a, b):
    """a's satisfying words are a subset of b's: a implies b on the sample."""
    return a | b == b


def cmd_fp(out, setting, subjects, dirs):
    words, prefix, cycle = setting.split("/")
    runs = runs_in(dirs)
    os.makedirs(out, exist_ok=True)

    def one(subject):
        files = sorted({f for (s, _, _), d in runs.items() if s == subject
                        for f in frontier(d)})
        lst = os.path.join(out, f"{subject}.list")
        open(lst, "w").write("".join(f + "\n" for f in files))
        spec = os.path.join(EXAMPLES, subject, "spec.json")
        with open(os.path.join(out, f"{subject}.tsv"), "w") as dst:
            r = subprocess.run([FPCAL, words, prefix, cycle, "0", spec, lst],
                               stdout=dst, stderr=subprocess.PIPE, text=True)
        print(f"{subject}: {len(files)} members rc={r.returncode} {r.stderr[:200]}",
              flush=True)

    with ThreadPoolExecutor(WORKERS) as ex:
        list(ex.map(one, subjects))


def place(files, prints):
    """paired.py's placement with fingerprint relations."""
    key = lambda f: json.dumps(json.load(open(f)), sort_keys=True)
    arms_of = collections.defaultdict(set)
    for a, fs in files.items():
        for f in fs:
            arms_of[prints[f]].add(a)
    classes = list(arms_of)
    maximal = {c for c in classes
               if not any(o != c and sub(o, c) for o in classes)}
    kinds = collections.Counter("shared" if len(arms_of[c]) == 2
                                else next(iter(arms_of[c])) for c in maximal)
    dom = {a: sum(prints[f] not in maximal for f in fs) for a, fs in files.items()}
    empty = sum(prints[f] == 0 for fs in files.values() for f in fs)
    return [len(maximal), kinds["directed"], kinds["uniform"], kinds["shared"],
            dom["directed"], dom["uniform"], empty]


def cmd_paired(fp_dir, exact_csv, out_csv, dirs):
    runs = runs_in(dirs)
    cache = {}
    rows = []
    for row in csv.DictReader(open(exact_csv)):
        subject, seed = row["subject"], int(row["seed"])
        if row["status"] not in ("ok", "empty"):
            continue
        if (subject, seed, "directed") not in runs or (subject, seed, "uniform") not in runs:
            continue
        if not os.path.exists(os.path.join(fp_dir, f"{subject}.tsv")):
            continue
        if subject not in cache:
            cache[subject] = load_fp(fp_dir, subject)
        files = {a: frontier(runs[(subject, seed, a)]) for a in ("directed", "uniform")}
        if [len(files["directed"]), len(files["uniform"])] != [int(row["d_frontier"]), int(row["u_frontier"])]:
            print(f"frontier size mismatch {subject} {seed}", file=sys.stderr)
            continue
        if not files["directed"] and not files["uniform"]:
            fp = [0] * 7
        else:
            fp = place(files, cache[subject])
        rows.append([subject, seed, row["d_frontier"], row["u_frontier"],
                     row["joint"], row["d_only"], row["u_only"], row["shared"],
                     row["d_dominated"], row["u_dominated"], *fp])
    with open(out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["subject", "seed", "d_frontier", "u_frontier",
                    "joint", "d_only", "u_only", "shared", "d_dominated", "u_dominated",
                    "fp_joint", "fp_d_only", "fp_u_only", "fp_shared",
                    "fp_d_dominated", "fp_u_dominated", "fp_empty"])
        w.writerows(rows)
    print(f"{out_csv}: {len(rows)} pairs")


def fp_rel(r, x):
    fwd, rev = sub(r, x), sub(x, r)
    if fwd and rev:
        return "equivalent"
    return "strictly stronger" if fwd else "strictly weaker" if rev else "incomparable"


def cmd_sample(fp_dir, sample_tsv, rank_dir, prefix, dirs):
    runs = runs_in(dirs)
    other = {"directed": "uniform", "uniform": "directed"}
    pairs = collections.Counter()
    members = collections.Counter()
    subject = os.path.basename(sample_tsv)[:-4]
    prints = load_fp(fp_dir, subject)
    for row in csv.DictReader(open(sample_tsv), delimiter="\t"):
        if row["status"] != "ok":
            continue
        a, s = row["arm"], int(row["seed"])
        x = prints[os.path.join(runs[(subject, s, a)], "accumulated", row["name"])]
        others = frontier(runs[(subject, s, other[a])])
        exact_m = {"stronger": False, "equivalent": False, "timeout": False}
        fp_m = {"stronger": False, "equivalent": False}
        for line in open(os.path.join(rank_dir, f"{row['rank']}.txt")):
            m = REL.match(line.strip())
            if not m:
                continue
            r = prints[others[int(m.group(1))]]
            exact, guess = m.group(2), fp_rel(r, x)
            pairs[(exact, guess)] += 1
            exact_m["stronger"] |= exact == "strictly stronger"
            exact_m["equivalent"] |= exact == "equivalent"
            exact_m["timeout"] |= exact == "timeout"
            fp_m["stronger"] |= guess == "strictly stronger"
            fp_m["equivalent"] |= guess == "equivalent"
        status = lambda d: ("subsumed" if d["stronger"] else
                            "shared" if d["equivalent"] else
                            "unknown" if d.get("timeout") else "only")
        members[(status(exact_m), status(fp_m))] += 1
    with open(prefix + ".pairs.csv", "w") as f:
        f.write("exact,fingerprint,n\n")
        for (e, g), n in sorted(pairs.items()):
            f.write(f"{e},{g},{n}\n")
    with open(prefix + ".members.csv", "w") as f:
        f.write("exact,fingerprint,n\n")
        for (e, g), n in sorted(members.items()):
            f.write(f"{e},{g},{n}\n")
    print(open(prefix + ".pairs.csv").read(), open(prefix + ".members.csv").read())


if __name__ == "__main__":
    mode = sys.argv[1]
    if mode == "fp":
        cmd_fp(sys.argv[2], sys.argv[3], sys.argv[4].split(","), sys.argv[5:])
    elif mode == "paired":
        cmd_paired(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:])
    elif mode == "sample":
        cmd_sample(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6:])
