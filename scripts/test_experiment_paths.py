#!/usr/bin/env python3
"""Round-trip tests for the factor-directory parsing in run_experiments.py.

No pytest dependency: run it directly (``python scripts/test_experiment_paths.py``)
and it exits non-zero on the first failure. The one thing worth guarding is that
``scheme_of`` / ``weakening_of`` / ``metric_of`` recover the right factor from a
path regardless of how deep the factors nest — a config that lands one level
deeper than a parser expects is silently mis-attributed to the wrong cell and
its rows key wrong (the trap REPORT.md flags).

The second half guards the commit-provenance columns, whose failure mode is the
mirror image: a column that joins the resume or merge key makes every archived
row miss and re-runs campaigns that are already finished.
"""

import csv
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import run_experiments as R  # noqa: E402
import gen_configs as G  # noqa: E402


def check(got, want, msg):
    if got != want:
        print(f"FAIL: {msg}\n  got:  {got!r}\n  want: {want!r}")
        sys.exit(1)


def factors(path_str):
    p = Path(path_str)
    return R.scheme_of(p), R.weakening_of(p), R.metric_of(p)


BASE = "experiments/configs/{}/sweep_C_default.toml"

# (layout, expected (scheme, weakening, metric)). LEGACY_WEAKENING/METRIC fill
# in for any factor segment the layout omits. The scheme component is the
# canonical name: the directories gen_configs.py writes today carry it already,
# and a directory named with a pre-rename spelling resolves through
# SCHEME_ALIASES to the same value, so the two layouts land on one resume key.
CASES = [
    # flat: predates both factors
    ("nsga2-truncate", ("nsga2-truncate", R.LEGACY_WEAKENING, R.LEGACY_METRIC)),
    # weakening only
    ("nsga2-truncate/wkon", ("nsga2-truncate", "wkon", R.LEGACY_METRIC)),
    ("weighted/wkoff", ("weighted", "wkoff", R.LEGACY_METRIC)),
    # metric only (weakening absent)
    ("nsga2-truncate/direct", ("nsga2-truncate", R.LEGACY_WEAKENING, "direct")),
    ("nsga2-apportion/log", ("nsga2-apportion", R.LEGACY_WEAKENING, "log")),
    # three deep: both factors, canonical <scheme>/<weakening>/<metric> order
    ("nsga2-truncate/wkon/direct", ("nsga2-truncate", "wkon", "direct")),
    ("nsga2-apportion/wkoff/log", ("nsga2-apportion", "wkoff", "log")),
    ("weighted/wkon/log", ("weighted", "wkon", "log")),
    # the same layouts under the pre-rename directory names, as every archived
    # config tree carries them
    ("nsga2", ("nsga2-truncate", R.LEGACY_WEAKENING, R.LEGACY_METRIC)),
    ("nsga2/wkon", ("nsga2-truncate", "wkon", R.LEGACY_METRIC)),
    ("nsga2/direct", ("nsga2-truncate", R.LEGACY_WEAKENING, "direct")),
    ("nsga2/log", ("nsga2-truncate", R.LEGACY_WEAKENING, "log")),
    ("nsga2/wkon/direct", ("nsga2-truncate", "wkon", "direct")),
    ("nsga2/wkoff/log", ("nsga2-truncate", "wkoff", "log")),
    ("nsga2-replicate/wkon/log", ("nsga2-apportion", "wkon", "log")),
]

for layout, want in CASES:
    check(factors(BASE.format(layout)), want, f"factors of <{layout}>")

# The scan is order-independent, so a swapped nesting still resolves — the
# guarantee the ancestor-scan buys over a fixed parent/parent.parent walk.
check(factors(BASE.format("nsga2-truncate/log/wkoff")),
      ("nsga2-truncate", "wkoff", "log"), "swapped metric/weakening nesting")
check(factors(BASE.format("nsga2/log/wkoff")),
      ("nsga2-truncate", "wkoff", "log"),
      "swapped nesting under the pre-rename scheme name")

