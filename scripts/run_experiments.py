#!/usr/bin/env python3
"""Run parameter sweep experiments and collect metrics to a results CSV.

A profile selects which (sweep, level, spec, seed) combinations run and which
CSV they land in (see PROFILES below and scripts/README.md). --profile is
required: there is no default campaign.

Usage:
    python scripts/run_experiments.py --profile tlsf                # one profile
    python scripts/run_experiments.py --profile tlsf --jobs 8       # 8 parallel runs
    python scripts/run_experiments.py --profile tlsf --sweeps A     # specific sweeps
    python scripts/run_experiments.py --profile tlsf --specs lift   # specific specs
    python scripts/run_experiments.py --profile tlsf --seeds 0 1 2  # specific seeds
    python scripts/run_experiments.py --profile tlsf --dry-run      # print plan only
    python scripts/run_experiments.py --profile tlsf --no-resume    # ignore results

Every row records the commit the PEREDUR binary was built from, read once at
startup from `peredur --version` rather than from the working tree — the two
diverge whenever a fix lands without a rebuild. A launch whose binary does not
match HEAD, or was built dirty, is refused; --allow-stale-binary downgrades
that to a warning. A per-host manifest lands beside the results CSV with the
branch, describe, binary commits and the sweep the launch asked for.
"""

import argparse
import csv
import json
import math
import os
import re
import socket
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO_ROOT = Path(__file__).parent.parent
EXPERIMENTS_DIR = REPO_ROOT / "experiments"
CONFIGS_DIR = EXPERIMENTS_DIR / "configs"
RESULTS_DIR = EXPERIMENTS_DIR / "results"

# Overridable so a checkout without a release build (e.g. a worktree) can
# point at another checkout's binaries.
PEREDUR_BIN = Path(os.environ.get("PEREDUR_BIN",
                                  REPO_ROOT / "build-release" / "peredur"))
COMPARE_BIN = Path(os.environ.get("COMPARE_BIN",
                                  REPO_ROOT / "build-release" / "compare"))

EXAMPLES_DIR = REPO_ROOT / "examples"


def _spec(name: str, ext: str) -> dict[str, Path]:
    return {"input": EXAMPLES_DIR / name / f"spec.{ext}",
            "ideals_dir": EXAMPLES_DIR / name / "fixes"}


# FRETISH (JSON) specs: the original repair benchmark.
# mode-arbiter is the one scoped family, and the only run that exercises the
# scope machinery at all: every other example here is global-scoped and declares
# no modes. Its single ideal, fixes/guard-maintenance.json, is out of reach of
# the search -- the fix guards a condition with the mode `maintenance`, and the
# mutation atom pool is m_in_atoms + m_out_atoms, with m_modes reaching
# mutate_requirement only as the scope arm's mode_pool -- so it contributes
# implies_ideal = 0 by construction and is carried for found_repair and
# n_repairs rather than for the quality endpoint. lint-ideals reports
# `reachable ok` here, which is not evidence: check_fretish_reachable compares
# list lengths only, and its comment predates m_scope.
FRETISH_SPECS: dict[str, dict[str, Path]] = {
    "takeoff": _spec("takeoff", "json"),
    "fsm": _spec("fsm", "json"),
    "fsm-timing": _spec("fsm-timing", "json"),
    "fsm-combined": _spec("fsm-combined", "json"),
    "mode-arbiter": _spec("mode-arbiter", "json"),
}

# Basic-TLSF specs with ideal fixes. PEREDUR infers the TLSF format from the
# .tlsf extension, and compare reads the .tlsf ideals the same way. The first
# .tlsf extension, and compare reads the .tlsf ideals the same way.
# takeoff-tlsf is the AuRUS takeoff case study as TLSF — a separate family
# from the FRETISH examples/takeoff/ that FRETISH_SPECS names. It is inert: no
# profile runs it, since both upstream "genuine" fixes are invalid, leaving it
# without ideals.
TLSF_SPECS: dict[str, dict[str, Path]] = {
    "arbiter": _spec("arbiter", "tlsf"),
    "gyro-var1": _spec("gyro-var1", "tlsf"),
    "humanoid-531": _spec("humanoid-531", "tlsf"),
    "lift": _spec("lift", "tlsf"),
    "lily02": _spec("lily02", "tlsf"),
    "minepump": _spec("minepump", "tlsf"),
    "gyro-var2": _spec("gyro-var2", "tlsf"),
    "humanoid-458": _spec("humanoid-458", "tlsf"),
    "amba": _spec("amba", "tlsf"),
    "codesample-un1": _spec("codesample-un1", "tlsf"),
    "codesample-un2": _spec("codesample-un2", "tlsf"),
    "rg1": _spec("rg1", "tlsf"),
    "rg2": _spec("rg2", "tlsf"),
    "takeoff-tlsf": _spec("takeoff-tlsf", "tlsf"),
    "arbiter-aurus": _spec("arbiter-aurus", "tlsf"),
    # SYNTCOMP arbiter family. Its ideals are hand-written system weakenings.
    "arbiter-handshake": _spec("arbiter-handshake", "tlsf"),
    "detector": _spec("detector", "tlsf"),
    "full-arbiter": _spec("full-arbiter", "tlsf"),
    "load-balancer": _spec("load-balancer", "tlsf"),
    "prioritized-arbiter": _spec("prioritized-arbiter", "tlsf"),
    "round-robin-arbiter": _spec("round-robin-arbiter", "tlsf"),
    "simple-arbiter": _spec("simple-arbiter", "tlsf"),
    # Imported from AuRUS's case-studies/syntcomp-unreal/. Its upstream
    # "genuine" repairs are other Acacia+ instances rather than weakenings of
    # it, so the ideal beside it is hand-written.
    "ltl2dba27": _spec("ltl2dba27", "tlsf"),
    # The full-TLSF half of the AuRUS corpus. PEREDUR's parser rejects the
    # GLOBAL block these carry, so each spec.tlsf is the `syfco -f basic`
    # lowering of its source rather than a copy of it. Six take their ideal
    # from the upstream genuine/ directory; ltl2dba-r-2 and ltl2dba-theta-2
    # have no upstream reference and their ideals are hand-written. The -aurus
    # suffix is load-bearing: this project already has same-named families at
    # different SYNTCOMP parameter instances, and they are not these.
    "detector-aurus": _spec("detector-aurus", "tlsf"),
    "full-arbiter-aurus": _spec("full-arbiter-aurus", "tlsf"),
    "load-balancer-aurus": _spec("load-balancer-aurus", "tlsf"),
    "prioritized-arbiter-aurus": _spec("prioritized-arbiter-aurus", "tlsf"),
    "round-robin-arbiter-aurus": _spec("round-robin-arbiter-aurus", "tlsf"),
    "simple-arbiter-aurus": _spec("simple-arbiter-aurus", "tlsf"),
    "ltl2dba-r-2": _spec("ltl2dba-r-2", "tlsf"),
    "ltl2dba-theta-2": _spec("ltl2dba-theta-2", "tlsf"),
    # The basic-TLSF half of the same corpus, byte-identical to its AuRUS
    # source. None has an upstream reference repair, so every ideal here is
    # hand-written. humanoid-741 is imported but absent from this table and
    # has no fixes/ dir: see H2H_UNSCOREABLE for why no valid ideal exists.
    "lily11": _spec("lily11", "tlsf"),
    "lily15": _spec("lily15", "tlsf"),
    "lily16": _spec("lily16", "tlsf"),
    "humanoid-503": _spec("humanoid-503", "tlsf"),
    "humanoid-742": _spec("humanoid-742", "tlsf"),
    "pcar-v2-888": _spec("pcar-v2-888", "tlsf"),
}

