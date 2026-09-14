#!/usr/bin/env python3
"""Generate TOML config files for all experiment sweeps.

Writes one file per (selection scheme, sweep, level) to
experiments/configs/<scheme>/. Safe to re-run — existing files are overwritten.

The scheme lives in the directory rather than the filename because
run_experiments.py parses (sweep, level) out of the filename, and it reads the
scheme back off the parent directory. Every sweep is generated once per scheme,
which makes selection_scheme a factor of the design rather than a constant.

Usage:
    python scripts/gen_configs.py                       # the standard grid
    python scripts/gen_configs.py --schemes nsga2-truncate   # one scheme only
    python scripts/gen_configs.py --sweeps C D E        # specific sweeps
    python scripts/gen_configs.py --sweeps C --levels default,status-only
                                                        # specific levels only
    python scripts/gen_configs.py --generations 40 --population-size 1000 \\
        --out-dir experiments/configs-cj-large          # a larger operating point
    python scripts/gen_configs.py --weakening off       # nest under <scheme>/wkoff/
    python scripts/gen_configs.py --metric both         # cross direct/log metric in
    python scripts/gen_configs.py --tlsf                # the TLSF campaign grid
"""

import argparse
from pathlib import Path

CONFIGS_DIR = Path(__file__).parent.parent / "experiments" / "configs"

# Selection schemes the whole grid is duplicated across. Every generated config
# pins the scheme explicitly rather than relying on the config.hpp default,
# which is "nsga2-apportion" and so is *not* the head of this list.
# nsga2-apportion is appended rather than ordered next to nsga2-truncate so the
# first two entries keep the positions the earlier grids were generated under.
#
# The two NSGA-II schemes were renamed: "nsga2" -> "nsga2-truncate" and
# "nsga2-replicate" -> "nsga2-apportion". That is a hard break rather than a
# deprecation -- config_io.cpp rejects the old spellings, so a config emitted
# under them does not run at all. Each string here is also a config *directory*
# name: scheme_of() in run_experiments.py reads it back into the `selection`
# column of every results CSV, and that column joins KEY_FIELDS in
# merge_experiments.py -- the natural key for resume and merge. Roughly a
# quarter of a million archived rows were written under the old names, so the
# rename would make every one of them miss its key and re-run a finished
# campaign. SCHEME_ALIASES / canonical_scheme() in run_experiments.py and
# merge_experiments.py close that gap by canonicalising every `selection` value
# read back; nothing else here may depend on the old spellings.
SCHEMES: list[str] = ["nsga2-truncate", "weighted", "nsga2-apportion"]

# The weakening directory, which names a state and no longer sets one. The
# weakening filter and its [filters] run_weakening key were removed on
# 2026-09-11, so the binary never weakens and no config states the key. The
# directory stays because `weakening` is one of merge_experiments.KEY_FIELDS:
# weakening_of() in run_experiments.py reads it back into that column, and a
# flat config with no weakening directory is attributed to LEGACY_WEAKENING
# ("wkon"), which was true of every run before 2026-08-20 and is false of every
# run since. A new campaign therefore passes --weakening off, as every one since
# 2026-08-20 has, so its rows say what the binary did.
WEAKENINGS: dict[str, list[str]] = {
    "off": ["wkoff"],
}

# model_counting.metric as a crossed factor, exactly like WEAKENINGS above.
# Each entry is (dir_label, toml_value): the directory + CSV label is the short
# "direct"/"log", but the TOML value must be the full "direct"/"logarithmic"
# string config_io.cpp's apply_model_counting accepts, or the run aborts. Like
# --weakening, --metric defaults to None so the flat layout and the metric from
# DEFAULTS stay byte-identical to the pre-factor grids.
METRICS: dict[str, list[tuple[str, str]]] = {
    "direct": [("direct", "direct")],
    "log":    [("log", "logarithmic")],
    "both":   [("direct", "direct"), ("log", "logarithmic")],
}

# tlsf.repair_mode as a crossed factor, exactly like WEAKENINGS/METRICS above.
# Each entry is (dir_label, toml_value): the directory + CSV label is the short
# "mono"/"muc", and here the TOML value happens to differ only for "mono"
# ("monolithic"), the string config_io.cpp's apply_tlsf accepts. Like
# --weakening/--metric, --repair defaults to None so the flat layout and the
# repair_mode from DEFAULTS stay byte-identical to the pre-factor grids.
REPAIRS: dict[str, list[tuple[str, str]]] = {
    "mono": [("mono", "monolithic")],
    "muc":  [("muc", "muc")],
    "both": [("mono", "monolithic"), ("muc", "muc")],
}

