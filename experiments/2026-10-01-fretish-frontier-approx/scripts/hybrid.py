"""Exact paired frontier placement with a fingerprint prefilter.

usage: COMPARE=... TIMEOUT=120 WORKERS=6 \
         hybrid.py <fp-dir> <subject> <out.csv> <results-dir>... [--seeds a-b]

For each (subject, seed) with both arms, every cross-arm pair (x directed,
y uniform) whose fingerprints refute both implications is incomparable, which
is proven: a sampled word satisfying one and not the other is a witness. Every
other cross pair goes to `compare` (one call per x, with only x's surviving
partners). Within an arm the run frontier is an antichain already.

Placement follows paired.py: a member is dominated when a member of the other
arm is strictly stronger; the joint frontier is the undominated members with
cross-arm equivalences merged; a class is shared when it holds both arms. A
compare timeout reads as non-implication, as `maximal` reads it, and is
counted. Rows go to <out.csv> (resumable) with the pair and query counts.
"""
import collections, csv, json, os, re, subprocess, sys, tempfile, threading, time
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from calib import runs_in, frontier, load_fp, sub  # noqa: E402

CMP = os.environ["COMPARE"]
MAXIMAL = os.environ["MAXIMAL"]
T = os.environ.get("TIMEOUT", "120")
# LTL=<fpdraw ltl dump>: a pair compare leaves at timeout goes to black, one
# direction at a time. UNSAT of x & !y proves x => y; a SAT model refutes it.
# black is complete, so this settles pairs SPOT's automata cannot.
LTL = os.environ.get("LTL")
_SPEC = None


def _black_implies(x, y):
    global _SPEC
    import draw_black
    if _SPEC is None:
        rows = open(LTL).read().splitlines()[1:]
        _SPEC = {r.split("\t")[0]: r.split("\t")[1:3] for r in rows}
    f = lambda m: f"(({_SPEC[m][0]}) -> ({_SPEC[m][1]}))"
    # Text output, not JSON: at its -t limit black 25.09.0 prints
    # "UNKNOWN (stopped at k = N)" as text but {"result": "UNSAT"} as JSON.
    try:
        r = subprocess.run([draw_black.BLACK, "solve", "-t", str(draw_black.TIMEOUT), "-"],
                           input=f"{f(x)} & !{f(y)}", capture_output=True, text=True,
                           timeout=draw_black.TIMEOUT + 10)
    except subprocess.TimeoutExpired:
        return None
    first = r.stdout.strip().split()[:1]
    return {"UNSAT": True, "SAT": False}.get(first[0] if first else None)


def black_relation(x, y):
    """compare's vocabulary for y against x, decided by black."""
    xy = _black_implies(x, y)
    yx = _black_implies(y, x)
    if xy is None or yx is None:
        return "timeout"
    return {(True, True): "equivalent", (False, True): "strictly stronger",
            (True, False): "strictly weaker", (False, False): "incomparable"}[(xy, yx)]
WORKERS = int(os.environ.get("WORKERS", "6"))
COLS = ["subject", "seed", "d_frontier", "u_frontier", "joint", "d_only", "u_only",
        "shared", "d_dominated", "u_dominated", "status", "wall_s",
        "all_pairs", "queried_pairs", "timeouts",
        "sem_d_only", "sem_u_only", "sem_shared", "sem_d_dominated", "sem_u_dominated",
        "multi_spelling_classes"]
LINE = re.compile(r"^class\s+(\d+)\s+(\S+)")


def key(path):
    return json.dumps(json.load(open(path)), sort_keys=True)