# A name outside the mapping passes through untouched, so a scheme added later
# needs no entry.
check(R.canonical_scheme("weighted"), "weighted", "unmapped scheme unchanged")
check(R.canonical_scheme("nsga2-truncate"), "nsga2-truncate",
      "canonical_scheme is idempotent")

# gen_configs emits directory names, so it must emit canonical ones only: a
# pre-rename name here would write a config the binary refuses to load.
for s in G.SCHEMES:
    check(R.canonical_scheme(s), s, f"gen_configs SCHEMES entry <{s}> is current")
check(G.DEFAULTS["selection_scheme"],
      R.canonical_scheme(G.DEFAULTS["selection_scheme"]),
      "gen_configs DEFAULTS pins a current selection_scheme")


def repair(path_str):
    return R.repair_mode_of(Path(path_str))


# repair_mode nests deepest; its presence must not disturb the other factors,
# and scheme_of must still skip a repair segment to find the scheme.
REPAIR_CASES = [
    ("nsga2", R.LEGACY_REPAIR),                     # flat: predates the factor
    ("nsga2/mono", "mono"),
    ("nsga2/muc", "muc"),
    ("nsga2/wkon/direct/muc", "muc"),               # four deep, canonical order
    ("nsga2/muc/wkon", "muc"),                       # order-independent scan
]
for layout, want in REPAIR_CASES:
    check(repair(BASE.format(layout)), want, f"repair_mode of <{layout}>")

# A repair segment must not be mistaken for the scheme or any other factor.
check(factors(BASE.format("nsga2/wkon/direct/muc")),
      ("nsga2-truncate", "wkon", "direct"), "factors ignore the repair segment")
check(repair(BASE.format("nsga2/wkon/direct")), R.LEGACY_REPAIR,
      "repair_mode falls back to LEGACY_REPAIR when absent")

# gen_configs REPAIRS maps the short dir label to the TOML value the C++ parser
# accepts; repair_mode_of must key on the label, not the value.
check([lbl for lbl, _ in G.REPAIRS["both"]], ["mono", "muc"],
      "REPAIRS both dir labels")
check([val for _, val in G.REPAIRS["both"]], ["monolithic", "muc"],
      "REPAIRS both toml values")
for lbl, _ in G.REPAIRS["both"]:
    assert lbl in R.REPAIR_DIRS, f"{lbl} not in REPAIR_DIRS"

# extract_metadata is unaffected by the extra depth.
p = Path(BASE.format("nsga2/wkon/direct"))
check(R.extract_metadata(p)[:2], ("C", "default"), "extract_metadata three-deep")

# gen_configs METRICS maps the short dir label to the full TOML value the C++
# parser accepts; metric_of must key on the label, not the value.
check([lbl for lbl, _ in G.METRICS["both"]], ["direct", "log"],
      "METRICS both dir labels")
check([val for _, val in G.METRICS["both"]], ["direct", "logarithmic"],
      "METRICS both toml values")
for lbl, _ in G.METRICS["both"]:
    assert lbl in R.METRIC_DIRS, f"{lbl} not in METRIC_DIRS"

# The merge key must carry every crossed factor, or crossed rows collapse.
import merge_experiments as M  # noqa: E402
for f in ("selection", "weakening", "metric", "repair_mode"):
    assert f in M.KEY_FIELDS, f"{f} missing from merge KEY_FIELDS"
    assert f in R.CSV_FIELDS, f"{f} missing from CSV_FIELDS"


# ── Ablation-campaign profile invariants ─────────────────────────────────────

P = R.PROFILES

# Extending TLSF_SPECS for the ablation campaign must not grow the corpora of
# the profiles whose results CSVs are already recorded against the original
# six families.
check(P["tlsf"]["specs"], R.TLSF_CORE_SPECS, "tlsf profile corpus")
check(P["muc"]["specs"], R.TLSF_CORE_SPECS, "muc profile corpus")
check(P["padd"]["specs"],
      [s for s in R.TLSF_CORE_SPECS if s != "humanoid-531"],
      "padd profile corpus")

