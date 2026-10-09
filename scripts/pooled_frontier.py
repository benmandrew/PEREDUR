#!/usr/bin/env python3
"""Pooled-frontier strength: frontier walks instead of an all-pairs pass.

    python3 scripts/pooled_frontier.py plan SIDES.json WORK --specs a,b [--hosts av1,av2,av3]
    python3 scripts/pooled_frontier.py run WORK --host av2 [--workers 4] [--solver-jobs 4] ...
    python3 scripts/pooled_frontier.py score WORK OUTDIR [--comp rq3 ...]

The quantity is the pooled strength of the paper's RQ2 and RQ3 passes
(experiments/2026-10-01-fretish-mixed/diversity/subsumption/pooled/ on
analysis/repair-diversity, and campaign/pooled-rq2). For each family and each
comparison (A, B), each side's pool is every last-cut frontier member of each
of its runs. Members whose bodies match once comments and whitespace are
removed form one class; classes that compare as equivalent form one group. A
side's pooled frontier is its groups that no other group of the side strictly
implies. sub(A by B) is the share of A's frontier strictly implied by some
group of B, and net = sub(B by A) - sub(A by B).

That pass compared every pair of classes across both pools. This one uses two
consequences of transitivity instead:

1. A side's pooled frontier is the maximal set of its pool, so one running
   antichain walk (`maximal --curve` over a synthetic index) finds it. The
   walk keeps one class per group, so its final size is the group count.
2. If some b in B strictly implies a, so does some member of B's frontier.
   sub() therefore needs frontier against frontier only. One walk over the
   union of the two frontiers settles both directions: a member of A's
   frontier missing from the union's antichain is strictly implied by a member
   of B's frontier, or equivalent to one. Equivalence leaves the two sampled
   fingerprints identical, so a missing member whose fingerprint matches no
   member of the other frontier is a hit outright; the few that do match go
   to one `compare` grid a side, whose best relation per repair separates
   `equivalent` from `strictly weaker` (in an antichain nothing can be both).
   A class in both frontiers is equivalent across the sides and never a hit.

Walks are cached in WORK/cache (or --cache) by the set of class hashes and the
walk's settings, including the binary's commit, so a side shared by several
comparisons (mrs-nsga2-apportion in all three) is walked once, and a side that
does not change between campaigns (AuRUS) is reused from the cache of an
earlier one. A cross walk is keyed by its two frontiers' keys.

Undecided pairs. Both binaries read a timeout as a non-implication, which is
the pass's "non" reading, and within a side it is the old reading too. The walk
reports how many implication queries it left undecided. Where a cross walk or
its equivalence grid left any, the "imp" reading cannot be read off the walk,
so `run` writes WORK/<spec>/fallback-<comp>.csv: every cross pair of the two
frontiers that the fingerprints do not refute in both directions, in
compare_pairs.py's id,a_path,b_path format. `score` reads
fallback-<comp>.results.csv beside it when present (compare_pairs.py's
output) and scores that comparison per pair, under both readings; otherwise it
leaves net_imp blank and names the family. Once every family of a host is
done, `run` joins the host's lists into WORK/fallback-<host>.csv (header only
when empty), so a compare phase can follow unconditionally;
`split-fallbacks` files its output back beside each family.

`plan` reads the sidecars and bodies and writes a self-contained work tree:
WORK/<spec>/classes/<md5>.tlsf (one body per class), classes.csv, members.csv,
prints.tsv, WORK/plan.json and WORK/jobs-<host>.csv (families dealt to the
hosts largest first). `run` processes its host's families, one worker each,
and writes WORK/<spec>/result.json; it is resumable and exits 0 only when
every family of the host has a result. `score` writes families-<comp>.csv in
score_pooled.py's columns and report.txt.

Stdlib only, on python 3.10: `run` is meant to execute on the lab hosts.
"""

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
THIRD_PARTY = os.path.join(REPO_ROOT, "build-release", "third_party")

# compare's per-repair verdicts, by prefix, as compare_pairs.py reads them.
KEYS = (("equivalent", "equivalent"), ("strictly weaker", "weaker"),
        ("strictly stronger", "stronger"), ("incomparable", "incomparable"),
        ("timeout", "undecided"))
TIMEOUT_RC = 124


# ---------------------------------------------------------------- plan


