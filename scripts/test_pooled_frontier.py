#!/usr/bin/env python3
"""Tests for pooled_frontier.py against stub `maximal` and `compare` binaries.

    python3 scripts/test_pooled_frontier.py

A stub spec is a body `SET x y ...`: one spec implies another when its set
contains the other's, and two specs with the same set (in any order) are
equivalent. Its fingerprint accepts the words outside its set, so a stronger
spec accepts a subset of a weaker one's words, as with the real prints. The
stubs never touch a solver. The pass's result is checked against a brute-force
evaluation of the definition over the whole pools.
"""

import csv
import itertools
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pooled_frontier as pf  # noqa: E402

FAILURES = []

STUB_COMMON = r'''
import os, sys
def load(path):
    return frozenset(open(path).read().split()[1:])
def implies(a, b):
    return a >= b
'''

STUB_MAXIMAL = STUB_COMMON + r'''
args = sys.argv[1:]
if args == ["--version"]:
    print("commit=stub\ndirty=0"); sys.exit(0)
index = args[args.index("--curve") + 1]
d = os.path.dirname(index)
names = [l.split("\t")[0] for l in open(index).read().splitlines()[1:]]
with open(os.environ["STUB_LOG"], "a") as fh:
    fh.write("maximal\n")
anti = []
print("elapsed_s\tfile\tevent\tn_maximal")
for t, n in enumerate(names):
    s = load(os.path.join(d, n))
    if any(implies(load(os.path.join(d, m)), s) for m in anti):
        print(f"{t}\t{n}\tdrop\t{len(anti)}"); continue
    for m in list(anti):
        if implies(s, load(os.path.join(d, m))):
            anti.remove(m); print(f"{t}\t{m}\tremove\t{len(anti)}")
    anti.append(n); print(f"{t}\t{n}\tadmit\t{len(anti)}")
sys.stderr.write(f"\narrivals   {len(names)}\nqueries    7\nundecided  {os.environ.get('STUB_UNDECIDED', '0')}\n")
'''

STUB_COMPARE = STUB_COMMON + r'''
args = sys.argv[1:]
if args == ["--version"]:
    print("commit=stub\ndirty=0"); sys.exit(0)
rd, idir = args[args.index("--repairs") + 1], args[args.index("--ideals") + 1]
with open(os.environ["STUB_LOG"], "a") as fh:
    fh.write("compare\n")
rank = {"equivalent to ": 4, "strictly stronger than ": 3, "strictly weaker than ": 2, "incomparable": 1}
for r in sorted(os.listdir(rd)):
    a, best = load(os.path.join(rd, r)), "incomparable"
    for i in sorted(os.listdir(idir)):
        b = load(os.path.join(idir, i))
        rel = ("equivalent to " if a == b else "strictly stronger than " if a > b
               else "strictly weaker than " if a < b else "incomparable")
        if rank[rel] > rank[best]:
            best = rel
    print(f"{r} : {best}")
print("\nSummary: done")
'''


def expect(cond, what):
    if not cond:
        FAILURES.append(what)
        print(f"FAIL {what}")


def write_side(root, label, runs):
    """runs: {seed: [body, ...]}; writes members/fingerprints sidecars and bodies."""
    for seed, bodies in runs.items():
        stem = f"{root}/side/{label}_fam_seed{seed:02d}"
        os.makedirs(f"{root}/bodies/{label}/{seed}", exist_ok=True)
        with open(f"{stem}.members.tsv", "w") as m, open(f"{stem}.fingerprints.tsv", "w") as f:
            m.write("cut_s\tfile\n")
            f.write("file\tprint\n")
            for k, body in enumerate(bodies):
                name = f"r{k}.tlsf"
                m.write(f"1.0\t{name}\n")
                bits = 0
                for word in range(16):
                    if f"w{word}" not in body.split():
                        bits |= 1 << word
                f.write(f"{name}\t{bits:04x}\n")
                with open(f"{root}/bodies/{label}/{seed}/{name}", "w") as fh:
                    fh.write(body + "\n")