# The two ablation arms are the same 4-cell factorial: 2 schemes x 2 metrics, at
# the single sweep-C default level. The third factor was the Halstead weight,
# retired on 2026-08-13 with the objective itself.
check(len(R.TLSF_ABLATION_SPECS), 20, "ablation TLSF corpus size")
for name in ("ablate-fret", "ablate-tlsf"):
    check(sorted(P[name]["schemes"]), ["nsga2-truncate", "weighted"],
          f"{name} schemes")
    check(P[name]["metrics"], ["direct", "log"], f"{name} metrics")
    check(P[name]["levels"], {"C": ["default"]}, f"{name} sweep-C levels")
    check(P[name]["baseline_aliases"], {}, f"{name} has no aliases")
check(P["ablate-tlsf"]["specs"], R.TLSF_ABLATION_SPECS, "ablate-tlsf corpus")

# h2h-tlsf dedups against ablate-tlsf by sharing its configs dir, results dir
# and results CSV: the resume key carries every factor, so the control-cell
# rows ablate-tlsf completed are skipped rather than re-run. If any of these
# three diverge the dedup silently breaks and the top-up re-runs 195 rows.
for field in ("configs_dir", "results_dir", "results_csv"):
    check(P["h2h-tlsf"][field], P["ablate-tlsf"][field],
          f"h2h-tlsf shares ablate-tlsf {field}")
# ... and it must stay inside the control cell, with the metric crossed (not
# None/legacy) so its run_id and CSV metric column match ablate-tlsf's log cell.
check(P["h2h-tlsf"]["schemes"], ["nsga2-truncate"], "h2h-tlsf control scheme")
check(P["h2h-tlsf"]["metrics"], ["log"], "h2h-tlsf control metric")
check(P["h2h-tlsf"]["levels"], {"C": ["default"]}, "h2h-tlsf control level")
check(P["h2h-tlsf"]["specs"], R.H2H_TLSF_SPECS_2026_07, "h2h-tlsf corpus")
check(P["aurus-h2h"]["specs"], R.H2H_TLSF_READY, "aurus-h2h corpus")
# The two profiles differ in the two things the resume key does not carry, so
# they must never share a results CSV: a 600s row and a 7200s row would land
# under one key and the merge would keep whichever arrived first.
assert (P["aurus-h2h"]["results_csv"] != P["h2h-tlsf"]["results_csv"]), \
    "aurus-h2h must not share h2h-tlsf's results CSV"
check(sorted(set(P["aurus-h2h"]["timeout_caps"].values())), [7200],
      "aurus-h2h wall cap matches AuRUS's published 2h GATO")

# The head-to-head corpus is the AuRUS paper's evaluation set, all 26 rows.
check(len(R.H2H_TLSF_SPECS), 26, "h2h TLSF corpus size")
# The July corpus is frozen separately so changing the live list cannot change
# what the h2h-tlsf profile — that campaign's definition — means.
check(len(R.H2H_TLSF_SPECS_2026_07), 12, "2026-07 h2h TLSF corpus size")
# codesample-un1 and codesample-un2 were July rows and are not rows in the
# paper's tables, so the two corpora differ in both directions. amba is in
# neither: July excluded it for the path reason, and it is not a paper row
# either, so it never belonged to the head-to-head under any reading.
for dropped in ("codesample-un1", "codesample-un2"):
    assert dropped in R.H2H_TLSF_SPECS_2026_07, \
        f"{dropped} should be in the 2026-07 corpus"
    assert dropped not in R.H2H_TLSF_SPECS, \
        f"{dropped} is not a row in the AuRUS paper's tables"
assert "amba" not in R.H2H_TLSF_SPECS_2026_07 \
    and "amba" not in R.H2H_TLSF_SPECS, \
    "amba is a row in neither head-to-head corpus"
# PEREDUR's own arbiter, detector, full-arbiter and the rest are different
# parameter instances from AuRUS's, which is why the imports carry an -aurus
# suffix. The bare names must never appear, or the corpus would assert a
# correspondence that does not hold.
for excluded in ("arbiter", "takeoff-tlsf", "arbiter-handshake",
                 "detector", "full-arbiter", "load-balancer",
                 "prioritized-arbiter", "round-robin-arbiter",
                 "simple-arbiter"):
    assert excluded not in R.H2H_TLSF_SPECS, \
        f"{excluded} must not be in the head-to-head corpus"

