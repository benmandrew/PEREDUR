#!/usr/bin/env python3
"""Generate TOML config files for all experiment sweeps.

Writes one file per (selection scheme, sweep, level) to
experiments/configs/<scheme>/[wkoff/]log/. Safe to re-run — existing files are
overwritten.

The scheme lives in the directory rather than the filename because
run_experiments.py parses (sweep, level) out of the filename, and it reads the
scheme back off the parent directory. Every sweep is generated once per scheme,
which makes selection_scheme a factor of the design rather than a constant.

Usage:
    python scripts/gen_configs.py                       # the standard grid
    python scripts/gen_configs.py --schemes nsga2-apportion  # one scheme only
    python scripts/gen_configs.py --sweeps A C          # specific sweeps
    python scripts/gen_configs.py --sweeps C --levels default,status-only
                                                        # specific levels only
    python scripts/gen_configs.py --generations 40 --population-size 1000 \\
        --out-dir experiments/configs-cj-large          # a larger operating point
    python scripts/gen_configs.py --weakening off       # nest under <scheme>/wkoff/
    python scripts/gen_configs.py --tlsf                # the TLSF campaign grid
"""

import argparse
from pathlib import Path

CONFIGS_DIR = Path(__file__).parent.parent / "experiments" / "configs"

# Selection schemes the whole grid is duplicated across. Every generated config
# pins the scheme explicitly rather than relying on the config.hpp default.
# nsga2-truncate is not offered: the binary is retiring it, so a new grid runs
# nsga2-apportion, and weighted stays as the comparison arm.
#
# Each string here is also a config *directory* name: scheme_of() in
# run_experiments.py reads it back into the `selection` column of every results
# CSV, and that column joins KEY_FIELDS in merge_experiments.py -- the natural
# key for resume and merge. config_io.cpp rejects the old spellings "nsga2" and
# "nsga2-replicate" of the two NSGA-II schemes.
SCHEMES: list[str] = ["nsga2-apportion", "weighted"]

# The weakening directory names a state and sets none: the binary never weakens
# and no config states a key. The directory stays because `weakening` is one of
# merge_experiments.KEY_FIELDS: weakening_of() in run_experiments.py reads it
# back into that column and attributes a flat config to LEGACY_WEAKENING
# ("wkon"). A new campaign therefore passes --weakening off.
WEAKENINGS: dict[str, list[str]] = {
    "off": ["wkoff"],
}

# Every config runs the logarithmic metric, the only one the binary has, and
# nests under a log/ directory. The TOML no longer names it, since the binary
# warns about the retired key. The directory is not decoration: `metric` is
# one of merge_experiments.KEY_FIELDS, metric_of() in run_experiments.py reads
# it back off the path, and a flat config is attributed to LEGACY_METRIC
# ("direct"), which would mislabel every row. METRIC_DIR is the short CSV label.
METRIC_DIR = "log"

# No repair-mode directory is written: monolithic repair is the only mode left,
# and repair_mode_of() reads a config without one as LEGACY_REPAIR ("mono"),
# which is correct.