def body_md5(text: str) -> str:
    """hash.py's class key: the body with comments and whitespace removed."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return hashlib.md5(re.sub(r"\s+", "", text).encode()).hexdigest()


def last_cut(members_tsv: str) -> list:
    """A run's frontier: its members.tsv rows at the last cut."""
    with open(members_tsv) as fh:
        rows = [line.rstrip("\n").split("\t") for line in list(fh)[1:] if "\t" in line]
    if not rows:
        return []
    last = max(float(c) for c, _ in rows)
    return [f for c, f in rows if float(c) == last]


def load_prints(path: str, names) -> dict:
    want, out = set(names), {}
    with open(path) as fh:
        next(fh, None)
        for line in fh:
            name, _, hexed = line.rstrip("\n").partition("\t")
            if name in want and hexed:
                out[name] = hexed
    return out


def parse_seeds(text: str) -> list:
    out = []
    for part in str(text).split(","):
        lo, _, hi = part.strip().partition("-")
        out.extend(range(int(lo), int(hi or lo) + 1))
    return out


def first_existing(templates, **fields):
    for t in templates if isinstance(templates, list) else [templates]:
        p = os.path.expanduser(t.format(**fields))
        if os.path.exists(p):
            return p
    return None


def plan_spec(spec: str, cfg: dict, work: str) -> dict:
    """Lists one family's pools and writes its class bodies."""
    labels = sorted({s for pair in cfg["comparisons"].values() for s in pair})
    classes, members, prints = {}, [], {}
    sides_with_runs = set()
    for label in labels:
        side = cfg["sides"][label]
        for seed in parse_seeds(cfg.get("seeds", "0-29")):
            mpath = first_existing(side["members"], spec=spec, seed=seed)
            if mpath is None:
                continue
            names = last_cut(mpath)
            fp = load_prints(mpath.replace(".members.tsv", ".fingerprints.tsv"), names)
            for name in names:
                if name not in fp:
                    continue  # members.py's rule: a member without a print is not listed
                body = first_existing(side["body"], spec=spec, seed=seed, file=name)
                if body is None:
                    raise SystemExit(f"{spec} {label} seed {seed}: no body for {name}")
                with open(body) as fh:
                    text = fh.read()
                md5 = body_md5(text)
                sides_with_runs.add(label)
                members.append((label, seed, name, md5))
                c = classes.get(md5)
                if c is None:
                    c = classes[md5] = {"sides": set(), "n": 0, "text": text,
                                        "pop": bin(int(fp[name], 16)).count("1"),
                                        "pdigest": hashlib.sha1(fp[name].encode()).hexdigest()}
                    prints[md5] = fp[name]
                c["sides"].add(label)
                c["n"] += 1
    comps = {k: v for k, v in cfg["comparisons"].items()
             if v[0] in sides_with_runs and v[1] in sides_with_runs}
    if not comps:
        return {}
    d = os.path.join(work, spec)
    os.makedirs(os.path.join(d, "classes"), exist_ok=True)
    for md5, c in classes.items():
        with open(os.path.join(d, "classes", f"{md5}.tlsf"), "w") as fh:
            fh.write(c["text"])
    with open(os.path.join(d, "classes.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["md5", "sides", "members", "popcount", "pdigest"])
        for md5 in sorted(classes):
            c = classes[md5]
            w.writerow([md5, ";".join(sorted(c["sides"])), c["n"], c["pop"], c["pdigest"]])
    with open(os.path.join(d, "members.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["side", "seed", "file", "md5"])
        w.writerows(sorted(members))
    with open(os.path.join(d, "prints.tsv"), "w") as fh:
        for md5 in sorted(prints):
            fh.write(f"{md5}\t{prints[md5]}\n")
    return {"comparisons": comps, "n_classes": len(classes), "n_members": len(members)}


def cmd_plan(args) -> int:
    with open(args.sides) as fh:
        cfg = json.load(fh)
    os.makedirs(args.work, exist_ok=True)
    plan = {"specs": {}, "sides": cfg["sides"], "comparisons": cfg["comparisons"]}
    for spec in args.specs.split(","):
        entry = plan_spec(spec, cfg, args.work)
        if entry:
            plan["specs"][spec] = entry
            print(f"{spec}: {entry['n_members']} members, {entry['n_classes']} classes, "
                  f"comparisons {','.join(entry['comparisons'])}", flush=True)
        else:
            print(f"{spec}: no comparison has runs on both sides, skipped", flush=True)
    with open(os.path.join(args.work, "plan.json"), "w") as fh:
        json.dump(plan, fh, indent=1)
    hosts = args.hosts.split(",")
    order = sorted(plan["specs"], key=lambda s: -plan["specs"][s]["n_classes"])
    load = {h: 0 for h in hosts}
    deal = {h: [] for h in hosts}
    for spec in order:  # largest first onto the least loaded host
        h = min(hosts, key=lambda x: (load[x], hosts.index(x)))
        deal[h].append(spec)
        load[h] += plan["specs"][spec]["n_classes"] ** 2
    for h in hosts:
        with open(os.path.join(args.work, f"jobs-{h}.csv"), "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["spec"])
            w.writerows([s] for s in deal[h])
    return 0


# ---------------------------------------------------------------- run


class Family:
    def __init__(self, work: str, spec: str):
        # Absolute, since compare_grid symlinks class files from a temporary
        # directory and a relative target would resolve against that.
        self.spec, self.dir = spec, os.path.abspath(os.path.join(work, spec))
        self.classes = {}
        with open(os.path.join(self.dir, "classes.csv"), newline="") as fh:
            for r in csv.DictReader(fh):
                self.classes[r["md5"]] = {"sides": set(r["sides"].split(";")),
                                          "pop": int(r["popcount"]), "pdigest": r["pdigest"]}

    def pool(self, side: str) -> list:
        return sorted(m for m, c in self.classes.items() if side in c["sides"])

    def order(self, md5s) -> list:
        """Fewest accepted sampled words first: a stronger spec accepts a
        subset of a weaker one's words, so strong arrivals come early and the
        weak ones behind them short-circuit on the first dominating member."""
        return sorted(md5s, key=lambda m: (self.classes[m]["pop"], m))

    def path(self, md5: str) -> str:
        return os.path.join(self.dir, "classes", f"{md5}.tlsf")


def tool_env() -> dict:
    env = dict(os.environ)
    # black's wrapper, not install/bin/black: only the wrapper puts
    # libblack.so on LD_LIBRARY_PATH.
    for var, path in (("PEREDUR_BLACK_PATH", os.path.join(THIRD_PARTY, "black", "black")),
                      ("PEREDUR_SPOT_BIN_DIR", os.path.join(THIRD_PARTY, "spot", "bin"))):
        if os.path.exists(path):
            env.setdefault(var, path)
    return env


def binary_commit(binary: str) -> str:
    try:
        out = subprocess.run([binary, "--version"], capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return "?"
    fields = dict(line.strip().partition("=")[::2] for line in out.splitlines() if "=" in line)
    return f"{fields.get('commit', '?')}{'+dirty' if fields.get('dirty') == '1' else ''}"


def atomic_json(path: str, obj) -> None:
    tmp = f"{path}.tmp{os.getpid()}"
    with open(tmp, "w") as fh:
        json.dump(obj, fh, indent=1)
    os.replace(tmp, path)


class Runner:
    def __init__(self, args):
        self.args = args
        self.env = tool_env()
        self.wrap = args.wrap.split() if args.wrap else []
        self.cache = args.cache or os.path.join(args.work, "cache")
        os.makedirs(self.cache, exist_ok=True)
        self.walk_settings = {
            "maximal": binary_commit(args.maximal), "timeout": args.timeout,
            "prefilter": args.prefilter_args}

    def maximal_walk(self, fam: Family, md5s: list, key: str) -> dict:
        """One `maximal --curve` walk over @p md5s; the final antichain."""
        index = os.path.join(fam.dir, "classes", f"walk-{key[:16]}.index.tsv")
        with open(index, "w") as fh:
            fh.write("file\tgeneration\telapsed_s\n")
            for rank, md5 in enumerate(fam.order(md5s)):
                fh.write(f"{md5}.tlsf\t0\t{rank}\n")
        cmd = [*self.wrap, self.args.maximal, "--curve", index, "--jobs", str(self.args.solver_jobs),
               "--timeout", str(self.args.timeout), *self.args.prefilter_args.split()]
        t = time.time()
        p = subprocess.run(cmd, env=self.env, capture_output=True, text=True)
        os.remove(index)
        if p.returncode != 0:
            raise RuntimeError(f"{fam.spec}: maximal exited {p.returncode}: {p.stderr[-2000:]}")
        alive = {}
        for line in p.stdout.splitlines()[1:]:
            parts = line.split("\t")
            if len(parts) < 3:
                continue
            md5 = parts[1][:-len(".tlsf")]
            if parts[2] == "admit":
                alive[md5] = True
            elif parts[2] == "remove":
                alive.pop(md5, None)
        stats = {}
        for line in p.stderr.splitlines():
            m = re.match(r"^(\w[\w ]*?)\s{2,}(\d+)", line.strip("\r").split("\r")[-1])
            if m:
                stats[m.group(1).strip()] = int(m.group(2))
        if stats.get("unparsed", 0):
            raise RuntimeError(f"{fam.spec}: maximal could not parse {stats['unparsed']} files")
        return {"members": sorted(alive), "arrivals": len(md5s), "queries": stats.get("queries", 0),
                "undecided": stats.get("undecided", 0), "secs": round(time.time() - t, 2)}

    def cached(self, kind: str, key_obj, compute):
        key = hashlib.sha256(json.dumps(key_obj, sort_keys=True).encode()).hexdigest()
        path = os.path.join(self.cache, f"{kind}-{key}.json")
        if os.path.exists(path):
            with open(path) as fh:
                out = json.load(fh)
            out["cached"] = True
            return key, out
        out = compute(key)
        atomic_json(path, out)
        out["cached"] = False
        return key, out

    def frontier(self, fam: Family, side: str):
        pool = fam.pool(side)
        return self.cached("frontier", {"pool": pool, **self.walk_settings},
                           lambda key: self.maximal_walk(fam, pool, key))

    def compare_grid(self, fam: Family, repairs: list, ideals: list) -> dict:
        """compare's best relation of each repair against the ideal set."""
        d = tempfile.mkdtemp(prefix="pooled-", dir=self.args.tmp)
        try:
            for sub, names in (("a", repairs), ("b", ideals)):
                os.mkdir(os.path.join(d, sub))
                for md5 in names:
                    os.symlink(fam.path(md5), os.path.join(d, sub, f"{md5}.tlsf"))
            cmd = [*self.wrap, self.args.compare, "--repairs", f"{d}/a", "--ideals", f"{d}/b",
                   *self.args.compare_args.split()]
            p = subprocess.run(cmd, env=self.env, capture_output=True, text=True)
            out = {}
            for line in p.stdout.splitlines():
                if " : " in line and not line.startswith("Summary"):
                    name, s = (x.strip() for x in line.split(" : ", 1))
                    out[name[:-len(".tlsf")]] = next((r for k, r in KEYS if s.startswith(k)), "error")
            if p.returncode != 0 or set(out) != set(repairs):
                raise RuntimeError(f"{fam.spec}: compare exited {p.returncode}: {p.stderr[-2000:]}")
            return out
        finally:
            shutil.rmtree(d, ignore_errors=True)

    def cross(self, fam: Family, fa: list, fb: list, key_a: str, key_b: str):
        def compute(key):
            union = sorted(set(fa) | set(fb))
            walk = self.maximal_walk(fam, union, key)
            alive, shared = set(walk["members"]), set(fa) & set(fb)
            out = {"walk": {k: walk[k] for k in ("arrivals", "queries", "undecided", "secs")},
                   "shared": sorted(shared), "compare_calls": 0}
            for s, mine, other in (("A", fa, fb), ("B", fb, fa)):
                missing = [x for x in mine if x not in shared and x not in alive]
                by_print = {}
                for y in other:
                    if y not in shared:
                        by_print.setdefault(fam.classes[y]["pdigest"], []).append(y)
                check = {x: by_print[fam.classes[x]["pdigest"]] for x in missing
                         if fam.classes[x]["pdigest"] in by_print}
                hits = [x for x in missing if x not in check]
                undecided, errors = [], []
                if check:
                    ideals = sorted({y for ys in check.values() for y in ys})
                    rel = self.compare_grid(fam, sorted(check), ideals)
                    out["compare_calls"] += 1
                    for x, r in rel.items():
                        if r == "weaker":
                            hits.append(x)
                        elif r == "undecided":
                            undecided.append(x)
                        elif r != "equivalent":
                            errors.append(x)  # the walk and compare disagree
                out[f"hit_{s}"] = sorted(hits)
                out[f"equiv_checked_{s}"] = len(check)
                out[f"undecided_{s}"] = sorted(undecided)
                out[f"errors_{s}"] = sorted(errors)
            return out
        return self.cached("cross", {"a": key_a, "b": key_b, **self.walk_settings}, compute)

    def fallback(self, fam: Family, comp: str, fa: list, fb: list) -> int:
        """compare_pairs.py's pair list for a comparison the walk could not
        read under both treatments of an undecided pair."""
        prints = {}
        with open(os.path.join(fam.dir, "prints.tsv")) as fh:
            for line in fh:
                md5, _, hexed = line.rstrip("\n").partition("\t")
                prints[md5] = int(hexed, 16)
        shared, n = set(fa) & set(fb), 0
        with open(os.path.join(fam.dir, f"fallback-{comp}.csv"), "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["id", "a_path", "b_path"])
            for a in fa:
                for b in fb:
                    if a in shared or b in shared:
                        continue
                    pa, pb = prints[a], prints[b]
                    if pa & ~pb and pb & ~pa:
                        continue  # a word refutes both directions: incomparable
                    w.writerow([f"{comp}|{fam.spec}|{a}|{b}", fam.path(a), fam.path(b)])
                    n += 1
        return n

    def family(self, spec: str, comps: dict) -> None:
        out_path = os.path.join(self.args.work, spec, "result.json")
        if os.path.exists(out_path):
            return
        fam = Family(self.args.work, spec)
        t = time.time()
        result = {"spec": spec, "settings": self.walk_settings, "frontiers": {}, "comparisons": {}}
        fronts = {}
        for side in sorted({s for pair in comps.values() for s in pair}):
            key, fr = self.frontier(fam, side)
            fronts[side] = (key, fr["members"])
            result["frontiers"][side] = {"key": key, "members": fr["members"], "pool": len(fam.pool(side)),
                                         "queries": fr["queries"], "undecided": fr["undecided"],
                                         "cached": fr["cached"], "secs": fr["secs"]}
        for comp, (a, b) in sorted(comps.items()):
            (ka, fa), (kb, fb) = fronts[a], fronts[b]
            _, cr = self.cross(fam, fa, fb, ka, kb)
            cr = dict(cr, A=a, B=b, front_A=len(fa), front_B=len(fb))
            if cr["walk"]["undecided"] or cr["undecided_A"] or cr["undecided_B"]:
                cr["fallback_pairs"] = self.fallback(fam, comp, fa, fb)
            result["comparisons"][comp] = cr
        result["secs"] = round(time.time() - t, 2)
        atomic_json(out_path, result)
        print(f"{spec}: done in {result['secs']} s", flush=True)


def cmd_run(args) -> int:
    with open(os.path.join(args.work, "plan.json")) as fh:
        plan = json.load(fh)
    with open(os.path.join(args.work, f"jobs-{args.host}.csv"), newline="") as fh:
        specs = [r["spec"] for r in csv.DictReader(fh)]
    os.makedirs(args.tmp, exist_ok=True)
    runner = Runner(args)
    failed = []

    def one(spec):
        try:
            runner.family(spec, plan["specs"][spec]["comparisons"])
        except Exception as exc:  # one family's failure must not stop the rest
            failed.append(spec)
            print(f"{spec}: FAILED {exc}", file=sys.stderr, flush=True)

    with ThreadPoolExecutor(args.workers) as ex:
        list(ex.map(one, specs))
    missing = [s for s in specs if not os.path.exists(os.path.join(args.work, s, "result.json"))]
    print(f"{len(specs) - len(missing)}/{len(specs)} families done", flush=True)
    if not missing:
        n = join_fallbacks(args.work, args.host, specs)
        print(f"{n} fallback pair(s) in fallback-{args.host}.csv", flush=True)
    return 0 if not missing else 1


def join_fallbacks(work: str, host: str, specs: list) -> int:
    """Every fallback pair of this host's families in one compare_pairs.py
    list, WORK/fallback-<host>.csv, header only when there are none, so a
    compare phase can follow this one unconditionally. Its ids carry the
    comparison and family, which split_fallbacks() uses to file the results
    beside each family."""
    n = 0
    tmp = os.path.join(work, f"fallback-{host}.csv.tmp")
    with open(tmp, "w", newline="") as out:
        w = csv.writer(out)
        w.writerow(["id", "a_path", "b_path"])
        for spec in sorted(specs):
            for path in sorted(os.listdir(os.path.join(work, spec))):
                if not (path.startswith("fallback-") and path.endswith(".csv")) or path.endswith(".results.csv"):
                    continue
                with open(os.path.join(work, spec, path), newline="") as fh:
                    for r in csv.DictReader(fh):
                        w.writerow([r["id"], r["a_path"], r["b_path"]])
                        n += 1
    os.replace(tmp, os.path.join(work, f"fallback-{host}.csv"))
    return n


def split_fallbacks(work: str, results: list) -> int:
    """compare_pairs.py's output for the joined lists, filed back as
    WORK/<spec>/fallback-<comp>.results.csv, which `score` reads."""
    rows = {}
    for path in results:
        with open(path, newline="") as fh:
            for r in csv.DictReader(fh):
                comp, spec, _, _ = r["id"].split("|")
                rows.setdefault((spec, comp), []).append(r)
    for (spec, comp), rs in rows.items():
        with open(os.path.join(work, spec, f"fallback-{comp}.results.csv"), "w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=["id", "relation", "rc", "secs"], extrasaction="ignore")
            w.writeheader()
            w.writerows(rs)
    return len(rows)


# ---------------------------------------------------------------- score


def wilcoxon_exact(x: list) -> float:
    """Two-sided exact signed-rank p, as scipy.stats.wilcoxon(x,
    method="exact") computes it: tie-averaged ranks for the statistic, the
    no-ties null distribution for the p."""
    n = len(x)
    if n == 0:
        return 1.0
    order = sorted(range(n), key=lambda i: abs(x[i]))
    ranks = [0.0] * n
    i = 0
    while i < n:
        j = i
        while j + 1 < n and abs(x[order[j + 1]]) == abs(x[order[i]]):
            j += 1
        for k in range(i, j + 1):
            ranks[order[k]] = (i + j) / 2 + 1
        i = j + 1
    r_plus = sum(r for r, v in zip(ranks, x) if v > 0)
    top = n * (n + 1) // 2
    counts = [1] + [0] * top
    for k in range(1, n + 1):
        for s in range(top, k - 1, -1):
            counts[s] += counts[s - k]
    total = 2 ** n
    lo = sum(counts[: int(r_plus) + 1]) / total
    hi = sum(counts[int(-(-r_plus // 1)):]) / total
    return min(1.0, 2 * min(lo, hi))


def comp_from_pairs(path: str, shared: set) -> dict:
    """Per-pair scoring of one comparison, under both readings."""
    hit = {(s, k): set() for s in "AB" for k in ("non", "imp")}
    with open(path, newline="") as fh:
        for r in csv.DictReader(fh):
            _, _, a, b = r["id"].split("|")
            rel = r["relation"]
            if rel == "weaker":
                hit[("A", "non")].add(a); hit[("A", "imp")].add(a)
            elif rel == "stronger":
                hit[("B", "non")].add(b); hit[("B", "imp")].add(b)
            elif rel in ("undecided", "error"):
                hit[("A", "imp")].add(a); hit[("B", "imp")].add(b)
    return {k: len(v - shared) for k, v in hit.items()}


def cmd_score(args) -> int:
    with open(os.path.join(args.work, "plan.json")) as fh:
        plan = json.load(fh)
    os.makedirs(args.out, exist_ok=True)
    comps = args.comp or sorted(plan["comparisons"])
    report = []
    for comp in comps:
        a_label, b_label = plan["comparisons"][comp]
        rows, unresolved, missing = [], [], []
        for spec in sorted(plan["specs"]):
            if comp not in plan["specs"][spec]["comparisons"]:
                continue
            rpath = os.path.join(args.work, spec, "result.json")
            if not os.path.exists(rpath):
                missing.append(spec)
                continue
            with open(rpath) as fh:
                cr = json.load(fh)["comparisons"][comp]
            n_a, n_b = cr["front_A"], cr["front_B"]
            if not n_a or not n_b:
                report.append(f"{comp} {spec}: empty frontier (A {n_a}, B {n_b}), skipped")
                continue
            sub_non = {"A": len(cr["hit_A"]) / n_a, "B": len(cr["hit_B"]) / n_b}
            sub_imp = None
            undec = cr["walk"]["undecided"] + len(cr["undecided_A"]) + len(cr["undecided_B"])
            errs = len(cr["errors_A"]) + len(cr["errors_B"])
            if undec == 0:
                sub_imp = sub_non
            else:
                fb_res = os.path.join(args.work, spec, f"fallback-{comp}.results.csv")
                if os.path.exists(fb_res):
                    h = comp_from_pairs(fb_res, set(cr["shared"]))
                    sub_non = {"A": h[("A", "non")] / n_a, "B": h[("B", "non")] / n_b}
                    sub_imp = {"A": h[("A", "imp")] / n_a, "B": h[("B", "imp")] / n_b}
                else:
                    unresolved.append(spec)
            net_non = round(sub_non["B"] - sub_non["A"], 4)
            net_imp = "" if sub_imp is None else round(sub_imp["B"] - sub_imp["A"], 4)
            rows.append([spec, n_a, n_b, net_non, net_imp, round(sub_non["A"], 4), round(sub_non["B"], 4),
                         cr["walk"]["queries"], undec, errs])
        with open(os.path.join(args.out, f"families-{comp}.csv"), "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["spec", "front_A", "front_B", "net_non", "net_imp", "subA_non", "subB_non",
                        "pairs", "undecided", "errors"])
            w.writerows(rows)
        report.append(f"\n== pooled {comp}: A = {a_label}, B = {b_label} ({len(rows)} families)")
        for col, k in ((3, "non"), (4, "imp")):
            net = [r[col] for r in rows if r[col] != ""]
            if not net:
                continue
            nz = [v for v in net if v != 0]
            p = wilcoxon_exact(nz) if nz else 1.0
            med = statistics.median(net)
            report.append(f"  [{k}] A stronger on {sum(v > 0 for v in net)}, B on {sum(v < 0 for v in net)}, "
                          f"tie {sum(v == 0 for v in net)}; median net {med:+.4f}, mean {sum(net) / len(net):+.4f}; "
                          f"exact Wilcoxon p {p:.4g}" + ("" if len(net) == len(rows) else f" (over {len(net)})"))
        if rows:
            report.append(f"  mean sub(A by B) {sum(r[5] for r in rows) / len(rows):.4f}, "
                          f"sub(B by A) {sum(r[6] for r in rows) / len(rows):.4f}")
        report.append(f"  solver queries in cross walks {sum(r[7] for r in rows)}, "
                      f"undecided {sum(r[8] for r in rows)}, errors {sum(r[9] for r in rows)}")
        if unresolved:
            report.append(f"  imp reading unresolved (run fallback-{comp}.csv): {', '.join(unresolved)}")
        if missing:
            report.append(f"  no result yet: {', '.join(missing)}")
    text = "\n".join(report).lstrip("\n") + "\n"
    with open(os.path.join(args.out, "report.txt"), "w") as fh:
        fh.write(text)
    sys.stdout.write(text)
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("plan")
    p.add_argument("sides", help="JSON: seeds, sides {label: {members, body}}, comparisons {name: [A, B]}")
    p.add_argument("work")
    p.add_argument("--specs", required=True, help="comma-separated families")
    p.add_argument("--hosts", default="av1,av2,av3")
    p.set_defaults(fn=cmd_plan)
    r = sub.add_parser("run")
    r.add_argument("work")
    r.add_argument("--host", required=True)
    r.add_argument("--workers", type=int, default=1, help="families in flight")
    r.add_argument("--solver-jobs", type=int, default=4, help="maximal --jobs per family")
    r.add_argument("--timeout", type=int, default=300, help="maximal --timeout (s per solver call)")
    r.add_argument("--prefilter-args", default="--prefilter-words 4096", help="extra maximal flags, e.g. '--prefilter-words 1024'")
    r.add_argument("--compare-args", default="", help="extra compare flags")
    r.add_argument("--maximal", default=os.path.join(REPO_ROOT, "build-release", "maximal"))
    r.add_argument("--compare", default=os.path.join(REPO_ROOT, "build-release", "compare"))
    r.add_argument("--wrap", default="", help="command prefix for every solver process")
    r.add_argument("--cache", default=None, help="walk cache directory (default WORK/cache)")
    r.add_argument("--tmp", default=tempfile.gettempdir())
    r.set_defaults(fn=cmd_run)
    s = sub.add_parser("score")
    s.add_argument("work")
    s.add_argument("out")
    s.add_argument("--comp", action="append")
    s.set_defaults(fn=cmd_score)
    f = sub.add_parser("split-fallbacks", help="file joined fallback results back per family")
    f.add_argument("work")
    f.add_argument("results", nargs="+", help="compare_pairs.py outputs of fallback-<host>.csv")
    f.set_defaults(fn=lambda a: print(f"{split_fallbacks(a.work, a.results)} comparison(s) filed") or 0)
    args = parser.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
