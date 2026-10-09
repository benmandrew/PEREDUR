"""perm_null.py over large fingerprints: the same pass with tlsf_prc.dist
replaced by a chunked matrix product, which the byte-table popcount cannot
match at 2^20 words (128 KiB a fingerprint). Same usage and env as perm_null,
plus WORDS: read only the first WORDS words of each fingerprint. The sampler
draws words in sequence from one seed, so these are exactly the words a
WORDS-word draw would give; the hex puts word 0 in its last digit, so they are
the last WORDS/4 digits.
"""
import os
import numpy as np
import tlsf_prc as T
import perm_null as P

CHUNK = 1 << 13  # bytes, 65536 words


def dist(a, b):
    na = np.zeros(len(a)); nb = np.zeros(len(b)); inter = np.zeros((len(a), len(b)))
    for s in range(0, a.shape[1], CHUNK):
        x = np.unpackbits(a[:, s:s + CHUNK], axis=1).astype(np.float32)
        y = np.unpackbits(b[:, s:s + CHUNK], axis=1).astype(np.float32)
        na += x.sum(1); nb += y.sum(1); inter += x @ y.T
    u = na[:, None] + nb[None, :] - inter
    return np.where(u > 0, (u - inter) / np.maximum(u, 1), 0.0)


def load_prints(path, names):
    want, out, keep = set(names), {}, int(os.environ.get("WORDS", "0")) // 4
    with open(path) as fh:
        next(fh, None)
        for line in fh:
            f, _, h = line.rstrip("\n").partition("\t")
            if f in want and h:
                out[f] = np.frombuffer(bytes.fromhex(h[-keep:] if keep else h), dtype=np.uint8)
    return out


T.dist = dist
T.load_prints = load_prints

if __name__ == "__main__":
    P.main()
