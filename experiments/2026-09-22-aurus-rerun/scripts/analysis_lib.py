"""Statistics and CSV helpers shared by the campaign analysers in this directory.

VENDORING: files under `experiments/<campaign>/scripts/` are verbatim standalone
copies, so an archived campaign reproduces from its own directory without the
git history. An analyser that imports this module is therefore *not* standalone:
vendoring it means copying `analysis_lib.py` into the same directory beside it,
and recording that file's source commit and blob sha in the campaign's
`PROVENANCE.json` `vendored_scripts` alongside the analyser's. Both scripts run
as `python3 scripts/<name>.py`, so `sys.path[0]` is the directory holding them
and a plain `import analysis_lib` resolves against the copy that was vendored
rather than against whatever the checkout happens to hold.

Nothing here encodes a campaign's decision rule. A helper that reads a PLAN.md
section stays in the analyser that owns that plan.
"""

import csv
import math
import statistics


# -- coercion ------------------------------------------------------------------

# `num_or_nan` and `num_or_zero` are two functions rather than one with a
# default because the choice is not a formatting detail: a missing
# `implies_ideal` read as 0.0 is a run that scored zero, and read as nan it is a
# run that was not scored. Callers that go on to sum or `int()` the result need
# the zero; callers that feed a median or a mean need the nan, so that a missing
# value is loud rather than a silent drag towards zero. A default argument puts
# the choice where a caller can miss it.

def num_or_nan(value) -> float:
    """Float, or nan where the value is absent or unparseable."""
    try:
        return float(value)
    except (TypeError, ValueError):
        return float("nan")


def num_or_zero(value) -> float:
    """Float, or 0.0 where the value is absent or unparseable."""
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


# -- loading -------------------------------------------------------------------


def load_rows(path) -> list:
    """Every row of a results CSV as a dict, header-keyed."""
    with open(path) as handle:
        return list(csv.DictReader(handle))


def pair_up(rows, left, right):
    """Pair (spec, seed) across two arms, and say what was dropped.

    Rows carry their arm under the `arm` key; the caller sets it, since which
    columns name an arm is the campaign's business. Returns the complete pairs
    plus counts of the two symmetric exclusions: a pair missing a side entirely,
    and a pair with a timed-out comparison on either side.
    """
    by_key = {}
    for row in rows:
        if row["arm"] not in (left, right):
            continue
        by_key.setdefault((row["spec"], int(row["seed"])), {})[row["arm"]] = row
    complete = {k: v for k, v in by_key.items() if len(v) == 2}
    missing = len(by_key) - len(complete)
    kept = {k: v for k, v in complete.items()
            if all(v[a]["compare_timed_out"] != "1" for a in (left, right))}
    return ([(k, kept[k]) for k in sorted(kept)],
            missing, len(complete) - len(kept))


# -- formatting ----------------------------------------------------------------

# An empty arm is a real state -- a corpus where nothing was repaired, or a
# filter that kept nothing -- and it reaches here as an empty list rather than
# as an error. Printing "n/a" says that; dividing by zero loses the whole
# secondary block, including the figures that are defined.

def pct(numerator, denominator, places=1) -> str:
    if not denominator:
        return "n/a"
    return f"{100 * numerator / denominator:.{places}f}%"


def med(values, places=0) -> str:
    if not values:
        return "n/a"
    return f"{statistics.median(values):.{places}f}"


# -- McNemar -------------------------------------------------------------------


def binom_cdf(k, n, p=0.5) -> float:
    """P(X <= k) for X ~ Binomial(n, p)."""
    return sum(math.comb(n, i) * p ** i * (1 - p) ** (n - i)
               for i in range(0, k + 1))


def mcnemar_exact(b, c) -> float:
    """Two-sided exact McNemar on the discordant counts."""
    n = b + c
    if n == 0:
        return 1.0
    lower = min(b, c)
    return min(1.0, 2.0 * binom_cdf(lower, n))


# -- Wilcoxon signed-rank ------------------------------------------------------


def signed_ranks(differences):
    """``(w_plus, ranks, tie_term)`` over the non-zero differences.

    Zero differences are dropped -- the classic Wilcoxon treatment, not
    Pratt's -- and tied absolute differences share a mid-rank. `tie_term` is
    ``sum(t**3 - t)`` over the tie groups, which the normal-approximation tail
    needs for its variance correction and the exact tail ignores.

    This is the half the two tails agree on. The tails themselves do not: an
    exact enumeration over the observed ranks and a normal approximation answer
    different questions at the sample sizes the two campaigns have, so each
    analyser keeps its own and neither is a default for the other.
    """
    values = [d for d in differences if d != 0]
    n = len(values)
    order = sorted(range(n), key=lambda i: abs(values[i]))
    ranks = [0.0] * n
    tie_term = 0.0
    i = 0
    while i < n:
        j = i
        while j + 1 < n and abs(values[order[j + 1]]) == abs(values[order[i]]):
            j += 1
        mid = (i + j) / 2.0 + 1.0      # average of ranks i+1 .. j+1
        for k in range(i, j + 1):
            ranks[order[k]] = mid
        size = j - i + 1
        tie_term += size ** 3 - size
        i = j + 1
    w_plus = sum(r for r, v in zip(ranks, values) if v > 0)
    return w_plus, ranks, tie_term
