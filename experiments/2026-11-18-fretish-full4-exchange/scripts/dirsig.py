"""Direction-split signatures: each place where a candidate differs from O is
'down' (the specification is weaker there), 'up' (stronger) or 'side'
(incomparable or undecided). Per field, with the slot's other fields taken
from O, by the orders of the draft's section 3.1:
  timing: closure of Table 2 (plain scopes) and its dual (only scopes);
  scope: Table 3; condition type: continual => trigger;
  response/condition/stop: polarity, with truth-table implication.
"""
import itertools, os, sys, csv, json
from functools import lru_cache
from multiprocessing import Pool
import sig

ONLY = {"OnlyIn", "OnlyBefore", "OnlyAfter"}
BOUNDARY = {"In", "NotIn", "Before", "OnlyIn", "OnlyAfter"}


@lru_cache(maxsize=None)
def fimplies(a, b):
    ea, eb = sig.parse(a), sig.parse(b)
    at = set(); sig.atoms(ea, at); sig.atoms(eb, at); at = sorted(at)
    if len(at) > 16:
        return None
    for bits in itertools.product((False, True), repeat=len(at)):
        v = dict(zip(at, bits))
        if sig.ev(ea, v) and not sig.ev(eb, v):
            return False
    return True


def tnorm(t):
    k, n = t.get("type"), t.get("ticks")
    if k in ("WithinTicks", "ForTicks") and n == 0:
        return ("Immediately", None)
    if k in ("Until", "Before"):
        return (k, "s")  # stop handled as its own field
    return (k, n)


def plain_edge(a, b, bnd):
    """One rule of Table 2's upper part, a => b."""
    ka, na = a; kb, nb = b
    if ka == "Always" and kb not in ("AfterTicks", "Before"):
        return True
    if kb == "Eventually":
        if ka in ("Immediately", "ForTicks", "Always"):
            return True
        if not bnd and ka in ("NextTimepoint", "WithinTicks", "AfterTicks"):
            return True
    if ka == "ForTicks" and kb == "ForTicks" and nb <= na:
        return True
    if ka == "ForTicks" and kb in ("Immediately", "NextTimepoint", "WithinTicks"):
        return True
    if ka in ("Immediately", "NextTimepoint") and kb == "WithinTicks":
        return True
    if ka == "WithinTicks" and kb == "WithinTicks" and nb >= na:
        return True
    if ka == "AfterTicks" and kb == "WithinTicks" and nb > na:
        return True
    if ka == "AfterTicks" and na == 0 and kb == "NextTimepoint":
        return True
    return False


def closure_implies(a, b, bnd):
    if a == b:
        return True
    ticks = {x[1] for x in (a, b) if isinstance(x[1], int)} | {0, 1}
    U = {a, b, ("Always", None), ("Eventually", None), ("Immediately", None),
         ("NextTimepoint", None), ("Until", "s")}
    for n in ticks:
        for k in ("WithinTicks", "ForTicks", "AfterTicks"):
            if n > 0 or k == "AfterTicks":
                U.add((k, n))
    U = list(U)
    reach = {u: {v for v in U if v == u or plain_edge(u, v, bnd)} for u in U}
    changed = True
    while changed:
        changed = False
        for u in U:
            new = set().union(*(reach[v] for v in reach[u]))
            if new - reach[u]:
                reach[u] |= new; changed = True
    return b in reach[a]


DUAL = {"WithinTicks": "ForTicks", "ForTicks": "WithinTicks", "Eventually": "Always",
        "Always": "Eventually", "Until": "Before", "Before": "Until"}


def timing_implies(a, b, scope):
    if scope in ONLY:
        X = scope != "OnlyBefore"
        if a[0] == "AfterTicks" or b[0] == "AfterTicks":
            if a == b:
                return True
            if b[0] != "AfterTicks":
                return False
            n = b[1]
            return (a[0] == "Eventually" or (a[0] == "WithinTicks" and a[1] > n)
                    or (a[0] == "NextTimepoint" and n == 0))
        d = lambda t: (DUAL.get(t[0], t[0]), t[1])
        return closure_implies(d(a), d(b), X)
    return closure_implies(a, b, scope in BOUNDARY)


