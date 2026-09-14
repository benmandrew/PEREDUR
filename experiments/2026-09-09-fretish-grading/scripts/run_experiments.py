#!/usr/bin/env python3
"""Run parameter sweep experiments and collect metrics to a results CSV.

A profile selects which (sweep, level, spec, seed) combinations run and which
CSV they land in (see scripts/README.md):
    full     — every level, all 4 specs, seeds 0-29 → experiments/results.csv

Usage:
    python scripts/run_experiments.py                    # the full sweep
    python scripts/run_experiments.py --profile full     # the same, explicitly
    python scripts/run_experiments.py --jobs 8           # 8 parallel runs
    python scripts/run_experiments.py --sweeps A B       # specific sweeps
    python scripts/run_experiments.py --specs takeoff    # specific specs
    python scripts/run_experiments.py --seeds 0 1 2      # specific seeds
    python scripts/run_experiments.py --dry-run          # print plan, no execution
    python scripts/run_experiments.py --no-resume        # ignore existing results

Every row records the commit the counter binary was built from, read once at
startup from `counter --version` rather than from the working tree — the two
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
COUNTER_BIN = Path(os.environ.get("COUNTER_BIN",
                                  REPO_ROOT / "build-release" / "counter"))
COMPARE_BIN = Path(os.environ.get("COMPARE_BIN",
                                  REPO_ROOT / "build-release" / "compare"))

EXAMPLES_DIR = REPO_ROOT / "examples"


def _spec(name: str, ext: str) -> dict[str, Path]:
    return {"input": EXAMPLES_DIR / name / f"spec.{ext}",
            "ideals_dir": EXAMPLES_DIR / name / "fixes"}


# FRETISH (JSON) specs: the original repair benchmark. repair_mode is TLSF-only
# and does not affect these, so the mono-vs-muc factor is never crossed over
# them — the "muc" profile uses the TLSF specs below.
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

# The four-family corpus every FRETISH profile before 2026-09-09 ran, frozen as
# its own name. mode-arbiter joined FRETISH_SPECS with the selection x grading
# campaign, and FRETISH_SPECS is the path registry rather than any campaign's
# corpus: a profile that spelled its specs as list(FRETISH_SPECS) would have
# silently gained a fifth family, widening the spec set a closed campaign is the
# record of. New profiles name the corpus they want.
FRETISH_SPECS_2026_07: list[str] = [
    "takeoff", "fsm", "fsm-timing", "fsm-combined",
]

# Basic-TLSF specs with ideal fixes. counter infers the TLSF format from the
# .tlsf extension, and compare reads the .tlsf ideals the same way. The first
# six are the original mono-vs-muc corpus; the rest were imported from the
# aurus tree (9e5fc08 and the takeoff-tlsf/arbiter-aurus follow-ups) for the
# ablation campaign. takeoff-tlsf is the AuRUS takeoff case study as TLSF — a
# separate family from the FRETISH examples/takeoff/ that FRETISH_SPECS names.
# It is inert: no profile runs it, since both upstream "genuine" fixes proved
# invalid (see EXPERIMENTS.md 2026-07-24), leaving it without ideals.
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
    # SYNTCOMP arbiter family, imported without ideals in 9e5fc08 and promoted
    # into the ablation corpus on 2026-07-24 when hand-written ideal repairs
    # landed (system-weakening fixes, realize-validated; see EXPERIMENTS.md).
    "arbiter-handshake": _spec("arbiter-handshake", "tlsf"),
    "detector": _spec("detector", "tlsf"),
    "full-arbiter": _spec("full-arbiter", "tlsf"),
    "load-balancer": _spec("load-balancer", "tlsf"),
    "prioritized-arbiter": _spec("prioritized-arbiter", "tlsf"),
    "round-robin-arbiter": _spec("round-robin-arbiter", "tlsf"),
    "simple-arbiter": _spec("simple-arbiter", "tlsf"),
    # Imported from AuRUS's case-studies/syntcomp-unreal/ for the 2026-08-14
    # head-to-head. Its upstream "genuine" repairs are other Acacia+ instances
    # rather than weakenings of it, so the ideal beside it is hand-written.
    "ltl2dba27": _spec("ltl2dba27", "tlsf"),
    # The full-TLSF half of the AuRUS corpus, imported 2026-08-17. counter's
    # parser rejects the GLOBAL block these carry, so each spec.tlsf is the
    # `syfco -f basic` lowering of its source rather than a copy of it. Six
    # took their ideal from the upstream genuine/ directory, where the repair
    # is the spec with its single injected guarantee deleted; ltl2dba-r-2 and
    # ltl2dba-theta-2 have no upstream reference at all and their ideals are
    # hand-written on the ltl2dba27 pattern. The -aurus suffix is load-bearing
    # throughout: this project already has same-named families at different
    # SYNTCOMP parameter instances, and they are not these.
    "detector-aurus": _spec("detector-aurus", "tlsf"),
    "full-arbiter-aurus": _spec("full-arbiter-aurus", "tlsf"),
    "load-balancer-aurus": _spec("load-balancer-aurus", "tlsf"),
    "prioritized-arbiter-aurus": _spec("prioritized-arbiter-aurus", "tlsf"),
    "round-robin-arbiter-aurus": _spec("round-robin-arbiter-aurus", "tlsf"),
    "simple-arbiter-aurus": _spec("simple-arbiter-aurus", "tlsf"),
    "ltl2dba-r-2": _spec("ltl2dba-r-2", "tlsf"),
    "ltl2dba-theta-2": _spec("ltl2dba-theta-2", "tlsf"),
    # The basic-TLSF half of the same corpus, imported 2026-08-17 verbatim --
    # these parse as they stand, so each spec.tlsf is byte-identical to its
    # AuRUS source. None had an upstream reference repair, so every ideal here
    # is hand-written. humanoid-741 is imported but absent from this table and
    # has no fixes/ dir at all: see H2H_UNSCOREABLE for why no valid ideal
    # exists for it.
    "lily11": _spec("lily11", "tlsf"),
    "lily15": _spec("lily15", "tlsf"),
    "lily16": _spec("lily16", "tlsf"),
    "humanoid-503": _spec("humanoid-503", "tlsf"),
    "humanoid-742": _spec("humanoid-742", "tlsf"),
    "pcar-v2-888": _spec("pcar-v2-888", "tlsf"),
}

# The original six-family TLSF corpus. The pre-ablation TLSF profiles (tlsf,
# muc, padd, and the retired wellsep) pin this list rather than
# list(TLSF_SPECS), so extending the table above does not silently grow what
# they run — their results CSVs are already recorded against exactly these six.
TLSF_CORE_SPECS: list[str] = [
    "arbiter", "gyro-var1", "humanoid-531", "lift", "lily02", "minepump",
]

# The 20 fixes-backed TLSF families of the ablation campaign (PLAN §2): the
# original 13, plus the 7 SYNTCOMP arbiter families promoted on 2026-07-24
# after hand-written ideals landed for them.
TLSF_ABLATION_SPECS: list[str] = [
    "arbiter", "gyro-var1", "gyro-var2", "humanoid-458", "humanoid-531",
    "lift", "lily02", "minepump", "amba", "codesample-un1", "codesample-un2",
    "rg1", "rg2",
    "arbiter-handshake", "detector", "full-arbiter", "load-balancer",
    "prioritized-arbiter", "round-robin-arbiter", "simple-arbiter",
]

# Per-spec wall caps for the 20-family TLSF corpus at gen10/pop200, sized by the
# 2026-08-11-status-grading pilot to at least 4x its worst observed wall time per
# spec. They are per-spec rather than flat because the ltlsynt budget moved from
# 500 ms to 10 s (74beaea): a single query may now run 20x longer than any flat
# cap calibrated under the old budget assumed, and the corpus spreads over an
# order of magnitude. amba is the case that forces it, measuring 1078 s
# standalone in the campaign's expensive arm — a flat 600 s or 900 s cap censors
# exactly the arm under test and returns the censoring as the result, which is
# the same failure the raised budget was fixing.
TLSF_WALL_CAPS_GEN10: dict[str, int] = {
    "amba": 4320, "humanoid-531": 1560, "lift": 840,
    "simple-arbiter": 720, "round-robin-arbiter": 660,
    "humanoid-458": 540, "full-arbiter": 420, "gyro-var1": 420,
    "gyro-var2": 420, "detector": 360, "arbiter": 300,
    "arbiter-handshake": 300, "codesample-un1": 300,
    "codesample-un2": 300, "lily02": 300, "load-balancer": 300,
    "minepump": 300, "prioritized-arbiter": 300, "rg1": 300,
    "rg2": 300,
}


# Upper bound on the compute-match factor any `-cm` profile will be generated
# at, used to scale that profile's wall caps. It is a ceiling and not a
# measurement: the real factor comes out of calibration, hours after this
# module is imported, so a cap cannot track it. Over-scaling a cap only wastes
# the headroom of a run that was never going to reach it, while under-scaling
# censors the longer arm and returns the censoring as the result, so the bound
# is deliberately loose. Raise it before generating with a larger
# --compute-match-factor.
CM_CAP_HEADROOM = 1.5


def scaled_wall_caps(factor: float) -> dict[str, int]:
    """`TLSF_WALL_CAPS_GEN10` scaled for an arm running more generations.

    Cost scales with generations*population, so a compute-matched arm needs its
    caps scaled by at least the factor its generation count was, or the cap
    censors the longer arm by construction and the comparison measures the cap.
    """
    return {spec: round(cap * factor)
            for spec, cap in TLSF_WALL_CAPS_GEN10.items()}


# The head-to-head corpus IS the AuRUS paper's evaluation set: the 26
# specifications the paper's tables evaluate. Both arms target all 26, so this
# is the campaign's declared scope rather than whatever counter happens to be
# able to run today. The paths live in `SPEC_TLSF` in
# scripts/aurus_campaign.py and the two are held equal by
# test_experiment_paths.py; that module imports this one, so the names are
# written here and the paths there rather than either importing the other.
#
# amba, codesample-un1 and codesample-un2 were in the 2026-07 corpus and are
# not rows in the paper's tables, so they leave the head-to-head. They keep
# their counter families and ideals for the ablation corpus.
#
# takeoff-tlsf never entered, on the older ground that both of its upstream
# "genuine" fixes are invalid (one truncated, one unsatisfiable) — see
# EXPERIMENTS.md 2026-07-24.
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

# The gap between the declared scope above and what counter can currently run,
# named rather than silently subtracted. A family lands here until it has an
# `examples/<name>/` with a spec and at least one lint-clean ideal; the counter
# arm runs `H2H_TLSF_SPECS` minus this list, and `test_experiment_paths.py`
# asserts the two partition exactly, so importing a family fails that test
# until it is struck off here.
#
# Two obstacles, neither structural. Seven parse today but have no reference
# repairs upstream, so each needs a hand-written weakening ideal: lily11,
# lily15, lily16, humanoid-503, humanoid-741, humanoid-742, pcar-v2-888. The
# other eight are full-format TLSF that counter's parser rejects on the GLOBAL
# block; `syfco -f basic` lowers them to a form it parses, and AuRUS ships one
# at lib/syfco. Six of those eight do carry upstream references, though AuRUS
# references replace rather than weaken as a rule, so each still needs
# validating with lint-ideals before it can serve as an ideal.
#
# Nothing here blocks the AuRUS arm, which runs all 26 regardless and archives
# every out.txt, so an import re-scores runs that already exist.
H2H_PENDING_IMPORT: list[str] = []

# Imported, but permanently unscoreable, so held out of the counter arm rather
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
# contribute a guaranteed zero to the counter arm while costing a full seed
# sweep to produce it.
#
# Note this is not the FRETISH input-screen case in the root CLAUDE.md, where
# an ill-separated input is warned about rather than rejected because a
# descendant may fix the property. On the TLSF assumption side no descendant
# can. A semantically sound repair does exist -- dropping the `G(!next_head)`
# ASSERT that `mucs` names makes it realizable -- but it is outside the
# operators' image, so no ideal built on it would be a fair target either.
#
# AuRUS produced zero repairs for this family across all 30 repeats, which is
# consistent with the same obstruction rather than with a budget limit.
H2H_UNSCOREABLE: list[str] = ["humanoid-741"]

# What the counter arm can run right now.
H2H_TLSF_READY: list[str] = [
    s for s in H2H_TLSF_SPECS
    if s not in H2H_PENDING_IMPORT and s not in H2H_UNSCOREABLE
]

# The four families the 2026-08-26 selection smoke test drops, and the rule that
# drops them: mean wall time above 600 s in the 2026-08-23-monotone `monoon`
# arm, which is that campaign's cell at the configuration this one runs. The
# rule is cost and nothing else -- outcome plays no part -- because a corpus
# chosen by how well the incumbent scheme scored on it is a corpus selected on
# the response, and the whole point of the comparison is that either arm may
# win a family. Stated here rather than applied inline so the exclusion is
# checkable against the archive it was measured from.
#
# The measured means: humanoid-742 7200 s (capped on every seed, and 0/20 on
# found_repair), humanoid-531 6149 s, pcar-v2-888 1643 s, humanoid-503 685 s.
# Together they are 84% of a seed's serial cost over the 25 families, and
# humanoid-742 alone is the whole 2 h per-host budget on one job -- so keeping
# any of them means the campaign cannot finish, not that it finishes slowly.
SELECTION_SMOKE_EXCLUDED: list[str] = [
    "humanoid-503", "humanoid-531", "humanoid-742", "pcar-v2-888",
]

# H2H_TLSF_READY minus the cost exclusions above: 21 families.
SELECTION_SMOKE_SPECS: list[str] = [
    s for s in H2H_TLSF_READY if s not in SELECTION_SMOKE_EXCLUDED
]

# The cost-calibration subset of the 2026-08-28 selection x grading campaign:
# 12 of the 25 H2H_TLSF_READY families, chosen to span the measured cost range
# rather than to answer anything. All four SELECTION_SMOKE_EXCLUDED families are
# in it deliberately -- they are the ones whose behaviour under an internal
# deadline is unknown, that exclusion having been a cost rule and the deadline
# being what retires it -- together with four upper-middle families, two middle
# and two cheap ones, taken from the 2026-08-23-monotone `monoon` per-family
# means the `selection-smoke` cap table quotes. A subset chosen on cost alone
# says nothing about the response, which is what keeps it usable for sizing the
# full run without selecting the corpus on the outcome.
CURVES_CALIB_SPECS: list[str] = [
    # the four the cost rule used to exclude (7200, 6149, 1643, 685 s)
    "humanoid-742", "humanoid-531", "pcar-v2-888", "humanoid-503",
    # upper-middle
    "lift", "prioritized-arbiter-aurus", "gyro-var1", "humanoid-458",
    # middle
    "gyro-var2", "ltl2dba-theta-2",
    # cheap
    "minepump", "rg2",
]

# The corpus the 2026-07-24 head-to-head actually ran, frozen so that changing
# the live list above cannot retroactively change what the `h2h-tlsf` profile
# means. That profile is the July campaign's definition and nothing else runs
# it; the 2026-08-14 campaign uses `aurus-h2h` instead. Written out rather than
# derived from H2H_TLSF_SPECS: it was derived once, and moving the live corpus
# to the paper's 26 rows silently carried this to 25.
H2H_TLSF_SPECS_2026_07: list[str] = [
    "gyro-var1", "gyro-var2", "humanoid-458", "humanoid-531", "lift",
    "lily02", "minepump", "codesample-un1", "codesample-un2", "rg1", "rg2",
    "arbiter-aurus",
]

# The arbitration corpus of the 2026-08-10 diversity probe. 2026-07-31-replicate
# found nsga2-apportion (then nsga2-replicate) repairing `arbiter` 120/120 where
# nsga2-truncate managed 0/120 — the only place either NSGA-II scheme has ever
# separated on found_repair, and the lever the arbiter-hp and arbiter-padd
# campaigns hunted through p_add_assumption without finding. That result rests on
# one family, so this list is every fixes-backed family whose unrealizability is
# an arbitration conflict: the seven arbiters, plus amba (a bus arbiter) and
# load-balancer (arbitration over servers). Six of the nine did not exist in the
# corpus when replicate ran.
#
# lily02 is the tenth and is not an arbiter. It is the counter-signal control:
# replicate's apportion arm cost it implies_ideal 1.00 -> 0.44, so a probe that
# only sampled families the scheme helps would report the upside alone.
ARBITER_PROBE_SPECS: list[str] = [
    "arbiter", "arbiter-aurus", "arbiter-handshake", "full-arbiter",
    "prioritized-arbiter", "round-robin-arbiter", "simple-arbiter",
    "amba", "load-balancer",
    "lily02",
]

# Unified lookup for run_one()/--specs; each profile picks its own subset.
SPECS: dict[str, dict[str, Path]] = {**FRETISH_SPECS, **TLSF_SPECS}

N_SEEDS = 30

# compare runs n_repairs * n_ideals * 2 implication checks, each with compare's
# own 20s black budget. fsm-timing (bounded-interval operators) is slow: a 29-
# repair run measured ~141s, and repair counts grow with generations/population.
# Keep this well above that, in line with the generous counter_timeout budgets.
# A profile whose arms differ in repair count should raise it via its own
# `compare_timeout` key -- see the compare_timed_out column below.
COMPARE_TIMEOUT_S = 600

CSV_FIELDS = [
    "sweep", "level_name", "level_value", "selection", "weakening", "metric",
    "repair_mode", "spec", "seed", "found_repair", "n_repairs", "best_fitness",
    "best_relation", "implies_ideal", "n_implies", "wall_time_s",
    "timed_out", "n_dropped", "compare_timed_out", "commit", "dirty",
]

# Rows written before `selection` existed are all NSGA-II: every config in use
# pinned selection_scheme = "nsga2", verified against the per-run config.toml
# snapshots of the 2026-07-14 sweep (1470/1470). Defaulting them to "nsga2"
# keeps resume working against those CSVs instead of re-running them.
LEGACY_SELECTION = "nsga2"

# The two NSGA-II schemes were renamed -- "nsga2" -> "nsga2-truncate" and
# "nsga2-replicate" -> "nsga2-apportion" -- and the binary rejects the old
# spellings outright. Only the name moved: the scheme each one selects is
# unchanged, so a row recorded under either spelling names the same cell of the
# design. 224,861 archived rows carry "nsga2" and the replicate campaign's rows
# carry "nsga2-replicate", against zero rows written under the new names so far.
# Canonicalising every `selection` value as it is read back -- from an archived
# CSV on resume, or from a config directory name -- is what keeps those rows
# matching the plan instead of missing it and re-running finished campaigns.
# gen_configs.py emits the new names only; this mapping is read-side alone.
SCHEME_ALIASES: dict[str, str] = {
    "nsga2": "nsga2-truncate",
    "nsga2-replicate": "nsga2-apportion",
}


def canonical_scheme(name: str) -> str:
    """Current spelling of a selection scheme; any other name passes through."""
    return SCHEME_ALIASES.get(name, name)


# Rows written before `weakening` existed ran configs with run_weakening = true
# (the DEFAULTS value, overridden only by sweep J's weaken-off level, which the
# profiles of that era did not run). Same purpose as LEGACY_SELECTION: keep
# resume matching those rows rather than re-running them.
LEGACY_WEAKENING = "wkon"

# Directory names gen_configs.py --weakening emits. A config directly under
# <scheme>/ predates the factor and is read as LEGACY_WEAKENING.
WEAKENING_DIRS: tuple[str, ...] = ("wkon", "wkoff")

# Rows written before `metric` existed ran the direct similarity metric (the
# config.hpp / DEFAULTS value; model_counting.metric did not yet exist, so the
# binary always scored directly). Same purpose as LEGACY_SELECTION: keep resume
# and merge matching those rows rather than re-running them.
LEGACY_METRIC = "direct"

# Directory names gen_configs.py --metric emits (the short label, not the TOML
# value "logarithmic"). A config with no such ancestor predates the factor and
# is read as LEGACY_METRIC.
METRIC_DIRS: tuple[str, ...] = ("direct", "log")

# Rows written before `repair_mode` existed ran monolithic repair (the
# config.hpp / DEFAULTS value; tlsf.repair_mode did not yet exist, so the binary
# always evolved the whole spec). Same purpose as LEGACY_SELECTION: keep resume
# and merge matching those rows rather than re-running them.
LEGACY_REPAIR = "mono"

# Directory names gen_configs.py --repair emits (the short label, not the TOML
# value "monolithic"). A config with no such ancestor predates the factor and is
# read as LEGACY_REPAIR.
REPAIR_DIRS: tuple[str, ...] = ("mono", "muc")

# Rows written before `commit` existed have no recorded commit at all: nothing
# asked the binary what it was built from, and a binary's commit is not
# recoverable after the fact (mtime bounds a window, and the binaries on av2/av3
# lag main by an unbounded amount — see the PROVENANCE.json files under
# experiments/). Unlike LEGACY_SELECTION this is an admission of ignorance
# rather than a known value, so it must never join the resume or merge key:
# keying on it would make every archived row miss and re-run whole campaigns.
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
# counter's scoring report (src/repair/reports.cpp print_scoring_report).
# That report prints unconditionally, unlike the rest of counter's end-of-run
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
# Aliasing is always within a scheme: nsga2's B/pop200 aliases onto nsga2's
# A/gen10, never onto weighted's. The byte-identity check below enforces that,
# since the configs differ on selection_scheme.
#
# The canonical is per-profile: it must be a level the profile actually runs.
BASELINE_ALIASES: dict[tuple[str, str], tuple[str, str]] = {
    ("B", "pop200"): ("A", "gen10"),
    ("C", "default"): ("A", "gen10"),
    ("D", "ptrig0.5"): ("A", "gen10"),
    ("E", "presp0.5"): ("A", "gen10"),
    ("F", "ptim0.15"): ("A", "gen10"),
    ("G", "bound20"): ("A", "gen10"),
    ("H", "cross0.1"): ("A", "gen10"),
    ("I", "mut1.0"): ("A", "gen10"),
}

# The TLSF campaign runs only sweeps A (generations) and B (population), which
# cross at the gen10/pop200 baseline, so B/pop200 aliases onto A/gen10 exactly
# as in the FRETISH grid. No other sweep is generated, so the rest of
# BASELINE_ALIASES has nothing to match and is left out.
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
# `timeout_caps` is a flat per-spec cap in seconds; None means the
# counter_timeout() formula.
#
# `configs_dir` and `results_dir` are per-profile because run_id names collide
# across profiles: the same (sweep, level, scheme, spec, seed) is one directory
# name regardless of operating point, so two profiles sharing a results_dir
# would have parse_repair_files() read the other's stale repair_*.json.
PROFILES: dict[str, dict] = {
    # The original sweep, pinned to the levels it has always had so its
    # results.csv stays a single comparable dataset even though gen_configs.py
    # now emits a finer grid around them.
    "full": {
        "schemes": ["nsga2-truncate"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["A", "B", "C"],
        "levels": {
            "A": ["gen5", "gen10", "gen20", "gen40"],
            "B": ["pop50", "pop100", "pop200", "pop500", "pop1000"],
        },
        "specs": list(FRETISH_SPECS_2026_07),
        "seeds": list(range(N_SEEDS)),
        "timeout_caps": None,
        "baseline_aliases": BASELINE_ALIASES,
        "configs_dir": CONFIGS_DIR,
        "results_dir": RESULTS_DIR,
        "results_csv": EXPERIMENTS_DIR / "results.csv",
        "default_jobs": 1,
    },
    # The full factorial: every level of every sweep, run under both selection
    # schemes, at 100 seeds. selection_scheme is a factor here rather than a
    # constant, which is what makes nsga2-vs-weighted answerable per level
    # instead of only at the baseline.
    "factorial": {
        "schemes": ["nsga2-truncate", "weighted"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": None,  # every sweep found in experiments/configs/
        "levels": {},
        "specs": list(FRETISH_SPECS_2026_07),
        "seeds": list(range(100)),
        "timeout_caps": None,
        "baseline_aliases": BASELINE_ALIASES,
        "configs_dir": CONFIGS_DIR,
        "results_dir": RESULTS_DIR,
        "results_csv": EXPERIMENTS_DIR / "results-factorial.csv",
        "default_jobs": 4,
    },
    # Direct-vs-log similarity metric as the sole crossed factor, at the same
    # large operating point as cj-large (generations=40, population_size=1000)
    # where repairs are strong enough for the metric to move outcomes. Only the
    # all-defaults C/default level runs — the metric is the experiment, so the
    # seed count is spent on statistical power for the main effect rather than
    # on a grid. No aliasing: a single level has nothing to alias onto.
    "metric": {
        "schemes": ["nsga2-truncate"],
        "weakenings": None,
        "metrics": ["direct", "log"],
        "repair_modes": None,
        "sweeps": ["C"],
        "levels": {"C": ["default"]},
        "specs": list(FRETISH_SPECS_2026_07),
        "seeds": list(range(100)),
        "timeout_caps": {"takeoff": 600, "fsm": 600, "fsm-timing": 600,
                         "fsm-combined": 900},
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-metric",
        "results_dir": EXPERIMENTS_DIR / "results-metric",
        "results_csv": EXPERIMENTS_DIR / "results-metric.csv",
        "default_jobs": 4,
    },
    # Monolithic vs MUC-guided repair (tlsf.repair_mode) crossed with the TLSF
    # assumption/guarantee mutation split (sweep M), on the TLSF spec corpus —
    # repair_mode is a no-op on the FRETISH specs, so crossing it over them would
    # compare two identical arms. The split interacts with repair_mode: muc keeps
    # the environment side at full size while shrinking the guarantee side to the
    # minimal core, so at a fixed p_assumption it spends a larger share of its
    # guarantee mutations on the culprit formulae (verified: the assumption side
    # stays live in muc, so the factor is not confounded). Responses are
    # implies_ideal and wall_time_s co-equal — muc's pitch is quality where
    # monolithic sits at implies_ideal~=0 (gyro/lift/humanoid) AND runtime on the
    # heavy specs. Operating point gen10/pop200 (the config defaults), matching
    # the prior monolithic TLSF data for comparability. Generate the configs with
    #   python scripts/gen_configs.py --tlsf --sweeps M --repair both \
    #       --out-dir experiments/configs-muc
    # (--tlsf carries the 500 ms ltlsynt timeout and 0.15 scoring tolerance the
    # heavy specs need; --sweeps C would not — TLSF_SWEEPS has no C level.)
    "muc": {
        "schemes": ["nsga2-truncate"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": ["mono", "muc"],
        "sweeps": ["M"],
        "levels": {"M": ["pg0.3", "pg0.5", "pg0.7", "pg0.9"]},
        "specs": list(TLSF_CORE_SPECS),
        # Seed-major disjoint ranges across av2/av3 (pass --seeds on launch); the
        # list is the ceiling a 60 h/machine deadline kill truncates to a
        # balanced design.
        "seeds": list(range(60)),
        # Caps sized to the slow (monolithic) arm so it is never censored — a cap
        # that bites records implies_ideal=0 and truncates wall_time_s for a run
        # that merely ran long, corrupting both responses. From the measured
        # gen10/pop200 monolithic means: humanoid 768 s (p90 1739 s), lift 73 s,
        # gyro 16 s, the rest single-digit. Applied identically to both arms so
        # muc's speed shows as a genuinely lower wall_time rather than a cap.
        "timeout_caps": {"humanoid-531": 2400, "lift": 600, "gyro-var1": 300,
                         "arbiter": 120, "lily02": 120, "minepump": 120},
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-muc",
        "results_dir": EXPERIMENTS_DIR / "results-muc",
        "results_csv": EXPERIMENTS_DIR / "results-muc.csv",
        # jobs=1: one counter process per machine using its full 32-worker pool.
        # ltlsynt is multi-GB resident per call, so jobs>1 would multiply peak
        # RAM and risk OOM.
        "default_jobs": 1,
    },
    # Follow-up to the muc campaign: sweep p_add_assumption (sweep P: 0.05, 0.15,
    # 0.3, 0.5) crossed with repair_mode {mono, muc}. The muc campaign found that
    # reaching an assumption-side ideal (e.g. arbiter's G F r0 & G F r1) is
    # bottlenecked on the fixed 0.05-rate p_add_assumption structural mutation, not
    # on the assumption/guarantee split; this tests whether raising it shifts
    # repairs from guarantee-weakening to the ideal. humanoid-531 is dropped: it
    # never reaches the ideal in either arm (so p_add_assumption cannot inform it)
    # and it is ~88% of the per-seed cost, so excluding it turns a ~12-seed
    # overnight run into a ~45-seed one on the specs where the lever operates.
    # Generate with:
    #   python scripts/gen_configs.py --tlsf --sweeps P --repair both \
    #       --out-dir experiments/configs-padd
    # (--tlsf carries the 60 s ltl2tgba-timeout, 500 ms ltlsynt-timeout and 0.15
    # scoring tolerance; the ltl2tgba-timeout needs the deployed counting-leak fix.)
    "padd": {
        "schemes": ["nsga2-truncate"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": ["mono", "muc"],
        "sweeps": ["P"],
        "levels": {"P": ["padd0.05", "padd0.15", "padd0.3", "padd0.5"]},
        "specs": [s for s in TLSF_CORE_SPECS if s != "humanoid-531"],
        # Generous ceiling; a 12 h/machine overnight deadline truncates the
        # seed-major order to a balanced design at whatever depth it reaches.
        "seeds": list(range(160)),
        "timeout_caps": {"arbiter": 120, "gyro-var1": 300, "lift": 600,
                         "lily02": 120, "minepump": 120},
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-padd",
        "results_dir": EXPERIMENTS_DIR / "results-padd",
        "results_csv": EXPERIMENTS_DIR / "results-padd.csv",
        "default_jobs": 1,
    },
    # The basic-TLSF examples swept over generations (A) and population (B), the
    # two operating-point parameters, NSGA-II only, default weakening. These
    # specs are much slower than the FRETISH ones — three of them (lift,
    # gyro-var1, humanoid-531) take ~3 min even at the gen10/pop200 baseline and
    # scale with generations*population — so the grid is a coarse cross (four
    # levels per axis, sharing the baseline) rather than the fine FRETISH grid,
    # trading gradations for seeds within a fixed wall-clock budget. Reads its
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
        # machine killed at the wall-clock deadline leaves a balanced design at
        # whatever seed depth it reached; splitting 0-29/30-59 across av2/av3
        # keeps the two seed ranges disjoint so a truncated pair still merges to
        # one balanced dataset. ~16 h/machine is the real stop condition.
        "seeds": list(range(60)),
        # Flat per-spec caps generous enough that the largest operating point
        # (gen40 / pop500) never censors a slow-but-progressing run — a cap that
        # bites records implies_ideal = 0 for a run that merely ran long, which
        # corrupts the response variable. Sized from the measured baseline
        # (~190s for the heavy specs at gen10/pop200) times the ~4x operating
        # range times a safety margin for the jobs>1 thread-pool cap.
        "timeout_caps": {"arbiter": 2400, "gyro-var1": 3600,
                         "humanoid-531": 3600, "lift": 3600,
                         "lily02": 1200, "minepump": 1200},
        "baseline_aliases": TLSF_ALIASES,
        "configs_dir": EXPERIMENTS_DIR / "configs-tlsf",
        "results_dir": EXPERIMENTS_DIR / "results-tlsf",
        "results_csv": EXPERIMENTS_DIR / "results-tlsf.csv",
        # jobs=1: one counter process per machine, using its full internal
        # thread pool. ltlsynt is multi-GB resident per call on these specs, so
        # running several counter processes at once (jobs>1) would multiply
        # peak RAM by jobs and risk an OOM.
        "default_jobs": 1,
    },
    # FRETISH arm of the ablation campaign (PLAN §1): a 2x2 factorial of
    # selection scheme (nsga2/weighted) x similarity metric (log/direct), at the
    # C/default weight preset — 4 cells, so the interaction is measurable, not
    # just main effects. It ran as a 2x2x2, the third factor being the Halstead
    # weight (C/default's 0.1 against C/no-halstead's 0.0); the Halstead
    # objective was removed on 2026-08-13 and its level went with it, so the
    # third factor is retired and the archived rows are the only record of it.
    # Operating point generations=40/population_size=1000, the metric-campaign
    # scale where repairs are strong enough for the factors to move outcomes.
    # Control cell: nsga2 / log. Generate with
    #   python scripts/gen_configs.py --sweeps C --levels default \
    #       --schemes nsga2-truncate weighted --metric both \
    #       --generations 40 --population-size 1000 \
    #       --out-dir experiments/configs-ablate-fret
    # (--levels keeps sweep C's other levels out of the grid — they would
    # silently inflate the 4 cells to 16.)
    "ablate-fret": {
        "schemes": ["nsga2-truncate", "weighted"],
        "weakenings": None,
        "metrics": ["direct", "log"],
        "repair_modes": None,
        "sweeps": ["C"],
        "levels": {"C": ["default"]},
        "specs": list(FRETISH_SPECS_2026_07),
        "seeds": list(range(30)),
        # The cj-large/metric caps for the same operating point: measured worst
        # case there was ~41s, so these are 15-20x margin — a cap that bites
        # records implies_ideal = 0 for a run that was merely slow.
        "timeout_caps": {"takeoff": 600, "fsm": 600, "fsm-timing": 600,
                         "fsm-combined": 900},
        # Only sweep C runs, so there is no A/gen baseline to alias onto.
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-ablate-fret",
        "results_dir": EXPERIMENTS_DIR / "results-ablate-fret",
        "results_csv": EXPERIMENTS_DIR / "results-ablate-fret.csv",
        "default_jobs": 4,
    },
    # TLSF arm of the same 4-cell factorial, over the 13 fixes-backed TLSF
    # families at the TLSF operating point (gen10/pop200, the config defaults).
    # A flat 600 s per-run cap (the arbiter-hp precedent) bounds the heavy tail
    # — humanoid means 768 s uncapped with a 1739 s p90, and the corpus mean of
    # ~150 s/run is what makes 4 x 13 x 15 fit the campaign window; the
    # censoring it introduces is deliberate and applied identically to every
    # cell. The configs also need the tlsf-profile runtime settings (500 ms
    # ltlsynt timeout, 60 s ltl2tgba timeout, 0.15 scoring-failure tolerance),
    # passed explicitly because --tlsf swaps in the TLSF sweep table, which has
    # no sweep C. Generate with
    #   python scripts/gen_configs.py --sweeps C --levels default \
    #       --schemes nsga2-truncate weighted --metric both \
    #       --ltlsynt-timeout 500 --ltl2tgba-timeout 60000 \
    #       --max-scoring-failure-rate 0.15 \
    #       --out-dir experiments/configs-ablate-tlsf
    # The status objective's grading scale (sweep G: tiered vs mrs), over the
    # TLSF ablation corpus at the config defaults. Everything but the grading is
    # held fixed -- one scheme, one metric, one operating point -- because the
    # question is whether the finer scale changes what the search finds, and
    # crossing more factors would spend the window on power the primary
    # comparison needs. See experiments/2026-08-11-status-grading/PLAN.md, which
    # pre-registers the endpoints and deliberately registers no decision rule.
    #
    # Generate with
    #   python scripts/gen_configs.py --tlsf --sweeps G \
    #       --ltlsynt-timeout 10000 \
    #       --out-dir experiments/configs-status-grading
    #
    # Per-spec caps at 4x the slowest run a 3-seed pilot over this exact corpus
    # and operating point measured, floored at 300s and rounded to the minute.
    # 4x rather than the 6-20x the older tables used: those were sized when
    # nothing bounded an individual ltlsynt query, whereas ltlsynt_timeout_ms is
    # 10000 here, so the per-run tail is far shorter. amba earns 4320s against
    # the flat 600s of ablate-tlsf -- its mrs arm alone measured 1078s -- and
    # capping it at 600s would censor the arm rather than measure it.
    "status-grading": {
        "schemes": ["nsga2-truncate"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["G"],
        "levels": {"G": ["tiered", "mrs"]},
        "specs": list(TLSF_ABLATION_SPECS),
        "seeds": list(range(24)),
        # A frozen byte-for-byte copy of TLSF_WALL_CAPS_GEN10, which was
        # extracted from this table and deliberately left duplicated here.
        # status-grading is a closed campaign, so this table is a record of
        # the caps its archived rows actually ran under; replacing it with a
        # reference to the constant would let a future recalibration of the
        # constant silently restate what that archive means. The two agreeing
        # today is not a reason to couple them. Do not deduplicate.
        "timeout_caps": {
            "amba": 4320, "humanoid-531": 1560, "lift": 840,
            "simple-arbiter": 720, "round-robin-arbiter": 660,
            "humanoid-458": 540, "full-arbiter": 420, "gyro-var1": 420,
            "gyro-var2": 420, "detector": 360, "arbiter": 300,
            "arbiter-handshake": 300, "codesample-un1": 300,
            "codesample-un2": 300, "lily02": 300, "load-balancer": 300,
            "minepump": 300, "prioritized-arbiter": 300, "rg1": 300,
            "rg2": 300,
        },
        # Sweep G has no gen/pop axis, so there is no A/gen baseline to alias.
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-status-grading",
        "results_dir": EXPERIMENTS_DIR / "results-status-grading",
        "results_csv": EXPERIMENTS_DIR / "results-status-grading.csv",
        # jobs=1 for the same RAM reason as the other TLSF profiles: ltlsynt is
        # multi-GB per call on these specs and the cap is per counter process.
        "default_jobs": 1,
    },
    "ablate-tlsf": {
        "schemes": ["nsga2-truncate", "weighted"],
        "weakenings": None,
        "metrics": ["direct", "log"],
        "repair_modes": None,
        "sweeps": ["C"],
        "levels": {"C": ["default"]},
        "specs": list(TLSF_ABLATION_SPECS),
        "seeds": list(range(15)),
        # A flat 600s over the whole set, sized while ltlsynt_timeout_ms was
        # 500. amba no longer fits it: at the 10000 budget a gen5/pop100 probe
        # took 404s, so gen10/pop200 will run past this cap and be censored —
        # trading its old zero-yield for a timeout rather than a result.
        # Recalibrate amba (and re-check the rest) before launching; see
        # TLSF_LTLSYNT_TIMEOUT_MS in gen_configs.py.
        "timeout_caps": {s: 600 for s in TLSF_ABLATION_SPECS},
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-ablate-tlsf",
        "results_dir": EXPERIMENTS_DIR / "results-ablate-tlsf",
        "results_csv": EXPERIMENTS_DIR / "results-ablate-tlsf.csv",
        # jobs=1 for the same RAM reason as the tlsf profile: ltlsynt is
        # multi-GB resident and its concurrency cap is per counter process.
        "default_jobs": 1,
    },
    # Head-to-head top-up against the AuRUS baseline (scripts/aurus_campaign.py):
    # the control cell only (nsga2 / log metric / C-default weights), extended
    # to seeds 0-19 and to the H2H_TLSF_SPECS corpus, so every family AuRUS
    # runs has 20 counter control runs to compare against. Dedup with
    # ablate-tlsf is by construction: this profile shares ablate-tlsf's
    # configs dir, results dir, and results CSV, and the resume key (sweep,
    # level, selection, weakening, metric, repair_mode, spec, seed) fully
    # identifies a control-cell row — so the (spec, seed) rows ablate-tlsf
    # already completed are skipped and only the top-up executes:
    # arbiter-aurus at seeds 0-19 plus the 11 shared families at seeds 15-19.
    # The same sharing means the two profiles must not run concurrently on
    # one machine (interleaved CSV appends and run dirs).
    "h2h-tlsf": {
        "schemes": ["nsga2-truncate"],
        "weakenings": None,
        # ["log"] (not None): keeps the factor-cell key, run_id and CSV metric
        # column identical to ablate-tlsf's log cell, which is what makes the
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
    # The 2026-08-14 head-to-head. Separate from `h2h-tlsf` in every path it
    # writes, because it differs from that profile in the two things the resume
    # key does not carry: the corpus is 25 scoreable families rather than 12,
    # and the wall cap is 7200s rather than 600s. Sharing ablate-tlsf's CSV as
    # `h2h-tlsf` does would put rows from both caps under one key and let the
    # merge keep whichever arrived first.
    #
    # 7200s matches AuRUS's `--gato 7200`, the published 2 h budget. The July
    # campaign ran counter at 600s against AuRUS at 3600s and reported the 6x
    # asymmetry as favouring the baseline; this one removes the asymmetry
    # instead, so neither tool's figure needs that caveat and neither compares
    # to July's on cost.
    "aurus-h2h": {
        # Both of these track the built-in defaults rather than pinning an
        # arm, because a head-to-head has to run counter as it ships: a
        # baseline comparison against a configuration no user gets by
        # default measures the wrong thing. nsga2-apportion became the
        # default in c71ecf0 and this profile followed it; `log` has been
        # the default metric throughout. Re-check both against
        # include/config.hpp whenever a default moves.
        "schemes": ["nsga2-apportion"],
        "weakenings": None,
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["C"],
        "levels": {"C": ["default"]},
        # H2H_TLSF_READY, not H2H_TLSF_SPECS: the campaign's declared scope is
        # all 26, and the counter arm runs the subset already imported. The
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
    # The same corpus and cap at the configuration counter ships on 2026-08-21,
    # plus the accumulator. `experiments/2026-08-14-aurus-h2h` measured a counter
    # that had none of the three changes since made permanent, and
    # `experiments/2026-08-20-ops-weakening/REPORT.md` could only read them
    # against the 10 families that campaign's corpus shares with this one --
    # where the test drops to 7 clusters and detects nothing. This runs the
    # arm over all 25 so the archived AuRUS rows are answered at full power.
    #
    # There is no control arm. The 499 archived `aurus-h2h` rows are it: same
    # corpus, same seeds, same 7200 s cap, same two hosts. The two configs
    # differ in exactly four keys -- `accumulate_repairs`, `run_weakening`,
    # `run_well_separation` and the operator grammar, which is unconditional in
    # the binary rather than a key -- so the comparison is a bundle test and
    # PLAN.md says so rather than attributing anything to one of them.
    #
    # Sweep N at `accon` alone, rather than crossing it: the accumulator was
    # measured on its own by `2026-08-19-accumulator` and this campaign holds it
    # on, so the level is a way to state the key in the archived config rather
    # than a factor. `--weakening off` does the same job for `run_weakening`.
    # `run_well_separation` is the one key nothing states: it rides the binary
    # default (false since b101ada) and PLAN.md records the value, since
    # `--pin-vintage` would write `run_well_separation = true` from a
    # `gen_configs.DEFAULTS` entry stale since 2026-08-10 -- the same defect that put that filter into the
    # archived arm against the `aurus-h2h` profile's own stated intent.
    "aurus-h2h-ship": {
        "schemes": ["nsga2-apportion"],
        "weakenings": ["wkoff"],
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["N"],
        "levels": {"N": ["accon"]},
        "specs": H2H_TLSF_READY,
        "seeds": list(range(20)),
        "timeout_caps": {s: 7200 for s in H2H_TLSF_READY},
        # 1800 s, against the 600 s default `aurus-h2h` took. Both changes this
        # arm carries push repairs into compare: over the sweep-O corpus the
        # accumulator with the screen off ran a median of 7 repairs per scoring
        # run against 4 with the screen on, and compare is n_repairs x n_ideals
        # x 2 implication checks. The archived arm timed out once in 499 at
        # 600 s with half the load, and a compare timeout costs the row's
        # implies_ideal silently, which is the endpoint.
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-aurus-h2h-ship",
        "results_dir": EXPERIMENTS_DIR / "results-aurus-h2h-ship",
        "results_csv": EXPERIMENTS_DIR / "results-aurus-h2h-ship.csv",
        # jobs = 8, as `aurus-h2h` runs it and for the reason its declaration
        # records: the 24-run calibration peaked at 18.7 GB of 125 GB, and at
        # jobs = 1 this campaign is about 31 hours per host against roughly 4.
        "default_jobs": 8,
    },
    # The follow-up to `aurus-h2h-ship`, on feat/monotone-operators. Same
    # corpus, same seeds, same 7200 s caps, same jobs count: that campaign's
    # archived rows are this one's outer control, and a paired (spec, seed) read
    # needs every one of those to match. Three arms rather than one because the
    # branch carries three independent changes -- see TLSF_SWEEP_T in
    # gen_configs.py for which contrast attributes which.
    "monotone": {
        "schemes": ["nsga2-apportion"],
        "weakenings": ["wkoff"],
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["T"],
        "levels": {"T": ["monooff", "monoon", "monoship"]},
        "specs": H2H_TLSF_READY,
        "seeds": list(range(20)),
        "timeout_caps": {s: 7200 for s in H2H_TLSF_READY},
        # 1800 s, matching `aurus-h2h-ship`. The accumulator is on here too, and
        # a compare timeout costs the row's implies_ideal silently.
        "compare_timeout": 1800,
        # No aliasing. None of the three arms is the grid baseline, and aliasing
        # one onto the archived control would import rows from a different
        # binary under this campaign's key.
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-monotone",
        "results_dir": EXPERIMENTS_DIR / "results-monotone",
        "results_csv": EXPERIMENTS_DIR / "results-monotone.csv",
        "default_jobs": 8,
    },
    # WeightedAverage against the shipped nsga2-apportion, at the configuration
    # counter ships on 2026-08-26 and with the selection scheme as the only key
    # that differs between the arms. A smoke test: 2 h per host, so it is sized
    # to answer whether the scheme still trades the way the 2026-07-24 ablation
    # measured it, not to close the question.
    #
    # Both arms run fresh rather than pairing the nsga2 arm against the
    # 2026-08-23-monotone `monoon` rows it would otherwise duplicate. Those rows
    # came from a different commit under a 7200 s cap, and this profile caps far
    # tighter, so their censoring differs from this one's -- and a control
    # inherited across a commit boundary is what the stale-binary and
    # cost-vintage notes in the root CLAUDE.md exist about. The archived rows
    # are a validity check on the fresh nsga2 arm instead, which is free.
    #
    # Sweep T at `monoon` alone, rather than sweep N or a bare C level: `monoon`
    # is the one level in any TLSF sweep whose four keys (p_monotone 0.25,
    # p_clone_assumption 0.25, elitism_rate 0.1, accumulate_repairs true) all
    # equal today's config.hpp defaults, so the archived config states every key
    # that has moved recently instead of inheriting it. `monoship` does not --
    # it pins elitism_rate 0.0, a default move the monotone campaign measured
    # and did not make. `--weakening off`, `--metric log` and `--pin-vintage`
    # cover the rest; see the campaign declaration for the generator line.
    #
    # The aggregate fitness weights are 0.1 / 0.2 / 0.7 (syntactic / semantic /
    # status), passed explicitly through gen_configs.py's `--weights`. They are
    # neither the binary's 0.2 / 0.5 / 0.5 nor the 0.33 triple gen_configs.py
    # pins into every other grid, so nothing here is inherited: the weighted arm
    # ranks by precisely this scalar, which makes the triple a parameter of the
    # question rather than of the harness. Every wall time and rate quoted below
    # and in the cap table was measured at 0.33 each, so they size the run and
    # do not control it.
    #
    # The corpus is SELECTION_SMOKE_SPECS, H2H_TLSF_READY minus four families
    # excluded on measured cost alone -- see that constant for the rule and the
    # numbers. found_repair is saturated at 1.00 on all 21 under the nsga2 arm,
    # so implies_ideal is the endpoint and found_repair is a control that should
    # not move.
    "selection-smoke": {
        "schemes": ["nsga2-apportion", "weighted"],
        "weakenings": ["wkoff"],
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["T"],
        "levels": {"T": ["monoon"]},
        "specs": SELECTION_SMOKE_SPECS,
        # 16 seeds, split 0-7 / 8-15. Both arms of a pair run on the same host,
        # so the split is over seeds and never over schemes, which the runner
        # crosses itself. 21 families x 16 seeds = 336 pairs.
        "seeds": list(range(16)),
        # 4x the slowest run the 2026-08-23-monotone `monoon` arm recorded per
        # family, floored at 300 s, rounded to the minute and clamped at 3600 s.
        # 4x rather than the older tables' 6-20x for status-grading's reason:
        # ltlsynt_timeout_ms bounds the per-query tail at 10 s here. The clamp
        # binds on lift alone (904 s max, so 3600 s is 4.0x by luck) and exists
        # because a single run at a cap above the per-host budget cannot be
        # distinguished from the campaign not finishing.
        #
        # The caps are sized off the nsga2 arm because it is the only arm with
        # archived timings. If the weighted arm is slower they bite it alone,
        # which is the one-sided censoring `replicate-recap` was written to
        # undo -- so read timed_out per arm before reading implies_ideal.
        "timeout_caps": {
            "lift": 3600, "prioritized-arbiter-aurus": 3300,
            "gyro-var1": 2100, "full-arbiter-aurus": 1980,
            "humanoid-458": 1560, "round-robin-arbiter-aurus": 1380,
            "gyro-var2": 840, "ltl2dba-theta-2": 600,
            "load-balancer-aurus": 360, "arbiter-aurus": 300,
            "detector-aurus": 300, "lily02": 300, "lily11": 300,
            "lily15": 300, "lily16": 300, "ltl2dba-r-2": 300,
            "ltl2dba27": 300, "minepump": 300, "rg1": 300, "rg2": 300,
            "simple-arbiter-aurus": 300,
        },
        # 1800 s, as monotone and aurus-h2h-ship set it. accumulate_repairs is
        # on, so a run emits several times more repairs than the 600 s default
        # was sized for, and a compare timeout costs the row's implies_ideal
        # silently -- which is the endpoint.
        "compare_timeout": 1800,
        # Neither arm is a grid baseline, and there is no gen/pop axis in sweep
        # T to alias onto.
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-selection-smoke",
        "results_dir": EXPERIMENTS_DIR / "results-selection-smoke",
        "results_csv": EXPERIMENTS_DIR / "results-selection-smoke.csv",
        # jobs = 8, the value the aurus-h2h calibration cleared on this corpus
        # (24 runs peaked at load 21.7 of 32 and 18.7 GB of 125 GB), and the
        # value monotone and aurus-h2h-ship ran -- which is what makes this
        # campaign's wall times readable against theirs.
        "default_jobs": 8,
    },
    # ── 2026-08-28 selection x grading ───────────────────────────────────────
    #
    # A 2x2 ablation on TLSF, measuring repair discovery over time rather than
    # at a fixed generation count: selection_scheme (nsga2-apportion against
    # weighted) crossed with fitness.status_grading (mrs, counter's own, against
    # aurus, AuRUS's six-level ladder). Two schemes x two sweep-G levels is the
    # whole cross -- the schemes are config directories and the grading arm
    # rides in level_name, so both are already key columns and nothing here
    # needs a new one.
    #
    # What makes it a curve rather than a point is genetic.max_wall_s: every arm
    # runs generations = 500, far more than any of these families reaches, so
    # the deadline is what ends every run and each arm gets the same wall clock
    # instead of the same number of generations. counter stops itself at the
    # next generation boundary past the deadline and writes run.json and its
    # repairs normally, so a run that hits the budget is measured rather than
    # censored -- which is exactly what the harness's own timeout_caps kill
    # cannot do. accumulate_repairs is on (the binary's default), so each repair
    # carries the generation it was found at and the response is readable as a
    # function of elapsed budget.
    #
    # runtime.parallel = 1 for the same reason: a scoring pool whose size varies
    # with --jobs makes wall time a property of the host's load rather than of
    # the arm, and the endpoint here is wall time. gen_configs.py states the key
    # in the config and derive_config leaves a stated value alone.
    #
    # The fitness weights are 0.1 / 0.2 / 0.7 (syntactic / semantic / status),
    # AuRUS's published triple, passed through gen_configs.py's --weights. Only
    # the weighted scheme reads them, so setting them globally is correct and
    # inert for the two nsga2-apportion arms; stating them keeps the archived
    # config self-describing instead of inheriting either the binary's
    # 0.2/0.5/0.5 or the 0.33 triple gen_configs.py otherwise pins.
    #
    # The corpus is H2H_TLSF_READY, all 25 families -- not SELECTION_SMOKE_SPECS.
    # That constant's four exclusions are a pure cost rule (families whose mean
    # wall time ran past 600 s, humanoid-742 capping at 7200 s on every seed),
    # and max_wall_s now bounds every run from inside, so the reason for the
    # exclusion is gone and it is retired for this campaign.
    #
    # Two profiles: `curves-calib` sizes the run, `curves` is the run. They keep
    # separate configs/results/CSV paths for the usual run_id-collision reason.
    "curves-calib": {
        "schemes": ["nsga2-apportion", "weighted"],
        "weakenings": ["wkoff"],
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["G"],
        "levels": {"G": ["mrs", "aurus"]},
        # 12 families spanning the cost range; see CURVES_CALIB_SPECS.
        "specs": CURVES_CALIB_SPECS,
        # 2 seeds: 12 families x 2 seeds x 4 arms = 96 runs, enough to read a
        # per-family wall time and a jobs-level peak RSS and nothing else. The
        # calibration is what fixes `curves`'s seed count.
        "seeds": list(range(2)),
        # 3600 s against the campaign's 1800 s internal deadline -- 2x headroom,
        # because max_wall_s stops the *search* and interrupts neither the
        # filters, the scoring, nor the final realizability gate, so a run
        # overruns by whatever the generation it was in had left. The external
        # cap is a backstop against a hung tool, not the budget; a run that hits
        # it has lost its row, which is the outcome the deadline exists to
        # avoid. Flat rather than per-family because the deadline, not the
        # family, is what ends every run here.
        "timeout_caps": {s: 3600 for s in CURVES_CALIB_SPECS},
        # 1800 s, as monotone, aurus-h2h-ship and selection-smoke set it.
        # accumulate_repairs is on and the budget is long, so a run emits far
        # more repairs than the 600 s default was sized for and a compare
        # timeout costs the row's implies_ideal silently.
        "compare_timeout": 1800,
        # Sweep G has no gen/pop axis, so there is no A/gen baseline to alias,
        # and neither arm is a grid baseline.
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-curves-calib",
        "results_dir": EXPERIMENTS_DIR / "results-curves-calib",
        "results_csv": EXPERIMENTS_DIR / "results-curves-calib.csv",
        # 16 rather than the 1 the other TLSF profiles use. That 1 is a RAM
        # argument -- ltlsynt is multi-GB resident per call and its concurrency
        # cap is per counter process, so N jobs multiply the cap by N -- but it
        # was sized for runs whose own thread pool was hardware concurrency.
        # These runs are single-threaded (parallel = 1), so each holds one
        # ltlsynt at a time and 16 jobs is 16 concurrent calls on a 32-core,
        # 125 GB machine. Unverified: 16 is the proposal this profile exists to
        # check, and the calibration's peak RSS is what confirms or lowers it
        # before `curves` runs.
        "default_jobs": 16,
    },
    "curves": {
        "schemes": ["nsga2-apportion", "weighted"],
        "weakenings": ["wkoff"],
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["G"],
        "levels": {"G": ["mrs", "aurus"]},
        "specs": H2H_TLSF_READY,
        # Six seeds, and the number is arithmetic rather than a power
        # calculation: at generations = 500 no run finishes before its
        # genetic.max_wall_s, so the campaign's cost is exactly
        # specs x arms x seeds x cap, or 25 x 4 x 6 x 400 s = 66.7 h against a
        # budget of 80 h. Must stay equal to the host split in
        # experiments/2026-08-28-selection-grading/campaign.toml, the two being
        # the same number written twice. Topping up is free -- the resume key
        # is read off the results CSV, so added seeds re-run nothing.
        "seeds": list(range(6)),
        "timeout_caps": {s: 3600 for s in H2H_TLSF_READY},
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-curves",
        "results_dir": EXPERIMENTS_DIR / "results-curves",
        "results_csv": EXPERIMENTS_DIR / "results-curves.csv",
        # See curves-calib: 16 on the single-threaded-run argument, unverified
        # until the calibration reads back a peak RSS.
        "default_jobs": 16,
    },
    # The same 2x2 as `curves`, budgeted in AuRUS's currency instead of seconds,
    # so a speed claim between the two tools has a common denominator. AuRUS
    # stops at GA_MAX_NUM_INDIVIDUALS = 1000 bred individuals at
    # GA_POPULATION_SIZE = 100 -- confirmed three ways: the settings banner of
    # all 679 archived h2h logs that carry one, the two
    # `numberOfVisitedIndividuals++` sites in GeneticAlgorithm.java (per accepted
    # mutant and per crossover offspring, both before the population is scored),
    # and Brizzio et al., GECCO '23 sec. 6, "the termination criterion is reached
    # either when 1000 individuals are generated or after 2hrs of execution
    # time". It is not a cap on solutions: no archived run reached 1000 of those,
    # the median being 486 and the maximum 741.
    #
    # `curves` measured the same four arms at a 400 s deadline, which is the
    # wrong currency for this question in counter's favour -- its manifests read
    # a median 81 generations at population 200 inside that deadline, about
    # 16,200 offspring, or 16x what AuRUS is allowed. Here the budget is fixed
    # and wall time becomes the measured outcome.
    #
    # generations = 500 rather than the ~12 that 1000 individuals needs at
    # population 100 (offspring_n is population_size - elite_n = 90 a generation,
    # and only an offspring differing from its parent counts). The generation
    # limit must not be what stops the run, or the campaign silently measures
    # something else; `stopped_by` must read `individuals` on every row and that
    # is the first thing to check in the results.
    #
    # Generate with
    #   python scripts/gen_configs.py --tlsf \
    #       --schemes nsga2-apportion weighted --sweeps G --levels mrs,aurus \
    #       --metric log --weakening off --weights 0.1 0.2 0.7 \
    #       --termination individuals --max-individuals 1000 \
    #       --generations 500 --population-size 100 --parallel 1 \
    #       --out-dir experiments/configs-matched --pin-vintage
    "matched": {
        "schemes": ["nsga2-apportion", "weighted"],
        "weakenings": ["wkoff"],
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["G"],
        "levels": {"G": ["mrs", "aurus"]},
        "specs": H2H_TLSF_READY,
        # 30 rather than the 6 `curves` ran, matching the 30 repeats a family
        # AuRUS's archived corpus carries (15 per host on each of av2 and av3).
        # The endpoint is a survival curve and 6 repeats per family per arm
        # estimates one poorly, which is the whole reason to spend the budget
        # here rather than on a longer deadline. A budget of 1000 individuals is
        # what makes it affordable: the smoke run measured 210 s on minepump at
        # this config, 73 s of it search and the rest the gate and the
        # implication filter, against the 2012 s `curves` averaged at a 400 s
        # search. At a mean of 500 s the 3000 runs are about 13 h of wall clock
        # over the two hosts.
        #
        # Must stay consistent with the campaign-level host split, which is the
        # same number written twice -- a host range wider than this list runs
        # nothing for the excess. Topping up further is free either way: the
        # resume key is read off the results CSV, so added seeds re-run nothing.
        "seeds": list(range(30)),
        # 7200 s rather than the 3600 `curves` used, and the reason is that
        # campaign's own casualty rate: 245 of its 600 runs were killed at 3600 s
        # and lost their manifests, because genetic.max_wall_s bounds the search
        # and nothing after it -- the final realizability gate and the
        # implication filter run past the deadline uncapped and cost about as
        # much again. A budget of 1000 individuals leaves a far smaller
        # accumulated set to filter, so this should not bind at all; it is here
        # so that a family which surprises us is measured rather than censored.
        # It also matches AuRUS's own GA_EXECUTION_TIMEOUT.
        "timeout_caps": {s: 7200 for s in H2H_TLSF_READY},
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-matched",
        "results_dir": EXPERIMENTS_DIR / "results-matched",
        "results_csv": EXPERIMENTS_DIR / "results-matched.csv",
        "default_jobs": 16,
    },
    # The 2026-08-29-aurus-matched design re-run at the engine `main` carries
    # after #170. Same 2x2, same 25 families, same 30 seeds, same 1000-individual
    # budget at population 100, same 7200 s cap -- so the only declared
    # difference from that archive is the binary, and the campaign is a
    # measurement of the tool as it now stands rather than a re-scoring of the
    # old one. Six commits move the search path between the two (0ef4d47,
    # 20d83e6, 9d70449, ed3b5dd, 720b4a9, 332a621), so its rows are not
    # comparable to the archive's row by row and nothing here pairs against
    # them; `4ad290d` also collapses equivalent specs in the implication filter,
    # which moves `n_repairs` and `n_implies` on their own.
    #
    # Sized by the `rematch-calib` phase rather than by the archive: no campaign
    # has run at this engine, and #170 took the final filter stage from 264 s of
    # a 312 s humanoid-531 run to a fraction of it, so the archive's 1152.1
    # core-hours is an upper bound of unknown tightness.
    #
    #   python scripts/gen_configs.py --tlsf \
    #       --schemes nsga2-apportion weighted --sweeps G --levels mrs,aurus \
    #       --metric log --weakening off --weights 0.1 0.2 0.7 \
    #       --termination individuals --max-individuals 1000 \
    #       --generations 500 --population-size 100 --parallel 1 \
    #       --out-dir experiments/configs-rematch --pin-vintage
    "rematch": {
        "schemes": ["nsga2-apportion", "weighted"],
        "weakenings": ["wkoff"],
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["G"],
        "levels": {"G": ["mrs", "aurus"]},
        "specs": H2H_TLSF_READY,
        "seeds": list(range(30)),
        "timeout_caps": {s: 7200 for s in H2H_TLSF_READY},
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-rematch",
        "results_dir": EXPERIMENTS_DIR / "results-rematch",
        "results_csv": EXPERIMENTS_DIR / "results-rematch.csv",
        "default_jobs": 16,
    },
    # Calibration for `rematch`, and the only thing it decides is whether the
    # main phase is affordable. Six families spanning the archive's measured
    # cost range at 2 seeds and all four cells: the three that dominated the
    # archive's bill (humanoid-742 at 60.0 core-hours in the control cell,
    # humanoid-531 57.8, pcar-v2-888 41.4), one upper-middle (full-arbiter-aurus
    # 23.3), and two cheap ones (minepump, rg2) so the light half is not
    # extrapolated from the heavy. Chosen on the archive's cost and on nothing
    # else -- picking families by how well an arm scored on them selects the
    # corpus on the response, which is what PLAN.md section 6 forbids.
    "rematch-calib": {
        "schemes": ["nsga2-apportion", "weighted"],
        "weakenings": ["wkoff"],
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["G"],
        "levels": {"G": ["mrs", "aurus"]},
        "specs": ["humanoid-742", "humanoid-531", "pcar-v2-888",
                  "full-arbiter-aurus", "minepump", "rg2"],
        "seeds": list(range(2)),
        "timeout_caps": {s: 7200 for s in H2H_TLSF_READY},
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-rematch",
        "results_dir": EXPERIMENTS_DIR / "results-rematch-calib",
        "results_csv": EXPERIMENTS_DIR / "results-rematch-calib.csv",
        "default_jobs": 16,
    },
    # nsga2 vs nsga2-replicate on FRETISH, at the gen40/pop1000 operating point
    # the cj-large and metric campaigns used — so the control arm is checkable
    # against their rows rather than being taken on trust. Three arms:
    #   nsga2-truncate/sweep_R    control
    #   nsga2-apportion/sweep_R   treatment
    #   nsga2-truncate/sweep_S    compute-matched control (see gen_configs)
    # The design is the config tree, not this table: sweep S is generated under
    # the truncate scheme only, so listing both schemes and both sweeps still
    # yields three arms rather than four. Generate with
    #   python scripts/gen_configs.py --schemes nsga2-truncate nsga2-apportion \
    #       --sweeps R --generations 40 --population-size 1000 \
    #       --out-dir experiments/configs-replicate
    #   python scripts/gen_configs.py --schemes nsga2-truncate --sweeps S \
    #       --generations 40 --population-size 1000 \
    #       --compute-match-factor <measured> \
    #       --out-dir experiments/configs-replicate
    #
    # Responses: implies_ideal (quality) with fsm carrying the power — it is the
    # only FRETISH family measured off both bounds (0.46-0.53 across cj-large
    # and metric) — fsm-combined (0.08) testing upward movement, and
    # takeoff/fsm-timing (~1.0) as saturation controls that should not move.
    # found_repair, n_repairs and wall_time_s are secondary.
    "replicate": {
        "schemes": ["nsga2-truncate", "nsga2-apportion"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["R", "S"],
        "levels": {},
        "specs": list(FRETISH_SPECS_2026_07),
        # Seed-major disjoint ranges across av2/av3 (pass --seeds on launch).
        # 200 pairs per cell resolves ~0.15 absolute on implies_ideal at fsm's
        # p~=0.5, ~0.10 pooled over the two elitism levels. Smaller effects are
        # out of reach at any budget this campaign can afford — 0.05 would need
        # ~1600 seeds per arm.
        "seeds": list(range(200)),
        # cj-large's caps for this operating point, scaled for the two arms that
        # cost more: replicate breeds from a replicated population and sweep S
        # runs a longer generation budget. Sized never to bite — a cap that
        # fires records implies_ideal = 0 for a run that was merely slow.
        "timeout_caps": {"takeoff": 900, "fsm": 900, "fsm-timing": 900,
                         "fsm-combined": 1500},
        # The treatment arm's whole claim is that it keeps more distinct
        # candidates, which shows up as more repairs, and compare's cost scales
        # with them. At the default 600 s the arm under test would be the one
        # whose comparisons get censored.
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-replicate",
        "results_dir": EXPERIMENTS_DIR / "results-replicate",
        "results_csv": EXPERIMENTS_DIR / "results-replicate.csv",
        "default_jobs": 4,
    },
    # The weakening cross of the FRETISH comparison, on the two specs with
    # headroom. `replicate` runs at the shipped run_weakening = true, where the
    # filter drops about 70% of each generation's offspring -- so it measures
    # replicate's advantage in a configuration that discards most of what
    # breeding produces, and the 2026-07-15 cj-large campaign found that turning
    # the filter off raises implies_ideal. This arm runs the same sweep R cells
    # with the filter off, to say whether "replicate keeps more distinct
    # candidates" survives the stage that throws most of them away.
    #
    # Only fsm and fsm-combined: takeoff and fsm-timing sit at implies_ideal
    # ~1.0, so a second factor buys nothing there. Arm C is not crossed in --
    # compute-matching is a statement about the scheme's cost, inherited from
    # the wkon arm -- so this is a two-arm comparison.
    #
    # It shares `replicate`'s results directory and CSV deliberately: the
    # weakening column separates the rows, run_id carries a _wkoff tag so no run
    # directory collides with the flat-layout runs, and the analysis reads one
    # file. Generate the configs it reads with
    #   python scripts/gen_configs.py --schemes nsga2-truncate nsga2-apportion \
    #       --sweeps R --weakening off --generations 40 --population-size 1000 \
    #       --out-dir experiments/configs-replicate
    #
    # The caps are 3600 s, not the wkon arm's 900/1500. Sweep R's partial results
    # showed the tighter caps censoring only the replicate arm -- 250 of 352
    # fsm-timing pairs, 56 of 358 fsm-combined -- because they were sized off
    # nsga2's costs and replicate runs several times longer. A cap that fires on
    # one arm and never on the other is a one-sided filter on the response under
    # test, so this arm gets a cap loose enough to bind on neither.
    "replicate-wkoff": {
        "schemes": ["nsga2-truncate", "nsga2-apportion"],
        "weakenings": ["wkoff"],
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["R"],
        "levels": {},
        "specs": ["fsm", "fsm-combined"],
        "seeds": list(range(200)),
        "timeout_caps": {"fsm": 3600, "fsm-combined": 3600},
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-replicate",
        "results_dir": EXPERIMENTS_DIR / "results-replicate",
        "results_csv": EXPERIMENTS_DIR / "results-replicate.csv",
        "default_jobs": 4,
    },
    # Sweep R again at a cap that binds on neither arm, to replace the rows the
    # original caps censored. Sweep R sized its timeouts off nsga2's costs, and
    # replicate runs several times longer, so the cap fired on the treatment arm
    # alone -- 250 of 352 fsm-timing pairs, 56 of 358 fsm-combined, 34 of 354
    # takeoff, against zero for nsga2 in every cell. A one-sided cap on the
    # response under test is a bias, not a budget. fsm is absent because nothing
    # censored there.
    #
    # It shares `replicate`'s CSV and results dir so resume does the selection:
    # delete the censored rows (and their run directories), point this profile at
    # the same cells, and only the deleted ones execute. Recap rows are therefore
    # indistinguishable from originals by key -- deliberately, since the analysis
    # wants one row per cell -- but recoverable from the data, as any surviving
    # row censored at the old cap still reads timed_out=1 at wall_time_s == 900
    # or 1500.
    #
    # fsm-timing is expected to be run on a seed subsample via --seeds: it scores
    # implies_ideal 1.000 in both arms on every pair that completed, so its
    # re-run buys an honest wall-time ratio rather than a quality contrast, and
    # the full 250 costs about 31 h per machine against roughly 6 h for 50 seeds.
    "replicate-recap": {
        "schemes": ["nsga2-truncate", "nsga2-apportion"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["R"],
        "levels": {},
        "specs": ["takeoff", "fsm-timing", "fsm-combined"],
        "seeds": list(range(200)),
        "timeout_caps": {
            "takeoff": 3600, "fsm-timing": 3600, "fsm-combined": 3600},
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-replicate",
        "results_dir": EXPERIMENTS_DIR / "results-replicate",
        "results_csv": EXPERIMENTS_DIR / "results-replicate.csv",
        "default_jobs": 4,
    },
    # The TLSF half of the same comparison, at the TLSF baseline operating point
    # (gen10/pop200) for comparability with the prior TLSF data. Two arms only:
    # the compute-matched control rides on the FRETISH half, where the responses
    # are less saturated and a run costs minutes rather than tens of minutes.
    # Generate with
    #   python scripts/gen_configs.py --tlsf \
    #       --schemes nsga2-truncate nsga2-apportion \
    #       --sweeps R --out-dir experiments/configs-replicate-tlsf
    # (--tlsf carries the 500 ms ltlsynt timeout, 60 s ltl2tgba timeout and 0.15
    # scoring tolerance the heavy specs need.)
    #
    # Specs chosen by measured headroom from the 2026-07-21 muc campaign at this
    # operating point: arbiter (found_repair 0.50) and lily02 (implies_ideal
    # 0.67) are the two that can move; gyro-var1, lift and minepump sit at
    # implies_ideal 0.000 with found_repair ~1.0 and serve as negative controls.
    # humanoid-531 is excluded on cost alone (965 s mean, 2400 s p90).
    "replicate-tlsf": {
        "schemes": ["nsga2-truncate", "nsga2-apportion"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["R"],
        "levels": {},
        "specs": ["arbiter", "gyro-var1", "lift", "lily02", "minepump"],
        "seeds": list(range(60)),
        # The muc profile's caps, roughly doubled: the treatment arm runs longer
        # and its extra survivors each cost realizability checks.
        "timeout_caps": {"arbiter": 300, "gyro-var1": 600, "lift": 1200,
                         "lily02": 300, "minepump": 300},
        "compare_timeout": 1200,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-replicate-tlsf",
        "results_dir": EXPERIMENTS_DIR / "results-replicate-tlsf",
        "results_csv": EXPERIMENTS_DIR / "results-replicate-tlsf.csv",
        # jobs=1 for the same RAM reason as the tlsf profile: ltlsynt is
        # multi-GB resident per call on these specs.
        "default_jobs": 1,
    },
    # Does nsga2-apportion's diversity unlock the arbiter family, or only the one
    # `arbiter` spec? See experiments/2026-08-10-arbiter-probe/PLAN.md for the
    # pre-registered decision rule. This is a generality probe, not a rerun of
    # 2026-07-31-replicate: that campaign's decision failed on FRETISH cost and
    # on the compute-matched control, neither of which this can reopen, and it
    # deliberately carries no arm C for the same reason.
    #
    # Both schemes, one level. Sweep R restricted to elit0.1 pins elitism_rate
    # into the archived config at the shipped default rather than inheriting it,
    # because 2026-08-07-elitism may yet move that default and an inherited key
    # would silently restate what this campaign ran under.
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
        # 40 seeds x 9 arbiter families = 360 pooled pairs. The effect under test
        # is the replicate arbiter split (0/120 vs 120/120); 40 pairs per family
        # resolve ~0.3 absolute on found_repair, which is far coarser than that
        # split and still fine enough to separate "unlocks the family" from
        # "unlocked one spec". Seed-major disjoint ranges across av2/av3.
        "seeds": list(range(40)),
        # elitism-tlsf's flat 900 s on the same operating point and hosts. Sized
        # never to bite: replicate censored its treatment arm alone with caps cut
        # to nsga2's costs, and apportion is the treatment arm here too.
        "timeout_caps": {spec: 900 for spec in ARBITER_PROBE_SPECS},
        # apportion's whole claim is more distinct survivors, so it returns more
        # repairs and compare's cost scales with them — the same asymmetry that
        # censored replicate's comparisons at the 600 s default.
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-arbiter-probe",
        "results_dir": EXPERIMENTS_DIR / "results-arbiter-probe",
        "results_csv": EXPERIMENTS_DIR / "results-arbiter-probe.csv",
        # jobs=1 for the same RAM reason as every other TLSF profile.
        "default_jobs": 1,
    },
    # ── 2026-08-11-selection-default ────────────────────────────────────────
    # Whether nsga2-apportion should replace nsga2-truncate as the shipped
    # default. See experiments/2026-08-11-selection-default/PLAN.md for the
    # pre-registered decision rule, its five criteria and the 0.05
    # non-inferiority margin with the power calculation behind it.
    #
    # Three arms, and they are split across two profiles per path rather than
    # crossed inside one. A single profile listing both schemes and both sweeps
    # emits the full product, which would add a fourth cell -- apportion at
    # matched generations -- that no criterion reads and that costs a third of
    # the campaign's wall time. The -cm profiles therefore carry the compute
    # control alone, and share the CSV and run directory of their R half so the
    # three arms merge and analyse as one dataset. KEY_FIELDS includes `sweep`
    # and `level_name`, so R/elit0.1 and S/cm-elit0.1 are distinct keys and the
    # sharing cannot collide.
    #
    # The corpus is deliberately not enriched: every FRETISH spec and the whole
    # 20-family TLSF corpus, with `arbiter` one family of twenty. 2026-08-10
    # measured its effect on a corpus picked because that effect was there,
    # which is why it could not speak to a default.
    #
    # Generate the R halves with
    #   python scripts/gen_configs.py [--tlsf] \
    #       --schemes nsga2-truncate nsga2-apportion --sweeps R --levels elit0.1 \
    #       --out-dir experiments/configs-seldefault[-tlsf] --pin-vintage
    # and the compute control with the ratio measured in calibration (PLAN §5):
    #   python scripts/gen_configs.py [--tlsf] --schemes nsga2-truncate \
    #       --sweeps S --levels cm-elit0.1 --compute-match-factor <measured> \
    #       --out-dir experiments/configs-seldefault[-tlsf] --pin-vintage
    # --pin-vintage on both, per PLAN §8: without it a config states only the
    # keys its sweep overrode, and the three whose C++ defaults have moved are
    # inherited from whatever binary reads them next. The factor is per path --
    # FRETISH and TLSF calibrate separately and measured 1.372 and 1.117.
    "seldefault-fret": {
        "schemes": ["nsga2-truncate", "nsga2-apportion"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["R"],
        "levels": {"R": ["elit0.1"]},
        "specs": list(FRETISH_SPECS_2026_07),
        # 4 specs x 70 seeds = 280 pairs per contrast, which is what PLAN §7's
        # power table requires for the 0.05 margin to clear at a true adverse
        # effect of -0.01, given the 0.341 paired SD measured on the elitism
        # campaign's 600 FRETISH pairs.
        "seeds": list(range(70)),
        "timeout_caps": {"takeoff": 900, "fsm": 900, "fsm-timing": 900,
                         "fsm-combined": 1800},
        # As arbiter-probe: apportion returns more repairs, and compare's cost
        # scales with them.
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-seldefault",
        "results_dir": EXPERIMENTS_DIR / "results-seldefault",
        "results_csv": EXPERIMENTS_DIR / "results-seldefault.csv",
        # 4 as on every FRETISH profile; ltlsynt is not in play here, so the
        # per-call RAM ceiling that pins the TLSF profiles to 1 does not bind.
        # Both halves must agree: wall_time_s is a response variable and arm C
        # is defined by it, so a contention difference between the arms would
        # land straight in the ratio that sets arm C's own generation count.
        "default_jobs": 4,
    },
    "seldefault-fret-cm": {
        "schemes": ["nsga2-truncate"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["S"],
        "levels": {"S": ["cm-elit0.1"]},
        "specs": list(FRETISH_SPECS_2026_07),
        "seeds": list(range(70)),
        # Arm C runs more generations than arm A by construction, so it must not
        # inherit a cap sized for A: censoring the control alone is how replicate
        # lost its comparison.
        "timeout_caps": {"takeoff": 1800, "fsm": 1800, "fsm-timing": 1800,
                         "fsm-combined": 3600},
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-seldefault",
        "results_dir": EXPERIMENTS_DIR / "results-seldefault",
        "results_csv": EXPERIMENTS_DIR / "results-seldefault.csv",
        "default_jobs": 4,
    },
    "seldefault-tlsf": {
        "schemes": ["nsga2-truncate", "nsga2-apportion"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["R"],
        "levels": {"R": ["elit0.1"]},
        "specs": list(TLSF_ABLATION_SPECS),
        # 20 families x 25 seeds = 500 pairs, above the 420 PLAN §7 requires at
        # the 0.418 paired SD measured on the arbiter probe's 400 TLSF pairs.
        "seeds": list(range(25)),
        # The R half runs at gen10/pop200, the operating point the caps were
        # calibrated at, so it takes them unscaled.
        "timeout_caps": dict(TLSF_WALL_CAPS_GEN10),
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-seldefault-tlsf",
        "results_dir": EXPERIMENTS_DIR / "results-seldefault-tlsf",
        "results_csv": EXPERIMENTS_DIR / "results-seldefault-tlsf.csv",
        # jobs=1 for the same RAM reason as every other TLSF profile.
        "default_jobs": 1,
    },
    "seldefault-tlsf-cm": {
        "schemes": ["nsga2-truncate"],
        "weakenings": None,
        "metrics": None,
        "repair_modes": None,
        "sweeps": ["S"],
        "levels": {"S": ["cm-elit0.1"]},
        "specs": list(TLSF_ABLATION_SPECS),
        "seeds": list(range(25)),
        # Raised over the R half for the same reason as seldefault-fret-cm.
        # CM_CAP_HEADROOM is a ceiling on the compute-match factor rather than
        # the factor itself: the factor is only known after calibration, which
        # runs long after this dict is imported, so the cap cannot track it and
        # the invariant is instead that it never falls below it. Calibration on
        # 2026-08-13 measured 1.117 on this path (11 generations against 10),
        # well inside the ceiling. A cap is only wrong when it censors, so the
        # slack costs nothing; a --compute-match-factor above CM_CAP_HEADROOM
        # is the case to catch, and raise this for.
        "timeout_caps": scaled_wall_caps(CM_CAP_HEADROOM),
        "compare_timeout": 1800,
        "baseline_aliases": {},
        "configs_dir": EXPERIMENTS_DIR / "configs-seldefault-tlsf",
        "results_dir": EXPERIMENTS_DIR / "results-seldefault-tlsf",
        "results_csv": EXPERIMENTS_DIR / "results-seldefault-tlsf.csv",
        "default_jobs": 1,
    },
    # The ops-pilot, opswk-fret, opswk-tlsf, ops-tlsf and ops-fret profiles are
    # retired. All five ran sweep O, the cross of `[genetic]
    # repaired_operators`; that key is gone and the repaired grammar is the only
    # one, so the sweep can no longer be generated and neither can a profile that
    # names it. merge_experiments.py keeps its entries for them, as it does for
    # wellsep-timing, so the archived CSVs stay mergeable.
    #
    # The assumption-reach profile is retired on the same terms. It ran sweep U,
    # two of whose three operator levels set `[tlsf.mutation]
    # p_union_assumption`; that key is gone with the union crossover it armed,
    # so the sweep cannot be generated and neither can this profile.
    # `experiments/2026-08-26-assumption-reach` is the record, and it reproduces
    # from its own vendored scripts/ at the commit its PROVENANCE.json names.
    #
    # Five more retired on 2026-09-11, when `[filters] run_weakening` and
    # `[filters] run_well_separation` were removed with the filters they
    # switched. cj-large and weakening-arbiter crossed the weakening filter on
    # and off through gen_configs --weakening, which now emits the wkoff
    # directory alone; wellsep, arbiter-hp and arbiter-padd ran TLSF sweeps W
    # and Q, whose every level states run_well_separation. None of the five can
    # be regenerated, and merge_experiments.py keeps its entries for them so the
    # archived CSVs stay mergeable. Each archive reproduces from its own
    # vendored scripts/ at the commit its PROVENANCE.json names.
    # ── 2026-09-09 FRETISH selection x grading ───────────────────────────────
    #
    # The FRETISH replication of experiments/2026-08-28-selection-grading:
    # selection_scheme (nsga2-apportion against weighted) crossed with
    # fitness.status_grading (mrs, counter's own, against aurus, AuRUS's
    # six-level ladder). Two schemes x two sweep-K levels is the whole cross;
    # the schemes are config directories and the grading arm rides in
    # level_name, so both are already key columns and neither needs a new one.
    #
    # The factor is only measurable on this path from 2026-09-09. The FRETISH
    # final realizability gate hard-coded Tiered until then, so a run configured
    # `aurus` scored its search on the six-level ladder and judged its output on
    # the three-point one; the admitted set never moved, the correctness table
    # being applied whatever the scale folded in, but the grading arm could not
    # be read at the gate. Running this against an earlier binary measures the
    # search alone.
    #
    # Unlike the TLSF twin this is a point rather than a curve: it runs the
    # FRETISH ablation operating point, gen40/pop1000, which is what every
    # archived FRETISH sweep ran, and genetic.max_wall_s = 7200 is a backstop
    # rather than the terminator. A 10-run probe at gen40/pop1000 measured
    # fsm at 151s, mode-arbiter at 345-606s and takeoff at 250s, so most runs
    # end on generations; the deadline exists because that probe ran under a
    # 900s harness timeout and lost five of ten runs, and a run that stops on
    # its own deadline writes its repairs and run.json where a harness kill
    # writes neither.
    #
    # timeout_caps at 16200s is the backstop above the backstop, sized on the
    # 2.2x rule: 2026-08-28-selection-grading ran a 400s deadline into 335
    # compute-hours against the 67 the cap predicted, and 245 of its 600 runs
    # were then killed by a 3600s timeout_caps and lost their manifests. The
    # gate and the implication filter run past max_wall_s uncapped, and the
    # filter is quadratic in the accumulated set, so the margin is deliberate.
    #
    # The weights are 0.1 / 0.2 / 0.7 (syntactic / semantic / status), AuRUS's
    # published triple, matching the TLSF campaign exactly so the two are
    # readable against each other on the grading factor. Only the weighted
    # scheme reads them, so they are inert for the two nsga2-apportion arms.
    # This makes the selection factor "NSGA-II against AuRUS-weighted
    # scalarisation" rather than "against weighting in general", the confound
    # 2026-08-28-selection-grading/PLAN.md registered before its own run.
    #
    # run_weakening is off, matching the binary's default since 2026-08-20
    # rather than gen_configs.py's pinned true, so the campaign measures the
    # shipping screen rather than one no default run applies.
    #
    # Generate with
    #   python3 scripts/gen_configs.py --schemes nsga2-apportion weighted \
    #       --sweeps K --levels mrs,aurus --metric log --weakening off \
    #       --weights 0.1 0.2 0.7 --generations 10 --population-size 200 \
    #       --max-wall-s 1800 \
    #       --out-dir experiments/configs-gradsel-fret-m --pin-vintage
    "gradsel-fret": {
        "schemes": ["nsga2-apportion", "weighted"],
        # Both stated rather than left None, and they must match the generator's
        # --weakening/--metric flags: these are path segments of the config tree
        # rather than declarations, so a None here globs `<scheme>/sweep_*.toml`
        # while gen_configs.py has written `<scheme>/wkoff/log/sweep_*.toml`,
        # and the runner exits on an empty configs directory.
        "weakenings": ["wkoff"],
        "metrics": ["log"],
        "repair_modes": None,
        "sweeps": ["K"],
        "levels": {"K": ["mrs", "aurus"]},
        # All five families, mode-arbiter included: it contributes
        # implies_ideal = 0 by construction (see FRETISH_SPECS) and is carried
        # for found_repair and n_repairs, being the only scoped family and so
        # the only run that exercises the scope machinery at all.
        "specs": list(FRETISH_SPECS),
        "seeds": list(range(30)),
        # 2.25x the 1800 s deadline, which the deadline itself does not bound:
        # the final gate and the implication filter both run past it, and the
        # TLSF twin overran its own by 2.2x.
        "timeout_caps": {"takeoff": 4050, "fsm": 4050, "fsm-timing": 4050,
                         "fsm-combined": 4050, "mode-arbiter": 4050},
        # Only sweep K runs, so there is no A/gen baseline to alias onto.
        "baseline_aliases": {},
        # -m for matched: the first declaration ran gen40/pop1000 and its 94
        # surviving rows are under the unsuffixed names on both hosts. The
        # resume key holds neither the operating point nor the binary, so a
        # re-run against those directories would skip the finished cells and
        # merge two operating points and two implication checks into one CSV.
        "configs_dir": EXPERIMENTS_DIR / "configs-gradsel-fret-m",
        "results_dir": EXPERIMENTS_DIR / "results-gradsel-fret-m",
        "results_csv": EXPERIMENTS_DIR / "results-gradsel-fret-m.csv",
        # 4 as on every FRETISH profile; ltlsynt is not in play here, so the
        # per-call RAM ceiling that pins the TLSF profiles to 1 does not bind.
        "default_jobs": 4,
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


def counter_timeout(level_name: str, level_value) -> int:
    """Return a generous per-run timeout in seconds (full profile)."""
    if isinstance(level_value, int) and level_name.startswith("gen"):
        return max(120, level_value * 90)
    if isinstance(level_value, int) and level_name.startswith("pop"):
        return max(120, level_value // 2)
    return 300


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
    """Individuals counter dropped after a fitness function threw (0 if none)."""
    try:
        text = log_path.read_text(errors="replace")
    except OSError:
        return 0
    m = DROPPED_RE.search(text)
    return int(m.group(1)) if m else 0


def tail_line(log_path: Path) -> str:
    """Return the last non-blank line of a log, for console error context.

    counter draws progress with carriage returns, so a raw line may pack many
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