# The original six-family TLSF corpus. The tlsf profile pins this list rather
# than list(TLSF_SPECS), so extending the table above does not silently grow
# what it runs.
TLSF_CORE_SPECS: list[str] = [
    "arbiter", "gyro-var1", "humanoid-531", "lift", "lily02", "minepump",
]

# The head-to-head corpus IS the AuRUS paper's evaluation set: the 26
# specifications the paper's tables evaluate. Both arms target all 26, so this
# is the campaign's declared scope rather than whatever PEREDUR happens to be
# able to run today. The paths live in `SPEC_TLSF` in
# scripts/aurus_campaign.py and the two are held equal by
# test_experiment_paths.py; that module imports this one, so the names are
# written here and the paths there rather than either importing the other.
#
# amba, codesample-un1 and codesample-un2 are not rows in the paper's tables,
# so they are not in this list. They keep their PEREDUR families and ideals
# for the ablation corpus.
#
# takeoff-tlsf is out because both of its upstream "genuine" fixes are invalid
# (one truncated, one unsatisfiable).
H2H_TLSF_SPECS: list[str] = [
    # Literature (5)
    "arbiter-aurus", "lift", "minepump", "rg1", "rg2",
    # SYNTCOMP (13)
    "detector-aurus", "full-arbiter-aurus", "lily02", "lily11", "lily15",
    "lily16", "load-balancer-aurus", "ltl2dba-r-2", "ltl2dba-theta-2",
    "ltl2dba27", "prioritized-arbiter-aurus", "round-robin-arbiter-aurus",
    "simple-arbiter-aurus",
    # SYNTECH15 (8)
    "gyro-var1", "gyro-var2", "humanoid-458", "humanoid-503", "humanoid-531",
    "humanoid-741", "humanoid-742", "pcar-v2-888",
]

# The gap between the declared scope above and what PEREDUR can currently run,
# named rather than silently subtracted. A family lands here until it has an
# `examples/<name>/` with a spec and at least one lint-clean ideal; the PEREDUR
# arm runs `H2H_TLSF_SPECS` minus this list, and `test_experiment_paths.py`
# asserts the two partition exactly, so importing a family fails that test
# until it is struck off here.
#
# Nothing here blocks the AuRUS arm, which runs all 26 regardless and archives
# every out.txt, so an import re-scores runs that already exist.
H2H_PENDING_IMPORT: list[str] = []

# Imported, but permanently unscoreable, so held out of the PEREDUR arm rather
# than left pending as though an import would fix it.
#
# humanoid-741's own input is ill-separated: both ASSUME conjuncts constrain
# the environment relative to the robot's own outputs, and the robot can force
# them false (entering move mode 11 once and never returning defeats
# `G(nmm=11 -> F(!obstacle && nmm=11))`). That is unreachable for the operators
# to fix, not merely hard. Well-separation reads the assumption side alone, so
# removing a guarantee cannot affect it, and the only assumption-side move is
# appending an ASSUME conjunct -- which strengthens the conjunction and can
# only make it easier to force false. Every descendant is therefore
# ill-separated too, the output gate rejects all of them, and the family would
# contribute a guaranteed zero to the PEREDUR arm while costing a full seed
# sweep to produce it.
#
# Note this is not the FRETISH input-screen case in docs/dev/algorithm.md, where
# an ill-separated input is warned about rather than rejected because a
# descendant may fix the property. On the TLSF assumption side no descendant
# can. A semantically sound repair does exist -- dropping the `G(!next_head)`
# ASSERT that `mucs` names makes it realizable -- but it is outside the
# operators' image, so no ideal built on it would be a fair target either.
H2H_UNSCOREABLE: list[str] = ["humanoid-741"]

# What the PEREDUR arm can run right now.
H2H_TLSF_READY: list[str] = [
    s for s in H2H_TLSF_SPECS
    if s not in H2H_PENDING_IMPORT and s not in H2H_UNSCOREABLE
]

# The corpus of the `h2h-tlsf` profile, frozen so that changing the live list
# above cannot change what that profile means. Written out rather than derived
# from H2H_TLSF_SPECS, so a change to the live corpus cannot carry over.
H2H_TLSF_SPECS_2026_07: list[str] = [
    "gyro-var1", "gyro-var2", "humanoid-458", "humanoid-531", "lift",
    "lily02", "minepump", "codesample-un1", "codesample-un2", "rg1", "rg2",
    "arbiter-aurus",
]

# The arbiter-probe corpus: every fixes-backed family whose unrealizability is
# an arbitration conflict -- the seven arbiters, plus amba (a bus arbiter) and
# load-balancer (arbitration over servers).
#
# lily02 is the tenth and is not an arbiter. It is the counter-signal control:
# a probe that only sampled families the scheme helps would report the upside
# alone.
ARBITER_PROBE_SPECS: list[str] = [
    "arbiter", "arbiter-aurus", "arbiter-handshake", "full-arbiter",
    "prioritized-arbiter", "round-robin-arbiter", "simple-arbiter",
    "amba", "load-balancer",
    "lily02",
]

# Unified lookup for run_one()/--specs; each profile picks its own subset.
SPECS: dict[str, dict[str, Path]] = {**FRETISH_SPECS, **TLSF_SPECS}

# compare runs n_repairs * n_ideals * 2 implication checks, each with compare's
# own 20s black budget. fsm-timing (bounded-interval operators) is slow, and
# repair counts grow with generations/population, so keep this generous.
# A profile whose arms differ in repair count should raise it via its own
# `compare_timeout` key -- see the compare_timed_out column below.
COMPARE_TIMEOUT_S = 600

CSV_FIELDS = [
    "sweep", "level_name", "level_value", "selection", "weakening", "metric",
    "repair_mode", "spec", "seed", "found_repair", "n_repairs", "best_fitness",
    "best_relation", "implies_ideal", "n_implies", "wall_time_s",
    "timed_out", "n_dropped", "compare_timed_out", "commit", "dirty",
]

# Rows written before `selection` existed are all NSGA-II. Defaulting them to
# "nsga2" keeps resume working against those CSVs instead of re-running them.
LEGACY_SELECTION = "nsga2"

# The two NSGA-II schemes were renamed -- "nsga2" -> "nsga2-truncate" and
# "nsga2-replicate" -> "nsga2-apportion" -- and the binary rejects the old
# spellings outright. Only the name moved: the scheme each one selects is
# unchanged, so a row recorded under either spelling names the same cell of the
# design. Canonicalising every `selection` value as it is read back -- from an
# archived CSV on resume, or from a config directory name -- is what keeps old
# rows matching the plan instead of missing it and re-running finished
# campaigns.
# gen_configs.py emits the new names only; this mapping is read-side alone.
SCHEME_ALIASES: dict[str, str] = {
    "nsga2": "nsga2-truncate",
    "nsga2-replicate": "nsga2-apportion",
}


def canonical_scheme(name: str) -> str:
    """Current spelling of a selection scheme; any other name passes through."""
    return SCHEME_ALIASES.get(name, name)


# Rows written before `weakening` existed ran configs with run_weakening = true
# (the DEFAULTS value). Same purpose as LEGACY_SELECTION: keep resume matching
# those rows rather than re-running them.
LEGACY_WEAKENING = "wkon"

# Directory names gen_configs.py --weakening emits. A config directly under
# <scheme>/ predates the factor and is read as LEGACY_WEAKENING.
WEAKENING_DIRS: tuple[str, ...] = ("wkon", "wkoff")

# Rows written before `metric` existed ran the direct similarity metric (the
# config.hpp / DEFAULTS value; model_counting.metric did not yet exist, so the
# binary always scored directly). Same purpose as LEGACY_SELECTION: keep resume
# and merge matching those rows rather than re-running them.
LEGACY_METRIC = "direct"