# Mirrors the built-in defaults from include/config.hpp. Every entry tracks the
# binary except the eight enumerated below, and check_config_schema.py enforces
# that: it walks this table against config.hpp's in-class initialisers and fails
# on any key that is neither mapped to a member nor listed there as exempt. A
# key whose C++ default moves is therefore corrected here rather than noticed
# later — which matters most for the VINTAGE_KEYS, since --pin-vintage writes
# *these* values into the generated configs.
#
# The deliberate divergences, and why each one does not follow the binary:
#
#   * metric — pinned "direct"; config.hpp defaults it to "logarithmic". A flat
#     config carries no metric directory, so metric_of() in run_experiments.py
#     attributes it to LEGACY_METRIC ("direct"), and `metric` is one of
#     merge_experiments.KEY_FIELDS, so the emitted value has to keep matching
#     that recorded CSV column or ~225k archived rows stop joining their own
#     key. Cross the metric explicitly with --metric to exercise
#     "logarithmic".
#   * selection_scheme — pinned "nsga2-truncate"; config.hpp defaults it to
#     "nsga2-apportion". Same argument again: the scheme is the config
#     *directory* name that scheme_of() reads back into the `selection` CSV key
#     column, and every generated config pins it explicitly, so the first entry
#     of SCHEMES has to stay the one past grids were generated under.
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
    "selection_scheme": "nsga2-truncate",
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
    # scope, along its implication order. Both default to p_timing's 0.15 since
    # 2026-09-11 and were 0 before, where neither arm costs an RNG draw. Emitted
    # into [mutation] only when a sweep overrides them (see make_toml), so the
    # standard grids stay byte-identical, which makes the move a "Config
    # vintage" entry in experiments/README.md; both are in VINTAGE_KEYS. No
    # sweep crosses them yet.
    "p_condition_type": 0.15,
    "p_scope": 0.15,
    # Probability a mutation appends a new environment (fairness) assumption
    # rather than rewriting an existing requirement/section. Emitted into
    # [mutation] only when a sweep overrides it (see make_toml), so the standard
    # grids stay byte-identical; the p_add_assumption sweep varies it.
    "p_add_assumption": 0.05,
    # The mirror action: delete one guarantee. Emitted into [mutation] only when
    # a sweep overrides it (see make_toml), like p_add_assumption; TLSF sweep D
    # varies it, and its prem0 arm is the pre-operator control.
    "p_remove_guarantee": 0.05,
    "default_bound": 20,
    "metric": "direct",
    "run_implication": True,
    # Keys in VINTAGE_KEYS must follow config.hpp rather than be pinned, because
    # VINTAGE_KEYS sources its written-out values from this table -- a stale
    # entry here is a wrong value written into every config generated with
    # --pin-vintage, by the mechanism that exists to stop a moved default going
    # unrecorded. check_config_schema.py enforces the agreement for every key
    # of this table that is not deliberately exempt.
    # Report every gate-passing candidate of every generation rather than only
    # the final population's. On in the binary since 2026-08-25, and emitted
    # into [genetic] only when a sweep overrides it (see make_toml), so every
    # existing grid stays byte-identical; TLSF sweep N crosses it. Sweep N
    # states it on both arms rather than letting the control inherit silence,
    # which is why that campaign is the one archive the move did not reinterpret
    # -- the exposure "Config vintage" in experiments/README.md records for
    # every other archive here.
    "accumulate_repairs": True,
    "black_timeout_ms": 1000,
    "repair_mode": "monolithic",
    # Mirrors include/config.hpp, which moved to "mrs" on the 2026-08-11
    # campaign. Emitted into [fitness] only when a sweep overrides it (see
    # make_toml), so every existing grid stays byte-identical; sweep G varies
    # it. That silence is what makes the move a config-vintage change for every
    # campaign archived before it -- see "Config vintage" in
    # experiments/README.md.
    "status_grading": "mrs",
    # Mirrors include/config.hpp, which moved to "degree" on 2026-08-14. Emitted
    # into [fitness] only when a sweep overrides it (see make_toml), so every
    # existing grid stays byte-identical. Read only under status_grading =
    # "mrs", so pinning it says nothing about a tiered arm either way -- and it
    # is a config-vintage change for every campaign archived before it, none of
    # which could state a key their binary had never heard of.
    "mrs_admission_order": "degree",
    # TLSF-only [tlsf.mutation] split (see config.hpp). Emitted only when a sweep
    # overrides one of them (see make_toml), so the FRETISH and A/B TLSF grids
    # stay byte-identical to the pre-factor output; the mutation-split sweep sets
    # them to vary how mutation divides its budget between the environment
    # (assumption) and guarantee sides.
    "p_assumption": 0.3,
    "p_temporal": 0.2,
    # The monotone arm and the clone-an-assumption branch of p_add_assumption
    # (see config.hpp). Both default on in the binary and each is emitted only
    # when a sweep overrides it (see make_toml), so every grid generated before
    # they existed stays byte-identical; TLSF sweep T crosses them. p_monotone
    # sits under [mutation] and is read by both paths since 2026-09-11; it was
    # TLSF-only under [tlsf.mutation] before, and the FRETISH arm defaulted to
    # 0. Every campaign archived before 2026-08-21 ran without either operator
    # and cannot state so, which makes this a "Config vintage" entry in
    # experiments/README.md rather than a silent one -- reproducing such a
    # campaign means writing both keys to 0. p_monotone is in VINTAGE_KEYS.
    "p_monotone": 0.25,
    "p_clone_assumption": 0.25,
    # The 2026-08-25 assumption-reach keys (see config.hpp): a width for the
    # disjunctive body tlsf_add_assumption draws and a bare-F form for an
    # appended assumption. Both default to their no-op value in the binary, so
    # a campaign archived before 2026-08-25 that omits them means exactly what
    # it always meant and no "Config vintage" entry is owed. p_remove_assumption
    # and p_burst_continue were removed with their operators on 2026-09-11. A
    # fifth key, p_union_assumption, was removed rather than kept at its no-op;
    # a config that still sets it is warned about and ignored, so reproduce
    # such a campaign from its vendored scripts/.
    "max_assumption_width": 1,
    "p_bare_assumption": 0.0,
    # Per-call ltlsynt timeout in ms; 0 = no timeout, matching config.hpp.
    # Emitted only when positive, so the standard grids stay byte-identical. The
    # heavy TLSF specs set it to cut ltlsynt's multi-minute realizability tail.
    "ltlsynt_timeout_ms": 0,
    # Per-call ltl2tgba (model-counting) timeout in ms; 0 = no timeout, matching
    # config.hpp. Emitted only when positive. The counting-path -D determinization
    # blows up super-exponentially on some deep formulae; the TLSF campaign sets
    # it to cut the multi-GB, hours-long counting tail (see the ltl2tgba leak fix).
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
        [f"accumulate_repairs = {_fmt(d['accumulate_repairs'])}"]
        if "accumulate_repairs" in overrides else []) + (
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
        [f"p_monotone = {_fmt(d['p_monotone'])}"]
        if "p_monotone" in overrides else []) + (
        [f"p_add_assumption = {_fmt(d['p_add_assumption'])}"]
        if "p_add_assumption" in overrides else []) + (
        [f"p_remove_guarantee = {_fmt(d['p_remove_guarantee'])}"]
        if "p_remove_guarantee" in overrides else []) + [
        "",
        "[model_counting]",
        f"default_bound = {d['default_bound']}",
        f'metric = "{d["metric"]}"',
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
        if d.get("dashboard") else []) + [
        "",
        "[tlsf]",
        f'repair_mode = "{d["repair_mode"]}"',
    ] + ([
        "",
        "[tlsf.mutation]",
        f"p_assumption = {_fmt(d['p_assumption'])}",
        f"p_temporal   = {_fmt(d['p_temporal'])}",
    ] + ([f"p_clone_assumption = {_fmt(d['p_clone_assumption'])}"]
        if "p_clone_assumption" in overrides else []) + (
        [f"max_assumption_width = {d['max_assumption_width']}"]
        if "max_assumption_width" in overrides else []) + (
        [f"p_bare_assumption = {_fmt(d['p_bare_assumption'])}"]
        if "p_bare_assumption" in overrides else [])
        if overrides.keys() & {"p_assumption", "p_temporal",
                               "p_clone_assumption",
                               "max_assumption_width",
                               "p_bare_assumption"}
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
#
# The "no-halstead" level was retired on 2026-08-13 with the Halstead objective
# itself. It set weight_halstead to 0 against a default of 0.1, so with the key
# gone the level says nothing the "default" level does not; the ablation it
# carried is retired with it. Archived rows recording it stay readable, and the
# campaigns that ran it reproduce from their vendored per-campaign scripts/ at
# the commit their PROVENANCE.json names.
SWEEP_C: list[tuple[str, dict]] = [
    ("default",         {}),
    ("syntactic-heavy", {"weight_syntactic": 0.8, "weight_semantic": 0.2}),
    ("semantic-heavy",  {"weight_syntactic": 0.2, "weight_semantic": 0.8}),
    ("status-only",     {"weight_syntactic": 0.0, "weight_semantic": 0.0,
                         "weight_status": 1.0}),
]

# Sweep D: vary the trigger mutation probability
SWEEP_D: list[tuple[str, dict]] = [
    ("ptrig0.0",  {"p_trigger": 0.0}),
    ("ptrig0.1",  {"p_trigger": 0.1}),
    ("ptrig0.25", {"p_trigger": 0.25}),
    ("ptrig0.5",  {"p_trigger": 0.5}),   # baseline
    ("ptrig0.75", {"p_trigger": 0.75}),
    ("ptrig0.9",  {"p_trigger": 0.9}),
    ("ptrig1.0",  {"p_trigger": 1.0}),
]

# Sweep E: vary the response mutation probability
SWEEP_E: list[tuple[str, dict]] = [
    ("presp0.0",  {"p_response": 0.0}),
    ("presp0.1",  {"p_response": 0.1}),
    ("presp0.25", {"p_response": 0.25}),
    ("presp0.5",  {"p_response": 0.5}),  # baseline
    ("presp0.75", {"p_response": 0.75}),
    ("presp0.9",  {"p_response": 0.9}),
    ("presp1.0",  {"p_response": 1.0}),
]

# Sweep F: vary the timing mutation probability
SWEEP_F: list[tuple[str, dict]] = [
    ("ptim0.0",  {"p_timing": 0.0}),
    ("ptim0.05", {"p_timing": 0.05}),
    ("ptim0.15", {"p_timing": 0.15}),   # baseline
    ("ptim0.3",  {"p_timing": 0.3}),
    ("ptim0.5",  {"p_timing": 0.5}),
    ("ptim0.75", {"p_timing": 0.75}),
    ("ptim1.0",  {"p_timing": 1.0}),
]

# Sweep G: vary the bounded model-counting horizon. Raising it costs almost
# nothing — the bound enters through the transfer matrix, not a SAT call, and
# bound 80 measured within noise of bound 5 — but it moves the semantic
# similarity score, so it changes which repairs win.
SWEEP_G: list[tuple[str, dict]] = [
    ("bound5",   {"default_bound": 5}),
    ("bound10",  {"default_bound": 10}),
    ("bound20",  {"default_bound": 20}),   # baseline
    ("bound40",  {"default_bound": 40}),
    ("bound80",  {"default_bound": 80}),
    ("bound160", {"default_bound": 160}),
]

# Sweep H: vary the crossover rate. The default of 0.1 leaves the search
# almost entirely mutation-driven; the 0.0 level tests whether crossover
# contributes at all.
SWEEP_H: list[tuple[str, dict]] = [
    ("cross0.0",  {"crossover_rate": 0.0}),
    ("cross0.1",  {"crossover_rate": 0.1}),  # baseline
    ("cross0.25", {"crossover_rate": 0.25}),
    ("cross0.5",  {"crossover_rate": 0.5}),
    ("cross0.75", {"crossover_rate": 0.75}),
    ("cross1.0",  {"crossover_rate": 1.0}),
]

# Sweep I: vary the mutation rate
SWEEP_I: list[tuple[str, dict]] = [
    ("mut0.1",  {"mutation_rate": 0.1}),
    ("mut0.25", {"mutation_rate": 0.25}),
    ("mut0.5",  {"mutation_rate": 0.5}),
    ("mut0.75", {"mutation_rate": 0.75}),
    ("mut1.0",  {"mutation_rate": 1.0}),   # baseline
]

# Sweep J is retired. It ablated the weakening filter through [filters]
# run_weakening, which was removed on 2026-09-11 with the filter itself, so
# neither arm is expressible. Its archived campaigns reproduce from their
# vendored per-campaign scripts/ at the commit their PROVENANCE.json names, as
# sweeps C, O and V do.

# Sweep K: the status objective's grading scale, on the FRETISH path. The TLSF
# table carries the same three levels under G; the FRETISH G is the model-count
# bound and was there first, so this is the same factor under a free letter
# rather than a letter reused across the two tables. K is unused in both, and O
# and U are avoided as retired rather than free.
#
# The scale is only measurable on this path from 2026-09-09: the FRETISH final
# realizability gate hard-coded Tiered until then, so a run configured `aurus`
# scored its search on the six-level ladder and judged its output on the
# three-point one. Generating this sweep against an earlier binary measures the
# search alone with a gate that ignores the factor.
SWEEP_K: list[tuple[str, dict]] = [
    ("tiered", {"status_grading": "tiered"}),
    ("mrs",    {"status_grading": "mrs"}),
    ("aurus",  {"status_grading": "aurus"}),
]

# Sweep R: vary elitism, for the nsga2-vs-nsga2-replicate campaign. Elitism
# carries the top fraction over verbatim, which re-injects exact duplicates into
# the pool -- the mechanism nsga2-replicate deduplicates away. The scheme's
# original A/B ran at elitism_rate = 0, which is *not* the shipped default, so
# crossing the two states is what separates "replicate helps" from "replicate
# helps only where nothing else re-injects duplicates".
SWEEP_R: list[tuple[str, dict]] = [
    ("elit0",   {"elitism_rate": 0.0}),
    ("elit0.1", {"elitism_rate": 0.1}),   # config.hpp default
]

# Sweep O is retired. It crossed [genetic] repaired_operators, the repaired
# mutation and crossover grammar of 2026-08-19 against the one every campaign
# before it ran; that grammar is now the only one and the key no longer exists,
# so neither arm is expressible and the sweep cannot be generated at all. Its
# archived campaigns are the only record, and they reproduce from their
# vendored per-campaign scripts/ at the commit their PROVENANCE.json names, as
# sweep V does after [filters.intervals] went.

# Sweep S: the compute-matched control for sweep R. nsga2-replicate costs more
# wall time per run than nsga2 (it breeds from a replicated population), so a
# raw R comparison confounds "better selection" with "more compute". S is
# sweep R's levels at a larger generation budget, run under nsga2 only: if
# replicate beats plain nsga2 at R but not this arm, the gain was the compute.
#
# The multiplier is a flag rather than a constant because the campaign fixes it
# from calibration -- measure replicate's actual cost ratio, then regenerate S
# with --compute-match-factor set to it. Only `generations` is scaled;
# population_size is the axis the duplication story is about, so holding it
# fixed keeps the arms comparable in the dimension under test.
DEFAULT_COMPUTE_MATCH_FACTOR = 1.5

# Keys whose built-in default has changed at least once since campaigns began
# being archived. A generated config states a key only where a sweep overrides
# it, so everything else is inherited from the binary at run time — which means
# changing a C++ default silently changes what every archived config *means*.
# These have crossed that line: status_grading went tiered -> mrs on
# 2026-08-12, swapping the status objective outright rather than shifting a
# threshold, mrs_admission_order went spec ->
# degree on 2026-08-14, which reorders the greedy walk inside that objective and
# so moves the score of every candidate the walk grades, and p_condition_type,
# p_scope and the FRETISH p_monotone went 0 -> 0.15, 0.15 and 0.25 on
# 2026-09-11. run_well_separation and allow_output_assumptions (which moved
# twice) were here until their keys were removed on 2026-09-11; the binary now
# warns on a config stating either and ignores the key.
# --pin-vintage writes these explicitly so a campaign archived today still
# describes the run it was, whatever the defaults do afterwards. Add a key here when its default moves; the cost of a spurious
# entry is one redundant line per config, and the cost of a missing one is an
# archive that cannot be reproduced.
VINTAGE_KEYS: tuple[str, ...] = (
    "status_grading", "mrs_admission_order",
    "p_condition_type", "p_scope", "p_monotone",
)


def make_sweep_s(generations: int, factor: float) -> list[tuple[str, dict]]:
    matched = max(1, round(generations * factor))
    return [(f"cm-{level_name}", {**overrides, "generations": matched})
            for level_name, overrides in SWEEP_R]


SWEEPS: list[tuple[str, list]] = [
    ("A", SWEEP_A),
    ("B", SWEEP_B),
    ("C", SWEEP_C),
    ("D", SWEEP_D),
    ("E", SWEEP_E),
    ("F", SWEEP_F),
    ("G", SWEEP_G),
    ("H", SWEEP_H),
    ("I", SWEEP_I),
    ("K", SWEEP_K),
    ("R", SWEEP_R),
    # Placeholder levels at the default operating point and match factor: main()
    # rebuilds this entry from --generations/--compute-match-factor. It is
    # tabulated anyway so --sweeps validates "S" and the default set includes it.
    ("S", make_sweep_s(DEFAULTS["generations"], DEFAULT_COMPUTE_MATCH_FACTOR)),
]

# ── TLSF campaign grid ───────────────────────────────────────────────────────

# The basic-TLSF examples are far slower than the FRETISH specs — three of them
# take ~3 min even at gen10/pop200 and cost scales with generations*population —
# so the TLSF campaign sweeps only the two operating-point axes over a coarse
# four-level cross rather than the fine FRETISH grid, spending the fixed
# wall-clock budget on seeds instead of gradations. The two axes cross at the
# gen10/pop200 baseline (shared level), matching the FRETISH aliasing so
# B/pop200 collapses onto A/gen10. Emitted by `--tlsf` into configs-tlsf/.
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

# TLSF sweep M: vary the assumption/guarantee mutation split (TLSF-only). Named
# by the guarantee share; p_assumption is its complement so the two sides always
# sum to 1. In monolithic mode this trades environment-side against guarantee-
# side mutation over the whole spec; in muc mode the environment side is kept at
# full size while the guarantee side is the minimal core, so the same split
# spends a larger share of guarantee mutations on the culprit formulae. pg0.7 is
# the config.hpp baseline (p_assumption=0.3). Crossed with tlsf.repair_mode via
# `--tlsf --sweeps M --repair both` for the mono-vs-muc campaign.
TLSF_SWEEP_M: list[tuple[str, dict]] = [
    # Level names still read as the guarantee-side share, which is now
    # 1 - p_assumption rather than its own key. They are load-bearing: level
    # names are parsed back out of archived campaign paths, so renaming them
    # would orphan every muc-campaign row.
    ("pg0.3", {"p_assumption": 0.7}),
    ("pg0.5", {"p_assumption": 0.5}),
    ("pg0.7", {"p_assumption": 0.3}),   # baseline
    ("pg0.9", {"p_assumption": 0.1}),
]

# TLSF sweep P: vary p_add_assumption (TLSF-only campaign use, though the key is
# shared by both modes). Raising it makes mutation append a fairness assumption
# more often, the structural move needed to reach assumption-side ideals (e.g.
# arbiter's G F r0 & G F r1). The 2026-07-21 muc campaign found the ideal-hit rate
# is bottlenecked on this fixed-rate mutation, not the assumption/guarantee split;
# this sweep tests whether raising it shifts repairs from guarantee-weakening to
# the ideal. padd0.05 is the config.hpp baseline. Crossed with tlsf.repair_mode.
TLSF_SWEEP_P: list[tuple[str, dict]] = [
    ("padd0.05", {"p_add_assumption": 0.05}),   # baseline
    ("padd0.15", {"p_add_assumption": 0.15}),
    ("padd0.3",  {"p_add_assumption": 0.3}),
    ("padd0.5",  {"p_add_assumption": 0.5}),
]

# TLSF sweep D: vary p_remove_guarantee, the guarantee-deletion operator (D for
# delete; R is taken by the shared generations sweep). prem0 turns the operator
# off and so reproduces a run from before it existed, which makes it the control
# arm; prem0.05 is the config.hpp default. The eight drop-* ideals are
# unreachable at prem0 and reachable above it, which is what this sweep is for;
# the open question is the cost, since deleting a guarantee only ever helps
# realizability and the similarity objectives are the only thing paying for it.
TLSF_SWEEP_D: list[tuple[str, dict]] = [
    ("prem0",    {"p_remove_guarantee": 0.0}),   # pre-operator control
    ("prem0.05", {"p_remove_guarantee": 0.05}),  # baseline, mirrors p_add_assumption
    ("prem0.15", {"p_remove_guarantee": 0.15}),
    ("prem0.3",  {"p_remove_guarantee": 0.3}),
]

# TLSF sweeps W and Q are retired. W crossed [filters] run_well_separation
# against allow_output_assumptions as a 2x2, and Q spread p_add_assumption over
# the two run_well_separation = true arms for the arbiter follow-up; that key
# was removed on 2026-09-11 with the per-generation well-separation filter, and
# allow_output_assumptions with it, so neither sweep is expressible. The
# well-separation row of the correctness table still drives the final gate and
# the input screen. Their archived
# campaigns (wellsep, arbiter-hp, arbiter-padd) reproduce from their vendored
# per-campaign scripts/ at the commit their PROVENANCE.json names.

# TLSF sweep R: the elitism cross of the FRETISH sweep R, at the TLSF baseline
# operating point (gen10/pop200) and carrying the TLSF runtime settings --tlsf
# supplies. Same letter as the FRETISH sweep because it is the same factor; the
# two never share a table, exactly as A and B do not.
TLSF_SWEEP_R: list[tuple[str, dict]] = list(SWEEP_R)

# Sweep G: the status objective's grading scale (fitness.status_grading).
# Expressed as a sweep rather than as a crossed factor directory, deliberately.
# A crossed factor would need its own CSV column, and that column would have to
# join KEY_FIELDS in merge_experiments.py to tell the two arms apart -- which
# means every one of the ~225k archived rows, none of which carries the column,
# would need a legacy default to keep matching. A sweep needs none of that: the
# arm rides in `level_name`, a field the key already carries, exactly as sweep C
# carries its weight presets. Cross it as a factor only when a campaign needs it
# crossed with something else.
#
# "tiered" is the config.hpp default and the baseline arm; "mrs" is the greedy
# maximal-realizable-subset scale. level_value_of() reads a trailing number off
# the level name and finds none in either, as with sweep C's default and
# status-only, so both record a null level value.
#
# "aurus" was appended on 2026-08-28, after the ladder landed in config.hpp, and
# is appended rather than ordered beside "tiered" for the reason SCHEMES is: the
# closed `status-grading` profile pins levels ["tiered", "mrs"] and its archived
# rows name those two, so the pair keeps the positions it was generated under.
# AuRUS's own six-level scale (0, 0.05, 0.1, 0.2, 0.5, 1.0) grades by which side
# of the specification is satisfiable on its own and never folds well-separation
# into its realizability query, so crossing it against "mrs" ablates counter's
# status objective against the design it derives from. It carries no trailing
# number either, so it records a null level value like the other two.
TLSF_SWEEP_G: list[tuple[str, dict]] = [
    ("tiered", {"status_grading": "tiered"}),
    ("mrs",    {"status_grading": "mrs"}),
    ("aurus",  {"status_grading": "aurus"}),
]

# TLSF sweep N: the cross-generation accumulator, off against on. counter
# otherwise reports the maximal antichain of its *final* population, so a
# candidate that passed the output gate in generation 3 and was not selected
# into generation 4 is a repair the search found and discarded. The AuRUS
# baseline keeps them all, which is most of why the 2026-08-14 head-to-head
# recorded it emitting a median of 448 solutions against counter's 4. Repair
# quality is judged existentially, so a larger emitted set cannot lower
# implies_ideal; the question is how much it raises it, and what the extra gate
# sweep per generation costs on this path.
TLSF_SWEEP_N: list[tuple[str, dict]] = [
    ("accoff", {"accumulate_repairs": False}),   # control
    ("accon",  {"accumulate_repairs": True}),
]

# TLSF sweep T: attribute the three changes on feat/monotone-operators, one
# contrast at a time, against the archived 2026-08-21-aurus-h2h-ship rows as the
# outer control. That campaign is main at the shipping configuration plus the
# accumulator over this same corpus, cap and seed split, so every arm below is
# paired with it on (spec, seed).
#
# The branch carries three independent changes and their keys do not separate
# them evenly. Widening the TLSF binary grammar to draw Implies is unconditional
# -- there is no key for it -- so it rides in every arm here and is attributable
# only against the archived rows. The monotone rewrite and the cloned assumption
# have keys, and both cost no RNG draw at 0 (the branch's
# test_zero_probability_costs_no_draw pins that), so monooff reproduces the
# grammar widening alone rather than approximating it. Elitism has a key too.
#
#   archived -> monooff   the Implies widening
#   monooff  -> monoon    the monotone rewrite + the cloned assumption
#   monoon   -> monoship  elitism_rate 0.1 -> 0, the branch's other default move
#   archived -> monoship  the branch as it ships
#
# elitism_rate is stated on all three arms rather than left to the default, for
# sweep N's reason: the branch moves that default, so an arm inheriting silence
# would mean one thing here and another after the next default move. 0.1 on the
# first two matches what the archived control ran, which is what makes the first
# two contrasts read as the code change alone.
#
# accumulate_repairs is stated on every arm for a second reason: the control ran
# it on, through sweep N's accon level, and it is off in the binary. An arm that
# left it silent would answer a different question from the one it is paired
# with, and the endpoint is per-run implies_ideal over everything emitted.
TLSF_SWEEP_T: list[tuple[str, dict]] = [
    ("monooff",  {"p_monotone": 0.0,  "p_clone_assumption": 0.0,
                  "elitism_rate": 0.1, "accumulate_repairs": True}),
    ("monoon",   {"p_monotone": 0.25, "p_clone_assumption": 0.25,
                  "elitism_rate": 0.1, "accumulate_repairs": True}),
    ("monoship", {"p_monotone": 0.25, "p_clone_assumption": 0.25,
                  "elitism_rate": 0.0, "accumulate_repairs": True}),
]

# Sweep U is retired. It crossed the 2026-08-25 assumption-reach operators
# against search size, and two of its three operator levels set
# [tlsf.mutation] p_union_assumption; that key is gone -- the union crossover
# it armed cannot reach what it was written for -- so neither `reach` nor
# `reachburst` is expressible and the sweep cannot be generated at all.
# `experiments/2026-08-26-assumption-reach` is the only record of what ran, and
# it reproduces from its vendored per-campaign scripts/ at the commit its
# PROVENANCE.json names, as sweeps C, O and V do.

TLSF_SWEEPS: list[tuple[str, list]] = [
    ("A", TLSF_SWEEP_A),
    ("B", TLSF_SWEEP_B),
    ("M", TLSF_SWEEP_M),
    ("P", TLSF_SWEEP_P),
    ("D", TLSF_SWEEP_D),
    ("R", TLSF_SWEEP_R),
    ("G", TLSF_SWEEP_G),
    ("N", TLSF_SWEEP_N),
    ("T", TLSF_SWEEP_T),
    # The compute-matched control, on the same terms as the FRETISH grid's S:
    # TLSF_SWEEP_R is SWEEP_R, so make_sweep_s already emits the right levels
    # and main() rebuilds this entry from --compute-match-factor whichever table
    # was selected. Registered here because 2026-08-11-selection-default asks
    # its question on both paths, and a compute control on one of them would
    # leave the other unable to separate a better search from a longer one.
    ("S", make_sweep_s(DEFAULTS["generations"], DEFAULT_COMPUTE_MATCH_FACTOR)),
]

TLSF_CONFIGS_DIR = Path(__file__).parent.parent / "experiments" / "configs-tlsf"

# Default per-call ltlsynt timeout (ms) for the TLSF campaign. ltlsynt has no
# internal timeout, and these specs occasionally produce synthesis queries that
# run for minutes; without a bound one such query stalls a whole run. A
# timed-out check is undecided: it admits no repair, and drops the candidate at
# the well-separation filter.
#
# This was 500ms, on the measured call-duration distribution: sharply bimodal,
# 95% of calls under 50ms, only ~2% past 100ms, an almost-empty 0.5-1s band, so
# 500ms and 10s abandon nearly the same set (~0.1% of calls apart) while 500ms
# caps the pathological tail far tighter. Every one of those figures counts
# calls pooled across the corpus, and pooling is what made 500ms look free. It
# hides the per-spec case: where a spec's *own* realizability query exceeds the
# budget, it does not shed 0.1% of its calls, it sheds all of them. amba is that
# spec — 1.5s for the unrealizable original, 6.1s to prove its known-good
# repair realizable — so every amba run in every campaign generated at 500ms
# scored a constant status objective and had its repairs rejected at the final
# gate. Re-run at a budget that decides it, amba yields; its 96/96 zero-yield
# across the archive measures this, not the search.
#
# 10s matches the C++ default (Config::ltlsynt_timeout) and clears the corpus
# worst case by a wide margin while still cutting the minutes-long tail. One
# query may now run 20x longer than it could before, so the per-spec
# `timeout_caps` in run_experiments.py — calibrated while 500ms bounded every
# query — need re-checking before a launch, amba's above all.
TLSF_LTLSYNT_TIMEOUT_MS = 10000

# Default per-call ltl2tgba (model-counting) timeout (ms) for the TLSF campaign.
# The counting path has no internal timeout and its -D determinization blows up
# super-exponentially on some deep formulae (observed multi-GB, hours-long, then
# orphaned). 60 s is generous — legitimate counts finish in milliseconds to ~20 s
# — while cutting the pathological tail. A timed-out count drops that individual
# (absorbed by max_scoring_failure_rate). Needs the ltl2tgba-timeout binary fix.
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
    # TLSF-only sweeps (e.g. M) are selectable but excluded from the default set,
    # so `--tlsf` alone still emits only A/B and an explicit `--sweeps M` is
    # needed to reach them.
    tlsf_only_sweeps = [n for n, _ in TLSF_SWEEPS if n not in dict(SWEEPS)]
    parser.add_argument("--sweeps", nargs="+",
                        choices=[n for n, _ in SWEEPS] + tlsf_only_sweeps,
                        default=[n for n, _ in SWEEPS], metavar="SWEEP",
                        help="Sweeps to emit (default: all FRETISH sweeps; "
                             f"TLSF-only: {' '.join(tlsf_only_sweeps)})")
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
                             f"{', '.join(WEAKENINGS)}). Omit for the flat "
                             "layout, which run_experiments.py attributes to "
                             "the legacy wkon")
    parser.add_argument("--metric", choices=list(METRICS), default=None,
                        metavar="METRIC",
                        help="Cross model_counting.metric in as a factor, "
                             "writing <scheme>/[<weakening>/]<direct|log>/ "
                             f"(choices: {', '.join(METRICS)}). Omit to keep the "
                             "flat layout and take metric from the defaults")
    parser.add_argument("--repair", choices=list(REPAIRS), default=None,
                        metavar="MODE",
                        help="Cross tlsf.repair_mode in as a factor, writing "
                             "<scheme>/[<weakening>/][<metric>/]<mono|muc>/ "
                             f"(choices: {', '.join(REPAIRS)}). Omit to keep the "
                             "flat layout and take repair_mode from the defaults")
    parser.add_argument("--out-dir", type=Path, default=CONFIGS_DIR, metavar="PATH",
                        help=f"Directory to write <scheme>/ dirs into (default: "
                             f"{CONFIGS_DIR})")
    parser.add_argument("--compute-match-factor", type=float,
                        default=DEFAULT_COMPUTE_MATCH_FACTOR, metavar="X",
                        help=f"Generation-budget multiplier for sweep S, the "
                             f"compute-matched control arm (default: "
                             f"{DEFAULT_COMPUTE_MATCH_FACTOR}). Set it to the "
                             f"cost ratio measured in calibration")
    parser.add_argument("--dashboard", action="store_true",
                        help="Emit runtime.dashboard = true, so each run writes "
                             "progress.jsonl (per-stage distinct-individual "
                             "counts). Costs a flushed write per stage; leave "
                             "off for a full campaign, on for a subset")
    parser.add_argument("--tlsf", action="store_true",
                        help="Emit the TLSF campaign grid: a coarse "
                             "generations x population cross (sweeps A, B) for "
                             "nsga2-truncate only, into configs-tlsf. Unless "
                             "overridden, sets --schemes nsga2-truncate and "
                             "--out-dir configs-tlsf")
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
            schemes = ["nsga2-truncate"]
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
    # Sweep S's levels depend on the operating point and the match factor, so
    # the tabulated placeholder is rebuilt here against the actual arguments.
    sweep_table = [
        ("S", make_sweep_s(args.generations, args.compute_match_factor))
        if name == "S" else (name, levels)
        for name, levels in sweep_table]
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
    # (subdirectory, metric override). None ⇒ flat layout with the metric from
    # DEFAULTS, mirroring the weakening default. The metric directory nests
    # below the weakening one: <scheme>/[<weakening>/]<direct|log>/.
    metrics: list[tuple[str | None, dict]] = (
        [(d, {"metric": v}) for d, v in METRICS[args.metric]]
        if args.metric else [(None, {})]
    )
    # (subdirectory, repair_mode override). None ⇒ flat layout with repair_mode
    # from DEFAULTS, mirroring weakening/metric. The repair directory nests
    # below the metric one: <scheme>/[<weakening>/][<metric>/]<mono|muc>/.
    repairs: list[tuple[str | None, dict]] = (
        [(d, {"repair_mode": v}) for d, v in REPAIRS[args.repair]]
        if args.repair else [(None, {})]
    )

    # Keys whose C++ default has moved at least once, written out explicitly so
    # the archive states them rather than inheriting whatever the binary means
    # by them next year. Merged first, so a sweep varying one of these on
    # purpose still wins — sweeps G and T each do.
    pinned = {k: defaults[k] for k in VINTAGE_KEYS} if args.pin_vintage else {}

    count = 0
    for scheme in schemes:
        for wk_dir, wk_override in weakenings:
            for mx_dir, mx_override in metrics:
                for rp_dir, rp_override in repairs:
                    out = out_dir / scheme
                    for seg in (wk_dir, mx_dir, rp_dir):
                        if seg is not None:
                            out = out / seg
                    out.mkdir(parents=True, exist_ok=True)
                    for sweep_name, levels in sweeps:
                        for level_name, overrides in levels:
                            path = out / f"sweep_{sweep_name}_{level_name}.toml"
                            path.write_text(make_toml(
                                {**pinned, **overrides,
                                 "selection_scheme": scheme,
                                 **wk_override, **mx_override, **rp_override},
                                defaults))
                            count += 1
                    label = "/".join(p for p in (scheme, wk_dir, mx_dir, rp_dir)
                                     if p is not None)
                    print(f"  {label:24} "
                          f"{len(list(out.glob('sweep_*.toml'))):3} configs")
    print(f"\nGenerated {count} config files in {out_dir}")


if __name__ == "__main__":
    main()