def brute(pools):
    """The definition, evaluated over the whole pools by set containment."""
    def key(body):
        return frozenset(body.split()[1:])
    groups = {s: {key(b) for b in bodies} for s, bodies in pools.items()}
    front = {s: [g for g in gs if not any(h > g for h in gs)] for s, gs in groups.items()}
    sub = {s: sum(any(h > g for h in groups[o]) for g in front[s]) / len(front[s])
           for s, o in (("P", "A"), ("A", "P"))}
    return len(front["P"]), len(front["A"]), round(sub["A"] - sub["P"], 4)


def main():
    with tempfile.TemporaryDirectory() as root:
        os.makedirs(f"{root}/side")
        for name, src in (("maximal", STUB_MAXIMAL), ("compare", STUB_COMPARE)):
            with open(f"{root}/{name}", "w") as fh:
                fh.write(f"#!{sys.executable}\n{src}")
            os.chmod(f"{root}/{name}", 0o755)
        os.environ["STUB_LOG"] = f"{root}/log"
        p_runs = {0: ["SET w1 w2 w3", "SET w4", "SET w9"],
                  1: ["SET w1 w2", "SET w5 w6", "SET  w4"],          # copy of w4, other spacing
                  2: ["SET w7 w8", "SET w10 w11"]}
        a_runs = {0: ["SET w1 w2 w3 w0", "SET w6 w5", "SET w4"],      # strictly above P's w1w2w3; equivalent to w5w6; shared
                  1: ["SET w8", "SET w12 w13"]}                      # below P's w7w8
        write_side(root, "P", p_runs)
        write_side(root, "A", a_runs)
        sides = {"seeds": "0-2",
                 "sides": {lab: {"members": f"{root}/side/{lab}_{{spec}}_seed{{seed:02d}}.members.tsv",
                                 "body": f"{root}/bodies/{lab}/{{seed}}/{{file}}"} for lab in ("P", "A")},
                 "comparisons": {"rq3": ["P", "A"]}}
        with open(f"{root}/sides.json", "w") as fh:
            json.dump(sides, fh)
        work = f"{root}/work"
        expect(pf.main(["plan", f"{root}/sides.json", work, "--specs", "fam", "--hosts", "h"]) == 0, "plan exits 0")
        # A relative work path, as the campaign phase passes it, still yields
        # absolute class paths: compare_grid symlinks them from /tmp.
        cwd = os.getcwd()
        os.chdir(root)
        try:
            expect(os.path.isabs(pf.Family("work", "fam").path("x")), "class paths are absolute")
        finally:
            os.chdir(cwd)
        # black's wrapper, never the bare binary that cannot find libblack.so.
        saved_tp, saved_env = pf.THIRD_PARTY, os.environ.pop("PEREDUR_BLACK_PATH", None)
        try:
            pf.THIRD_PARTY = f"{root}/tp"
            for f in ("black/black", "black/install/bin/black"):
                os.makedirs(os.path.dirname(f"{root}/tp/{f}"), exist_ok=True)
                open(f"{root}/tp/{f}", "w").close()
            got = pf.tool_env().get("PEREDUR_BLACK_PATH", "")
            expect(got == f"{root}/tp/black/black", f"black path is the wrapper, got {got}")
        finally:
            pf.THIRD_PARTY = saved_tp
            if saved_env is not None:
                os.environ["PEREDUR_BLACK_PATH"] = saved_env
        run = ["run", work, "--host", "h", "--maximal", f"{root}/maximal", "--compare", f"{root}/compare",
               "--tmp", f"{root}/tmp", "--prefilter-args", ""]
        expect(pf.main(run) == 0, "run exits 0")
        with open(f"{work}/fam/result.json") as fh:
            cr = json.load(fh)["comparisons"]["rq3"]
        log = open(f"{root}/log").read().split()
        expect(log.count("maximal") == 3, f"two frontier walks and one cross walk, got {log}")
        expect(log.count("compare") == 1, f"one compare grid for the print-matched candidate, got {log}")
        expect(cr["equiv_checked_A"] + cr["equiv_checked_B"] == 1 and len(cr["shared"]) == 1, f"equivalence and shared class: {cr}")
        pf.main(["score", work, f"{root}/out"])
        with open(f"{root}/out/families-rq3.csv") as fh:
            row = next(csv.DictReader(fh))
        pools = {"P": [b for bs in p_runs.values() for b in bs], "A": [b for bs in a_runs.values() for b in bs]}
        n_p, n_a, net = brute(pools)
        expect((int(row["front_A"]), int(row["front_B"]), float(row["net_non"])) == (n_p, n_a, net),
               f"score {row} against brute force {(n_p, n_a, net)}")
        expect(row["net_imp"] == row["net_non"], "no undecided pair: both readings agree")

        # A second run reuses every walk from the cache.
        os.remove(f"{work}/fam/result.json")
        open(f"{root}/log", "w").close()
        expect(pf.main(run) == 0, "cached run exits 0")
        expect(open(f"{root}/log").read() == "", "cached run starts no process")

        # An undecided cross query writes the fallback pair list and leaves net_imp open.
        os.remove(f"{work}/fam/result.json")
        os.environ["STUB_UNDECIDED"] = "1"
        expect(pf.main([*run, "--cache", f"{root}/cache2"]) == 0, "undecided run exits 0")
        del os.environ["STUB_UNDECIDED"]
        expect(os.path.exists(f"{work}/fam/fallback-rq3.csv"), "fallback pair list written")
        pf.main(["score", work, f"{root}/out"])
        with open(f"{root}/out/families-rq3.csv") as fh:
            row = next(csv.DictReader(fh))
        expect(row["net_imp"] == "", f"net_imp left blank without fallback results: {row}")

        # The host's lists are joined for a compare phase, and its output is filed back.
        with open(f"{work}/fam/fallback-rq3.csv") as fh:
            planned = list(csv.DictReader(fh))
        with open(f"{work}/fallback-h.csv") as fh:
            joined = list(csv.DictReader(fh))
        expect(joined == planned, f"fallback-h.csv joins the family's list: {len(joined)} vs {len(planned)}")
        with open(f"{root}/fb-out.csv", "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["id", "relation", "rc", "secs"])
            w.writerows([r["id"], "incomparable", 0, 0.1] for r in joined)
        expect(pf.main(["split-fallbacks", work, f"{root}/fb-out.csv"]) == 0, "split-fallbacks exits 0")
        expect(os.path.exists(f"{work}/fam/fallback-rq3.results.csv"), "results filed beside the family")
        pf.main(["score", work, f"{root}/out"])
        with open(f"{root}/out/families-rq3.csv") as fh:
            row = next(csv.DictReader(fh))
        expect(row["net_imp"] != "", f"net_imp read from the filed results: {row}")

        # A family with a sampled estimate and no walk result is scored from the estimate.
        os.remove(f"{work}/fam/result.json")
        est = {"net_non": -0.25, "net_imp": -0.5, "subA_non": 0.3, "subB_non": 0.05, "undecided": 4}
        with open(f"{work}/fam/sampled.json", "w") as fh:
            json.dump({"spec": "fam", "pairs": 7, "comparisons": {"rq3": est}}, fh)
        pf.main(["score", work, f"{root}/out"])
        with open(f"{root}/out/families-rq3.csv") as fh:
            row = next(csv.DictReader(fh))
        expect((row["front_A"], row["net_non"], row["net_imp"], row["pairs"]) == ("sampled", "-0.25", "-0.5", "7"),
               f"sampled estimate scored: {row}")
        expect("no result yet" not in open(f"{root}/out/report.txt").read(), "sampled family is not missing")

    # The exact Wilcoxon against values scipy gave for the archived passes.
    expect(abs(pf.wilcoxon_exact([1.0, -2.0, 3.0, 4.0, 5.0]) - 0.1875) < 1e-12, "wilcoxon n=5")
    expect(pf.wilcoxon_exact([]) == 1.0, "wilcoxon empty")
    for a, b in itertools.combinations([0.1, -0.2, 0.3], 2):
        expect(0 < pf.wilcoxon_exact([a, b]) <= 1, "wilcoxon bounds")

    print(f"\n{len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