def git_output(*args: str) -> str | None:
    """Stripped stdout of `git -C REPO_ROOT <args>`, or None if there is none.

    One None covers every way the answer can be absent — git missing from
    PATH, a non-zero exit, a timeout, empty output — because each caller
    substitutes the same fallback for all of them. The except stays this wide
    on purpose: narrowing it to CalledProcessError turns a box without git
    into a crash at campaign launch rather than an unknown commit.
    """
    try:
        proc = subprocess.run(
            ["git", "-C", str(REPO_ROOT), *args],
            check=True, timeout=60, capture_output=True, text=True)
    except (OSError, subprocess.SubprocessError):
        return None
    return proc.stdout.strip() or None


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
    afterwards from config mtimes is the guesswork the PROVENANCE.json files
    under experiments/ document; this writes it down at run time instead.
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
            for name, bin_path in (("counter", COUNTER_BIN),
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

    Caps counter's internal thread pool so that --jobs concurrent runs do not
    oversubscribe the machine (counter defaults to hardware_concurrency).

    A config that already states `parallel` is copied through unchanged. The cap
    exists to fill in for a config that says nothing, so that jobs * parallel
    stays near the core count; a stated value is the campaign's own claim about
    how each run is meant to execute, and overwriting it silently makes the
    thread pool a function of --jobs rather than of the design. The rule is
    general rather than a per-profile exemption because there is no config in
    any grid this reads that states the key by accident: gen_configs.py emits it
    only under --parallel, which a campaign passes deliberately (the `curves`
    profiles pass 1, their runs being single-threaded on purpose). Before that
    flag existed nothing emitted the key at all, so the rewrite branch this
    replaces was never reached and no archived grid changes meaning.
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
    """Execute counter (+ compare) once; return the metric columns.

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
        str(COUNTER_BIN),
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
            print(f"    [{run_id}] ERROR: counter exited {e.returncode}"
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
    parser.add_argument("--profile", choices=list(PROFILES), default="full",
                        help="Experiment profile (default: full)")
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
    versions = {"counter": dict(unknown_version),
                "compare": dict(unknown_version)}
    if not args.dry_run:
        for bin_path in [COUNTER_BIN, COMPARE_BIN]:
            if not bin_path.exists():
                sys.exit(
                    f"Binary not found: {bin_path}\n"
                    f"Run: cmake --workflow --preset release"
                )
        # Once, at startup: every run of this campaign uses these binaries, so
        # asking per run would cost thousands of subprocesses to learn one fact.
        versions = {"counter": binary_version(COUNTER_BIN),
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
    print(f"    binary:  {version_label('counter')}, "
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
        print(f"Per-run counter thread pool capped at parallel = {parallel_k}")

    lock = threading.Lock()
    state = {"completed": 0, "errors": 0, "rows_written": 0}
    n_exec = len(to_execute)
    t0 = time.monotonic()

    def execute(key: tuple) -> None:
        (scheme, weakening, metric, repair, c_sweep, c_level,
         spec_name, seed) = key
        cfg = configs_by_key[
            (scheme, weakening, metric, repair, c_sweep, c_level)]
        caps = profile["timeout_caps"]
        timeout = (caps[spec_name] if caps
                   else counter_timeout(c_level, level_value_of(c_level)))
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
                       "commit": versions["counter"]["commit_short"],
                       "dirty": versions["counter"].get("dirty", "")}
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
