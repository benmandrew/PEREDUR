# Experiment Scripts

Scripts for running parameter sweep experiments and analysing the results.

## What is in here

The sections below cover the harness in depth. This table is the index, so that a script with no section is still findable and its absence from one is not read as disuse.

| Script | Role |
|---|---|
| `campaign.py` | The operating surface for a campaign: `stage`, `start`, `enqueue`, `tick`, `status`, `queue`, `collect`, `describe`. Everything that acts on a campaign goes through it. See `docs/dev/campaigns.md` for the state each verb leaves behind. |
| `run_experiments.py` | Runs one phase of a sweep on one host and appends rows to the results CSV. Vendored into every campaign archive. |
| `score_campaign.py` | The runner's scoring twin: one `score_curves.py --maximality` per run directory over one host's seeds, under a pinned worker pool, with a manifest of the budgets and binaries. What a `kind = "score"` phase runs. |
| `gen_configs.py` | Writes the config tree a sweep runs over. Vendored likewise. |
| `merge_experiments.py` | Joins per-host CSVs on `KEY_FIELDS`, keeping one row per key. Vendored likewise. |
| `check_config_schema.py` | Lint, wired into CI and the pre-commit hook. Holds `config_io.cpp`, `config-schema.json`, `example-config.toml` and `gen_configs.DEFAULTS` against `include/config.hpp`. |
| `coverage_badge.py` | Measures coverage and writes `docs/coverage.svg`. `--check` re-measures and fails on drift; CI runs it. |
| `recompare.py` | Re-runs `compare` over repairs already on disk and rewrites the relation columns. The standing pass whenever an ideal changes. |
| `check_well_separated.py` | Standalone `ltlsynt` well-separation check that bypasses counter's cached verdict. Also imported as a library by `aurus_validate.py`. |
| `aurus_campaign.py` | Drives the AuRUS baseline arm of a head-to-head. |
| `aurus_validate.py` | Re-checks AuRUS's claimed repairs with `realize` and scores them with `compare`. Run it before any AuRUS analysis. |
| `analysis_lib.py` | Statistics and CSV helpers shared by the analysers. Read its header before vendoring an analyser — importing it means the analyser is no longer standalone. |
| `analyse_aurus_h2h.py` | Scores a counter-versus-AuRUS head-to-head, and reads other campaigns against that archived reference. |
| `analyse_selection_default.py` | Scores the selection-default campaign. Carries a `--self-test` over synthetic rows. |
| `analyse.ipynb` | Generic notebook over a sweep's `results.csv`; `RESULTS_CSV` overrides the path. |
| `drop_censored_rows.py` | Deletes timeout-censored rows and their run directories so a resume re-runs them under a looser cap. Used once, on the replicate recap. |
| `maximality_sweep.py` | Runs the `maximal` binary over both arms of a head-to-head. Hard-wired to two directory layouts. |
| `test_campaign.py` | Covers `campaign.py`. No pytest; run it directly. |
| `test_experiment_paths.py` | Covers the factor-path parsers and the resume-key invariants in `run_experiments.py` and `gen_configs.py`. |

A campaign's own `experiments/<campaign>/scripts/` holds verbatim copies of the scripts that ran it, so the archive reproduces without the git history. Those copies are frozen at the commit named in the campaign's `PROVENANCE.json`; the versions here are the maintained ones, and the two are expected to diverge.

## Prerequisites

- Python 3.11+, because `check_config_schema.py` imports stdlib `tomllib` with no fallback. `campaign.py` itself runs on 3.10, having its own TOML parser for the lab hosts (see below).
- The `counter` and `compare` binaries built in release mode (see below)

## 1. Build the project

From the repo root:

```sh
cmake --workflow --preset release
```

This produces `build-release/counter` and `build-release/compare`, which the
runner script expects at those exact paths.