def representatives(files):
    """The members `maximal` keeps from one equivalence class, as paired.py
    reads it: maximal collapses a class to one survivor, and paired.py counts
    every other spelling as dominated."""
    with tempfile.TemporaryDirectory(dir="/dev/shm") as d:
        link = {}
        for i, f in enumerate(files):
            name = os.path.join(d, f"m{i:05d}.json")
            os.symlink(f, name); link[name] = f
        r = subprocess.run([MAXIMAL, d, "--jobs", "1"], capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError(f"maximal rc={r.returncode}: {r.stderr[:300]}")
        return [link[m.group(2)] for m in map(LINE.match, r.stdout.splitlines()) if m]


def query(x, ys):
    """compare's relation of each y to x: 'strictly stronger' means y => x."""
    with tempfile.TemporaryDirectory(dir="/dev/shm") as d:
        os.makedirs(f"{d}/r"); os.makedirs(f"{d}/x")
        for i, y in enumerate(ys):
            os.symlink(y, f"{d}/r/{i:05d}.json")
        os.symlink(x, f"{d}/x/x.json")
        r = subprocess.run([CMP, "--timeout", T, "--repairs", f"{d}/r", "--ideals", f"{d}/x"],
                           capture_output=True, text=True)
    out = {}
    for line in r.stdout.splitlines():
        parts = line.split(":")
        if len(parts) == 2 and parts[0].strip().endswith(".json"):
            name, text = parts[0].strip(), parts[1].strip()
            for rel in ("equivalent", "strictly stronger", "strictly weaker",
                        "incomparable", "timeout"):
                if text.startswith(rel):
                    out[ys[int(name[:5])]] = rel
                    break
    missing = [y for y in ys if y not in out]
    if missing:
        raise RuntimeError(f"compare rc={r.returncode} left {len(missing)} pairs: {r.stderr[:300]}")
    if LTL:
        for y in ys:
            if out[y] == "timeout":
                out[y] = black_relation(x, y)
                if out[y] != "timeout":
                    BLACK_SETTLED[0] += 1
    return out


BLACK_SETTLED = [0]


def place(subject, seed, d, u, prints, pool):
    """Relations over every pair of d + u, within an arm as well as across:
    a run frontier is the streaming filter's survivors and need not be an
    antichain under the budget `maximal` gives the union."""
    t = time.time()
    members = d + u
    arm = {m: "d" for m in d}
    arm.update({m: "u" for m in u})
    stronger = set()  # (a, b): a strictly implies b
    equivalent = []
    todo = {}
    for i, x in enumerate(members):
        for y in members[i + 1:]:
            if sub(prints[x], prints[y]) or sub(prints[y], prints[x]):
                todo.setdefault(x, []).append(y)
    queried = sum(len(v) for v in todo.values())
    timeouts = 0
    for x, got in zip(todo, pool.map(lambda kv: query(*kv), todo.items())):
        for y, r in got.items():
            if r == "strictly stronger":
                stronger.add((y, x))
            elif r == "strictly weaker":
                stronger.add((x, y))
            elif r == "equivalent":
                equivalent.append((x, y))
            elif r == "timeout":
                timeouts += 1
    dominated = {b for _, b in stronger}
    parent = {m: m for m in members}

    def find(m):
        while parent[m] != m:
            parent[m] = parent[parent[m]]
            m = parent[m]
        return m

    for x, y in equivalent:
        if x not in dominated and y not in dominated:
            parent[find(x)] = find(y)
    classes = collections.defaultdict(list)
    for m in members:
        if m not in dominated:
            classes[find(m)].append(m)
    keys = {m: key(m) for m in members}
    groups = collections.defaultdict(set)
    for m in members:
        groups[keys[m]].add(arm[m])
    sem = [("shared" if len({arm[m] for m in c}) == 2 else arm[c[0]]) for c in classes.values()]
    # paired.py's convention: one spelling survives each class.
    kinds, kept, multi = [], set(), 0
    for c in classes.values():
        spellings = {}
        for m in c:
            spellings.setdefault(keys[m], m)
        if len(spellings) > 1:
            multi += 1
            reps = representatives(list(spellings.values()))
        else:
            reps = list(spellings.values())
        for r in reps:
            kept.add(keys[r])
            g = groups[keys[r]]
            kinds.append("shared" if len(g) == 2 else next(iter(g)))
    return [subject, seed, len(d), len(u), len(kinds), kinds.count("d"),
            kinds.count("u"), kinds.count("shared"),
            sum(keys[m] not in kept for m in d), sum(keys[m] not in kept for m in u),
            "ok", round(time.time() - t, 1), len(members) * (len(members) - 1) // 2,
            queried, timeouts,
            sem.count("d"), sem.count("u"), sem.count("shared"),
            sum(m in dominated for m in d), sum(m in dominated for m in u), multi]


def main():
    args = sys.argv[1:]
    seeds = None
    if "--seeds" in args:
        i = args.index("--seeds")
        lo, hi = map(int, args[i + 1].split("-"))
        seeds = range(lo, hi + 1)
        del args[i:i + 2]
    fp_dir, subject, out_path, dirs = args[0], args[1], args[2], args[3:]
    runs = runs_in(dirs)
    prints = load_fp(fp_dir, subject)
    pairs = sorted({s for (subj, s, _) in runs if subj == subject
                    and (subj, s, "directed") in runs and (subj, s, "uniform") in runs
                    and (seeds is None or s in seeds)})
    done = set()
    if os.path.exists(out_path):
        done = {(r["subject"], int(r["seed"])) for r in csv.DictReader(open(out_path))}
    out = open(out_path, "a", newline="")
    w = csv.writer(out)
    if not done:
        w.writerow(COLS); out.flush()
    with ThreadPoolExecutor(WORKERS) as pool:
        for seed in pairs:
            if (subject, seed) in done:
                continue
            d = frontier(runs[(subject, seed, "directed")])
            u = frontier(runs[(subject, seed, "uniform")])
            if not d and not u:
                row = [subject, seed] + [0] * 8 + ["empty", 0.0] + [0] * 9
            else:
                row = place(subject, seed, d, u, prints, pool)
            w.writerow(row); out.flush()
            print(" ".join(map(str, row)), flush=True)


if __name__ == "__main__":
    main()