# The declared scope partitions exactly into what PEREDUR can run, what is
# still to be imported, and what is imported but can never be scored. A family
# imported without being struck off H2H_PENDING_IMPORT fails here rather than
# silently staying out of the arm, and a holdout dropped from every list fails
# here rather than vanishing from the corpus.
check(sorted(R.H2H_TLSF_READY + R.H2H_PENDING_IMPORT + R.H2H_UNSCOREABLE),
      sorted(R.H2H_TLSF_SPECS),
      "h2h ready/pending/unscoreable partition the corpus")
assert not set(R.H2H_TLSF_READY) & set(R.H2H_PENDING_IMPORT), \
    "a family cannot be both ready and pending import"
for spec in R.H2H_PENDING_IMPORT:
    assert spec not in R.TLSF_SPECS, \
        f"{spec} has a PEREDUR family now; strike it off H2H_PENDING_IMPORT"

# aurus_campaign runs AuRUS on the whole declared corpus, keyed by the name
# each row carries so the eleven with PEREDUR families join the h2h rows.
import aurus_campaign as A  # noqa: E402
check(sorted(A.SPEC_TLSF), sorted(R.H2H_TLSF_SPECS),
      "aurus_campaign covers the head-to-head corpus")
check(A.SPEC_TLSF["arbiter-aurus"], "case-studies/arbiter/arbiter.tlsf",
      "arbiter-aurus maps to AuRUS's arbiter case study")
# The paper files Lily02 under SYNTCOMP, and that copy carries the five
# references the row is scored against; the top-level case-studies/lily02 is a
# separate one-reference setup with a byte-identical spec.
check(A.SPEC_TLSF["lily02"],
      "case-studies/syntcomp-unreal/lily02/lilydemo02.tlsf",
      "lily02 maps to the paper's SYNTCOMP row, not the top-level copy")
# The four SYNTECH15 rows the paper takes from examples/ are the reason
# SPEC_TLSF paths are rooted at <aurus-root> rather than <aurus-root>/
# case-studies: the case-studies-relative form could not name them at all.
assert A.SPEC_TLSF["pcar-v2-888"].startswith("examples/icse2019/"), \
    "pcar-v2-888 maps into AuRUS's examples/ tree, not case-studies"

# -onlyInputsA is matched per group against the AuRUS authors' own drivers, so
# a typo in a name would silently drop the flag for that spec rather than fail.
assert A.ONLY_INPUTS_A <= set(A.SPEC_TLSF), \
    f"unknown specs in ONLY_INPUTS_A: {A.ONLY_INPUTS_A - set(A.SPEC_TLSF)}"
check(len(A.ONLY_INPUTS_A), 9, "specs run with -onlyInputsA")
# -factors takes three values at 3f6f01f (STATUS,SYN,SEMANTIC), and
# Settings.setFactors halves the semantic weight into LOST_MODELS and
# WON_MODELS. A four-value spelling belongs to this project's fork, where the
# semantic weight was split in two, and prints usage and exits against the
# base -- so it would produce a campaign of zero-solution rows.
ga_factors = [f for f in A.BASE_FLAGS if f.startswith("-factors=")]
check(ga_factors, ["-factors=0.7,0.1,0.2"],
      "uniform GA factors at the base three-value CLI")
# Guarantee removal appears in none of the drivers and must stay off.
assert not any("removeG" in f for f in A.flags_for("arbiter-aurus")), \
    "AuRUS must not be run with -removeGuarantees"

# aurus_validate scores implies_genuine against examples/<spec>/fixes, so every
# family it scores must exist and hold .tlsf ideals — a missing or empty fixes
# dir silently records "unknown" for the whole family rather than erroring. The
# scorable set is H2H_TLSF_READY, not the whole corpus: AuRUS runs all 26 and
# the pending imports have no PEREDUR family to score against yet, which is
# what H2H_PENDING_IMPORT records. And the compare parsing must stay the shared
# run_experiments function (imported, not copied), or implies_genuine drifts
# from implies_ideal.
import aurus_validate as V  # noqa: E402
for spec in R.H2H_TLSF_READY:
    fixes = R.EXAMPLES_DIR / spec / "fixes"
    assert any(fixes.glob("*.tlsf")), f"no .tlsf ideals in {fixes}"