## 2. Set up the Python environment

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install -r scripts/requirements.txt
```

Dependencies installed:

| Package | Purpose |
|---|---|
| `pandas` | Data loading and aggregation |
| `scipy` | Kruskal-Wallis and chi-square tests |
| `matplotlib` / `seaborn` | Plotting |
| `scikit-posthocs` | Post-hoc Dunn test |
| `numpy` | Array arithmetic in `analyse.ipynb` |
| `notebook` / `ipykernel` | Running `analyse.ipynb` |

## 3. Generate experiment configs

```sh
python scripts/gen_configs.py
```

Writes one TOML file per (scheme, sweep, level) to
`experiments/configs/<scheme>/`. Safe to re-run — existing files are
overwritten, and regenerating reproduces them byte-for-byte.

The whole grid is generated once per selection scheme, so `selection_scheme` is
a factor of the design rather than a constant. The scheme lives in the
directory because `run_experiments.py` parses (sweep, level) out of the
filename and reads the scheme back off the parent directory.

Every generated config pins `selection_scheme` explicitly. That matters:
`config.hpp` defaults to `weighted`, so a config omitting the key hands the run
to weighted selection without saying so. Check with
`grep -rc nsga2-truncate experiments/configs/nsga2-truncate/` if a run's results look unexpectedly
poor.

Sweeps generated, each holding every other parameter at its default:

| Sweep | Parameter varied | Levels |
|---|---|---|
| A | Generations | 5, 10, 15, 20, 30, 40, 60, 80 — `population_size=200` |
| B | Population size | 50, 75, 100, 150, 200, 300, 500, 750, 1000, 1500 — `generations=10` |
| C | Fitness weight presets | default, syntactic-heavy, semantic-heavy, status-only |
| D | `p_trigger` | 0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0 |
| E | `p_response` | 0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0 |
| F | `p_timing` | 0.0, 0.05, 0.15, 0.3, 0.5, 0.75, 1.0 |
| G | `default_bound` | 5, 10, 20, 40, 80, 160 |
| H | `crossover_rate` | 0.0, 0.1, 0.25, 0.5, 0.75, 1.0 |
| I | `mutation_rate` | 0.1, 0.25, 0.5, 0.75, 1.0 |

60 levels per scheme, 120 configs. Because each sweep holds the others at their
defaults, exactly one level of each is byte-identical to the `A/gen10` baseline
— the aliasing below collapses those eight onto one run per scheme. Sweep J,
which crossed `run_weakening`, retired with the key on 2026-09-11.

`--weakening off` nests configs under `<scheme>/wkoff/` and writes no key, the
weakening screen having been removed. It is the only value the flag accepts.
The directory stays because `weakening` is one of `KEY_FIELDS`, and a flat
config is attributed to the legacy `wkon`, so a new campaign should pass
`--weakening off` to keep its rows from reading as the screen being on.

`--metric both` crosses `model_counting.metric` in as a factor, nesting it
under `<scheme>/[wkoff/][<direct|log>/]`. The directory label is the short
`direct`/`log`; the TOML value written is the full `direct`/`logarithmic` the
C++ parser accepts. Omitting a flag keeps the flat layout and takes that factor
from the defaults, so a no-arg run reproduces the pre-factor grid.

`--weights SYNTACTIC SEMANTIC STATUS` moves the baseline aggregate-fitness
weights for one campaign. They are the one part of `DEFAULTS` deliberately not
the binary's — 0.33 each against `config.hpp`'s 0.1 / 0.2 / 0.7, pinned so every
archived grid states the same triple — and the flag changes the baseline without
touching that pin, so a run that omits it is byte-identical to what it always
was. Sweep `C` still overrides them per level, exactly as `A` and `B` override
`--generations` and `--population-size`. The weights reach the `weighted`
selection scheme alone: under either NSGA-II scheme, ranking, elitism and the
output gate all read the per-objective vector, and the scalar decides only the
order repairs are written in and the reported `best_fitness`.

Two of these are worth a note. `G` is nearly free: the bound enters through the
transfer matrix rather than a SAT call, and bound 160 measures within noise of
bound 5, but it moves the semantic similarity score and so changes which
repairs win. `H`'s `cross0.0` level tests whether crossover contributes at all
— the default of 0.1 leaves the search almost entirely mutation-driven.

## 4. Run the experiments

```sh
python scripts/run_experiments.py                 # the full sweep (default)
python scripts/run_experiments.py --jobs 4        # four runs at a time
```

Runs the selected combinations of (sweep, level, spec, seed) and appends
results to the profile's results CSV. Runs are skipped automatically if they
already appear in the CSV, so the script is safe to interrupt and resume.

All four specs run in every profile — `takeoff`, `fsm`, `fsm-timing`, and
`fsm-combined`.

### Profiles

A profile names one (schemes, sweeps, levels, specs, seeds) selection and the
CSV it writes:

| Profile | Schemes | Sweeps / levels | Seeds | Results CSV | Wall-clock at `--jobs 4` |
|---|---|---|---|---|---|
| `full` (default) | nsga2-truncate | the original 14 levels of A, B, C | 0–29 | `results.csv` | ~29 min (1440 runs) |
| `factorial` | nsga2-truncate, weighted | every level of A–J (J since retired) | 0–99 | `results-factorial.csv` | ~14.6 h (43,200 runs) |
| `metric` | nsga2-truncate | C/default only, `metric` crossed direct×log | 0–99 | `results-metric.csv` | ~1 h split across two machines (800 runs at generations=40/population=1000) |
| `tlsf` | nsga2-truncate | A + B, coarse 4-level cross (7 operating points) | 0–59 (ceiling) | `results-tlsf.csv` | ~16 h split across av2+av3 (`--jobs 1`; the six TLSF specs) |

`full` is pinned to the four generation and five population levels it has
always had, so its `results.csv` stays one comparable dataset even though
`gen_configs.py` now emits a finer grid around them. It is 1680 rows from 1440
executions. This README documented 70+ hours for it until `ca46331`, and the
difference is not a tuning win — `black` was timing out on the implication
checks the genetic algorithm generates by the thousand, and now the ones SPOT
folds to a boolean constant are decided without invoking `black` at all.

Measured across two 32-core machines splitting the seeds, each running
`python scripts/run_experiments.py --profile full --jobs 4`: 690 runs in 13.2
minutes and 13.9 minutes. The table assumes `--jobs 4`; `full` defaults to
`--jobs 1`, which serialises the same work into about 115 minutes.

`factorial` is the wide one: 63 levels × 4 specs × 100 seeds × 2 schemes =
50,400 rows from 43,200 executions, about 58 hours of serial compute. Split
across av2 and av3 at `--jobs 4` that is roughly 7.3 hours each. Its point is
that `selection_scheme` becomes a factor rather than a constant, so
nsga2-vs-weighted is answerable at every level instead of only at the baseline
— `results.csv` holds no weighted runs at all, so that comparison cannot be
made from it. Seeds extend cheaply afterwards: `--seeds $(seq -s' ' 100 149)`
against an existing `results-factorial.csv` runs only the new ones.

`metric` isolates the `model_counting.metric` factor — the direct-vs-log choice
of how bounded trace counts become a semantic-similarity score. It crosses that
factor (`direct` × `logarithmic`) over the single all-defaults `C/default` level
at the larger `generations=40`/`population_size=1000` operating point, where
repairs are strong enough for the metric to move outcomes. It reads its own
`experiments/configs-metric/<scheme>/<direct|log>/` grid, so generate it with
`--metric both` (below) before running:

```sh
python scripts/gen_configs.py --generations 40 --population-size 1000 \
    --schemes nsga2-truncate --sweeps C --metric both --out-dir experiments/configs-metric