# Mirrors the built-in defaults from include/config.hpp. Every entry tracks the
# binary except those enumerated below, and check_config_schema.py enforces
# that: it walks this table against example-config.toml, which is generated
# with the binary's defaults, and fails on any key that is neither mapped to a
# TOML path nor listed there as exempt. A
# key whose C++ default moves is therefore corrected here rather than noticed
# later — which matters most for the VINTAGE_KEYS, since --pin-vintage writes
# *these* values into the generated configs.
#
# The deliberate divergences, and why each one does not follow the binary:
#
#   * weight_syntactic / weight_semantic / weight_status — pinned 0.33 each;
#     config.hpp reads 0.1 / 0.2 / 0.7, AuRUS's weights. These three are emitted
#     *unconditionally* into every config, so every archived config states them
#     and is self-describing about them. Following the binary would change the
#     emitted values and break comparability with every past grid, and would buy
#     nothing, since no archived config was ever silent about them.
#   * ltlsynt_timeout_ms / ltl2tgba_timeout_ms / max_scoring_failure_rate — 0
#     and 0.0 are "do not emit" sentinels read by make_toml, not mirrors of
#     config.hpp's 10000 ms, 60000 ms and 0.15. A campaign that needs a cap sets
#     one; everything else inherits the binary's.
#   * dashboard — False is the same kind of sentinel (emitted only when true).
#     It coincides with config.hpp today, and must stay False if that moves.
DEFAULTS: dict = {
    "generations": 10,
    "population_size": 200,
    "selection_scheme": "nsga2-apportion",
    "crossover_rate": 0.1,
    "mutation_rate": 1.0,
    # Fraction of the population carried over verbatim, mirroring config.hpp.
    # Emitted into [genetic] only when a sweep overrides it (see make_toml), so
    # every existing grid stays byte-identical; the elitism sweep (R) crosses it
    # because carrying elites over re-injects exact duplicates, which is the
    # mechanism the replicate scheme exists to undo.
    "elitism_rate": 0.1,
    "weight_syntactic": 0.33,
    "weight_semantic": 0.33,
    "weight_status": 0.33,
    "p_trigger": 0.5,
    "p_response": 0.5,
    "p_timing": 0.15,
    # FRETISH only: probability a mutation moves the condition type, and the
    # scope, along its implication order. Emitted into [mutation] only when a
    # sweep overrides them (see make_toml), so the standard grids stay
    # byte-identical; both are in VINTAGE_KEYS.
    "p_condition_type": 0.15,
    "p_scope": 0.15,
    # FRETISH only: probability a mutation rewrites the stop of an until or
    # before timing. Emitted into [mutation] only when a sweep overrides it.
    "p_stop": 0.15,
    # Probability a mutation appends a new environment (fairness) assumption
    # rather than rewriting an existing requirement/section. Emitted into
    # [mutation] only when a sweep overrides it (see make_toml), so the standard
    # grids stay byte-identical.
    "p_add_assumption": 0.05,
    # The mirror action: delete one guarantee. Emitted into [mutation] only when
    # a sweep overrides it (see make_toml), like p_add_assumption. At 0 it costs
    # no draw, so a run reproduces one from before the operator existed.
    "p_remove_guarantee": 0.05,
    "default_bound": 20,
    "run_implication": True,
    # Keys in VINTAGE_KEYS must follow the binary rather than be pinned, because
    # VINTAGE_KEYS sources its written-out values from this table -- a stale
    # entry here is a wrong value written into every config generated with
    # --pin-vintage, by the mechanism that exists to stop a moved default going
    # unrecorded. check_config_schema.py enforces the agreement for every key
    # of this table that is not deliberately exempt.
    "black_timeout_ms": 1000,
    # Mirrors include/config.hpp. Emitted into [fitness] only when a sweep
    # overrides it (see make_toml), so every existing grid stays
    # byte-identical; it is in VINTAGE_KEYS.
    "status_grading": "mrs",
    # Mirrors include/config.hpp. Emitted into [fitness] only when a sweep
    # overrides it (see make_toml), so every existing grid stays
    # byte-identical.
    "mrs_admission_order": "degree",
    # TLSF-only [tlsf.mutation] split (see config.hpp). Emitted only when a sweep
    # overrides one of them (see make_toml), so the FRETISH and A/B TLSF grids
    # stay byte-identical to the pre-factor output. They divide mutation's
    # budget between the environment (assumption) and guarantee sides.
    "p_assumption": 0.3,
    "p_temporal": 0.2,
    # The monotone arm and the clone-an-assumption branch of p_add_assumption
    # (see config.hpp). Each is emitted only when a sweep overrides it (see
    # make_toml), so existing grids stay byte-identical. p_monotone sits under
    # [mutation], is read by both paths, and is in VINTAGE_KEYS.
    "p_monotone": 0.25,
    "p_clone_assumption": 0.25,
    # Per-call ltlsynt timeout in ms; 0 = no timeout, matching config.hpp.
    # Emitted only when positive, so the standard grids stay byte-identical. The
    # heavy TLSF specs set it to cut ltlsynt's multi-minute realizability tail.
    "ltlsynt_timeout_ms": 0,
    # Per-call ltl2tgba (model-counting) timeout in ms; 0 = no timeout, matching
    # config.hpp. Emitted only when positive. The counting-path -D determinization
    # blows up super-exponentially on some deep formulae; the TLSF campaign sets
    # it to cut the multi-GB, hours-long counting tail.
    "ltl2tgba_timeout_ms": 0,
    # 0 = fall back to config.hpp's 0.05. Emitted only when positive, so the
    # standard grids stay byte-identical. The TLSF one-hot encodings mutate into
    # tautological guarantees that SPOT 2.15.1's ltl2tgba rejects (exit 2); the
    # circuit breaker drops them, but at the smallest population the default 0.05
    # tolerance floors to 1 and aborts the run, so the campaign raises it.
    "max_scoring_failure_rate": 0.0,
    # Wall-clock deadline for the whole run in seconds; 0 = no deadline,
    # matching config.hpp. Emitted into [genetic] only when positive (see
    # make_toml), so the standard grids stay byte-identical. A run past the
    # deadline stops itself at the next generation boundary and writes its
    # output normally, which is what separates it from the harness's external
    # timeout_caps kill: a killed run leaves no run.json and is censored with
    # nothing recorded. A campaign setting this must still leave the profile's
    # cap well above it, the deadline interrupting neither the filters, the
    # scoring, nor the final realizability gate.
    "max_wall_s": 0,
    # What ends the search. "generations" is config.hpp's default and make_toml's
    # "emit nothing" sentinel, so the standard grids stay byte-identical;
    # "individuals" caps the run at `max_individuals` bred offspring instead,
    # reproducing AuRUS's own stopping rule (GA_MAX_NUM_INDIVIDUALS, 1000 in the
    # drivers behind its published numbers). Only an offspring differing from the
    # parent it was bred from counts, matching AuRUS's !chromosome.equals(mutated).
    # config_io.cpp rejects "individuals" with max_individuals = 0, so the two
    # keys are set together or not at all.
    "termination": "generations",
    # The cap for termination = "individuals". Emitted only when positive.
    # Matching AuRUS means matching its population too: it breeds 1000 offspring
    # at GA_POPULATION_SIZE = 100, so the cap alone at a different population
    # buys a different number of generations for the same number of individuals.
    "max_individuals": 0,
    # Scoring thread pool size. 0 is make_toml's "emit nothing" sentinel rather
    # than a mirror of config.hpp, whose default is available_parallelism() and
    # so has no literal to track. Emitted into [runtime] only when positive, so
    # the standard grids stay byte-identical. A config that states it also keeps
    # run_experiments.py's own per-run cap off it -- derive_config there fills in
    # for a config that says nothing and defers to one that does not.
    "parallel": 0,
    # Progress log + dashboard page, off by default in config.hpp and emitted
    # only when true. A campaign of many runs should leave it off; the
    # mechanism-check subset turns it on because progress.jsonl is where the
    # per-stage distinct-individual counts are recorded.
    "dashboard": False,
}


