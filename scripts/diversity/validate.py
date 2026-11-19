#!/usr/bin/env python3
"""Check the screen against a pooled pass.

    python3 scripts/diversity/validate.py frontier DESIGN MEMBERS COLLECTED \\
        --subjects fsm mode-arbiter [--arms ARM...] [--work DIR]
    python3 scripts/diversity/validate.py containment DESIGN MEMBERS COLLECTED \\
        --contrast full OUT.json [--subjects ...] [--k 3] [--draws 20]
    python3 scripts/diversity/validate.py compare SCREEN.json POOLED.json

DESIGN, MEMBERS and COLLECTED are pooled_reduce.py's inputs.

`frontier` takes each arm's frontier classes twice over the pass's own nodes:
from the pass's BLACK verdicts (pooled_reduce.frontier, undecided read as no
implication) and from `compare --rule-only`. It reports the class counts, the
classes found by one and not the other, and the nodes on one frontier only.
Exhaustive subjects only, since a sampled one has no verdicts for most pairs.

`containment` runs pooled_reduce.pooled_task for one contrast of the design
on the pass's own fingerprints: the pass's net containment for that contrast,
which `pooled_reduce.py --pooled-balls` would write among all the others.

`compare` sets a screen's per-subject containment beside a `containment`
output: the sign agreement and the paired summary of each, and the Spearman
correlation, per subject, of descriptor count and run containment.
"""

import argparse
import json
import os
import statistics
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import fingerprint_members as F  # noqa: E402
import pooled_design as D  # noqa: E402
import pooled_reduce as R  # noqa: E402
import screen as S  # noqa: E402


def cmd_frontier(args) -> int:
    design = D.load_design(args.design)
    by = R.load_rows(args.collected)
    out = []
    for subject in args.subjects:
        rec = design["subjects"][subject]
        if rec["sampled"]:
            sys.exit(f"validate: {subject} is sampled")
        nodes = R.load_nodes(args.members, subject)
        V, _ = R.verdicts(by.get(subject, {}))
        paths = F.materialise(nodes, args.work, subject)
        wdir = os.path.join(args.work, subject)
        for arm in args.arms or rec["arms"]:
            ids = [n["node"] for n in nodes if arm in n["arms"]]
            black = R.frontier(nodes, V, arm)
            rule = R.frontier(nodes, S.rule_verdicts(
                ids, paths, wdir, D.arm_slug(arm), args.compare), arm)
            cb, cr = {tuple(c) for c in black}, {tuple(c) for c in rule}
            nb = {i for c in black for i in c}
            nr = {i for c in rule for i in c}
            # Proved pairs BLACK decided: how many BLACK implications in the
            # arm the rules miss.
            held = set(ids)
            black_imp = {(x, y) for (x, y), v in V.items()
                         if v == "1" and x in held and y in held}
            row = {"subject": subject, "arm": arm, "nodes": len(ids),
                   "black_classes": len(black), "rule_classes": len(rule),
                   "classes_black_only": len(cb - cr),
                   "classes_rule_only": len(cr - cb),
                   "nodes_black_only": len(nb - nr),
                   "nodes_rule_only": len(nr - nb),
                   "black_implications": len(black_imp)}
            out.append(row)
            print(json.dumps(row), flush=True)
    if args.out:
        F.write_whole(args.out, json.dumps(out, indent=1) + "\n")
    return 0


def cmd_containment(args) -> int:
    design = D.load_design(args.design)
    rows = []
    for subject in args.subjects or list(design["subjects"]):
        rec = design["subjects"][subject]
        c = next(c for c in rec["contrasts"] if c["name"] == args.contrast)
        nodes = R.load_nodes(args.members, subject)
        bits = R.load_bits(R.subject_dir(args.collected, subject), len(nodes))
        got = R.pooled_task((subject, c, nodes, bits, [args.k], args.draws,
                             args.seed))[0]
        if got["b_in_a"] != "":
            got["net"] = round(got["b_in_a"] - got["a_in_b"], 4)
        rows.append(got)
        print(json.dumps(got), flush=True)
    F.write_whole(args.out, json.dumps(rows, indent=1) + "\n")
    return 0


def cmd_compare(args) -> int:
    with open(args.screen) as h:
        screen = json.load(h)
    with open(args.pooled) as h:
        pooled = {r["subject"]: r for r in json.load(h)}
    pairs = []
    for row in screen["subjects"]:
        p = pooled.get(row["subject"])
        s_net = row["containment"].get("net")
        if p is None or "net" not in p or s_net is None:
            continue
        pairs.append((row["subject"], s_net, p["net"]))
    agree = sum((a > 0) == (b > 0) for _, a, b in pairs if a != 0 and b != 0)
    rho = {r["subject"]: r["spearman_descriptors_containment"]
           for r in screen["subjects"]}
    out = {"subjects": [{"subject": s, "screen_net": a, "pooled_net": b}
                        for s, a, b in pairs],
           "sign_agreement": f"{agree}/{len(pairs)}",
           "screen": S.paired([a for _, a, _ in pairs]),
           "pooled": S.paired([b for _, _, b in pairs]),
           "spearman_screen_vs_pooled_net": S.spearman(
               [a for _, a, _ in pairs], [b for _, _, b in pairs]),
           "descriptor_containment_rho": rho}
    print(json.dumps(out, indent=1))
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("frontier")
    for p in (f, sub.add_parser("containment")):
        p.add_argument("design")
        p.add_argument("members")
        p.add_argument("collected")
        p.add_argument("--subjects", nargs="*")
    c = sub.choices["containment"]
    f.add_argument("--arms", nargs="*")
    f.add_argument("--work", default="experiments/2026-11-19-diversity-screen/validate-work")
    f.add_argument("--compare", default=S.binary("compare"))
    f.add_argument("--out")
    c.add_argument("--contrast", required=True)
    c.add_argument("out")
    c.add_argument("--k", type=int, default=3)
    c.add_argument("--draws", type=int, default=20)
    c.add_argument("--seed", default="0")
    m = sub.add_parser("compare")
    m.add_argument("screen")
    m.add_argument("pooled")
    args = ap.parse_args(argv)
    return {"frontier": cmd_frontier, "containment": cmd_containment,
            "compare": cmd_compare}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