python scripts/run_experiments.py --profile metric --seeds $(seq -s' ' 0 49) --jobs 4   # av2
python scripts/run_experiments.py --profile metric --seeds $(seq -s' ' 50 99) --jobs 4  # av3
python scripts/merge_experiments.py av2 av3 --profile metric
```

The 800 rows are `2 metrics × 4 specs × 100 seeds` — no sweep grid, so the
budget buys statistical power on the main effect. Ordering is seed-major, so
killing a machine at the wall-clock deadline leaves a balanced design (every
metric/spec sampled at the same seeds, just fewer of them) rather than a
half-finished one.

`tlsf` runs the six basic-TLSF examples (arbiter, gyro-var1, humanoid-531,
lift, lily02, minepump) rather than the four FRETISH specs, swept over
generations (A) and population (B). These specs are far heavier: `ltlsynt`
turns multi-gigabyte resident on them, and three — lift, gyro-var1,
humanoid-531 — take about three minutes each even at the `gen10/pop200`
baseline, against a few seconds for the FRETISH specs. So the grid is a coarse
four-level cross per axis (seven distinct operating points, sharing the
baseline) rather than the fine FRETISH grid: the budget is spent on seeds
instead of gradations. It reads its own `experiments/configs-tlsf/nsga2-truncate/` grid,
so generate it with `--tlsf` first:

```sh
python scripts/gen_configs.py --tlsf
python scripts/run_experiments.py --profile tlsf --seeds $(seq -s' ' 0 29)   # av2
python scripts/run_experiments.py --profile tlsf --seeds $(seq -s' ' 30 59)  # av3
python scripts/merge_experiments.py av2 av3 --profile tlsf
```

The seed count is a ceiling, not a commitment: ordering is seed-major, so a
machine killed at its ~16 h deadline leaves a balanced design at whatever seed
depth it reached, and the disjoint 0-29 / 30-59 ranges merge to one balanced
dataset whether or not either finishes.

The profile runs at `--jobs 1`, unlike the FRETISH profiles' `--jobs 4`, for two
reasons. First, `ltlsynt` turns multi-gigabyte resident on these specs, and
nothing caps its concurrency except the counter process's `runtime.parallel`, so
one process per machine keeps that the machine-wide limit — on the 128 GB
av2/av3 grid, 32 cores × ~2.7 GB peaks near 86 GB, and a smaller-RAM box should
lower `runtime.parallel`. (`runtime.max_concurrent_realizability` and
`--max-realizability`, which capped it separately, were removed on 2026-09-11.)
Second, `ltlsynt` has
no internal timeout and the genetic search occasionally generates a synthesis
query that runs for minutes; the campaign sets `runtime.ltlsynt_timeout_ms`
(500 ms — call durations are sharply bimodal, 95% finishing under 50 ms with an
almost-empty 0.5-1 s band) so such a query is killed and reported as undecided
rather than stalling the run: it admits no repair, and the well-separation
check at the output gate refuses its candidate — the count of these appears as `(N timeouts)` in
each run's `ltlsynt` timing row. `compare` decides implies-ideal on TLSF the
same way it does on FRETISH, via the whole-formula implication check, so
`results-tlsf.csv` carries the same columns.

Reduced `quick` and `smoke` profiles existed until that speedup made them
pointless — they saved about 27 minutes between them, at the cost of dropping
`fsm-combined` on the grounds that it never produced a repair. It now finds one
in 328 of 418 runs (78%), so that exclusion was discarding real data.

`--sweeps`, `--specs`, and `--seeds` narrow a profile without defining a new
one: `--specs` and `--seeds` replace the profile's set (so `--specs
fsm-combined` runs that spec alone), and `--sweeps` selects which sweeps run
(so `--sweeps C` runs all five C levels).

### Parallel runs (`--jobs`)

`--jobs N` runs N experiments concurrently (`full` defaults to 1, so `--jobs 4`
is worth passing). The `counter` binary parallelises internally with a thread
pool sized to the machine's core count by default, so when jobs > 1 the
runner caps each run's pool: it writes a derived config
(`<output-dir>/config.toml` — the level's TOML with
`parallel = max(1, cores // jobs)` under `[runtime]`) and passes that to
`counter`. With jobs = 1 the original config file is used unchanged.

### Baseline aliasing

Every sweep holds the other parameters at their defaults, so each sweep's
default level is byte-identical to the `A/gen10` baseline (generations 10,
population 200, default weights): `B/pop200`, `C/default`, `D/ptrig0.5`,
`E/presp0.5`, `F/ptim0.15`, `G/bound20`, `H/cross0.1` and `I/mut1.0`.
The runner executes the canonical `A/gen10` run once per
(scheme, spec, seed) and emits one CSV row per requested alias — the rows
differ only in `sweep`/`level_name`/`level_value`. Identity is verified
byte-for-byte before aliasing; if the files ever diverge the runner warns and
runs them separately. `--dry-run` tags aliased rows with `(alias of A/gen10)`.

Aliasing never crosses schemes: nsga2-truncate's `B/pop200` aliases onto
nsga2-truncate's
`A/gen10`, never onto weighted's. The byte-identity check enforces that on its
own, since the two configs differ on `selection_scheme`.

### The `n_dropped` column

`counter` drops an individual whose fitness scoring throws — in practice an
external tool failing on one evolved formula — rather than aborting the whole
run, up to `max_scoring_failure_rate` (5%) of a generation. It prints a scoring
report naming the count and reasons, and stays silent when nothing was dropped.

The runner parses that count into `n_dropped`. A run with drops evolved a
thinner population than it asked for, so without the column it is
indistinguishable from a clean one — which is the mistake the report exists to
prevent. Filter on `n_dropped == 0` for a strictly clean dataset.

### The `timed_out` column

Each row records `timed_out` (0/1) so the analysis can distinguish
timeout-censored runs from genuine failures to find a repair. When appending
to a CSV written before this column existed, the runner keeps the file's
original header and drops the column, so legacy files stay consistent.

### The `commit` and `dirty` columns

Each row records the git commit the `counter` binary was built from, read once
at startup from `counter --version` — not from the working tree. The two
diverge every time a fix lands without a rebuild, and the binary is what
produced the numbers. The column holds the abbreviated hash; the manifest below
keeps the full one. `dirty` is 1 when that binary was built from a modified
tracked tree, which means its commit does not identify the code that ran.

`commit` is deliberately absent from the resume key and from
`merge_experiments.py`'s `KEY_FIELDS`. Two rows differing only in the binary
that produced them are the same design cell, and keying on the commit would
make every archived row miss on resume and re-run campaigns that are already
finished. Rows written before the column existed read back as `unknown`, the
same way `selection` and `metric` fall back to their legacy defaults.

### The launch manifest and the staleness check

Alongside the results CSV each launch writes
`<stem>-manifest-<host>.json`: branch, `git describe`, the working tree's HEAD,
the commit and dirty state of both binaries with their paths, and the sweep the
launch asked for (schemes, factors, sweeps, levels, specs, seeds, jobs,
directories). One per host, because av2 and av3 run the same campaign into one
merged CSV and a shared name would have them overwrite each other.

Before any run starts, the runner compares the binaries' embedded commits
against the working tree's HEAD and refuses to launch when they differ, when a
binary was built dirty, or when a binary is too old to answer `--version` at
all. A campaign run against a binary that predates a fix produces numbers that
look valid and are not, and the PROVENANCE.json files under `experiments/`
record how little of that is recoverable afterwards. `--allow-stale-binary`
downgrades the refusal to a warning for the cases where the mismatch is
deliberate.

### Useful flags

```sh
# Preview what would run without executing anything
python scripts/run_experiments.py --dry-run

# Run only specific sweeps
python scripts/run_experiments.py --sweeps A B

# Run only specific specs
python scripts/run_experiments.py --specs takeoff

# Run a small set of seeds for a quick check
python scripts/run_experiments.py --seeds 0 1 2

# Re-run even if results already exist in the CSV
python scripts/run_experiments.py --no-resume

# Launch anyway when the binaries do not match the working tree's HEAD
python scripts/run_experiments.py --allow-stale-binary

# Combine flags
python scripts/run_experiments.py --sweeps A --specs takeoff --seeds 0 1 2
```

Per-run outputs (repair JSON files) land in `experiments/results/<run-id>/`.

## 5. Splitting across machines

Each run is independent, so the work can be divided on any of the three
filtering axes and recombined afterwards. At ~29 minutes for the full sweep the
coordination rarely pays for itself now, but the seed split below is how the
current `results.csv` was collected.

**By seeds** — recommended, parallelises all sweeps and specs evenly:

```sh
# Machine 1 (seeds 0–14)
python scripts/run_experiments.py --seeds $(seq -s' ' 0 14)

# Machine 2 (seeds 15–29)
python scripts/run_experiments.py --seeds $(seq -s' ' 15 29)
```

**By spec** — useful for isolating `fsm-combined`, the slowest spec at a 6.9s
mean against `fsm`'s 3.3s:

```sh
python scripts/run_experiments.py --specs takeoff fsm            # machine 1
python scripts/run_experiments.py --specs fsm-timing fsm-combined  # machine 2
```

**By sweep** — useful when sweep C is run separately:

```sh
python scripts/run_experiments.py --sweeps A B   # machine 1
python scripts/run_experiments.py --sweeps C     # machine 2
```

### Merging results

`merge_experiments.py` pulls each machine's results and merges them into this
checkout. Configure the machines once in its `REMOTES` dict, then:

```sh
# Pull from every configured remote
python scripts/merge_experiments.py

# Only named remotes
python scripts/merge_experiments.py av2 av3

# Show the rsync plan without transferring or writing
python scripts/merge_experiments.py --dry-run

# Merge another checkout on this machine
python scripts/merge_experiments.py /path/to/counter
```

**`--profile` must match the profile the runs used.** Each profile writes its
own CSV — `full` → `results.csv`, `factorial` → `results-factorial.csv` — and
the flag defaults to `full`. When no source carries a CSV for the chosen
profile, the merge stops and names the mismatch rather than writing anything.

The script rsyncs each remote's `experiments/results/` into the local one, then
merges the CSVs on the natural key
`(sweep, level_name, selection, weakening, metric, spec, seed)`, keeping one row
per key. Local rows win, so a machine's own results are never
overwritten by a remote copy of the same run. Output is sorted by key, which
makes the file byte-stable and the whole operation idempotent — re-running it
never duplicates rows. Per-run directories encode their scheme and seed
(`sweep_A_gen10_nsga2-truncate_fsm_seed17`), so they never collide between machines or
between schemes.

`selection`, `weakening` and `metric` are part of the key because a profile may
run every level under both selection schemes (`factorial`), both weakening
states (`cj-large`, retired with the screen on 2026-09-11 but still merged),
or both similarity metrics (`metric`); without them the
crossed rows collapse onto one key and half are dropped in silence. Rows written
before a column existed carry its legacy default — nsga2, wkon, direct — so both
scripts read an absent value as that rather than empty, which keeps resume and
merge working against the older CSVs.

The selection schemes were renamed on 2026-08-06 — `nsga2` to `nsga2-truncate`,
`nsga2-replicate` to `nsga2-apportion` — and `gen_configs.py` emits the new
names, so a `selection` value depends on when its row was written. Only the
names moved, so `canonical_scheme()` in both scripts maps a retired spelling
onto its replacement wherever the column is read: at `scheme_of()`, at the
resume key in `load_done_set()`, and at `key_of()` in `merge_experiments.py`.
Archived rows therefore still match, and a campaign closed before the rename
resumes instead of re-running from zero. Row values are never rewritten — a
merged CSV keeps whatever each campaign recorded, and only the key is
canonical. Renaming any other factor directory needs the same treatment;
skipping it is silent, and shows up as a finished campaign re-running.

Sources are reached over ssh. An entry in `~/.ssh/config` must match the
hostname as written in `REMOTES` — a bare `Host av3` block does not apply to
`benandrew@av3.cs.man.ac.uk`, so give the block both names and a `HostName`:

```
Host av3 av3.cs.man.ac.uk
	HostName av3.cs.man.ac.uk
	IdentityFile ~/.ssh/id_avlab
	User benandrew
```

## 6. Campaign status and collection

`campaign.py` covers a campaign's whole life on the lab machines: declaring it,
staging the hosts, launching it, polling it and bringing its results home.
Every verb reaches the machines by their ssh-config alias (`av2`) rather than by
the fully qualified domain name (FQDN) in `merge_experiments.REMOTES` — that
form falls through to password auth under `BatchMode=yes`, and a status poll
cannot answer a prompt.

`docs/dev/campaigns.md` is the operating manual for the same tool: which verb to
reach for, what each state means, and how a campaign is closed. This section is
the command surface.

### Declaring a campaign

`experiments/<name>/campaign.toml`, tracked in git through a `.gitignore`
negation because the hosts get it by checking out the campaign's branch:

```toml
name = "arbiter-probe"          # must equal the directory name
branch = "feat/arbiter-probe"   # the branch every host runs from
profile = "arbiter-probe"       # default profile for phases that omit one
build = "cmake --build build-release"   # optional; this is the default
configs = "python3 scripts/gen_configs.py --sweeps C --out-dir experiments/configs-arbiter-probe"   # optional; no default
hosts = { av2 = "0-99", av3 = "100-199" }

[[phases]]
profile = "arbiter-probe"
jobs = 4

[[phases]]
profile = "tlsf"
jobs = 1
hosts = { av2 = "0-24", av3 = "100-124" }   # this phase alone
```

`configs` is the `gen_configs.py` line this campaign needs. `stage` runs it on
each host after the build and before the version check, and then checks that
every profile the phases name has a configs directory holding at least one
`.toml` — that check runs whether or not the key is declared, because
`run_experiments.py` exits 1 on an empty one and a queued campaign otherwise
spends its attempts finding out.

A phase takes `profile`, `jobs`, and optionally `name`, `sweeps`, `specs` and
`hosts`; `[[phases]]` headers mean the same as the inline array. Seed ranges are
inclusive and may be comma-separated (`"0-9,20-29"`). Phases run in order and
stop at the first failure.

A phase's `kind` is `run`, the default, or `score`. A score phase is the
offline maximality pass over a results directory, run after the search that
wrote it, and takes keys of its own in place of `jobs`, `sweeps` and `specs`,
which it refuses by name:

```toml
[[phases]]
kind = "score"
results = "experiments/results-rematch"   # default: the profile's results directory
out = "experiments/curves-rematch"        # default: curves-<stem>, beside it
workers = 8            # scorers in flight, each pinned to `cores` cores
cores = 4              # cores per scorer, also score_curves.py --jobs
cuts = 20
maximal_timeout = 900  # seconds per maximal call, per cut
compare_timeout = 600  # seconds for the compare call
deadline_s = 4500      # score_curves.py stops adding cuts after this
wall_cap_s = 5400      # outer timeout on one scorer; default deadline_s + 900
```

Either `results` or a profile (the phase's own, or the campaign's) must be
present. Every budget defaults to `score_campaign.DEFAULTS` and is stated on
the scorer's command line whatever the declaration said, so the manifest the
host writes records the declaration's values. `hosts` narrows a score phase
as it narrows a run phase, and each host scores its own seeds alone.

A phase's own `hosts` table overrides the campaign-level split for that phase
alone, and may only narrow it — every host it names must already be declared at
the top level, since `stage` staged no other one. A host the table omits runs
nothing for that phase, contributing no command to that host's `&&` chain under
`start` and being skipped by a tick. `enqueue` freezes the per-phase ranges into
the queue entry as `phase_seeds`, for the reason it already freezes the campaign
range, and an entry written before that field existed falls back to the campaign
split. The case for it is a campaign whose paths have different sample sizes,
which cannot share one range because `run_experiments.py --seeds` replaces a
profile's own seed list rather than intersecting with it. Forcing 70 FRETISH
seeds over 4 specs onto TLSF phases sized at 25 over 20 families would run 4,200
TLSF rows where the plan says 1,500.

The file names a profile that `run_experiments.py` defines and never redefines
one, so the load is validated against the current checkout's `PROFILES` and
fails on a name it does not find. It also fails on a `name` that disagrees with
its directory, an unknown key, a malformed range, and two hosts whose ranges
overlap. A phase's table is checked the same way, and fails in addition on a
host the campaign level does not declare. The overlap is the reason the file
exists: it has both hosts run the shared seeds, and the merge keeps one row per
key, so the campaign costs more machine time and returns fewer rows than the
plan says without anything looking wrong.

`campaign.py` reads this TOML with a parser of its own rather than `tomllib`,
because `tick` runs on av2 and av3, whose python3 is 3.10.12 with neither
`tomllib` (3.11) nor `tomli` present. `test_campaign.py` checks that parser
against `tomllib` on every fixture.

### Staging the hosts

```sh
python scripts/campaign.py stage arbiter-probe --dry-run   # probe only
python scripts/campaign.py stage arbiter-probe
python scripts/campaign.py stage arbiter-probe --force     # asks before it does
```

Pushes the branch, then per host fetches it, checks it out at the pushed commit,
runs the build command, runs the `configs` command and checks the configs
directories, and reads `build-release/counter --version` back to confirm the
binary carries that commit with `dirty=0`. A score phase's results directory
is checked the same way, unless an earlier run phase of the campaign writes
it, in which case the tick checks it when the phase's turn comes.

It refuses by default on three readings, all three reported at once: a dirty
checkout, a live `counter` or `run_experiments.py` process, and a checkout on
another branch. `--force` prints the modified files by name and the branch and
head it would leave, then requires the host name typed back at a terminal;
without a terminal it refuses, so no script can stage past a refusal. `git
clean` is never run — a host's untracked files are its results — and an unforced
stage also refuses a checkout ahead of the pushed commit.

### Launching

```sh
python scripts/campaign.py start arbiter-probe --dry-run   # print the commands
python scripts/campaign.py start arbiter-probe
```

`start` re-probes each host, refuses one that is unstaged, already running a
campaign, or holding a queue entry for this campaign in any state a tick could
still run, and launches that host's phases as one detached `nohup` chain joined
with `&&`. `--ignore-queue` overrides the last of those and names the entry it
is racing. Each launch is appended to `experiments/<name>/launches.jsonl`.
Neither `start` nor `enqueue` accepts a seed range: both read the split from
`campaign.toml`, which is the only place it is written down. A score phase
joins the chain like any other phase, as a `score_campaign.py` command over
the same seeds, and `start --dry-run` and `tick --dry-run` print it.

### Scoring a finished search

```sh
# What a score phase runs on the host, over that host's seeds
python3 scripts/score_campaign.py --results experiments/results-rematch \
    --out experiments/curves-rematch --seeds 0 1 2 --workers 8 --cores 4 \
    --cuts 20 --maximal-timeout 900 --compare-timeout 600 --deadline-s 4500
python3 scripts/score_campaign.py ... --dry-run   # the queue and the command
```

`score_campaign.py` queues the run directories under `--results` whose name
ends `_seed<N>` with N in `--seeds`, smallest first by the line count of
`accumulated/index.tsv`, and runs one `score_curves.py --maximality` per
directory from `--workers` threads, each pinned with `taskset` to a block of
`--cores` cores and each invocation under coreutils `timeout --wall-cap-s`.
A curve is written to `<out>/<run>.csv.part` and moved into place on a zero
exit with a non-empty file; a failed attempt lands as `rc run` in
`<out>/failures.txt`, every attempt appends its budgets and elapsed time to
`<out>/timings.txt`, and every scorer's output goes to `<out>/warnings.log`.
`<out>/score-manifest-<host>.json` records the seeds, the budgets, the
invocation template, both scoring binaries' commits and the counts, and is
what an archive's `maximality_pass` block is assembled from.

It reads `build-release/maximal --version` and `build-release/compare
--version` before it starts and refuses, as `run_experiments.py` does, a
binary built from another commit, a dirty tree, or none; `--allow-stale-binary`
downgrades that to a warning and is recorded in the manifest. It exits 0
only when every queued run has a curve, and a rerun skips the runs that
already have one, so a requeued phase re-scores only what failed. The three
budgets and the wall cap are the two campaigns' values: 900 s per `maximal`
call, 600 s for `compare`, a 4500 s deadline and a cap 900 s past it.
`COUNTER_SCORE_CURVES_CMD`, `COUNTER_BIN_DIR`, `MAXIMAL_BIN` and
`COMPARE_BIN` override what it runs, for a worktree or a test.

### The queue

```sh
python scripts/campaign.py enqueue arbiter-probe        # one entry per host
python scripts/campaign.py queue                        # every host, read-only
python scripts/campaign.py cron --host av2 --print      # the crontab line
python scripts/campaign.py requeue --host av2 001-arbiter-probe.toml
```

An entry is `experiments/queue/NNN-<name>.toml` on the host that runs it,
untracked because a tick rewrites its state and a tracked file doing that would
leave the checkout dirty. A tick takes `~/.counter-queue.lock`, recovers any
entry a killed tick left `running`, and runs the next phase of the
lowest-numbered queued entry in the foreground, so the lock is held for the
phase's whole duration and every tick landing during it exits at once. States
are `queued`, `running`, `done` and `failed`; a failed phase costs an attempt
and the entry stops at the cap (`--max-attempts`, three by default) with the
reason in `last_error`, rather than being retried for ever. `cron --print`
prints the line and installs nothing.

`enqueue` pushes the campaign's branch to origin and freezes its commit and its
build command into the entry, beside the seed ranges it already froze. A tick
whose entry names a branch the checkout is not on fetches that commit, checks it
out and runs the build, then reads `build-release/counter --version` back before
it runs a phase, which is `stage` performed by the tick with no terminal to
answer to. It stages for the branch alone: a dirty checkout and a live `counter`
or `run_experiments.py` each stop it, holding the reason in `last_error`, and so
does a HEAD no remote branch contains. Those are the cases where the checkout
carries something no fetch brings back, and `stage --force` — which names what
it will discard and wants the host name typed back — stays the only way past
them. `tick --no-stage` restores the old refusal for a host driven by hand.

A stage that moves `scripts/` restarts the tick. The declaration is tracked on
the campaign's own branch, so it can only be read after the checkout has moved,
and the running process keeps the `campaign.py` it imported before that: a
declaration using keys the older revision does not define fails to parse, and a
phase kind it does not define cannot be rendered even where it does parse. Both
read as a bad declaration rather than as a stale parser, which sends the reader
to edit a file that is correct. The tick therefore re-execs itself once the
stage lands, carrying the queue lock's descriptor across the exec rather than
dropping it, and the restarted process runs the phase. `ensure_staged` has put
the entry back in `queued` by then, so the restart spends no attempt. It fires
only where the two commits differ under `scripts/`; a campaign that merely sits
on another branch of the same scripts runs its phase in the staging tick as
before.

### Polling a running campaign

```sh
python scripts/campaign.py status                   # every host, plus this checkout
python scripts/campaign.py status --host av3        # one host (av2, av3 or local)
python scripts/campaign.py status --campaign tlsf   # one profile
python scripts/campaign.py status --json            # machine-readable
python scripts/campaign.py status --all             # include older manifests
```

A read-only poll of av2, av3 and the local checkout, taking about a second. It
prints two tables. The first is the checkout each host is sitting on — branch,
head, whether the working tree is clean, and which runner or engine processes
are alive. The second is one row per campaign: rows done against rows planned,
the percentage, the state (done, running, stuck or stalled), STALE — the age
of the newest `run.log` under that campaign's results directory — a crude ETA,
the
branch the campaign was launched from, and the commit of the `counter` binary
producing its rows.

```
HOST   MACHINE  BRANCH              HEAD     TREE   PROCESSES
av2    avlab12  feat/arbiter-probe  b093374  clean  idle
av3    av3      feat/arbiter-probe  b093374  clean  idle
local  ben      feat/campaign-cli   972136c  clean  idle

HOST   CAMPAIGN             ROWS     PCT   STATE  STALE  ETA  BRANCH              BINARY
av2    arbiter-probe        400/400  100%  done   1h06m  -    feat/arbiter-probe  b093374
av3    arbiter-probe        400/400  100%  done   53m    -    feat/arbiter-probe  b093374
local  (busy, no manifest)  -        -     -      -      -    -                   -
```

The two branch columns can disagree, which is why both are printed. The
checkout table names the branch a resumed run would use; a campaign row names
the branch its manifest was written on, and so the branch its existing rows
came from.

Neither row count is re-derived here. `campaign.py` reads the per-host launch
manifest for the selection the launch asked for (sweeps, specs, seeds),
replays that selection onto `run_experiments.py --dry-run` on the same host,
and takes both numbers off the `Plan:` line. The runner stays the only thing
that expands a sweep, so a profile edit cannot make the two disagree about the
size of a campaign. `--no-plan` skips the query, and planned totals then read
as unknown.

Rows done is that same plan's already-done count, not the length of the
results CSV, and the two are not interchangeable. Profiles share CSVs —
`replicate`, `replicate-wkoff` and `replicate-recap` all write
`results-replicate.csv` — and a top-up relaunch rewrites the manifest to a
handful of seeds while the file still holds every earlier row. Reading the
file's length as progress reports 400/40 in that second case and calls a
campaign that started minutes ago finished. Where no plan came back the file's
length is used as a fallback, and the cell is prefixed with `~` and explained
in a note, since that is the one row that can still overstate progress.

Times are differenced against each host's own clock, sampled in the same call.
av3 drifts minutes away from av2 whenever the Network Time Protocol (NTP) is
off, and a staleness measured across that skew is fiction.

Process detection matches on `comm`, never on the whole command line. `pgrep
-f run_experiments.py` would match this script's own ssh command, whose text
names it, and report an idle machine as running.

A process is attributed to a campaign only when it names that campaign's
profile, and a process that names none — a bare `counter` or `ltlsynt` — is
attributed to no campaign at all. It still appears in the checkout table's
PROCESSES column, which is the honest place for something that cannot be tied
to a campaign. Matching those against every campaign instead had one unrelated
`counter` on av2 report all six of its archived campaigns as running while
idle av3 reported the same six, from the same data, as stalled.

A matched process is then corroborated against the campaign's own output
before it counts as progress. STATE reads `running` only where the newest
`run.log` is fresher than three hours; where a runner is alive over an older
log it reads `stuck`, with a note. The threshold is the harness's own bound on
how long one run can be silent rather than a guess: the largest per-run
`counter` timeout any profile allows is 3600s and the largest `compare`
timeout is 1800s, so 90 minutes bounds a single run, doubled for the
granularity of the log writes and for a `--jobs 1` host where the next
`run.log` only appears once the previous run has finished. `running` beside a
nine-day staleness is the reading that stops someone investigating a dead run,
which is the failure this tool exists to prevent, so it is reported as neither
`running` nor plain `stalled`.

Manifests accumulate — a machine keeps one per campaign it has ever launched.
Only those whose branch matches the host's current checkout are shown by
default, since the rest cannot be in flight and their profile is usually no
longer defined; `--all` shows them. A campaign whose launch branch differs
from the checkout's current branch is flagged with `!`, and one launched off a
binary built dirty — which needs `--allow-stale-binary`, so its rows name a
commit they did not come from — is flagged with `*` on BINARY.

A score phase is a `score:<out>` row, read from the `score-manifest-<host>.json`
inside its output directory. ROWS is the count of curve CSVs under that
directory against the queued count the manifest froze, STALE is the age of
the newest file under it, BINARY is `maximal`'s commit (flagged `*` where the
gate was overridden), and STATE follows the same three-hour rule with a
`score_campaign.py` naming that directory in place of a runner naming a
profile. `--campaign <out>` selects it.