def _fmt(v: object) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        s = f"{v:.6g}"
        return s if "." in s else s + ".0"
    return str(v)


def make_toml(overrides: dict, defaults: dict = DEFAULTS) -> str:
    # Per-level overrides win over `defaults`, so --generations/--population-size
    # shift the operating point of every sweep that leaves those keys alone
    # without flattening sweeps A/B, which vary exactly those keys per level.
    d = {**defaults, **overrides}
    return "\n".join([
        "[genetic]",
        f"generations     = {d['generations']}",
        f"population_size = {d['population_size']}",
        f'selection_scheme = "{d["selection_scheme"]}"',
        f"crossover_rate  = {_fmt(d['crossover_rate'])}",
        f"mutation_rate   = {_fmt(d['mutation_rate'])}",
    ] + ([f"elitism_rate    = {_fmt(d['elitism_rate'])}"]
         if "elitism_rate" in overrides else []) + (
        [f"max_wall_s      = {d['max_wall_s']}"]
        if d.get("max_wall_s") else []) + (
        [f'termination     = "{d["termination"]}"']
        if d.get("termination", "generations") != "generations" else []) + (
        [f"max_individuals = {d['max_individuals']}"]
        if d.get("max_individuals") else []) + [
        "",
        "[fitness]",
        f"weight_syntactic = {_fmt(d['weight_syntactic'])}",
        f"weight_semantic  = {_fmt(d['weight_semantic'])}",
        f"weight_status    = {_fmt(d['weight_status'])}",
    ] + ([
        f'status_grading   = "{d["status_grading"]}"',
    ] if "status_grading" in overrides else []) + ([
        f'mrs_admission_order = "{d["mrs_admission_order"]}"',
    ] if "mrs_admission_order" in overrides else []) + [
        "",
        "[mutation]",
        f"p_trigger  = {_fmt(d['p_trigger'])}",
        f"p_response = {_fmt(d['p_response'])}",
        f"p_timing   = {_fmt(d['p_timing'])}",
    ] + ([f"p_condition_type = {_fmt(d['p_condition_type'])}"]
         if "p_condition_type" in overrides else []) + (
        [f"p_scope = {_fmt(d['p_scope'])}"]
        if "p_scope" in overrides else []) + (
        [f"p_stop = {_fmt(d['p_stop'])}"]
        if "p_stop" in overrides else []) + (
        [f"p_monotone = {_fmt(d['p_monotone'])}"]
        if "p_monotone" in overrides else []) + (
        [f"p_add_assumption = {_fmt(d['p_add_assumption'])}"]
        if "p_add_assumption" in overrides else []) + (
        [f"p_remove_guarantee = {_fmt(d['p_remove_guarantee'])}"]
        if "p_remove_guarantee" in overrides else []) + [
        "",
        "[model_counting]",
        f"default_bound = {d['default_bound']}",
        "",
        "[filters]",
        f"run_implication = {_fmt(d['run_implication'])}",
        "",
        "[runtime]",
        f"black_timeout_ms = {d['black_timeout_ms']}",
    ] + ([f"parallel = {d['parallel']}"]
         if d.get("parallel") else []) + (
        [f"ltlsynt_timeout_ms = {d['ltlsynt_timeout_ms']}"]
        if d.get("ltlsynt_timeout_ms") else []) + (
        [f"ltl2tgba_timeout_ms = {d['ltl2tgba_timeout_ms']}"]
        if d.get("ltl2tgba_timeout_ms") else []) + (
        [f"max_scoring_failure_rate = {_fmt(d['max_scoring_failure_rate'])}"]
        if d.get("max_scoring_failure_rate") else []) + (
        [f"dashboard = {_fmt(d['dashboard'])}"]
        if d.get("dashboard") else []) + ([
        "",
        "[tlsf.mutation]",
        f"p_assumption = {_fmt(d['p_assumption'])}",
        f"p_temporal   = {_fmt(d['p_temporal'])}",
    ] + ([f"p_clone_assumption = {_fmt(d['p_clone_assumption'])}"]
        if "p_clone_assumption" in overrides else [])
        if overrides.keys() & {"p_assumption", "p_temporal",
                               "p_clone_assumption"}
        else []) + [
        "",
    ])


