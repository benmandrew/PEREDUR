#!/usr/bin/env python3
"""Check that the one-way-wall runs repeat sweeps F and T's repairs.

Each run of this campaign has a twin: its full is sweep F's no-gate, and each
no-<f> is sweep T's no-<f>, at the same (spec, seed). The twins must agree
byte for byte in every repair_*.json and in the accumulated set (the files
under accumulated/, less the elapsed-time column of its index.tsv), so that only wall time differs and the pooled passes,
which read the twins, stand. Prints one line per pair that differs, and a
summary; exits 1 if any pair differs or lacks a twin.

Usage: check_repairs.py [EXPERIMENTS_DIR]
"""

import sys
from pathlib import Path

# This campaign's results dir -> the twin dirs it is checked against.
TREES = {"results-one-way-wall": ("results-final-engine", "results-one-way"),
         "results-one-way-wall-large": ("results-final-engine-large",
                                        "results-one-way-large")}


def files(run):
    """Relative path -> bytes of every file that holds a repair."""
    out = {p.name: p.read_bytes() for p in run.glob("repair_*.json")}
    acc = run / "accumulated"
    if acc.is_dir():
        out.update({f"accumulated/{p.relative_to(acc)}": p.read_bytes()
                    for p in acc.rglob("*") if p.is_file() and p.name != "index.tsv"})
        index = acc / "index.tsv"
        if index.exists():
            # file, generation, elapsed_s: the elapsed time is a timing.
            out["accumulated/index.tsv"] = b"\n".join(
                b"\t".join(line.split(b"\t")[:2])
                for line in index.read_bytes().splitlines())
    return out


def twin(run, f_dir, t_dir):
    # sweep_T_<level>_<rest>
    _, _, level, rest = run.name.split("_", 3)
    if level == "full":
        return f_dir / f"sweep_F_no-gate_{rest}"
    return t_dir / run.name


def main(argv):
    experiments = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parents[2]
    pairs = differ = missing = 0
    for tree, (f_tree, t_tree) in TREES.items():
        for run in sorted((experiments / tree).glob("sweep_T_*")):
            other = twin(run, experiments / f_tree, experiments / t_tree)
            if not other.is_dir():
                missing += 1
                print(f"no twin: {run.name}")
                continue
            pairs += 1
            a, b = files(run), files(other)
            bad = sorted(k for k in a.keys() | b.keys() if a.get(k) != b.get(k))
            if bad:
                differ += 1
                print(f"differs: {run.name} vs {other.name}: {len(bad)} file(s), "
                      f"e.g. {bad[0]}")
    print(f"{pairs} pairs, {differ} differ, {missing} without a twin")
    return 1 if differ or missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