An unreachable host prints a row saying so rather than aborting the poll, and
sets a non-zero exit status, so a wrapper can tell a full poll from a partial
one.

The tables are coloured on a terminal and plain anywhere else, so a redirected
poll and the cron tick's log stay readable. `--no-color`, on every verb that
prints a table, and a non-empty `NO_COLOR` in the environment both turn it off;
`CLICOLOR_FORCE=1` forces it on down a pipe, which is what `less -R` wants.
`--json` is never coloured.

### Collecting a finished one

```sh
# What would move, without moving any of it
python scripts/campaign.py collect --profile tlsf --dry-run

# rsync back, merge, verify
python scripts/campaign.py collect --profile tlsf

# One host, or the CSV without the per-run trees
python scripts/campaign.py collect --profile tlsf --host av2
python scripts/campaign.py collect --profile tlsf --no-results

# A score phase's curves
python scripts/campaign.py collect --curves curves-rematch --dry-run
python scripts/campaign.py collect --curves curves-rematch
```

`collect --curves NAME` is the score phase's half and reads none of
`--profile`, `--csv` or `--results-dir`. It pulls each host's
`experiments/NAME/` whole into `experiments/NAME/<host>/`, so the timings,
failures and warnings files and the host's score manifest stay beside the
curves and apart per host, then joins every per-run CSV into
`experiments/NAME.csv` with the header written once. The verification is the
one below over run names rather than keys: a run on both hosts is written
once where the two copies are byte-identical and reported as MISMATCH where
they differ, a curve whose header comes from another `score_curves.py`
vintage is refused, and a host that answers nothing is INCOMPLETE.

