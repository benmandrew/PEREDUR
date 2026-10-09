#!/usr/bin/env python3
"""Score pooled RQ3: PEREDUR (A, mrs-nsga2-apportion) against AuRUS (B).

    python3 score_rq3.py WORK AURUS_FRONTIER RESULTS.csv... OUTDIR

The archived score_pooled.py's measure, read from frontier against frontier:
PEREDUR's pooled frontier is the strength phase's walk
(WORK/strength/<spec>/result.json), AuRUS's is the archived one
(AURUS_FRONTIER, aurus_frontier.py), and RESULTS are compare_pairs.py's
output for WORK/rq3/pairs-<host>.csv, whose id is rq3|spec|peredur|aurus
and whose relation is the PEREDUR class's to the AuRUS one. A pair the
fingerprints refuted both ways is absent and reads as incomparable.

By transitivity a PEREDUR frontier member is strictly implied by some AuRUS
repair exactly when it is by some member of AuRUS's frontier, and an AuRUS
frontier member is strictly implied by some PEREDUR repair exactly when it is
by some class of PEREDUR's pool, which the pairs cover. sub(A by B) is the
share of A's frontier so implied, net = sub(B by A) - sub(A by B), and an
undecided pair counts as a hit for both under the imp reading. The test is
the exact Wilcoxon signed-rank over families. Writes OUTDIR/families-rq3.csv
in score_pooled.py's columns and OUTDIR/report-rq3.txt.
"""
import collections
import csv
import json
import os
import statistics
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "scripts"))
import pooled_frontier as pf  # noqa: E402

SIDE = "mrs-nsga2-apportion"


def main(argv) -> int:
    work, aurus_csv, *results, out = argv
    front_b = collections.defaultdict(list)
    with open(aurus_csv, newline="") as fh:
        for r in csv.DictReader(fh):
            front_b[r["spec"]].append(r["md5"])
    rel = collections.defaultdict(dict)
    for path in results:
        with open(path, newline="") as fh:
            for r in csv.DictReader(fh):
                _, spec, a, b = r["id"].split("|")
                rel[spec][(a, b)] = r["relation"]
    with open(os.path.join(work, "rq3", "plan.json")) as fh:
        planned = json.load(fh)["specs"]
    rows, report = [], []
    for spec in sorted(planned):
        rpath = os.path.join(work, "strength", spec, "result.json")
        if not os.path.exists(rpath):
            report.append(f"{spec}: no strength result, skipped")
            continue
        with open(rpath) as fh:
            fa = json.load(fh)["frontiers"][SIDE]["members"]
        fb = front_b[spec]
        r = rel[spec]
        if len(r) != planned[spec]["pairs"]:
            report.append(f"{spec}: {len(r)} of {planned[spec]['pairs']} pairs decided, skipped")
            continue
        if not fa or not fb:
            report.append(f"{spec}: empty frontier (A {len(fa)}, B {len(fb)}), skipped")
            continue
        sub = {}
        for k in ("non", "imp"):
            und = ("undecided", "error") if k == "imp" else ()
            # Every planned b is on AuRUS's frontier, so these are frontier hits.
            above_a = {a for (a, _), v in r.items() if v in ("weaker", *und)}
            above_b = {b for (_, b), v in r.items() if v in ("stronger", *und)}
            hit_a = [a in above_a for a in fa]
            hit_b = [b in above_b for b in fb]
            sub[("A", k)] = sum(hit_a) / len(fa)
            sub[("B", k)] = sum(hit_b) / len(fb)
        c = collections.Counter(r.values())
        rows.append([spec, len(fa), len(fb),
                     round(sub[("B", "non")] - sub[("A", "non")], 4),
                     round(sub[("B", "imp")] - sub[("A", "imp")], 4),
                     round(sub[("A", "non")], 4), round(sub[("B", "non")], 4),
                     sum(c.values()), c["undecided"], c["error"]])
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "families-rq3.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["spec", "front_A", "front_B", "net_non", "net_imp", "subA_non", "subB_non",
                    "pairs", "undecided", "errors"])
        w.writerows(rows)
    report.append(f"\n== pooled rq3: A = {SIDE}, B = aurus (archived frontier) ({len(rows)} families)")
    for col, k in ((3, "non"), (4, "imp")):
        net = [x[col] for x in rows]
        if not net:
            continue
        nz = [x for x in net if x != 0]
        report.append(f"  [{k}] A stronger on {sum(x > 0 for x in net)}, B on {sum(x < 0 for x in net)}, "
                      f"tie {sum(x == 0 for x in net)}; median net {statistics.median(net):+.4f}, "
                      f"mean {statistics.mean(net):+.4f}; exact Wilcoxon p {pf.wilcoxon_exact(nz):.4g}")
    if rows:
        report.append(f"  mean sub(A by B) {statistics.mean(x[5] for x in rows):.4f}, "
                      f"sub(B by A) {statistics.mean(x[6] for x in rows):.4f}")
        n, u, e = (sum(x[i] for x in rows) for i in (7, 8, 9))
        report.append(f"  pairs run {n}, undecided {u} ({u / max(n, 1):.4%}), errors {e}")
    text = "\n".join(report).lstrip("\n") + "\n"
    with open(os.path.join(out, "report-rq3.txt"), "w") as fh:
        fh.write(text)
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