for spec in R.H2H_PENDING_IMPORT:
    assert not (R.EXAMPLES_DIR / spec).is_dir(), \
        f"examples/{spec}/ exists; strike it off H2H_PENDING_IMPORT"
# The unscoreable ones are the opposite shape: imported, so the spec is there,
# but deliberately without ideals. Asserting the absence of fixes/ is what
# stops one being quietly authored later and the family staying held out
# anyway -- if an ideal ever becomes possible, this fails and forces the
# holdout to be revisited rather than silently outliving its reason.
for spec in R.H2H_UNSCOREABLE:
    assert (R.EXAMPLES_DIR / spec / "spec.tlsf").is_file(), \
        f"examples/{spec}/spec.tlsf missing; H2H_UNSCOREABLE is for " \
        f"imported families, not pending ones"
    assert not any((R.EXAMPLES_DIR / spec / "fixes").glob("*.tlsf")), \
        f"examples/{spec}/fixes/ has ideals; if one is valid, move {spec} " \
        f"out of H2H_UNSCOREABLE"
assert not (set(R.H2H_PENDING_IMPORT) & set(R.H2H_UNSCOREABLE)), \
    "a family cannot be both pending and unscoreable"
for spec in R.H2H_UNSCOREABLE + R.H2H_PENDING_IMPORT:
    assert spec in R.H2H_TLSF_SPECS, \
        f"{spec} is held out of a corpus it is not in"
    assert spec not in R.H2H_TLSF_READY, f"{spec} is both held out and ready"
assert V.parse_compare_output is R.parse_compare_output, \
    "aurus_validate must reuse run_experiments.parse_compare_output"

# merge_experiments mirrors the per-profile CSV names and result directories,
# in two separate tables that nothing but this ties together. Over every profile
# rather than a named few: aurus-h2h was added to PROFILE_CSVS alone and the
# omission surfaced as a KeyError from `campaign.py collect`, after the campaign
# had finished, which is the worst moment to discover it.
for name in P:
    assert name in M.PROFILE_CSVS, \
        f"profile {name} is missing from merge_experiments.PROFILE_CSVS"
    assert name in M.PROFILE_RESULT_DIRS, \
        f"profile {name} is missing from merge_experiments.PROFILE_RESULT_DIRS"
    check(M.PROFILE_CSVS[name], P[name]["results_csv"].name,
          f"merge CSV for {name}")
    check(M.PROFILE_RESULT_DIRS[name], P[name]["results_dir"].name,
          f"merge result dir for {name}")

# Every spec a profile names must resolve to an on-disk input, and every
# capped profile must cap every spec it runs (run_one indexes caps[spec]).
for name, prof in P.items():
    # A profile's schemes are directory names under configs_dir, and
    # gen_configs.py writes only the current spelling, so a pre-rename name here
    # would silently select nothing and exit "No configs found".
    for scheme in prof["schemes"]:
        assert R.canonical_scheme(scheme) == scheme, \
            f"{name}: schemes names the pre-rename scheme {scheme}"
    for s in prof["specs"]:
        assert R.SPECS[s]["input"].exists(), \
            f"{name}: missing input for spec {s}"
        if prof["timeout_caps"] is not None:
            assert s in prof["timeout_caps"], f"{name}: no timeout cap for {s}"


# ── Commit provenance ────────────────────────────────────────────────────────

# The output format of `peredur --version` (src/version.cpp). Parsing must
# survive a line that is not a key=value pair, so a future banner cannot break
# a campaign launch.
check(R.parse_version_output(
          "commit=c38f582109c1c3ea7fa9b935a9a37f40f0fbba99\n"
          "commit_short=c38f582\n"
          "dirty=0\n"),
      {"commit": "c38f582109c1c3ea7fa9b935a9a37f40f0fbba99",
       "commit_short": "c38f582", "dirty": "0"},
      "parse_version_output on the shipped format")
