"""List the members of the pooled RQ3 pass: every per-run frontier member of
PEREDUR (mrs-nsga2-apportion) and of AuRUS (well-separation screened), for
each family where both sides have runs. The pooled frontier of a side is the
frontier of the union of its per-run frontiers.

usage: members.py <out.csv>
Columns: side (P|A), spec, seed, file, origin (the host holding the run:
av2 for seeds 0-14, av3 for 15-29), rel (path under ~ on that host).
"""
import csv, glob, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "../../ball-radii"))
import plan
import tlsf_prc as T


def main():
    runs = {}
    for d in plan.DIRS:
        for p in glob.glob(f"{d}/*.members.tsv"):
            m = T.NAME.match(os.path.basename(p))
            if not m:
                continue
            label = "aurus" if m.group(1) else f"{m.group(2)}-{m.group(3)}"
            if label in ("aurus", "mrs-nsga2-apportion"):
                runs[(label, m.group(1) or m.group(4), int(m.group(5)))] = p
    specs = {k[1] for k in runs if k[0] == "aurus"} & {k[1] for k in runs if k[0] != "aurus"}
    w = csv.writer(open(sys.argv[1], "w", newline=""))
    w.writerow(["side", "spec", "seed", "file", "origin", "rel"])
    for (label, spec, seed), p in sorted(runs.items()):
        if spec not in specs:
            continue
        names = T.frontier(p)
        fp = T.load_prints(p.replace(".members.tsv", ".fingerprints.tsv"), names)
        for f in names:
            if f in fp:
                _, rel = plan.host_path(label, spec, seed, f)
                w.writerow(["A" if label == "aurus" else "P", spec, seed, f, "av3" if seed >= 15 else "av2",
                            rel])


if __name__ == "__main__":
    main()