# Metric directory names (the short label, not the TOML value "logarithmic").
# gen_configs.py now writes log/ under every config; direct/ survives in archived
# grids. A config with no such ancestor predates the factor and is read as
# LEGACY_METRIC.
METRIC_DIRS: tuple[str, ...] = ("direct", "log")

# Rows written before `repair_mode` existed ran monolithic repair (the
# config.hpp / DEFAULTS value; tlsf.repair_mode did not yet exist, so the binary
# always evolved the whole spec). Same purpose as LEGACY_SELECTION: keep resume
# and merge matching those rows rather than re-running them.
LEGACY_REPAIR = "mono"

# Repair-mode directory names (the short label, not the TOML value
# "monolithic"). Only archived grids carry one; gen_configs.py writes none, since
# monolithic repair is the only mode left, and a config with no such ancestor is
# read as LEGACY_REPAIR.
REPAIR_DIRS: tuple[str, ...] = ("mono", "muc")

# Rows written before `commit` existed have no recorded commit at all, and a
# binary's commit is not recoverable after the fact. Unlike LEGACY_SELECTION
# this is an admission of ignorance rather than a known value, so it must never
# join the resume or merge key: keying on it would make every archived row miss
# and re-run whole campaigns.
LEGACY_COMMIT = "unknown"

# Every factor directory name, so scheme_of can tell a scheme segment apart
# from a factor segment regardless of nesting order or depth.
FACTOR_DIRS: frozenset[str] = (frozenset(WEAKENING_DIRS) | frozenset(METRIC_DIRS)
                               | frozenset(REPAIR_DIRS))

# Higher index = better relation
RELATION_PRIORITY: dict[str, int] = {
    "equivalent":       4,
    "strictly stronger": 3,
    "strictly weaker":  2,
    "incomparable":     1,
    "timeout":          0,
    "none":            -1,
}

SUMMARY_RE = re.compile(
    r"Summary:\s*(\d+) equivalent,\s*(\d+) strictly stronger,"
    r"\s*(\d+) strictly weaker,\s*(\d+) incomparable,\s*(\d+) timeout"
)
PER_REPAIR_RE = re.compile(
    r"^\S.*?\s+:\s+(equivalent|strictly stronger|strictly weaker|incomparable|timeout)"
)
# PEREDUR's scoring report (src/repair/reports.cpp print_scoring_report).
# That report prints unconditionally, unlike the rest of PEREDUR's end-of-run
# counters, precisely because this regex reads it out of the log. Individuals
# whose fitness scoring throws are dropped from their generation rather than
# aborting the run, up to Config::max_scoring_failure_rate. The report is
# silent when nothing was dropped, so an absent match means zero — but a run
# that dropped individuals evolved a thinner population than it asked for, and
# without this column that is indistinguishable from a clean run.
DROPPED_RE = re.compile(r"^(\d+) individual\(s\) dropped", re.MULTILINE)


# ── Baseline aliasing ────────────────────────────────────────────────────────

# Each sweep holds every other parameter at its default, so each one's default
# level is byte-identical to the canonical baseline. A canonical run executes at
# most once and emits one CSV row per requested alias — the rows differ only in
# sweep/level_name/level_value.
#
# Aliasing is always within a scheme: one scheme's B/pop200 aliases onto the
# same scheme's A/gen10, never onto another's. The byte-identity check below
# enforces that, since the configs differ on selection_scheme.
#
# The canonical is per-profile: it must be a level the profile actually runs.
# The tlsf profile runs only sweeps A (generations) and B (population), which
# cross at the gen10/pop200 baseline, so B/pop200 aliases onto A/gen10.
TLSF_ALIASES: dict[tuple[str, str], tuple[str, str]] = {
    ("B", "pop200"): ("A", "gen10"),
}


# ── Profiles ─────────────────────────────────────────────────────────────────