check(R.parse_version_output("peredur 0.1.0\ncommit=abc\n"),
      {"commit": "abc"}, "parse_version_output ignores non-key=value lines")

# A binary that predates --version exits non-zero rather than printing; so does
# a path that is not a binary at all. Both must read as LEGACY_COMMIT rather
# than raising, since the staleness check is what turns that into a refusal.
missing = R.binary_version(Path("/nonexistent/peredur"))
check(missing["commit"], R.LEGACY_COMMIT, "absent binary reads as unknown")

# The provenance columns are recorded but never keyed on. `commit` in
# CSV_FIELDS and out of KEY_FIELDS is the whole backward-compatibility
# contract: archived rows have no commit, and must still match on resume.
for f in ("commit", "dirty"):
    assert f in R.CSV_FIELDS, f"{f} missing from CSV_FIELDS"
    assert f not in M.KEY_FIELDS, \
        f"{f} must not be a merge key field: archived rows have no commit"
check(M.LEGACY_COMMIT, R.LEGACY_COMMIT, "LEGACY_COMMIT agrees across scripts")

# ... and the merge key must be blind to it: the same run recorded by two
# binaries is one row, not two.
legacy_row = {"sweep": "C", "level_name": "default", "selection": "nsga2",
              "weakening": "wkon", "metric": "log", "repair_mode": "mono",
              "spec": "lift", "seed": "3"}
check(M.key_of({**legacy_row, "commit": "c38f582", "dirty": "0"}),
      M.key_of(legacy_row), "merge key ignores the commit columns")


# ── Selection-scheme rename ──────────────────────────────────────────────────

# Both scripts carry their own copy of the mapping (they are standalone and
# vendored per campaign), so they can drift; a drift splits every archived row
# from its new-name counterpart on merge.
check(M.SCHEME_ALIASES, R.SCHEME_ALIASES, "SCHEME_ALIASES agrees across scripts")
check(R.canonical_scheme(R.LEGACY_SELECTION), "nsga2-truncate",
      "the legacy selection default canonicalises")

# The scheme was renamed, not changed: a row saying "nsga2" and a row saying
# "nsga2-truncate" are one cell, and merging the two copies must not produce two
# rows.
check(M.key_of(legacy_row),
      M.key_of({**legacy_row, "selection": "nsga2-truncate"}),
      "merge key joins nsga2 with nsga2-truncate")
check(M.key_of({**legacy_row, "selection": "nsga2-replicate"}),
      M.key_of({**legacy_row, "selection": "nsga2-apportion"}),
      "merge key joins nsga2-replicate with nsga2-apportion")
# ... and the two schemes still key apart, or the rename would collapse the
# arms of the replicate campaign onto each other.
assert (M.key_of({**legacy_row, "selection": "nsga2"})
        != M.key_of({**legacy_row, "selection": "nsga2-replicate"})), \
    "the two NSGA-II schemes must stay distinct cells"

# The historical header, copied from experiments/2026-07-23-arbiter-hp/
# results-arbiter-hp.csv. Resuming against it must find its rows, and appending
# to it must drop the new columns rather than widening a closed campaign's CSV.
LEGACY_HEADER = [
    "sweep", "level_name", "level_value", "selection", "weakening", "metric",
    "repair_mode", "spec", "seed", "found_repair", "n_repairs", "best_fitness",
    "best_relation", "implies_ideal", "n_implies", "wall_time_s", "timed_out",
    "n_dropped",
]
check(R.CSV_FIELDS[:len(LEGACY_HEADER)], LEGACY_HEADER,
      "CSV_FIELDS extends the archived header rather than reordering it")