`collect` supersedes running `merge_experiments.py` by hand. It rsyncs each
host's per-run tree and results CSV back and merges them on the same natural
key through `merge_experiments.py` itself, so nothing is reimplemented, then
verifies the outcome against three counts — the merged row count, a
duplicate-key scan, and every source's rows against the merged union.

The arithmetic is a union rather than a sum. Two hosts on disjoint seed ranges
overlap on nothing while a re-collect of an already merged campaign overlaps
on everything, and only the union separates either from a silent loss.
Discrepancies print as MISMATCH and set a non-zero exit status.

A host that was asked for and contributed nothing is the one case that
arithmetic cannot see: computed over the hosts that did answer it agrees with
itself perfectly. Such a host is therefore carried into the check explicitly,
printed as INCOMPLETE, and sets a non-zero exit status. What did arrive is
still merged — the merge is keyed and idempotent, so a later run against the
recovered host completes the file rather than redoing it.

`--dry-run` reports what would move — rows on each host, the size and file
count of its results directory, and what `rsync -a --dry-run --stats` says it
would actually transfer. It transfers nothing and writes nothing.

`--csv NAME` and `--results-dir NAME` exist for a campaign whose profile this
checkout does not define, which is the normal case while the campaign is still
on its own branch. `merge_experiments.PROFILE_CSVS` is the authority and an
unmerged profile is absent from it, so `collect` refuses to guess
`results-<profile>.csv` rather than merge a campaign into a file nobody is
watching.