def scope_edges(ctype, tkind):
    # Table 3 (whenever = continual, upon = trigger)
    if (ctype == "continual" and tkind != "Eventually") or (ctype == "trigger" and tkind == "Always"):
        e = {("Global", s) for s in ("In", "NotIn", "Before", "After")} | {("NotIn", "Before")}
    elif ctype == "trigger" and tkind != "Eventually":
        e = {("Global", "Before"), ("NotIn", "Before")}
    elif ctype == "continual":
        e = {("Global", "After"), ("NotIn", "Before")}
    else:
        e = {("NotIn", "Before")}
    return e


def rel(imp_cand_o, imp_o_cand):
    """Relation of the candidate requirement to O's: down = weaker."""
    if imp_cand_o and imp_o_cand:
        return "eq"
    if imp_o_cand:
        return "down"
    if imp_cand_o:
        return "up"
    return "side"


def field_dir(o, c, k):
    """Direction of changing field k of O's requirement o to c's value."""
    scope = (o.get("scope") or {}).get("type", "Global")
    tk = o["timing"].get("type")
    if k in ("tkind", "ticks"):
        a, b = tnorm(c["timing"]), tnorm(o["timing"])
        return rel(timing_implies(a, b, scope), timing_implies(b, a, scope))
    if k == "ctype":
        cc, oc = c["condition-type"], o["condition-type"]
        return "down" if (oc, cc) == ("continual", "trigger") else "up"
    if k in ("scope", "mode"):
        cs = (c.get("scope") or {}); os_ = (o.get("scope") or {})
        if cs.get("mode") != os_.get("mode") and cs and os_:
            return "side"
        e = scope_edges(o["condition-type"], tk)
        a, b = cs.get("type", "Global"), os_.get("type", "Global")
        return rel((a, b) in e, (b, a) in e)
    if k == "resp":
        if tk == "AfterTicks":
            return "side"
        x, y = c["response"], o["response"]
        r = rel(fimplies(x, y), fimplies(y, x))
        return {"up": "down", "down": "up"}.get(r, r) if scope in ONLY else r
    if k == "cond":
        if o["condition-type"] != "continual":
            return "side"
        x, y = c.get("condition", "true"), o.get("condition", "true")
        r = rel(fimplies(x, y), fimplies(y, x))
        return {"up": "down", "down": "up"}.get(r, r)  # negative polarity
    if k == "stop":
        if "stop" not in c["timing"] or "stop" not in o["timing"] or c["timing"]["type"] != tk:
            return field_dir(o, c, "tkind")  # the stop came or went with the kind
        x, y = c["timing"]["stop"], o["timing"]["stop"]
        r = rel(fimplies(x, y), fimplies(y, x))
        neg = (tk == "Before") != (scope in ONLY)
        return {"up": "down", "down": "up"}.get(r, r) if neg else r
    return "side"


def slot_dir(ds):
    """A slot's direction: up or down only when every changed field agrees.
    Two fields that disagree, or any undecided one, make the slot sideways."""
    ds = set(ds) - {"eq"}
    return ds.pop() if len(ds) == 1 and ds <= {"up", "down"} or ds in ({"up"}, {"down"}) else ("eq" if not ds else "side")


def places(O, R):
    """Set of (side, slot, field, dir) with dir relative to the specification."""
    og, rg = O["guarantees"], R["guarantees"]
    oa, ra = O["assumptions"], R["assumptions"]
    fo = [sig.fields(g) for g in og]; fr = [sig.fields(g) for g in rg]
    keep = sig.align(fo, fr)
    out = set()
    for i in range(len(og)):
        if i not in keep:
            out.add(("G", i, "removed", "down"))
    for j, i in enumerate(keep):
        ks = sig.diff(fo[i], fr[j])
        sd = slot_dir([field_dir(og[i], rg[j], k) for k in ks])
        for k in ks:
            out.add(("G", i, k, sd))
    fa = [sig.fields(a) for a in oa]; fb = [sig.fields(a) for a in ra]
    left_o = list(range(len(oa))); left_r = list(range(len(ra)))
    for i in list(left_o):  # identical assumptions pair first
        for j in left_r:
            if fa[i] == fb[j]:
                left_o.remove(i); left_r.remove(j); break
    for i, j in zip(left_o, left_r):  # then edited ones, in order
        ks = sig.diff(fa[i], fb[j])
        d = slot_dir([field_dir(oa[i], ra[j], k) for k in ks])
        d = {"up": "down", "down": "up"}.get(d, d)  # a stronger assumption weakens S
        for k in ks:
            out.add(("A", i, k, d))
    for i in left_o[len(left_r):]:
        out.add(("A", i, "removed", "up"))
    for _ in left_r[len(left_o):]:
        out.add(("A", "+", "added", "down"))
    return frozenset(out)