# ── Sweep definitions ────────────────────────────────────────────────────────

# Every sweep holds the other parameters at DEFAULTS, so exactly one level per
# sweep is byte-identical to the A/gen10 baseline. run_experiments.py aliases
# those onto the baseline run rather than executing them again, which is why
# each grid below includes its own default value as a level.

# Sweep A: vary generations (population_size fixed at 200)
SWEEP_A: list[tuple[str, dict]] = [
    ("gen5",  {"generations": 5}),
    ("gen10", {"generations": 10}),   # baseline
    ("gen15", {"generations": 15}),
    ("gen20", {"generations": 20}),
    ("gen30", {"generations": 30}),
    ("gen40", {"generations": 40}),
    ("gen60", {"generations": 60}),
    ("gen80", {"generations": 80}),
]

# Sweep B: vary population size (generations fixed at 10)
SWEEP_B: list[tuple[str, dict]] = [
    ("pop50",   {"population_size": 50}),
    ("pop75",   {"population_size": 75}),
    ("pop100",  {"population_size": 100}),
    ("pop150",  {"population_size": 150}),
    ("pop200",  {"population_size": 200}),   # baseline
    ("pop300",  {"population_size": 300}),
    ("pop500",  {"population_size": 500}),
    ("pop750",  {"population_size": 750}),
    ("pop1000", {"population_size": 1000}),
    ("pop1500", {"population_size": 1500}),
]

# Sweep C: vary fitness weight presets (generations=10, population_size=200)
SWEEP_C: list[tuple[str, dict]] = [
    ("default",         {}),
    ("syntactic-heavy", {"weight_syntactic": 0.8, "weight_semantic": 0.2}),
    ("semantic-heavy",  {"weight_syntactic": 0.2, "weight_semantic": 0.8}),
    ("status-only",     {"weight_syntactic": 0.0, "weight_semantic": 0.0,
                         "weight_status": 1.0}),
]

