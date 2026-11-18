"""Difference signatures of pre-climb and post-climb repairs, per run.

A repair's coarse signature is the set of (side, original slot, field) where it
differs from the original O; a removed guarantee gives (G, i, removed), an added
assumption (A, +, added). The fine signature also records the new value.
"""
import csv, itertools, json, os, re, sys
from functools import lru_cache
from multiprocessing import Pool

ROOT = os.path.expanduser("~/projects/peredur")
RES = f"{ROOT}/experiments/results-fretish-order"
SPECS = ["fsm", "fsm-timing", "takeoff", "mode-arbiter", "fsm-combined",
         "liquid-mixer", "valu3s-uc6", "fsm-lmcps", "lift-plus-cruise-mini"]

TOK = re.compile(r"\s*(<->|->|[()!&|]|[A-Za-z_][A-Za-z0-9_.]*)")


def parse(s):
    toks = TOK.findall(s)
    pos = 0

    def peek():
        return toks[pos] if pos < len(toks) else None

    def eat():
        nonlocal pos
        pos += 1
        return toks[pos - 1]

    def iff():
        l = imp()
        while peek() == "<->":
            eat(); l = ("iff", l, imp())
        return l

    def imp():
        l = orr()
        if peek() == "->":
            eat(); return ("imp", l, imp())
        return l

    def orr():
        l = andd()
        while peek() == "|":
            eat(); l = ("or", l, andd())
        return l

    def andd():
        l = un()
        while peek() == "&":
            eat(); l = ("and", l, un())
        return l

    def un():
        t = eat()
        if t == "!":
            return ("not", un())
        if t == "(":
            e = iff(); eat(); return e
        return ("var", t)

    return iff()


def atoms(e, acc):
    if e[0] == "var":
        if e[1] not in ("true", "false"):
            acc.add(e[1])
    else:
        for c in e[1:]:
            atoms(c, acc)


def ev(e, v):
    k = e[0]
    if k == "var":
        return {"true": True, "false": False}.get(e[1], v.get(e[1], False))
    if k == "not":
        return not ev(e[1], v)
    a, b = ev(e[1], v), ev(e[2], v)
    return {"and": a and b, "or": a or b, "imp": (not a) or b, "iff": a == b}[k]


@lru_cache(maxsize=None)
def canon(s):
    """Truth-table canonical form, so differently bracketed formulas compare equal."""
    e = parse(s)
    at = set(); atoms(e, at); at = sorted(at)
    if len(at) > 14:
        return re.sub(r"[\s()]", "", s)
    rows = []
    for bits in itertools.product((False, True), repeat=len(at)):
        rows.append("1" if ev(e, dict(zip(at, bits))) else "0")
    # drop atoms the formula does not depend on
    return (tuple(at), "".join(rows))


def fields(r):
    f = {"ctype": r.get("condition-type"), "cond": canon(r.get("condition", "true")),
         "resp": canon(r["response"])}
    t = r.get("timing", {})
    f["tkind"] = t.get("type")
    f["ticks"] = t.get("ticks")
    if "stop" in t:
        f["stop"] = canon(t["stop"])
    sc = r.get("scope")
    if sc:
        f["scope"] = sc.get("type"); f["mode"] = sc.get("mode")
    return f


def diff(a, b):
    return {k for k in set(a) | set(b) if a.get(k) != b.get(k)}


def align(orig, rep):
    """Order-preserving alignment of rep's guarantees to orig's (removals only)."""
    n, m = len(orig), len(rep)
    if m == n:
        return list(range(n))
    best = None
    for keep in itertools.combinations(range(n), m):
        cost = sum(len(diff(orig[i], rep[j])) for j, i in enumerate(keep))
        if best is None or cost < best[0]:
            best = (cost, keep)
    return list(best[1])


def signature(O, R):
    og = [fields(g) for g in O["guarantees"]]
    oa = [fields(a) for a in O["assumptions"]]
    rg = [fields(g) for g in R["guarantees"]]
    ra = [fields(a) for a in R["assumptions"]]
    coarse, fine = set(), set()
    keep = align(og, rg)
    for i in range(len(og)):
        if i not in keep:
            coarse.add(("G", i, "removed")); fine.add(("G", i, "removed"))
    for j, i in enumerate(keep):
        for k in diff(og[i], rg[j]):
            coarse.add(("G", i, k)); fine.add(("G", i, k, str(rg[j].get(k))))
    # assumptions: original ones first (may be edited), then appended
    na = len(oa)
    for i in range(min(na, len(ra))):
        for k in diff(oa[i], ra[i]):
            coarse.add(("A", i, k)); fine.add(("A", i, k, str(ra[i].get(k))))
    for a in ra[na:]:
        coarse.add(("A", "+", "added")); fine.add(("A", "+", str(sorted(a.items()))))
    return frozenset(coarse), frozenset(fine)


def load(p):
    with open(p) as fh:
        return json.load(fh)


def run_stats(args):
    arm, spec, seed = args
    d = f"{RES}/sweep_O_{arm}_nsga2-apportion_wkoff_log_{spec}_seed{seed:02d}"
    if not os.path.isdir(d):
        return None
    O = load(f"{ROOT}/examples/{spec}/spec.json")
    acc = f"{d}/accumulated"
    pre = [load(f"{acc}/{n}") for n in os.listdir(acc) if n.startswith("gen")] if os.path.isdir(acc) else []
    out = [load(f"{d}/{n}") for n in os.listdir(d) if n.startswith("repair_")]
    sp = [signature(O, r) for r in pre]
    so = [signature(O, r) for r in out]
    cl = load(f"{d}/run.json").get("climb", {})
    return dict(arm=arm, spec=spec, seed=seed, n_pre=len(pre), n_out=len(out),
                c_pre=len({s[0] for s in sp}), f_pre=len({s[1] for s in sp}),
                c_out=len({s[0] for s in so}), f_out=len({s[1] for s in so}),
                slots_pre=len({x[:2] for s in sp for x in s[0]}),
                removed_pre=sum(any(x[2] == "removed" for x in s[0]) for s in sp),
                steps_acc=cl.get("steps_accepted", 0), climbed=cl.get("climbed", 0))


if __name__ == "__main__":
    arms = sys.argv[1].split(",")
    jobs = [(a, s, k) for a in arms for s in SPECS for k in range(30)]
    with Pool(16) as p:
        rows = [r for r in p.map(run_stats, jobs, chunksize=4) if r]
    w = csv.DictWriter(sys.stdout, fieldnames=list(rows[0]))
    w.writeheader(); w.writerows(rows)