with tempfile.TemporaryDirectory() as tmp:
    legacy_csv = Path(tmp) / "results-legacy.csv"
    with open(legacy_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=LEGACY_HEADER)
        w.writeheader()
        w.writerow({**legacy_row, "level_value": "default",
                    "found_repair": "1", "n_repairs": "2",
                    "best_fitness": "0.9", "best_relation": "equivalent",
                    "implies_ideal": "1", "n_implies": "1",
                    "wall_time_s": "12.0", "timed_out": "0", "n_dropped": "0"})

    # The archived row says selection = "nsga2"; the plan is keyed off
    # scheme_of(), which canonicalises. Resume only skips the row if both sides
    # agree, so the key here must be the canonical name — otherwise 224,861
    # archived rows miss and re-run.
    done = R.load_done_set(legacy_csv)
    check(done,
          {("C", "default", "nsga2-truncate", "wkon", "log", "mono", "lift", 3)},
          "load_done_set canonicalises an archived CSV's scheme name")
    cfg = Path("experiments/configs/nsga2/wkon/log/mono/sweep_C_default.toml")
    assert ("C", "default", R.scheme_of(cfg), R.weakening_of(cfg),
            R.metric_of(cfg), R.repair_mode_of(cfg), "lift", 3) in done, \
        "an old-name config tree resolves onto the archived CSV's resume key"

    # A new-format row appended to that CSV keeps its header: extrasaction
    # ignores the columns it predates.
    fieldnames = R.existing_fieldnames(legacy_csv)
    check(fieldnames, LEGACY_HEADER, "archived header is read back unchanged")
    R.append_row(legacy_csv, {**legacy_row, "level_value": "default",
                              "found_repair": "0", "n_repairs": "0",
                              "best_fitness": "", "best_relation": "none",
                              "implies_ideal": "0", "n_implies": "0",
                              "wall_time_s": "1.0", "timed_out": "0",
                              "n_dropped": "0", "seed": "4",
                              "commit": "c38f582", "dirty": "0"},
                 fieldnames)
    check(R.existing_fieldnames(legacy_csv), LEGACY_HEADER,
          "appending a provenance row does not widen an archived CSV")
    check(len(R.load_done_set(legacy_csv)), 2, "the appended row resumes too")

# Merging a provenance-carrying CSV into an archived one widens the header and
# fills the archived rows with the legacy default, rather than dropping the new
# columns or writing them blank.
new_header = LEGACY_HEADER + ["commit", "dirty"]
check(M.fill_defaults(legacy_row, new_header)["commit"], M.LEGACY_COMMIT,
      "an archived row fills commit with the legacy default")
check(M.fill_defaults(legacy_row, new_header)["dirty"], "",
      "dirty has no legacy value and stays blank")

# ── Staleness check ──────────────────────────────────────────────────────────

HEAD = "c38f582109c1c3ea7fa9b935a9a37f40f0fbba99"
FRESH = {"commit": HEAD, "commit_short": "c38f582", "dirty": "0"}

check(R.staleness_problems({"peredur": FRESH}, HEAD), [],
      "a clean binary at HEAD is not stale")
check(len(R.staleness_problems({"peredur": {**FRESH, "dirty": "1"}}, HEAD)), 1,
      "a dirty binary is stale")
check(len(R.staleness_problems(
          {"peredur": {"commit": "0" * 40, "commit_short": "0000000",
                       "dirty": "0"}}, HEAD)), 1,
      "a binary built from another commit is stale")
check(len(R.staleness_problems(
          {"peredur": {"commit": R.LEGACY_COMMIT,
                       "commit_short": R.LEGACY_COMMIT, "dirty": ""}}, HEAD)),
      1, "a binary that cannot report a commit is stale")
# Outside a git work tree there is nothing to compare against, so only the
# binary's own dirty/unknown state can condemn it.
check(R.staleness_problems({"peredur": FRESH}, None), [],
      "no working-tree HEAD means no mismatch to report")

# The manifest is per host: campaigns run on av2 and av3 at once and merge into
# one CSV, so a shared manifest name would have them overwrite each other.
mp = R.manifest_path(Path("experiments/results-ablate-tlsf.csv"))
assert mp.name.startswith("results-ablate-tlsf-manifest-"), mp
assert mp.suffix == ".json", mp
check(mp.parent, Path("experiments"), "manifest sits beside its CSV")

print("ok: all factor-path round-trips pass")