# Sweep R: vary elitism. Elitism carries the top fraction over verbatim, which
# re-injects exact duplicates into the pool -- the mechanism nsga2-replicate
# deduplicates away. Crossing the two states separates "replicate helps" from
# "replicate helps only where nothing else re-injects duplicates".
SWEEP_R: list[tuple[str, dict]] = [
    ("elit0",   {"elitism_rate": 0.0}),
    ("elit0.1", {"elitism_rate": 0.1}),   # config.hpp default
]

# Keys whose built-in default has changed at least once since campaigns began
# being archived. A generated config states a key only where a sweep overrides
# it, so everything else is inherited from the binary at run time — which means
# changing a C++ default silently changes what every archived config *means*.
# --pin-vintage writes these explicitly so a campaign archived today still
# describes the run it was, whatever the defaults do afterwards. Add a key here
# when its default moves; the cost of a spurious entry is one redundant line
# per config, and the cost of a missing one is an archive that cannot be
# reproduced.
VINTAGE_KEYS: tuple[str, ...] = (
    "status_grading", "mrs_admission_order",
    "p_condition_type", "p_scope", "p_monotone",
)


SWEEPS: list[tuple[str, list]] = [
    ("A", SWEEP_A),
    ("B", SWEEP_B),
    ("C", SWEEP_C),
    ("R", SWEEP_R),
]

# ── TLSF campaign grid ───────────────────────────────────────────────────────

# The basic-TLSF examples are far slower than the FRETISH specs, and cost
# scales with generations*population, so the TLSF campaign sweeps only the two
# operating-point axes over a coarse four-level cross rather than the fine
# FRETISH grid. The two axes cross at the gen10/pop200 baseline (shared level),
# matching the FRETISH aliasing so B/pop200 collapses onto A/gen10. Emitted by
# `--tlsf` into configs-tlsf/.
TLSF_SWEEP_A: list[tuple[str, dict]] = [
    ("gen5",  {"generations": 5}),
    ("gen10", {"generations": 10}),   # baseline (shared with B/pop200)
    ("gen20", {"generations": 20}),
    ("gen40", {"generations": 40}),
]

TLSF_SWEEP_B: list[tuple[str, dict]] = [
    ("pop50",  {"population_size": 50}),
    ("pop100", {"population_size": 100}),
    ("pop200", {"population_size": 200}),   # baseline (shared with A/gen10)
    ("pop500", {"population_size": 500}),
]

# TLSF sweep R: the elitism cross of the FRETISH sweep R, at the TLSF baseline
# operating point (gen10/pop200) and carrying the TLSF runtime settings --tlsf
# supplies. Same letter as the FRETISH sweep because it is the same factor; the
# two never share a table, exactly as A and B do not.
TLSF_SWEEP_R: list[tuple[str, dict]] = list(SWEEP_R)

TLSF_SWEEPS: list[tuple[str, list]] = [
    ("A", TLSF_SWEEP_A),
    ("B", TLSF_SWEEP_B),
    ("R", TLSF_SWEEP_R),
]

TLSF_CONFIGS_DIR = Path(__file__).parent.parent / "experiments" / "configs-tlsf"

# Default per-call ltlsynt timeout (ms) for the TLSF campaign. ltlsynt has no
# internal timeout, and these specs occasionally produce synthesis queries that
# run for minutes; without a bound one such query stalls a whole run. A
# timed-out check is undecided: it admits no repair, and drops the candidate at
# the well-separation filter.
#
# The value matches the C++ default (Config::ltlsynt_timeout). The budget must
# exceed each spec's *own* realizability query: below it, every run of that
# spec scores a constant status objective and has its repairs rejected at the
# final gate.
TLSF_LTLSYNT_TIMEOUT_MS = 10000

# Default per-call ltl2tgba (model-counting) timeout (ms) for the TLSF campaign.
# The counting path has no internal timeout and its -D determinization blows up
# super-exponentially on some deep formulae. The bound sits well above any
# legitimate count while cutting that tail. A timed-out count drops that
# individual (absorbed by max_scoring_failure_rate).
TLSF_LTL2TGBA_TIMEOUT_MS = 60000

