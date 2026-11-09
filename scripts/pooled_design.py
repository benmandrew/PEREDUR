"""The design of a pooled strength-and-diversity pass, read from TOML.

One file declares which run directories hold each subject's arms and which
pairs of arms are compared. Three scripts read it: pool_members.py (locally,
after `collect`), fingerprint_members.py (on the host, to plan pairs) and
pooled_reduce.py (locally, to reduce the collected verdicts). Each reads the
same file, so the arms pooled, the pairs planned and the contrasts reported
cannot disagree.

    draws = 100                       # sampled draws an arm, default 100

    [[groups]]
    results = "experiments/results-fretish-order"
    run_dir = "sweep_O_{level}_{scheme}_wkoff_log_{subject}_seed{seed:02d}"
    seeds = "0-29"                    # every arm must have every seed
    members = "maximal"               # accumulated/maximal.tsv, or "output"
    subjects = ["fsm", "fsm-timing"]
    sampled = []                      # subjects estimated from draws
    contrasts = [
      { name = "only-climb", a = "nsga2-apportion/only-climb",
        b = "nsga2-apportion/base", family = "only" },
    ]

An arm is `<scheme>/<level>`, the two fields run_dir names. A subject's arms
are the arms its contrasts name, in order of first appearance; arms no contrast
names are not pooled. A contrast's net is positive when arm `a` is the
stronger. `family` groups contrasts for a Holm correction in the summary.

Stdlib only: fingerprint_members.py imports this on the lab hosts from a cron
tick. tomllib is Python 3.11's, which the hosts have; tomli stands in for it
on an older interpreter, as in campaign.py.
"""

import random

try:
    import tomllib
except ModuleNotFoundError:  # Python 3.10
    import tomli as tomllib  # type: ignore[no-redef]

DEFAULT_DRAWS = 100
DESIGN_KEYS = {"draws", "groups"}
GROUP_KEYS = {"results", "run_dir", "seeds", "members", "subjects", "sampled",
              "contrasts", "draws"}
CONTRAST_KEYS = {"name", "a", "b", "family"}
MEMBER_SOURCES = ("maximal", "output")


class DesignError(Exception):
    """A design file that cannot be acted on."""


def parse_seeds(text, where: str) -> list:
    """``"0-29"`` or ``"0-4,15-19"`` to a sorted seed list."""
    if not isinstance(text, str) or not text.strip():
        raise DesignError(f"{where}: seeds must be a range such as \"0-29\"")
    seeds = []
    for part in text.split(","):
        lo, dash, hi = part.strip().partition("-")
        if not lo.isdigit() or (dash and not hi.isdigit()):
            raise DesignError(f"{where}: {part!r} is not a seed or a range")
        seeds.extend(range(int(lo), int(hi if dash else lo) + 1))
    if len(set(seeds)) != len(seeds):
        raise DesignError(f"{where}: {text!r} repeats a seed")
    return sorted(seeds)


def split_arm(arm: str, where: str) -> tuple:
    scheme, sep, level = arm.partition("/")
    if not sep or not scheme or not level or "/" in level:
        raise DesignError(f"{where}: arm {arm!r} is not <scheme>/<level>")
    return scheme, level


def arm_slug(arm: str) -> str:
    """An arm name safe as a file name component."""
    return arm.replace("/", "__")


def run_dir_name(subject: dict, arm: str, seed: int) -> str:
    scheme, level = split_arm(arm, "run_dir")
    return subject["run_dir"].format(scheme=scheme, level=level,
                                     subject=subject["name"], seed=seed)


def string_list(group: dict, key: str, where: str) -> list:
    value = group.get(key, [])
    if not isinstance(value, list) or not all(
            isinstance(v, str) and v for v in value):
        raise DesignError(f"{where}: {key} must be an array of strings")
    if len(set(value)) != len(value):
        raise DesignError(f"{where}: {key} repeats an entry")
    return value