# Each profile selects a subset of the configs generated by gen_configs.py.
# `schemes` picks which <configs_dir>/<scheme>/ directories are read.
# `weakenings` picks the <scheme>/<wkon|wkoff>/ directories under those; None
# means the profile predates the factor and reads the flat <scheme>/ layout,
# recording every row as LEGACY_WEAKENING.
# `metrics` picks the <direct|log> directories nested below (after weakening,
# where present); None means the flat layout, recorded as LEGACY_METRIC.
# `repair_modes` picks the <mono|muc> directories nested below the metric one;
# None means the flat layout, recorded as LEGACY_REPAIR.
# `sweeps` is the set run by default (--sweeps overrides it, so a profile can
# hold back a sweep until asked). `levels` restricts a sweep to named levels;
# sweeps absent from the map keep every level found in `configs_dir`.
# `timeout_caps` is a per-spec cap in seconds, and must name every spec the
# profile runs.
#
# The generator lines quoted with each profile below ran before gen_configs.py
# stopped writing nsga2-truncate, the direct metric and the flat metric layout.
# At this commit they no longer reproduce those grids: regenerate one at the
# commit its campaign archive names.
#
# `configs_dir` and `results_dir` are per-profile because run_id names collide
# across profiles: the same (sweep, level, scheme, spec, seed) is one directory
# name regardless of operating point, so two profiles sharing a results_dir
# would have parse_repair_files() read the other's stale repair_*.json.
PROFILES: dict[str, dict] = {
    # The basic-TLSF examples swept over generations (A) and population (B), the
    # two operating-point parameters, NSGA-II only, default weakening. These
    # specs are much slower than the FRETISH ones and scale with
    # generations*population, so the grid is a coarse cross (four levels per
    # axis, sharing the baseline) rather than the fine FRETISH grid. Reads its
    # own experiments/configs-tlsf/ grid; generate it with
    #   python scripts/gen_configs.py --tlsf
    "tlsf": {
        "schemes": ["nsga2-truncate"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["A", "B"],
        "levels": {},
        "specs": list(TLSF_CORE_SPECS),
        # A generous ceiling, not a commitment. Ordering is seed-major, so a
        # machine killed at a wall-clock deadline leaves a balanced design at
        # whatever seed depth it reached; disjoint seed ranges per host keep a
        # truncated pair mergeable into one balanced dataset.
        "seeds": list(range(60)),
        # Flat per-spec caps generous enough that the largest operating point
        # (gen40 / pop500) never censors a slow-but-progressing run — a cap that
        # bites records implies_ideal = 0 for a run that merely ran long, which
        # corrupts the response variable.
        "timeout_caps": {"arbiter": 2400, "gyro-var1": 3600,
                         "humanoid-531": 3600, "lift": 3600,
                         "lily02": 1200, "minepump": 1200},
        "baseline_aliases": TLSF_ALIASES,
        "configs_dir": EXPERIMENTS_DIR / "configs-tlsf",
        "results_dir": EXPERIMENTS_DIR / "results-tlsf",
        "results_csv": EXPERIMENTS_DIR / "results-tlsf.csv",
        # jobs=1: one PEREDUR process per machine, using its full internal
        # thread pool. ltlsynt is multi-GB resident per call on these specs, so
        # running several PEREDUR processes at once (jobs>1) would multiply
        # peak RAM by jobs and risk an OOM.
        "default_jobs": 1,
    },
    # Head-to-head top-up against the AuRUS baseline (scripts/aurus_campaign.py):
    # the control cell only (nsga2 / log metric / C-default weights), at seeds
    # 0-19 over H2H_TLSF_SPECS_2026_07. It reads and writes the paths of the
    # retired ablate-tlsf profile, whose 2x2 factorial contained this cell, and
    # the resume key (sweep, level, selection, weakening, metric, repair_mode,
    # spec, seed) fully identifies a control-cell row, so the (spec, seed) rows
    # that campaign completed are skipped and only the top-up executes.
    # Its configs were generated with
    #   python scripts/gen_configs.py --sweeps C --levels default \
    #       --schemes nsga2-truncate weighted --metric both \
    #       --ltlsynt-timeout 500 --ltl2tgba-timeout 60000 \
    #       --max-scoring-failure-rate 0.15 \
    #       --out-dir experiments/configs-ablate-tlsf
    "h2h-tlsf": {
        "schemes": ["nsga2-truncate"],
        "weakenings": None,
        # ["log"] (not None): keeps the factor-cell key, run_id and CSV metric
        # column identical to the ablation's log cell, which is what makes the
        # shared-CSV dedup hold.
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["C"],
        "levels": {"C": ["default"]},
        "specs": H2H_TLSF_SPECS_2026_07,
        "seeds": list(range(20)),
        "timeout_caps": {s: 600 for s in H2H_TLSF_SPECS_2026_07},
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-ablate-tlsf",
        "results_dir": EXPERIMENTS_DIR / "results-ablate-tlsf",
        "results_csv": EXPERIMENTS_DIR / "results-ablate-tlsf.csv",
        "default_jobs": 1,
    },
    # The head-to-head at AuRUS's own budget. Separate from `h2h-tlsf` in
    # every path it writes, because it differs from that profile in the two
    # things the resume key does not carry: the corpus and the wall cap.
    # Sharing the ablation's CSV as `h2h-tlsf` does would put rows from both
    # caps under one key and let the merge keep whichever arrived first.
    #
    # 7200s matches AuRUS's `--gato 7200`, the published 2 h budget, so
    # neither tool's figure needs a budget caveat.
    "aurus-h2h": {
        # Both of these track the built-in defaults rather than pinning an
        # arm, because a head-to-head has to run PEREDUR as it ships: a
        # baseline comparison against a configuration no user gets by
        # default measures the wrong thing. Re-check both against
        # include/config.hpp whenever a default moves.
        "schemes": ["nsga2-apportion"],
        "weakenings": None,
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["C"],
        "levels": {"C": ["default"]},
        # H2H_TLSF_READY, not H2H_TLSF_SPECS: the campaign's declared scope is
        # all 26, and the PEREDUR arm runs the subset already imported. The
        # remainder is named in H2H_PENDING_IMPORT rather than subtracted
        # silently, and topping up as families land is a resume rather than a
        # re-run, (spec, seed) being in the key.
        "specs": H2H_TLSF_READY,
        "seeds": list(range(20)),
        "timeout_caps": {s: 7200 for s in H2H_TLSF_READY},
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-aurus-h2h",
        "results_dir": EXPERIMENTS_DIR / "results-aurus-h2h",
        "results_csv": EXPERIMENTS_DIR / "results-aurus-h2h.csv",
        "default_jobs": 1,
    },
    # Does nsga2-apportion's diversity unlock the arbiter family, or only the
    # one `arbiter` spec? Both schemes, one level. Sweep R restricted to
    # elit0.1 pins elitism_rate into the config at the shipped default rather
    # than inheriting it, so a later change of default cannot restate what
    # this profile runs.
    #
    # Generate the configs with
    #   python scripts/gen_configs.py --tlsf \
    #       --schemes nsga2-truncate nsga2-apportion --sweeps R --levels elit0.1 \
    #       --out-dir experiments/configs-arbiter-probe
    "arbiter-probe": {
        "schemes": ["nsga2-truncate", "nsga2-apportion"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["R"],
        "levels": {"R": ["elit0.1"]},
        "specs": list(ARBITER_PROBE_SPECS),
        # 40 seeds x 9 arbiter families = 360 pooled pairs: enough to separate
        # "unlocks the family" from "unlocked one spec".
        "seeds": list(range(40)),
        # A flat cap sized never to bite: a tighter cap would censor the
        # treatment arm (apportion) alone.
        "timeout_caps": {spec: 900 for spec in ARBITER_PROBE_SPECS},
        # apportion's whole claim is more distinct survivors, so it returns
        # more repairs and compare's cost scales with them; the 600 s default
        # would censor its comparisons.
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-arbiter-probe",
        "results_dir": EXPERIMENTS_DIR / "results-arbiter-probe",
        "results_csv": EXPERIMENTS_DIR / "results-arbiter-probe.csv",
        # jobs=1 for the same RAM reason as every other TLSF profile.
        "default_jobs": 1,
    },
}


def verify_aliases(configs_by_key: dict[tuple[str, str, str, str, str, str], Path],
                   scheme: str, weakening: str, metric: str, repair: str,
                   aliases: dict) -> dict:
    """Return the alias map restricted to pairs whose files are byte-identical."""
    active: dict[tuple[str, str], tuple[str, str]] = {}
    for alias, canon in aliases.items():
        a_path = configs_by_key.get((scheme, weakening, metric, repair, *alias))
        c_path = configs_by_key.get((scheme, weakening, metric, repair, *canon))
        if a_path is None:
            continue  # alias config not generated; nothing to alias
        if c_path is None or a_path.read_bytes() != c_path.read_bytes():
            print(f"WARN: {scheme}/{weakening}/{metric}/{repair}/{a_path.name} "
                  f"is not byte-identical to canonical "
                  f"sweep_{canon[0]}_{canon[1]}.toml — treating as distinct")
            continue
        active[alias] = canon
    return active


# ── Metadata ─────────────────────────────────────────────────────────────────

def level_value_of(level_name: str) -> int | float | str:
    """Trailing number if present ('gen10' → 10, 'ptrig0.75' → 0.75).

    The fractional form matters: matching only \\d+ would read 'ptrig0.75' as
    75 and sort it above 'ptrig0.5'.
    """
    m = re.search(r"(\d+\.\d+|\d+)$", level_name)
    if not m:
        return level_name
    return float(m.group(1)) if "." in m.group(1) else int(m.group(1))


def _factor_dir(config_path: Path, known: tuple[str, ...] | frozenset[str]):
    """First ancestor directory whose name is in `known`, or None.

    Scans ancestors rather than assuming a fixed depth, so a factor is found
    wherever it nests. With metric crossed in the layout is up to three deep
    (<scheme>/<weakening>/<metric>/sweep_*.toml), and a parent/parent.parent
    walk would mis-attribute the segments.
    """
    for p in config_path.parents:
        if p.name in known:
            return p.name
    return None


def scheme_of(config_path: Path) -> str:
    """Selection scheme: the first ancestor that is not a factor directory.

    Canonicalised, so a config tree generated under the old scheme names still
    reads back as the name the `selection` column now carries -- the same value
    load_done_set() derives from an archived CSV, which is what lets the two
    meet on the resume key.
    """
    for p in config_path.parents:
        if p.name not in FACTOR_DIRS:
            return canonical_scheme(p.name)
    return canonical_scheme(config_path.parent.name)


def weakening_of(config_path: Path) -> str:
    """Weakening state from the config's ancestor directories."""
    return _factor_dir(config_path, WEAKENING_DIRS) or LEGACY_WEAKENING


def metric_of(config_path: Path) -> str:
    """Similarity metric (short label) from the config's ancestor directories."""
    return _factor_dir(config_path, METRIC_DIRS) or LEGACY_METRIC


def repair_mode_of(config_path: Path) -> str:
    """Repair mode (short label) from the config's ancestor directories."""
    return _factor_dir(config_path, REPAIR_DIRS) or LEGACY_REPAIR


def extract_metadata(config_path: Path) -> tuple:
    """Return (sweep, level_name, level_value) from a config filename."""
    stem = config_path.stem  # e.g. 'sweep_A_gen10'
    parts = stem.split("_", 2)  # ['sweep', 'A', 'gen10']
    return parts[1], parts[2], level_value_of(parts[2])


# ── Parsing ───────────────────────────────────────────────────────────────────

def parse_repair_files(output_dir: Path) -> tuple[int, float]:
    """Return (n_repairs, best_fitness) from a run's repair files.

    FRETISH runs write repair_N.json with the weighted total nested under a
    "fitness" object. TLSF runs write repair_N.tlsf beside a repair_N.fitness.json
    sidecar carrying the total at top level. The sidecar also matches
    repair_*.json, so TLSF is detected first (by its .tlsf files) to avoid
    counting sidecars as repairs and reading a missing nested "fitness".
    """
    tlsf_files = list(output_dir.glob("repair_*.tlsf"))
    if tlsf_files:
        best = float("-inf")
        for f in tlsf_files:
            try:
                total = json.loads(
                    f.with_suffix(".fitness.json").read_text()).get("total")
                if total is not None:
                    best = max(best, float(total))
            except Exception:
                pass
        return len(tlsf_files), best if best != float("-inf") else float("nan")

    files = list(output_dir.glob("repair_*.json"))
    if not files:
        return 0, float("nan")
    best = float("-inf")
    for f in files:
        try:
            data = json.loads(f.read_text())
            total = (data.get("fitness") or {}).get("total")
            if total is not None:
                best = max(best, float(total))
        except Exception:
            pass
    return len(files), best if best != float("-inf") else float("nan")


def parse_dropped(log_path: Path) -> int:
    """Individuals PEREDUR dropped after a fitness function threw (0 if none)."""
    try:
        text = log_path.read_text(errors="replace")
    except OSError:
        return 0
    m = DROPPED_RE.search(text)
    return int(m.group(1)) if m else 0


def tail_line(log_path: Path) -> str:
    """Return the last non-blank line of a log, for console error context.

    PEREDUR draws progress with carriage returns, so a raw line may pack many
    updates; keep only the final segment after the last '\\r'.
    """
    try:
        text = log_path.read_text(errors="replace")
    except OSError:
        return ""
    for raw in reversed(text.splitlines()):
        line = raw.split("\r")[-1].strip()
        if line:
            return line
    return ""


def parse_compare_output(stdout: str) -> tuple[str, int, int]:
    """Return (best_relation, implies_ideal, n_implies) from compare stdout.

    implies_ideal is 1 when at least one repair is equivalent or strictly
    stronger than an ideal (i.e. the repair is at least as strong).
    """
    m = SUMMARY_RE.search(stdout)
    if not m:
        return "unknown", 0, 0

    groups = list(map(int, m.groups()))
    n_equiv, n_stronger = groups[0], groups[1]
    n_implies = n_equiv + n_stronger

    best_priority = -1
    best_relation = "timeout"
    for line in stdout.splitlines():
        lm = PER_REPAIR_RE.match(line.strip())
        if lm:
            rel = lm.group(1)
            if RELATION_PRIORITY.get(rel, -1) > best_priority:
                best_priority = RELATION_PRIORITY[rel]
                best_relation = rel

    return best_relation, int(n_implies > 0), n_implies


# ── Binary provenance ────────────────────────────────────────────────────────

def parse_version_output(text: str) -> dict[str, str]:
    """Parse `<binary> --version` output into its key=value pairs.

    The binary prints one `key=value` line per fact (commit, commit_short,
    dirty). Lines without a `=` are ignored, so a future banner line cannot
    break the parse.
    """
    fields: dict[str, str] = {}
    for line in text.splitlines():
        key, sep, value = line.partition("=")
        if sep:
            fields[key.strip()] = value.strip()
    return fields


def binary_version(bin_path: Path) -> dict[str, str]:
    """Ask a binary what it was built from. Once per campaign, not per run.

    Deliberately not `git rev-parse`: the working tree says what the source is
    now, and the binary says what it was compiled from. Those diverge every
    time a fix lands without a rebuild, and that gap is exactly what a recorded
    commit is for. A binary that predates --version exits non-zero and reads as
    LEGACY_COMMIT, which the staleness check then treats as stale — it is, by
    definition, older than this script.
    """
    unknown = {"commit": LEGACY_COMMIT, "commit_short": LEGACY_COMMIT,
               "dirty": ""}
    try:
        proc = subprocess.run([str(bin_path), "--version"], check=True,
                              timeout=60, capture_output=True, text=True)
    except (OSError, subprocess.SubprocessError):
        return unknown
    fields = parse_version_output(proc.stdout)
    if "commit" not in fields:
        return unknown
    return {**unknown, **fields}


def git_result(args: list[str], root: Path,
               timeout: float | None = None) -> tuple[str | None, str | None]:
    """``(stripped stdout, None)`` of `git -C root <args>`, or ``(None, why)``.

    `why` is the last line git wrote to stderr, or a note of the exit status
    when it wrote none. The except stays this wide on purpose: narrowing it to
    CalledProcessError turns a box without git into a crash at campaign launch
    rather than an unknown commit. campaign.py shares this for its own calls.
    """
    try:
        proc = subprocess.run(["git", "-C", str(root), *args],
                              timeout=timeout, capture_output=True, text=True)
    except (OSError, subprocess.SubprocessError) as exc:
        return None, f"git {args[0]}: {exc}"
    if proc.returncode != 0:
        tail = [ln.strip() for ln in proc.stderr.splitlines() if ln.strip()]
        return None, (tail[-1] if tail
                      else f"git {args[0]} exit {proc.returncode}")
    return proc.stdout.strip(), None


def git_output(*args: str) -> str | None:
    """Stripped stdout of `git -C REPO_ROOT <args>`, or None if there is none.

    One None covers every way the answer can be absent — git missing from
    PATH, a non-zero exit, a timeout, empty output — because each caller
    substitutes the same fallback for all of them.
    """
    out, _ = git_result(list(args), REPO_ROOT, timeout=60)
    return out or None


def working_tree_head() -> str | None:
    """HEAD of the checkout this script lives in, or None outside a work tree."""
    return git_output("rev-parse", "HEAD")


def git_describe() -> str:
    described = git_output("describe", "--always", "--dirty", "--tags")
    return described or LEGACY_COMMIT


def git_branch() -> str:
    branch = git_output("rev-parse", "--abbrev-ref", "HEAD")
    return branch or LEGACY_COMMIT


def staleness_problems(versions: dict[str, dict[str, str]],
                       head: str | None) -> list[str]:
    """Reasons the binaries must not be trusted to represent HEAD.

    Empty means every binary reports a clean build of the working tree's HEAD.
    """
    problems: list[str] = []
    for name, version in versions.items():
        if version["commit"] == LEGACY_COMMIT:
            problems.append(
                f"{name} does not report a commit: it predates --version, or "
                f"was built outside a git work tree")
            continue
        if version.get("dirty") == "1":
            problems.append(
                f"{name} was built from a modified working tree "
                f"({version['commit_short']}-dirty), so its commit does not "
                f"identify the code that ran")
        if head is not None and version["commit"] != head:
            problems.append(
                f"{name} was built from {version['commit_short']}, but the "
                f"working tree is at {head[:7]}")
    return problems


def enforce_binary_freshness(versions: dict[str, dict[str, str]],
                             head: str | None, allow_stale: bool) -> None:
    problems = staleness_problems(versions, head)
    if not problems:
        return
    bar = "!" * 72
    print(f"\n{bar}")
    print("STALE BINARY: the binaries do not match this working tree.")
    for problem in problems:
        print(f"  - {problem}")
    print("A campaign run against a binary that predates a fix produces "
          "numbers\nthat look valid and are not. Rebuild "
          "(cmake --workflow --preset release)\nor pass --allow-stale-binary "
          "if the mismatch is deliberate.")
    print(f"{bar}\n")
    if not allow_stale:
        sys.exit("Refusing to start with a stale binary.")
    print("Continuing anyway (--allow-stale-binary).\n")


def write_manifest(path: Path, profile_name: str, profile: dict,
                   versions: dict[str, dict[str, str]], head: str | None,
                   jobs: int, specs: list, seeds: list,
                   sweeps: set | None) -> None:
    """Record what this host ran, beside the CSV it appends to.

    The CSV carries the binary commit per row, but nothing else about the
    launch: which branch, which levels, how many workers. Reconstructing that
    afterwards from config mtimes is guesswork; this writes it down at run time
    instead.
    """
    manifest = {
        "profile": profile_name,
        "hostname": socket.gethostname(),
        "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "git": {
            "branch": git_branch(),
            "describe": git_describe(),
            "head": head or LEGACY_COMMIT,
        },
        "binaries": {
            name: {"path": str(bin_path), **versions[name]}
            for name, bin_path in (("peredur", PEREDUR_BIN),
                                   ("compare", COMPARE_BIN))
        },
        "sweep": {
            "schemes": profile["schemes"],
            "weakenings": profile["weakenings"],
            "metrics": profile["metrics"],
            "repair_modes": profile["repair_modes"],
            "sweeps": sorted(sweeps) if sweeps is not None else None,
            # Restricted to the sweeps this launch asked for: a profile's level
            # map covers sweeps a --sweeps run never touches, and recording
            # those makes the manifest describe a campaign that did not happen.
            "levels": {k: v for k, v in profile["levels"].items()
                       if sweeps is None or k in sweeps},
            "specs": list(specs),
            "seeds": list(seeds),
            "jobs": jobs,
            "configs_dir": str(profile["configs_dir"]),
            "results_dir": str(profile["results_dir"]),
            "results_csv": str(profile["results_csv"]),
        },
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2) + "\n")