# Scoring-failure tolerance for the TLSF campaign. The one-hot balancer/direction
# encodings mutate into tautological guarantees (e.g. G((b2 -> !x) | !b0 | b1 | b2)),
# which SPOT 2.15.1's ltl2tgba rejects with exit 2. score_population's circuit
# breaker drops those individuals, but its tolerance is max_scoring_failure_rate *
# population floored at 1, so at the smallest rung (pop50 -> ~25 offspring) the
# default 0.05 tolerates only 1 and an unlucky seed hitting 2-3 tautologies aborts
# the whole run. 0.15 absorbs the observed rate while still catching a genuinely
# broken tool (which fails ~all individuals). Larger populations already clear it.
TLSF_MAX_SCORING_FAILURE_RATE = 0.15


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--generations", type=int, default=DEFAULTS["generations"],
                        metavar="N",
                        help=f"Baseline generations (default: "
                             f"{DEFAULTS['generations']}); sweep A overrides it "
                             f"per level")
    parser.add_argument("--population-size", type=int,
                        default=DEFAULTS["population_size"], metavar="N",
                        help=f"Baseline population size (default: "
                             f"{DEFAULTS['population_size']}); sweep B overrides "
                             f"it per level")
    parser.add_argument("--weights", nargs=3, type=float, default=None,
                        metavar=("SYNTACTIC", "SEMANTIC", "STATUS"),
                        help=f"Baseline aggregate-fitness weights, in the order "
                             f"syntactic semantic status (default: "
                             f"{DEFAULTS['weight_syntactic']} "
                             f"{DEFAULTS['weight_semantic']} "
                             f"{DEFAULTS['weight_status']}). Sweep C overrides "
                             f"them per level, exactly as sweeps A and B "
                             f"override --generations and --population-size. "
                             f"Omit to keep the pinned 0.33 triple every past "
                             f"grid was generated under")
    parser.add_argument("--schemes", nargs="+", choices=SCHEMES, default=SCHEMES,
                        metavar="SCHEME",
                        help=f"Selection schemes to emit (default: "
                             f"{' '.join(SCHEMES)})")
    # Every TLSF sweep letter is also a FRETISH one, so the FRETISH table's
    # letters are the whole choice list.
    parser.add_argument("--sweeps", nargs="+",
                        choices=[n for n, _ in SWEEPS],
                        default=[n for n, _ in SWEEPS], metavar="SWEEP",
                        help="Sweeps to emit (default: every sweep of the "
                             "selected table)")
    parser.add_argument("--levels", default=None, metavar="NAMES",
                        help="Comma-separated level names; each selected sweep "
                             "emits only its matching levels (e.g. --levels "
                             "default,status-only restricts sweep C to that "
                             "pair). A name matching no level of the "
                             "selected sweeps is an error — a typo would "
                             "otherwise silently shrink the grid. Omit to emit "
                             "every level")
    parser.add_argument("--weakening", choices=list(WEAKENINGS), default=None,
                        metavar="STATE",
                        help="Nest configs under <scheme>/wkoff/ so the "
                             "results' weakening column reads wkoff. Names the "
                             "directory only: the weakening filter no longer "
                             "exists and no key is written (choices: "
                             f"{', '.join(WEAKENINGS)}). Omit to leave the "
                             "directory out, which run_experiments.py "
                             "attributes to the legacy wkon")
    parser.add_argument("--out-dir", type=Path, default=CONFIGS_DIR, metavar="PATH",
                        help=f"Directory to write <scheme>/ dirs into (default: "
                             f"{CONFIGS_DIR})")
    parser.add_argument("--dashboard", action="store_true",
                        help="Emit runtime.dashboard = true, so each run writes "
                             "progress.jsonl (per-stage distinct-individual "
                             "counts). Costs a flushed write per stage; leave "
                             "off for a full campaign, on for a subset")
    parser.add_argument("--tlsf", action="store_true",
                        help="Emit the TLSF campaign grid: a coarse "
                             "generations x population cross (sweeps A, B) and "
                             "the elitism sweep R, for nsga2-apportion only, "
                             "into configs-tlsf. Unless overridden, sets "
                             "--schemes nsga2-apportion and --out-dir "
                             "configs-tlsf")
    parser.add_argument("--pin-vintage", action="store_true",
                        help="Write every key whose built-in default has moved "
                             f"({', '.join(VINTAGE_KEYS)}) into each config "
                             "explicitly, at its current default, so the "
                             "archive states them instead of inheriting them "
                             "from a future binary. A sweep that varies one of "
                             "these still wins. Recommended for any campaign "
                             "meant to be reproducible")
    parser.add_argument("--max-wall-s", type=int, default=None, metavar="S",
                        help="Per-run wall-clock deadline in seconds "
                             "(genetic.max_wall_s). 0 = no deadline; the key is "
                             "omitted from the emitted TOML when 0, keeping the "
                             "standard grids byte-identical. Unlike the "
                             "harness's own timeout_caps this stops the search "
                             "from inside, so the run still writes run.json and "
                             "its repairs instead of being censored -- set the "
                             "profile's cap well above it, since the deadline "
                             "does not interrupt filters, scoring or the final "
                             "realizability gate")
    parser.add_argument("--termination", choices=("generations", "individuals"),
                        default=None,
                        help="What ends the search (genetic.termination). "
                             "\"generations\" is the default and is omitted "
                             "from the emitted TOML, keeping the standard grids "
                             "byte-identical; \"individuals\" stops at "
                             "--max-individuals bred offspring instead, which "
                             "is AuRUS's own rule and the only way to give the "
                             "two tools the same search budget. Requires "
                             "--max-individuals, the binary rejecting the mode "
                             "at a cap of 0")
    parser.add_argument("--max-individuals", type=int, default=None, metavar="N",
                        help="Offspring budget for --termination individuals "
                             "(genetic.max_individuals). Only an offspring "
                             "differing from its parent counts. AuRUS's "
                             "published runs use 1000, at population 100 -- "
                             "pass --population-size 100 with it, or the same "
                             "cap buys a different number of generations")
    parser.add_argument("--parallel", type=int, default=None, metavar="N",
                        help="Scoring thread pool size (runtime.parallel). 0 = "
                             "omit the key and take the binary's hardware "
                             "concurrency, keeping the standard grids "
                             "byte-identical. A stated value also stops "
                             "run_experiments.py deriving its own per-run cap "
                             "over it, so pass 1 for a campaign whose runs are "
                             "meant to be single-threaded")
    parser.add_argument("--ltlsynt-timeout", type=int, default=None,
                        metavar="MS",
                        help="Per-call ltlsynt timeout in ms "
                             "(runtime.ltlsynt_timeout_ms). 0 = no timeout; the "
                             "key is omitted from the emitted TOML when 0. "
                             "ltlsynt has no internal timeout and the heavy TLSF "
                             "specs occasionally generate multi-minute synthesis "
                             f"queries, so --tlsf defaults to "
                             f"{TLSF_LTLSYNT_TIMEOUT_MS} ms")
    parser.add_argument("--ltl2tgba-timeout", type=int, default=None,
                        metavar="MS",
                        help="Per-call ltl2tgba model-counting timeout in ms "
                             "(runtime.ltl2tgba_timeout_ms). 0 = no timeout; the "
                             "key is omitted from the emitted TOML when 0. The "
                             "counting-path -D determinization blows up on some "
                             "deep formulae (multi-GB, hours), so --tlsf defaults "
                             f"to {TLSF_LTL2TGBA_TIMEOUT_MS} ms")
    parser.add_argument("--max-scoring-failure-rate", type=float, default=None,
                        metavar="RATE",
                        help="Fraction of a population allowed to fail scoring "
                             "before the run aborts "
                             "(runtime.max_scoring_failure_rate). 0 = fall back "
                             "to the built-in 0.05; the key is omitted from the "
                             "emitted TOML when 0, keeping the standard grids "
                             "byte-identical. The TLSF one-hot encodings produce "
                             "tautologies that ltl2tgba rejects, so --tlsf "
                             f"defaults to {TLSF_MAX_SCORING_FAILURE_RATE}")
    args = parser.parse_args()
    # The binary rejects the mode at a cap of 0, so refuse it here rather than
    # emitting a grid of configs that every run exits 1 on.
    if args.termination == "individuals" and not args.max_individuals:
        parser.error("--termination individuals requires --max-individuals")
    return args