def positive_int(value, where: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise DesignError(f"{where} must be a positive integer")
    return value


def parse_contrasts(group: dict, where: str) -> list:
    raw = group.get("contrasts")
    if not isinstance(raw, list) or not raw:
        raise DesignError(f"{where}: contrasts must be a non-empty array")
    out, names, pairs = [], set(), set()
    for k, c in enumerate(raw):
        at = f"{where}.contrasts[{k}]"
        if not isinstance(c, dict):
            raise DesignError(f"{at} must be a table")
        unknown = sorted(set(c) - CONTRAST_KEYS)
        if unknown:
            raise DesignError(f"{at}: unknown key(s) {', '.join(unknown)}")
        for key in ("name", "a", "b"):
            if not isinstance(c.get(key), str) or not c[key]:
                raise DesignError(f"{at} needs {key} = \"...\"")
        split_arm(c["a"], at)
        split_arm(c["b"], at)
        if c["a"] == c["b"]:
            raise DesignError(f"{at}: a and b are the same arm")
        if c["name"] in names or (c["a"], c["b"]) in pairs:
            raise DesignError(f"{at}: contrast {c['name']!r} is declared twice")
        names.add(c["name"])
        pairs.add((c["a"], c["b"]))
        family = c.get("family", c["name"])
        if not isinstance(family, str) or not family:
            raise DesignError(f"{at}: family must be a non-empty string")
        out.append({"name": c["name"], "a": c["a"], "b": c["b"],
                    "family": family})
    return out


def parse_design(raw: dict, where: str = "design") -> dict:
    """Validate a parsed design. Returns {"draws", "subjects": {name: rec}}."""
    unknown = sorted(set(raw) - DESIGN_KEYS)
    if unknown:
        raise DesignError(f"{where}: unknown key(s) {', '.join(unknown)}")
    draws = positive_int(raw.get("draws", DEFAULT_DRAWS), f"{where}: draws")
    groups = raw.get("groups")
    if not isinstance(groups, list) or not groups:
        raise DesignError(f"{where}: needs at least one [[groups]] table")
    subjects: dict = {}
    for g, group in enumerate(groups):
        at = f"{where}: groups[{g}]"
        if not isinstance(group, dict):
            raise DesignError(f"{at} must be a table")
        unknown = sorted(set(group) - GROUP_KEYS)
        if unknown:
            raise DesignError(f"{at}: unknown key(s) {', '.join(unknown)}")
        for key in ("results", "run_dir"):
            if not isinstance(group.get(key), str) or not group[key]:
                raise DesignError(f"{at} needs {key} = \"...\"")
        try:
            group["run_dir"].format(scheme="s", level="l", subject="x", seed=0)
        except (KeyError, IndexError, ValueError):
            raise DesignError(f"{at}: run_dir may use only {{scheme}}, "
                              f"{{level}}, {{subject}} and {{seed}}") from None
        seeds = parse_seeds(group.get("seeds"), f"{at}.seeds")
        source = group.get("members", "maximal")
        if source not in MEMBER_SOURCES:
            raise DesignError(f"{at}: members must be one of "
                              f"{', '.join(MEMBER_SOURCES)}")
        names = string_list(group, "subjects", at)
        if not names:
            raise DesignError(f"{at}: subjects must name at least one subject")
        sampled = string_list(group, "sampled", at)
        stray = sorted(set(sampled) - set(names))
        if stray:
            raise DesignError(f"{at}: sampled names {', '.join(stray)}, "
                              f"which subjects lacks")
        group_draws = positive_int(group.get("draws", draws), f"{at}.draws")
        contrasts = parse_contrasts(group, at)
        arms: list = []
        for c in contrasts:
            for arm in (c["a"], c["b"]):
                if arm not in arms:
                    arms.append(arm)
        for name in names:
            if name in subjects:
                raise DesignError(f"{at}: subject {name!r} is in two groups")
            subjects[name] = {
                "name": name, "results": group["results"],
                "run_dir": group["run_dir"], "seeds": seeds,
                "members": source, "sampled": name in sampled,
                "draws": group_draws, "contrasts": contrasts, "arms": arms}
    return {"draws": draws, "subjects": subjects}


def load_design(path: str) -> dict:
    with open(path, "rb") as handle:
        try:
            raw = tomllib.load(handle)
        except tomllib.TOMLDecodeError as exc:
            raise DesignError(f"{path}: {exc}") from None
    return parse_design(raw, path)


def partners(subject: dict, arm: str) -> list:
    """The arms a contrast pairs with `arm`, in declaration order."""
    out = []
    for c in subject["contrasts"]:
        other = c["b"] if c["a"] == arm else c["a"] if c["b"] == arm else None
        if other is not None and other not in out:
            out.append(other)
    return out


def arm_masks(subject: dict, nodes: list) -> tuple:
    """Per node, a bit per design arm holding it, and a bit per arm some
    contrast pairs with an arm holding it. A node pair needs a verdict when
    the two share an arm (to build that arm's frontier) or when one holds an
    arm the other's contrasts name (to compare the two frontiers)."""
    bit = {arm: 1 << k for k, arm in enumerate(subject["arms"])}
    partner_bits = {arm: sum(bit[p] for p in partners(subject, arm))
                    for arm in subject["arms"]}
    held, paired = [], []
    for n in nodes:
        h = p = 0
        for arm in n["arms"]:
            if arm in bit:
                h |= bit[arm]
                p |= partner_bits[arm]
        held.append(h)
        paired.append(p)
    return held, paired


def draw_order(subject: str, arm: str, seed: int, node_ids: list) -> list:
    """A seeded permutation of an arm's nodes, so any prefix is a simple
    random sample. String seeding hashes the string with SHA-512, which is
    stable across Python versions and processes."""
    order = sorted(node_ids)
    random.Random(f"{subject}|{arm}|{seed}").shuffle(order)
    return order


def subset(fx: int, fy: int) -> bool:
    """x => y survives the words: x accepts no word y rejects."""
    return fx | fy == fy


def exhaustive_plan(subject: dict, nodes: list, bits: list):
    """(x, y, dirs) for every node pair x < y the design needs, with a
    direction no word refutes. `bits` is each node's fingerprint as an int,
    indexed as `nodes`."""
    held, paired = arm_masks(subject, nodes)
    n = len(nodes)
    for x in range(n):
        fx, hx, px = bits[x], held[x], paired[x]
        for y in range(x + 1, n):
            hy = held[y]
            if not (hx & hy or px & hy):
                continue
            fy = bits[y]
            union = fx | fy
            fwd, rev = union == fy, union == fx
            if fwd or rev:
                yield x, y, ("both" if fwd and rev else "fwd" if fwd else "rev")


def sampled_plan(subject: dict, nodes: list, bits: list, orders: dict):
    """(x, y, dirs) for the sampled estimator, x < y, each pair once.

    For each arm, the first `draws` nodes of its order are drawn. A drawn node
    d is paired with every other node y of its own arm and of each partner
    arm whose fingerprint is a subset of d's, since only such a y can imply
    d: that decides d's frontier membership and class size within its arm,
    and whether some partner repair strictly implies it.
    """
    index = {n["node"]: k for k, n in enumerate(nodes)}
    pool = {arm: [k for k, n in enumerate(nodes) if arm in n["arms"]]
            for arm in subject["arms"]}
    seen: set = set()
    for arm in subject["arms"]:
        cands = sorted(set(pool[arm]).union(
            *(pool[p] for p in partners(subject, arm))))
        for node in orders[arm][:subject["draws"]]:
            d = index[node]
            fd = bits[d]
            for y in cands:
                if y == d or not subset(bits[y], fd):
                    continue
                x, z = (d, y) if d < y else (y, d)
                if (x, z) in seen:
                    continue
                seen.add((x, z))
                fx, fz = bits[x], bits[z]
                fwd, rev = subset(fx, fz), subset(fz, fx)
                yield x, z, ("both" if fwd and rev else "fwd" if fwd else "rev")