def manifest_path(results_csv: Path) -> Path:
    """One manifest per host beside the CSV; campaigns run on av2 and av3 at
    once and merge into one file, so a shared name would have them overwrite
    each other."""
    host = socket.gethostname().split(".")[0]
    return results_csv.with_name(f"{results_csv.stem}-manifest-{host}.json")


# ── CSV helpers ───────────────────────────────────────────────────────────────

def load_done_set(csv_path: Path) -> set:
    done: set = set()
    if not csv_path.exists():
        return done
    with open(csv_path, newline="") as f:
        for row in csv.DictReader(f):
            done.add((row["sweep"], row["level_name"],
                      canonical_scheme(row.get("selection")
                                       or LEGACY_SELECTION),
                      row.get("weakening") or LEGACY_WEAKENING,
                      row.get("metric") or LEGACY_METRIC,
                      row.get("repair_mode") or LEGACY_REPAIR,
                      row["spec"], int(row["seed"])))
    return done


def existing_fieldnames(csv_path: Path) -> list | None:
    """Header of an existing results CSV, or None if absent/empty."""
    if not csv_path.exists():
        return None
    with open(csv_path, newline="") as f:
        return next(csv.reader(f), None)


def append_row(csv_path: Path, row: dict, fieldnames: list) -> None:
    write_header = not csv_path.exists() or csv_path.stat().st_size == 0
    with open(csv_path, "a", newline="") as f:
        # extrasaction='ignore' drops columns (e.g. timed_out) that a legacy
        # CSV's header does not have, keeping appends compatible.
        writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        if write_header:
            writer.writeheader()
        writer.writerow(row)


