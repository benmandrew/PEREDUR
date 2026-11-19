#!/usr/bin/env python3
"""Draw a subject's word bank once, for every screen of that subject.

    python3 scripts/diversity/wordbank.py SUBJECT OUT_DIR \\
        (--aimed-from WORDS | --members POOL.jsonl.gz) \\
        [--words 32768] [--aimed-share 0.75] [--seed 3] [--max-stem 8] \\
        [--max-loop 8] [--workers 4]

A bank is `OUT_DIR/<subject>/words`, one lasso word a line in the format
`fpdraw eval` reads, and `words.meta.json` beside it. The first
`aimed-share * words` lines are aimed at a reference pool, and the rest are
uniform random lassos.

Aimed words come from one of two places. `--aimed-from` names the `words`
file of a fingerprint phase (scripts/fingerprint_members.py), whose draws
already aim at a pool: black is asked for a model of one member, drawn
uniformly over members, so any prefix of the file is a uniform subsample, and
its first lines are taken. `--members` names a pool (scripts/pool_members.py)
and draws the aimed words here, through `fpdraw ltl` and scripts/draw_black.py,
as a fingerprint phase does; black must be at $BLACK.

A uniform word has a stem of 0 to --max-stem positions and a loop of 1 to
--max-loop, each length uniform, and every signal a fair coin at every
position, from random.Random(f"{seed}|{subject}|{i}"). The signals are those
the aimed words name. The defaults are the paper's bound (a stem and a loop
of at most 8 steps).
"""

import argparse
import gzip
import hashlib
import json
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import fingerprint_members as F  # noqa: E402


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def signals_of(lines: list) -> list:
    out = set()
    for line in lines:
        for item in line.split()[2:]:
            out.add(item.split("=", 1)[0])
    return sorted(out)


def uniform_word(signals: list, rng: random.Random, max_stem: int,
                 max_loop: int) -> str:
    stem = rng.randint(0, max_stem)
    loop = rng.randint(1, max_loop)
    n = stem + loop
    masks = " ".join(f"{s}={rng.getrandbits(n):x}" for s in signals)
    return f"{stem} {n} {masks}"


def draw_aimed(subject: str, members: str, n: int, seed: int, workers: int,
               fpdraw: str, work: str) -> tuple:
    """Aimed words drawn here, one draw target a member, as
    fingerprint_members.do_subject draws them. Returns the kept words and the
    LTL file's signals."""
    with gzip.open(members, "rt") as handle:
        nodes = [json.loads(line) for line in handle if line.strip()]
    paths = F.materialise(nodes, work, subject)
    draw_list = os.path.join(work, subject, "members.list")
    F.write_whole(draw_list, "".join(paths[k] + "\n" for k, node in
                                     enumerate(nodes)
                                     for _ in node["members"]))
    ltl = os.path.join(work, subject, "members.ltl")
    spec = str(F.REPO_ROOT / "examples" / subject / "spec.json")
    F.run_to(ltl, [fpdraw, "ltl", spec, draw_list], dict(os.environ),
             ltl + ".err")
    out = os.path.join(work, subject, "aimed.words")
    subprocess.run([sys.executable, str(F.DRAW_SCRIPT), ltl, str(n),
                    str(seed), out, str(workers)], check=True)
    with open(out + ".meta") as handle:
        errors = sum(ln.split("\t")[3] == "ERROR" for ln in handle)
    if errors:
        sys.exit(f"wordbank: {subject}: black answered ERROR on {errors} "
                 f"draws")
    with open(ltl) as handle:
        signals = handle.readline().split("\t")[1].split()
    with open(out) as handle:
        return handle.read().splitlines(), signals, sha256(out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("subject")
    ap.add_argument("out")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--aimed-from", help="a fingerprint phase's words file")
    src.add_argument("--members", help="a pool to draw aimed words over")
    ap.add_argument("--words", type=int, default=32768)
    ap.add_argument("--aimed-share", type=float, default=0.75)
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--max-stem", type=int, default=8)
    ap.add_argument("--max-loop", type=int, default=8)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--fpdraw", default=str(F.REPO_ROOT / "build" / "fpdraw"))
    args = ap.parse_args(argv)

    n_aimed = round(args.words * args.aimed_share)
    sdir = os.path.join(args.out, args.subject)
    os.makedirs(sdir, exist_ok=True)
    if args.aimed_from:
        with open(args.aimed_from) as handle:
            source = handle.read().splitlines()
        aimed = source[:n_aimed]
        signals = signals_of(aimed)
        source_rec = {"kind": "fingerprint-phase words",
                      "path": os.path.abspath(args.aimed_from),
                      "sha256": sha256(args.aimed_from),
                      "lines": len(source)}
    else:
        with tempfile.TemporaryDirectory() as work:
            # Draw a few spare targets: a draw black cannot answer is dropped.
            drawn, signals, digest = draw_aimed(
                args.subject, args.members, n_aimed + n_aimed // 16,
                args.seed, args.workers, args.fpdraw, work)
        aimed = drawn[:n_aimed]
        source_rec = {"kind": "drawn here", "members": os.path.abspath(args.members),
                      "members_sha256": sha256(args.members),
                      "draw_seed": args.seed, "drawn_sha256": digest,
                      "lines": len(drawn)}
    if len(aimed) < n_aimed:
        print(f"wordbank: {args.subject}: only {len(aimed)} aimed words of "
              f"{n_aimed}", file=sys.stderr)
    n_uniform = args.words - len(aimed)
    uniform = [uniform_word(signals, random.Random(f"{args.seed}|{args.subject}|{i}"),
                            args.max_stem, args.max_loop)
               for i in range(n_uniform)]
    words = os.path.join(sdir, "words")
    F.write_whole(words, "".join(w + "\n" for w in aimed + uniform))
    meta = {"subject": args.subject, "words": len(aimed) + n_uniform,
            "aimed": len(aimed), "uniform": n_uniform,
            "aimed_lines": [0, len(aimed)],
            "uniform_lines": [len(aimed), len(aimed) + n_uniform],
            "aimed_share_requested": args.aimed_share,
            "aimed_source": source_rec,
            "uniform_draw": {"seed": args.seed, "rng": "random.Random(f'{seed}|{subject}|{i}')",
                        "max_stem": args.max_stem, "max_loop": args.max_loop},
            "signals": signals, "sha256": sha256(words)}
    F.write_whole(os.path.join(sdir, "words.meta.json"),
                  json.dumps(meta, indent=2) + "\n")
    print(f"{args.subject}: {len(aimed)} aimed + {n_uniform} uniform words")
    return 0


if __name__ == "__main__":
    sys.exit(main())