def summary(ps):
    dirs = {p[3] for p in ps}
    return dict(up=int("up" in dirs), side=int("side" in dirs),
                down_sig=frozenset(p[:3] for p in ps if p[3] == "down"))


def small_run(args):
    arm, spec, seed = args
    d = f"{sig.RES}/sweep_O_{arm}_nsga2-apportion_wkoff_log_{spec}_seed{seed:02d}"
    if not os.path.isfile(f"{d}/run.json"):
        return None
    O = sig.load(f"{sig.ROOT}/examples/{spec}/spec.json")
    acc = f"{d}/accumulated"
    pre = [sig.load(f"{acc}/{n}") for n in os.listdir(acc) if n.startswith("gen")] if os.path.isdir(acc) else []
    out = [sig.load(f"{d}/{n}") for n in os.listdir(d) if n.startswith("repair_")]
    sp = [summary(places(O, r)) for r in pre]; so = [summary(places(O, r)) for r in out]
    f = lambda xs, k: sum(x[k] for x in xs) / len(xs) if xs else float("nan")
    return dict(arm=arm, spec=spec, seed=seed, n_pre=len(sp), n_out=len(so),
                up_pre=f(sp, "up"), side_pre=f(sp, "side"), up_out=f(so, "up"), side_out=f(so, "side"),
                down_pre=len({x["down_sig"] for x in sp}), down_out=len({x["down_sig"] for x in so}))


E = os.path.expanduser("~/projects/peredur/experiments")
SUB = f"{E}/2026-11-13-seeded-corpus/subjects"
SRES = f"{E}/results-fretish-seeded"


def seeded_run(args):
    arm, sub, seed = args
    d = f"{SRES}/sweep_O_{arm}_nsga2-apportion_wkoff_log_{sub}_seed{seed:02d}"
    if not os.path.isfile(f"{d}/run.json"):
        return None
    O = sig.load(f"{SUB}/{sub}/spec.json"); B = sig.load(f"{SUB}/{sub}/fixes/base.json")
    eb = {p[:3] for p in places(O, B)}
    last = sig.load(f"{d}/run.json")["generations_run"]
    acc = f"{d}/accumulated"
    pre = [sig.load(f"{acc}/{n}") for n in os.listdir(acc)
           if n.startswith("gen") and int(n[3:n.index("_")]) <= last] if os.path.isdir(acc) else []
    out = [sig.load(f"{d}/{n}") for n in os.listdir(d) if n.startswith("repair_")]
    hs = hl = 0
    for r in pre:
        ps = places(O, r)
        down = {p[:3] for p in ps if p[3] == "down"}
        other = {p[:3] for p in ps if p[3] != "down"}
        if down and down <= eb:
            hl = 1  # loose: up and side places allowed anywhere
            if not other:
                hs = 1  # strict: nothing but down places inside B's
    so = [summary(places(O, r)) for r in out]
    sp = [summary(places(O, r)) for r in pre]
    f = lambda xs, k: sum(x[k] for x in xs) / len(xs) if xs else float("nan")
    return dict(arm=arm, spec=sub, seed=seed, hit_strict=hs, hit_loose=hl,
                up_pre=f(sp, "up"), up_out=f(so, "up"), side_out=f(so, "side"),
                down_pre=len({x["down_sig"] for x in sp}))


if __name__ == "__main__":
    which, arms = sys.argv[1], sys.argv[2].split(",")
    if which == "small":
        jobs = [(a, s, k) for a in arms for s in sig.SPECS for k in range(30)]; fn = small_run
    else:
        subs = [l.strip() for l in open(f"{E}/2026-11-13-seeded-corpus/selected.txt") if l.strip() and not l.startswith("#")]
        jobs = [(a, s, k) for a in arms for s in subs for k in range(5)]; fn = seeded_run
    with Pool(16) as p:
        rows = [r for r in p.map(fn, jobs, chunksize=4) if r]
    w = csv.DictWriter(sys.stdout, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
