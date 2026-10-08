#!/usr/bin/env python3
"""Pool each subject's frontier members over the 30 seeds and both arms.

    python3 pool_members.py RESULTS_DIR OUT_DIR SUBJECT...

RESULTS_DIR holds the fretish-rerun run directories
(`sweep_O_{directed,uniform}_nsga2-apportion_wkoff_log_<subject>_seed<NN>`),
all 30 seeds of both arms. A member is a file named in a run's
`accumulated/maximal.tsv`. Members whose JSON is identical once its keys are
sorted are one node, as in the archive's pooled.py `prep`. Nodes are numbered
in order of first appearance, walking run directories in sorted name order and
each maximal.tsv in its own order.

Writes OUT_DIR/<subject>.jsonl.gz, one node a line:

    {"node": 0, "arms": ["directed"], "n_d": 2, "n_u": 0,
     "members": [["directed", 0, "gen03_0011.json"], ...], "spec": {...}}

`members` lists every (arm, seed, file) holding the node, so the draw phase can
weight its targets by member as the archive did and the ball-radii reduction
can rebuild each run's frontier. The gzip header carries no name or mtime, so
the same inputs give the same bytes. Also writes OUT_DIR/index.csv with one
row per subject.
"""
import csv
import gzip
import hashlib
import json
import os
import re
import sys

RUN = re.compile(r"^sweep_O_(directed|uniform)_nsga2-apportion_wkoff_log_(.+)_seed(\d+)$")


def pool(results, subject):
    nodes = {}
    for name in sorted(os.listdir(results)):
        m = RUN.match(name)
        if not m or m.group(2) != subject:
            continue
        arm, seed = m.group(1), int(m.group(3))
        acc = os.path.join(results, name, "accumulated")
        tsv = os.path.join(acc, "maximal.tsv")
        if not os.path.exists(tsv):
            continue
        with open(tsv) as handle:
            files = [ln.strip() for ln in handle.read().splitlines()[1:] if ln.strip()]
        for f in files:
            with open(os.path.join(acc, f)) as handle:
                spec = json.load(handle)
            key = json.dumps(spec, sort_keys=True)
            node = nodes.get(key)
            if node is None:
                node = nodes[key] = {"node": len(nodes), "arms": [], "n_d": 0,
                                     "n_u": 0, "members": [], "spec": spec}
            if arm not in node["arms"]:
                node["arms"].append(arm)
                node["arms"].sort()
            node["n_d" if arm == "directed" else "n_u"] += 1
            node["members"].append([arm, seed, f])
    return list(nodes.values())


def main():
    results, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    index = []
    for subject in sys.argv[3:]:
        nodes = pool(results, subject)
        path = os.path.join(out, f"{subject}.jsonl.gz")
        body = "".join(json.dumps(n, sort_keys=True) + "\n" for n in nodes).encode()
        with open(path, "wb") as raw:
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as gz:
                gz.write(body)
        row = {"subject": subject,
               "members_d": sum(n["n_d"] for n in nodes),
               "members_u": sum(n["n_u"] for n in nodes),
               "nodes": len(nodes),
               "nodes_d": sum("directed" in n["arms"] for n in nodes),
               "nodes_u": sum("uniform" in n["arms"] for n in nodes),
               "sha256_jsonl": hashlib.sha256(body).hexdigest()}
        index.append(row)
        print(subject, row["members_d"], row["members_u"], row["nodes"],
              os.path.getsize(path), flush=True)
    with open(os.path.join(out, "index.csv"), "w", newline="") as handle:
        w = csv.DictWriter(handle, list(index[0]))
        w.writeheader()
        w.writerows(index)


if __name__ == "__main__":
    main()