# ── Core runner ───────────────────────────────────────────────────────────────

def derive_config(config_path: Path, output_dir: Path, parallel_k: int) -> Path:
    """Write a copy of the level's TOML with `parallel = k` under [runtime].

    Caps PEREDUR's internal thread pool so that --jobs concurrent runs do not
    oversubscribe the machine (PEREDUR defaults to hardware_concurrency).

    A config that already states `parallel` is copied through unchanged. The cap
    exists to fill in for a config that says nothing, so that jobs * parallel
    stays near the core count; a stated value is the campaign's own claim about
    how each run is meant to execute, and overwriting it silently makes the
    thread pool a function of --jobs rather than of the design. gen_configs.py
    emits the key only under --parallel, which a campaign passes deliberately.
    """
    text = config_path.read_text()
    line = f"parallel = {parallel_k}"
    if not re.search(r"(?m)^\s*parallel\s*=", text):
        if re.search(r"(?m)^\[runtime\]\s*$", text):
            text = re.sub(r"(?m)^\[runtime\]\s*$", f"[runtime]\n{line}",
                          text, count=1)
        else:
            text = text.rstrip("\n") + f"\n\n[runtime]\n{line}\n"
    derived = output_dir / "config.toml"
    derived.write_text(text)
    return derived


def run_one(config_path: Path, sweep: str, level_name: str, spec_name: str,
            seed: int, timeout: int, results_dir: Path, parallel_k=None,
            run_id: str | None = None,
            compare_timeout: int = COMPARE_TIMEOUT_S) -> dict | None:
    """Execute PEREDUR (+ compare) once; return the metric columns.

    The returned dict carries spec/seed and all metric fields but no
    sweep/level/selection columns — the caller stamps those per emitted
    (alias) row.

    `run_id` names the output directory and must be unique per executed run;
    the caller passes one that includes every crossed factor (selection scheme,
    and weakening state where the profile crosses it), since the same
    (sweep, level, spec, seed) is run once per factor combination and two of
    them would otherwise share a directory and read each other's repair_*.json.
    `run_id` does not encode the operating point, so profiles that differ only
    in that must pass different `results_dir`s for the same reason.
    """
    spec = SPECS[spec_name]

    if run_id is None:
        run_id = f"sweep_{sweep}_{level_name}_{spec_name}_seed{seed:02d}"
    output_dir = results_dir / run_id
    output_dir.mkdir(parents=True, exist_ok=True)

    effective_config = config_path
    if parallel_k is not None:
        effective_config = derive_config(config_path, output_dir, parallel_k)

    cmd = [
        str(PEREDUR_BIN),
        "--input", str(spec["input"]),
        "--output-dir", str(output_dir),
        "--config", str(effective_config),
        "--seed", str(seed),
    ]

    log_path = output_dir / "run.log"

    t_start = time.monotonic()
    timed_out = False
    with open(log_path, "wb") as log_file:
        try:
            subprocess.run(
                cmd, check=True, timeout=timeout,
                stdout=log_file, stderr=subprocess.STDOUT,
            )
        except subprocess.TimeoutExpired:
            timed_out = True
            print(f"    [{run_id}] TIMEOUT after {timeout}s  (see {log_path})")
        except subprocess.CalledProcessError as e:
            log_file.flush()
            ctx = tail_line(log_path)
            print(f"    [{run_id}] ERROR: PEREDUR exited {e.returncode}"
                  f"  (see {log_path})" + (f"\n           {ctx}" if ctx else ""))
            return None
    wall = round(time.monotonic() - t_start, 2)

    n_repairs, best_fitness = parse_repair_files(output_dir)

    base = {
        "spec": spec_name,
        "seed": seed,
        "found_repair": int(n_repairs > 0),
        "n_repairs": n_repairs,
        "best_fitness": "" if math.isnan(best_fitness) else round(best_fitness, 6),
        "wall_time_s": wall,
        "timed_out": int(timed_out),
        "compare_timed_out": 0,
        "n_dropped": parse_dropped(log_path),
    }

    if n_repairs == 0 or timed_out:
        return {**base, "best_relation": "none", "implies_ideal": 0, "n_implies": 0}

    # compare's cost scales with n_repairs * n_ideals, so an arm that produces
    # more repairs hits the timeout more often. Scoring that as implies_ideal = 0
    # would read a *slower* comparison as a *worse* repair and bias the
    # comparison against exactly the arm under test, so the timeout is recorded
    # in its own column for the analysis to exclude rather than folded into the
    # response. A compare that failed for any other reason keeps the old
    # behaviour: it is a broken run, not a censored measurement.
    compare_timed_out = False
    try:
        result = subprocess.run(
            [str(COMPARE_BIN), "--repairs", str(output_dir),
             "--ideals", str(spec["ideals_dir"])],
            check=True, timeout=compare_timeout, capture_output=True, text=True,
        )
        best_rel, implies_ideal, n_implies = parse_compare_output(result.stdout)
    except subprocess.TimeoutExpired:
        print(f"    [{run_id}] WARN: compare timed out after {compare_timeout}s "
              f"on {n_repairs} repairs — recorded, not scored")
        compare_timed_out = True
        best_rel, implies_ideal, n_implies = "unknown", 0, 0
    except subprocess.CalledProcessError as e:
        print(f"    [{run_id}] WARN: compare failed — {e}")
        best_rel, implies_ideal, n_implies = "unknown", 0, 0

    return {**base, "best_relation": best_rel, "implies_ideal": implies_ideal,
            "n_implies": n_implies, "compare_timed_out": int(compare_timed_out)}


