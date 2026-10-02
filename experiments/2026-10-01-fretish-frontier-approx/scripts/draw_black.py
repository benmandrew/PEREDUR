"""Draw lasso words that non-vacuously satisfy random frontier repairs.

usage: draw_black.py <repairs.ltl> <n_words> <seed> <out.words> [workers]

<repairs.ltl> is `fpdraw ltl` output. Word i picks repair r uniformly with
Random(seed, i), asks black for a model of A(r) & G(r) & hints, where the hints
are 1-8 random literals at random offsets 0-15, halving the hints on UNSAT
until none are left. Atoms black leaves undefined or never mentions are filled
with fair coins from the same Random. A model longer than 64 states is dropped.

Writes one word a line, "<loop> <n> <sig>=<hexmask>...", the format
`fpdraw eval` reads, and <out.words>.meta with the target and hint count.
"""
import json, os, random, subprocess, sys, threading
from concurrent.futures import ThreadPoolExecutor

BLACK = os.environ.get("BLACK", os.path.expanduser(
    "~/projects/counter/build-release/third_party/black/black"))
TIMEOUT = int(os.environ.get("BLACK_TIMEOUT", "30"))
# BOUNDARY=1 draws boundary words instead: A(r), every guarantee of r but one,
# and the negation of that one, so the word sits just outside r. It needs the
# per-guarantee fourth column of `fpdraw ltl`. A guarantee the others imply
# gives UNSAT and the word falls back to the ordinary target.
BOUNDARY = os.environ.get("BOUNDARY") == "1"


def solve(formula):
    try:
        r = subprocess.run([BLACK, "solve", "-m", "-o", "json", "-t", str(TIMEOUT), "-"],
                           input=formula, capture_output=True, text=True,
                           timeout=TIMEOUT + 10)
    except subprocess.TimeoutExpired:
        return "TIMEOUT", None
    try:
        out = json.loads(r.stdout)
    except json.JSONDecodeError:
        return "ERROR", None
    return out.get("result", "ERROR"), out.get("model")


def main():
    ltl_path, n_words, seed, out_path = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    workers = int(sys.argv[5]) if len(sys.argv) > 5 else 4
    lines = open(ltl_path).read().splitlines()
    signals = lines[0].split("\t")[1].split()
    rows = [l.split("\t") for l in lines[1:]]
    words = [None] * n_words
    meta = [None] * n_words

    def one(i):
        rng = random.Random(seed * 1_000_003 + i)
        target = rng.randrange(len(rows))
        _, assumptions, guarantees = rows[target][:3]
        dropped = None
        if BOUNDARY:
            parts = rows[target][3].split("\x1f")
            dropped = rng.randrange(len(parts))
            rest = [g for k, g in enumerate(parts) if k != dropped]
            guarantees = " & ".join(f"({g})" for g in rest) or "true"
            guarantees += f" & !({parts[dropped]})"
        hints = []
        for _ in range(rng.randint(1, 8)):
            lit = rng.choice(signals)
            if rng.random() < 0.5:
                lit = f"!{lit}"
            k = rng.randrange(16)
            hints.append("X(" * k + lit + ")" * k)
        while True:
            formula = f"({assumptions}) & ({guarantees})"
            if hints:
                formula += " & " + " & ".join(f"({h})" for h in hints)
            result, model = solve(formula)
            if result == "SAT" or result != "UNSAT":
                break
            if not hints:
                if dropped is None:
                    break
                guarantees = rows[target][2]; dropped = None
                continue
            hints = hints[: len(hints) // 2]
        # black sometimes reports loop == size, which is no lasso at all; the
        # evaluator would read a loop bit past the word's end.
        if (result != "SAT" or model is None or model["size"] > 64
                or not 0 <= model["loop"] < model["size"]):
            meta[i] = (target, len(hints), result, dropped)
            return
        n, loop = model["size"], model["loop"]
        masks = {}
        for s in signals:
            mask = 0
            for t, state in enumerate(model["states"]):
                v = state.get(s, "undef")
                bit = v == "true" if v != "undef" else rng.random() < 0.5
                mask |= int(bit) << t
            masks[s] = mask
        words[i] = f"{loop} {n} " + " ".join(f"{s}={m:x}" for s, m in masks.items())
        meta[i] = (target, len(hints), result, dropped)

    with ThreadPoolExecutor(workers) as ex:
        list(ex.map(one, range(n_words)))
    with open(out_path, "w") as f:
        f.writelines(w + "\n" for w in words if w)
    with open(out_path + ".meta", "w") as f:
        for i, (m, w) in enumerate(zip(meta, words)):
            f.write(f"{i}\t{rows[m[0]][0]}\t{m[1]}\t{m[2]}\t{int(w is not None)}\t{m[3]}\n")
    kept = sum(w is not None for w in words)
    results = {}
    for m in meta:
        results[m[2]] = results.get(m[2], 0) + 1
    boundary = sum(m[3] is not None and w is not None for m, w in zip(meta, words))
    print(f"kept {kept} of {n_words} ({boundary} boundary); results {results}", file=sys.stderr)


if __name__ == "__main__":
    main()