def main() -> None:
    args = parse_args()
    defaults = {**DEFAULTS,
                "generations": args.generations,
                "population_size": args.population_size}
    # The three weights are the one part of DEFAULTS that is deliberately not
    # the binary's (0.33 each against config.hpp's 0.2 / 0.5 / 0.5), pinned so
    # every archived grid states the same triple and stays comparable. --weights
    # moves the baseline for one campaign without touching that pin, so a grid
    # generated without the flag is byte-identical to what it always was. A
    # sweep C level still wins over it, make_toml merging overrides last.
    if args.weights is not None:
        defaults["weight_syntactic"], defaults["weight_semantic"], \
            defaults["weight_status"] = args.weights
    # --tlsf swaps in the coarse TLSF cross and, unless the user overrode them,
    # pins the scheme and output directory the campaign expects. Comparing
    # against the argparse defaults is how "left unset" is detected.
    sweep_table = SWEEPS
    schemes = args.schemes
    out_dir = args.out_dir
    ltlsynt_timeout = args.ltlsynt_timeout
    ltl2tgba_timeout = args.ltl2tgba_timeout
    max_scoring_failure_rate = args.max_scoring_failure_rate
    if args.tlsf:
        sweep_table = TLSF_SWEEPS
        if schemes == SCHEMES:
            schemes = ["nsga2-apportion"]
        if out_dir == CONFIGS_DIR:
            out_dir = TLSF_CONFIGS_DIR
        if ltlsynt_timeout is None:
            ltlsynt_timeout = TLSF_LTLSYNT_TIMEOUT_MS
        if ltl2tgba_timeout is None:
            ltl2tgba_timeout = TLSF_LTL2TGBA_TIMEOUT_MS
        if max_scoring_failure_rate is None:
            max_scoring_failure_rate = TLSF_MAX_SCORING_FAILURE_RATE
    # Neither of these gets a --tlsf default: a deadline and a thread-pool size
    # belong to the campaign that measured them, not to the path, and silently
    # bounding every TLSF grid at one would change what the archived ones mean.
    defaults["max_wall_s"] = args.max_wall_s or 0
    defaults["termination"] = args.termination or "generations"
    defaults["max_individuals"] = args.max_individuals or 0
    defaults["parallel"] = args.parallel or 0
    defaults["ltlsynt_timeout_ms"] = ltlsynt_timeout or 0
    defaults["ltl2tgba_timeout_ms"] = ltl2tgba_timeout or 0
    defaults["max_scoring_failure_rate"] = max_scoring_failure_rate or 0.0
    defaults["dashboard"] = args.dashboard
    wanted = set(args.sweeps)
    sweeps = [(name, levels) for name, levels in sweep_table if name in wanted]
    # --levels restricts every selected sweep to the named levels (a sweep left
    # with none is dropped). Unknown names abort rather than warn: the flag
    # exists to keep a grid's cell count exact, so a typo silently emitting a
    # smaller grid is precisely the failure mode to prevent.
    if args.levels is not None:
        wanted_levels = set(args.levels.split(","))
        sweeps = [(name, [lv for lv in levels if lv[0] in wanted_levels])
                  for name, levels in sweeps]
        sweeps = [(name, levels) for name, levels in sweeps if levels]
        found = {name for _, levels in sweeps for name, _ in levels}
        missing = wanted_levels - found
        if missing:
            raise SystemExit(f"--levels: no such level(s) in the selected "
                             f"sweeps: {', '.join(sorted(missing))}")
    # (subdirectory, override). The weakening directory writes no key; see
    # WEAKENINGS.
    weakenings: list[tuple[str | None, dict]] = (
        [(d, {}) for d in WEAKENINGS[args.weakening]]
        if args.weakening else [(None, {})]
    )

    # Keys whose C++ default has moved at least once, written out explicitly so
    # the archive states them rather than inheriting whatever the binary means
    # by them next year. Merged first, so a sweep varying one of these on
    # purpose still wins.
    pinned = {k: defaults[k] for k in VINTAGE_KEYS} if args.pin_vintage else {}

    count = 0
    for scheme in schemes:
        for wk_dir, wk_override in weakenings:
            segments = [p for p in (scheme, wk_dir, METRIC_DIR) if p is not None]
            out = out_dir.joinpath(*segments)
            out.mkdir(parents=True, exist_ok=True)
            for sweep_name, levels in sweeps:
                for level_name, overrides in levels:
                    path = out / f"sweep_{sweep_name}_{level_name}.toml"
                    path.write_text(make_toml(
                        {**pinned, **overrides, "selection_scheme": scheme,
                         **wk_override},
                        defaults))
                    count += 1
            print(f"  {'/'.join(segments):24} "
                  f"{len(list(out.glob('sweep_*.toml'))):3} configs")
    print(f"\nGenerated {count} config files in {out_dir}")


if __name__ == "__main__":
    main()