# ── Entry point ───────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--profile", choices=list(PROFILES), required=True,
                        help="Experiment profile")
    parser.add_argument("--jobs", type=int, metavar="N",
                        help="Concurrent runs (default: per profile)")
    parser.add_argument("--sweeps", nargs="+", metavar="SWEEP",
                        help="Sweeps to run (default: per profile)")
    parser.add_argument("--specs", nargs="+", choices=list(SPECS),
                        metavar="SPEC",
                        help="Specs to run (default: per profile)")
    parser.add_argument("--seeds", nargs="+", type=int, metavar="N",
                        help="Seeds to run (default: per profile)")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-resume", action="store_true",
                        help="Re-run even if a result already exists in the CSV")
    parser.add_argument("--allow-stale-binary", action="store_true",
                        help="Warn instead of refusing when the binaries were "
                             "not built from the working tree's HEAD")
    args = parser.parse_args()

    profile = PROFILES[args.profile]
    jobs = args.jobs if args.jobs else profile["default_jobs"]
    if jobs < 1:
        sys.exit("--jobs must be >= 1")
    specs = args.specs or profile["specs"]
    seeds = args.seeds if args.seeds is not None else profile["seeds"]
    results_csv: Path = profile["results_csv"]
    configs_dir: Path = profile["configs_dir"]
    results_dir: Path = profile["results_dir"]

    # Validate binaries (skipped for --dry-run so plans work without a build)
    head = working_tree_head()
    unknown_version = {"commit": LEGACY_COMMIT, "commit_short": LEGACY_COMMIT,
                       "dirty": ""}
    versions = {"peredur": dict(unknown_version),
                "compare": dict(unknown_version)}
    if not args.dry_run:
        for bin_path in [PEREDUR_BIN, COMPARE_BIN]:
            if not bin_path.exists():
                sys.exit(
                    f"Binary not found: {bin_path}\n"
                    f"Run: cmake --workflow --preset release"
                )
        # Once, at startup: every run of this campaign uses these binaries, so
        # asking per run would cost thousands of subprocesses to learn one fact.
        versions = {"peredur": binary_version(PEREDUR_BIN),
                    "compare": binary_version(COMPARE_BIN)}

    def version_label(name: str) -> str:
        version = versions[name]
        suffix = "-dirty" if version.get("dirty") == "1" else ""
        return f"{name} {version['commit_short']}{suffix}"

    print("=" * 64)
    print(f"  Profile: {args.profile}")
    print(f"    jobs:    {jobs}")
    print(f"    specs:   {', '.join(specs)}")
    print(f"    seeds:   {min(seeds)}-{max(seeds)} ({len(seeds)} seeds)")
    print(f"    configs: {configs_dir}")
    print(f"    runs:    {results_dir}")
    print(f"    results: {results_csv}")
    print(f"    binary:  {version_label('peredur')}, "
          f"{version_label('compare')}")
    print("=" * 64)

    if not args.dry_run:
        enforce_binary_freshness(versions, head, args.allow_stale_binary)

    # Collect config files
    if not configs_dir.exists():
        sys.exit(
            f"No configs found at {configs_dir}\n"
            f"Run: python scripts/gen_configs.py"
        )
    def _config_sort_key(p: Path):
        sweep, _, level_value = extract_metadata(p)
        numeric = isinstance(level_value, (int, float))
        return (scheme_of(p), weakening_of(p), metric_of(p), repair_mode_of(p),
                sweep, 0 if numeric else 1, level_value if numeric else 0,
                str(p))

    wanted_schemes = profile["schemes"]
    wanted_weakenings = profile["weakenings"]
    wanted_metrics = profile["metrics"]
    wanted_repairs = profile["repair_modes"]
    # Build the directory list by nesting each crossed factor a level deeper:
    # <scheme>/[<weakening>/][<metric>/][<repair>/]. A None factor means the
    # profile predates it and its segment is skipped, keeping the flat layout
    # readable.
    config_dirs = []
    for s in wanted_schemes:
        for w in (wanted_weakenings or [None]):
            for m in (wanted_metrics or [None]):
                for r in (wanted_repairs or [None]):
                    d = configs_dir / s
                    for seg in (w, m, r):
                        if seg is not None:
                            d = d / seg
                    config_dirs.append(d)
    all_configs = sorted(
        (c for d in config_dirs for c in d.glob("sweep_*.toml")),
        key=_config_sort_key)
    if not all_configs:
        sys.exit(
            f"No configs found under {configs_dir} for scheme(s) "
            f"{', '.join(wanted_schemes)}\nRun: python scripts/gen_configs.py"
        )
    configs_by_key = {
        (scheme_of(c), weakening_of(c), metric_of(c), repair_mode_of(c),
         *extract_metadata(c)[:2]): c
        for c in all_configs}

    if args.sweeps:
        wanted_sweeps = {s.upper() for s in args.sweeps}
    elif profile["sweeps"] is not None:
        wanted_sweeps = set(profile["sweeps"])
    else:
        wanted_sweeps = None  # every sweep found

    def selected(cfg: Path) -> bool:
        sweep, level_name, _ = extract_metadata(cfg)
        if wanted_sweeps is not None and sweep not in wanted_sweeps:
            return False
        allowed = profile["levels"].get(sweep)
        return allowed is None or level_name in allowed

    selected_configs = [c for c in all_configs if selected(c)]
    if not selected_configs:
        sys.exit("No matching config files found.")

    # Aliasing holds within one (scheme, weakening, metric, repair) cell:
    # wkoff/log's D/ptrig0.5 aliases onto wkoff/log's C/default, never onto
    # another cell's. The byte-identity check enforces it, since the configs
    # differ on the factor keys.
    # canonical_scheme, because configs_by_key is keyed by scheme_of(), which
    # canonicalises: a profile pointed at a config tree with the old directory
    # names would otherwise build its alias table under a key the lookup below
    # never asks for.
    factor_cells = [(canonical_scheme(s), w, m, r) for s in wanted_schemes
                    for w in (wanted_weakenings or [LEGACY_WEAKENING])
                    for m in (wanted_metrics or [LEGACY_METRIC])
                    for r in (wanted_repairs or [LEGACY_REPAIR])]
    active_aliases = {
        (s, w, m, r): verify_aliases(configs_by_key, s, w, m, r,
                                     profile["baseline_aliases"])
        for s, w, m, r in factor_cells}

    done = set() if args.no_resume else load_done_set(results_csv)

    # Build the plan as the set of desired result rows, grouped by the
    # canonical run that produces them: (sweep, level, spec, seed) →
    # list of (sweep, level_name) rows to emit (aliases share one execution).
    runs: dict[tuple, list] = {}
    n_rows = n_aliased = 0
    for cfg in selected_configs:
        scheme, weakening, metric, repair = (
            scheme_of(cfg), weakening_of(cfg), metric_of(cfg),
            repair_mode_of(cfg))
        sweep, level_name, _ = extract_metadata(cfg)
        canon = active_aliases[(scheme, weakening, metric, repair)].get(
            (sweep, level_name), (sweep, level_name))
        for spec_name in specs:
            for seed in seeds:
                n_rows += 1
                if canon != (sweep, level_name):
                    n_aliased += 1
                key = (scheme, weakening, metric, repair, canon[0], canon[1],
                       spec_name, seed)
                runs.setdefault(key, []).append((sweep, level_name))

    def row_key(key: tuple, sweep: str, level_name: str) -> tuple:
        scheme, weakening, metric, repair, _, _, spec_name, seed = key
        return (sweep, level_name, scheme, weakening, metric, repair,
                spec_name, seed)

    n_done = sum(
        1
        for key, row_list in runs.items()
        for sweep, level_name in row_list
        if row_key(key, sweep, level_name) in done
    )
    # Seed-major, so that killing the run at a wall-clock deadline leaves a
    # balanced design: every level sampled at the same seeds, just fewer of
    # them. The natural (config-major) order would instead finish the first
    # levels and leave the last ones at zero seeds, which is not analysable.
    #
    # Within a seed the order is spec, then the factor cell, so the arms of a
    # comparison run adjacently: at any kill point two cells of the same spec
    # and seed differ by at most one run. A profile whose factor is the thing
    # under test depends on that ordering for its design to survive an early
    # stop, not merely on its seeds being disjoint across hosts.
    def execution_order(key: tuple) -> tuple:
        (scheme, weakening, metric, repair, c_sweep, c_level,
         spec_name, seed) = key
        return (seed, spec_name, scheme, weakening, metric, repair,
                c_sweep, c_level)

    to_execute = sorted(
        (key for key, row_list in runs.items()
         if any(row_key(key, s, l) not in done for s, l in row_list)),
        key=execution_order,
    )
    print(f"Plan: {n_rows} result rows ({n_aliased} via aliasing), "
          f"{n_done} already done; {len(to_execute)} runs to execute")

    if args.dry_run:
        # Same order as `to_execute`. Printing the dict's config-major
        # insertion order instead would show every run of the first factor cell
        # before any of the second, which is what the ordering above exists to
        # avoid -- so the plan a launch is checked against would misreport the
        # one property (balance under an early stop) it is read to confirm.
        for key, row_list in sorted(runs.items(),
                                    key=lambda kv: execution_order(kv[0])):
            (scheme, weakening, metric, repair, c_sweep, c_level,
             spec_name, seed) = key
            for sweep, level_name in row_list:
                tags = []
                if (sweep, level_name) != (c_sweep, c_level):
                    tags.append(f"(alias of {c_sweep}/{c_level})")
                if row_key(key, sweep, level_name) in done:
                    tags.append("(skip)")
                # Names the config as it sits on disk, so a flat profile shows
                # no factor segment even though its rows record the defaults.
                cfg_dir = "/".join(
                    p for p in (scheme,
                                None if wanted_weakenings is None else weakening,
                                None if wanted_metrics is None else metric,
                                None if wanted_repairs is None else repair)
                    if p is not None)
                print(f"  {cfg_dir}/sweep_{sweep}_{level_name}"
                      f"  spec={spec_name}  seed={seed:02d}"
                      + ("  " + " ".join(tags) if tags else ""))
        return

    results_dir.mkdir(parents=True, exist_ok=True)
    results_csv.parent.mkdir(parents=True, exist_ok=True)

    manifest = manifest_path(results_csv)
    write_manifest(manifest, args.profile, profile, versions, head, jobs,
                   specs, seeds, wanted_sweeps)
    print(f"Manifest: {manifest}")

    fieldnames = existing_fieldnames(results_csv) or CSV_FIELDS
    for column in ["timed_out", "compare_timed_out", "weakening", "metric",
                   "repair_mode", "commit", "dirty"]:
        if column not in fieldnames:
            print(f"Note: {results_csv.name} predates the {column} column; "
                  f"appending without it")

    # Cap each run's internal thread pool so jobs * parallel ≈ core count.
    parallel_k = max(1, (os.cpu_count() or 1) // jobs) if jobs > 1 else None
    if parallel_k is not None:
        print(f"Per-run PEREDUR thread pool capped at parallel = {parallel_k}")

    lock = threading.Lock()
    state = {"completed": 0, "errors": 0, "rows_written": 0}
    n_exec = len(to_execute)
    t0 = time.monotonic()

    def execute(key: tuple) -> None:
        (scheme, weakening, metric, repair, c_sweep, c_level,
         spec_name, seed) = key
        cfg = configs_by_key[
            (scheme, weakening, metric, repair, c_sweep, c_level)]
        timeout = profile["timeout_caps"][spec_name]
        # A factor state joins run_id only where the profile crosses it: adding
        # it unconditionally would rename every existing run directory of the
        # profiles that predate the factor, orphaning their results.
        wk_tag = "" if wanted_weakenings is None else f"_{weakening}"
        mx_tag = "" if wanted_metrics is None else f"_{metric}"
        rp_tag = "" if wanted_repairs is None else f"_{repair}"
        run_id = (f"sweep_{c_sweep}_{c_level}_{scheme}{wk_tag}{mx_tag}{rp_tag}"
                  f"_{spec_name}_seed{seed:02d}")
        with lock:
            print(f"[start]      {run_id}  (timeout {timeout}s)", flush=True)

        result = run_one(cfg, c_sweep, c_level, spec_name, seed,
                         timeout, results_dir, parallel_k, run_id,
                         profile.get("compare_timeout", COMPARE_TIMEOUT_S))

        with lock:
            state["completed"] += 1
            n = state["completed"]
            elapsed = time.monotonic() - t0
            eta = elapsed / n * (n_exec - n)
            if result is None:
                state["errors"] += 1
                print(f"[{n}/{n_exec}]  {run_id}  FAILED"
                      f"  ETA {eta/60:.1f}min", flush=True)
                return
            for sweep, level_name in runs[key]:
                if row_key(key, sweep, level_name) in done:
                    continue
                row = {**result, "sweep": sweep, "level_name": level_name,
                       "level_value": level_value_of(level_name),
                       "selection": scheme, "weakening": weakening,
                       "metric": metric, "repair_mode": repair,
                       # The abbreviated hash, since this is a column a human
                       # reads; the manifest beside the CSV keeps the full one.
                       "commit": versions["peredur"]["commit_short"],
                       "dirty": versions["peredur"].get("dirty", "")}
                append_row(results_csv, row, fieldnames)
                done.add(row_key(key, sweep, level_name))
                state["rows_written"] += 1
            print(f"[{n}/{n_exec}]  {run_id}  done in {result['wall_time_s']}s"
                  f"  ETA {eta/60:.1f}min", flush=True)

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(execute, to_execute))

    elapsed_total = time.monotonic() - t0
    print(
        f"\nDone. {state['completed']} runs ({state['rows_written']} rows) "
        f"in {elapsed_total/60:.1f} min, {state['errors']} errors."
        f"\nResults: {results_csv}"
    )


if __name__ == "__main__":
    main()