### Describing a closed one

```sh
# One archived campaign's declaration, derived from its directory
python scripts/campaign.py describe 2026-08-07-elitism

# Every archive, machine-readable
python scripts/campaign.py describe --all --json

# Read the archives from another checkout (results CSVs are gitignored)
python scripts/campaign.py describe --all --archive-root ~/projects/counter/experiments
```

The campaigns under `experiments/` closed before `campaign.toml` existed, so
none of them has a declaration. `describe` derives one and prints it: the
factor cross from the merged results CSV, the host split from the per-host
CSVs where their seed blocks are disjoint, and the branch from
`PROVENANCE.json`. It writes nothing, into the archive or anywhere else.

Every field printed is named against the file it came from in `derived_from`,
and every field no file in the archive records is listed in `not_recorded`
rather than defaulted. `attribution = "inferred"` marks the whole output as a
reading of the archive rather than a record kept at the time. It is not
runnable: it names retired profiles and retired selection-scheme spellings,
and an archived campaign is reproduced from `experiments/<campaign>/scripts/`
at the commit its `PROVENANCE.json` names.

### Tests

```sh
python scripts/test_campaign.py
```

A plain script with no pytest dependency, like `test_experiment_paths.py`. It
diverges from that suite in one way: it records every failure and prints them
together at the end, exiting non-zero after the whole run rather than at the
first one, so read the closing summary rather than the first `FAIL` line.
Exiting early hid 79 of its 227 assertions behind a single ambient-state
failure, which is the reason for the difference. It runs against temporary
fixture checkouts and
never touches a lab machine: the remote protocol is exercised against captured
marker output, `collect` against two throwaway checkouts, and the stage and
queue paths against temporary git repositories with `COUNTER_RUNNER_CMD`
pointed at a stub that records its arguments instead of running a campaign.

The verification is the half worth having. A merge that quietly drops one
machine's share of a campaign reads the same as a merge that worked, and the
union check is what separates them before any analysis is run on the file.

## 7. Analyse the results

Launch Jupyter and open the notebook:

```sh
jupyter notebook scripts/analyse.ipynb
```

Or run it non-interactively:

```sh
jupyter nbconvert --to notebook --execute scripts/analyse.ipynb \
    --output scripts/analyse_executed.ipynb
```

The notebook reads `experiments/results.csv` by default; point it at another
results file — an archived or per-machine copy, say — with the `RESULTS_CSV`
env var, which also works for `nbconvert --execute`:

```sh
RESULTS_CSV=../experiments/results-av2.csv jupyter notebook scripts/analyse.ipynb
```

It produces:

- Box plots of best fitness per level
- Bar charts of implies-ideal rate and found-repair rate
- Kruskal-Wallis H test (continuous fitness) with post-hoc Dunn / Holm correction
- Chi-square test (binary implies-ideal) with post-hoc Fisher's exact / Bonferroni correction
- Cross-sweep summary table

Sweep C is displayed only if its results are present in the CSV, which they are
unless the run was narrowed with `--sweeps`.
