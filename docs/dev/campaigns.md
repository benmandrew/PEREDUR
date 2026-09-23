# Campaigns

The campaign declaration, the verbs that act on it, and what each reading means; `scripts/README.md` is the reference for every flag.

## Campaigns

A campaign is declared once, in `experiments/<name>/campaign.toml`: its branch, the seed range each lab host takes, and its phases. Everything that acts on a campaign goes through `scripts/campaign.py`, so the seed split exists in one place. Never hand-roll an `ssh … nohup … run_experiments.py` line and never choose a seed range at the prompt; two hosts sharing a seed run it twice and the merge keeps one row per key, which costs machine time and yields nothing. `campaign.toml` is tracked (a `.gitignore` negation), because the hosts get it by checking out the campaign's branch.

The verbs are `stage` (put a host on the branch and rebuild it), `start` (launch the phases detached), `enqueue` plus the cron-driven `tick` (the unattended path), `dequeue` (take an entry back out, killing the tick holding it), `status`, `queue`, `collect` and `describe` (derive a declaration for an archived campaign).

`campaign.py status` is the source of truth, read-only, and costs about a second. Never carry campaign state in conversation context; re-run the poll later rather than predicting what it will say. Never poll in a loop, and never spawn an agent to watch a run: `enqueue` the campaign and let the five-minute tick drive it.

`KEY_FIELDS` in `merge_experiments.py` and the resume key in `run_experiments.py` are off limits, and `commit`/`dirty` may never join either (see "Commit provenance" in `config-and-provenance.md`): 224,861 archived rows resume against those keys, and a campaign whose key moved re-runs from zero in silence.

## The declaration

A campaign is one directory, `experiments/<name>/`, holding `campaign.toml` and, where it answers a question, the pre-registered `PLAN.md`:

```toml
name = "arbiter-probe"          # must equal the directory name
branch = "feat/arbiter-probe"   # the branch every host runs from
profile = "arbiter-probe"       # default profile for phases that omit one
build = "cmake --build build-release"   # optional, this is the default
configs = "python3 scripts/gen_configs.py --sweeps C --out-dir experiments/configs-arbiter-probe"  # optional, no default
hosts = { av2 = "0-99", av3 = "100-199" }
phases = [ { profile = "arbiter-probe", jobs = 4 } ]
```

`configs` runs on the host during `stage`, after the build and before the version check so a failing generator reports as itself, in a subshell so `&&` behaves as written. It has no default, since no line suits every campaign and config trees are untracked.

A phase takes `profile`, `jobs`, and optionally `name`, `sweeps`, `specs` and `hosts`; `[[phases]]` headers are equivalent. Seed ranges are inclusive and may be comma-separated (`"0-9,20-29"`). Phases run in order and stop at the first failure, so a phase depending on an earlier one is safe. `kind` is `run` (the default), `score`, or `aurus-score`. Each of the other two takes its own keys and refuses every key belonging to another kind by name, so a budget nobody reads is never carried along unread.

A phase's `hosts` table overrides the campaign split for that phase under the same rules, and may only narrow it, since `stage` staged no other host; an omitted host runs nothing for that phase and a tick advances past it. It exists because `run_experiments.py --seeds` replaces a profile's seed list rather than intersecting it, so paths with different sample sizes cannot share one range without silently changing the row count. `enqueue` freezes these as `phase_seeds`; older entries fall back to the campaign split.

A declaration names a profile `run_experiments.py` defines and never redefines one, since archived campaigns vendor that file. The load fails on an unknown profile, a `name` that disagrees with its directory, a malformed range, an unknown key, or overlapping host ranges, the last being invisible once the merge has kept one row per key.

## Staging a host

```sh
python scripts/campaign.py stage arbiter-probe --dry-run   # probe only
python scripts/campaign.py stage arbiter-probe
```

`stage` pushes the branch, then on each host fetches it, checks out the pushed commit, builds, runs `configs`, checks the configs directory of every phase's profile, and confirms `build-release/peredur --version` names that commit with `dirty=0`. A failing host is reported and nothing launches. The configs check runs even with no `configs` key, because `run_experiments.py` exits 1 on a directory holding no `.toml` and a queued campaign would spend its attempts discovering that one tick at a time.

Staging *refuses by default* the three ways to destroy work that cannot be recovered from this side: a dirty checkout, a live `peredur` or `run_experiments.py`, and a checkout on another branch. All three are reported at once. Hosts normally sit on somebody's in-flight branch, so `--force` is the expected answer, and it must be a deliberate one: it prints the modified files, branch and head, then requires the host name typed back at a terminal, and refuses outright without one, so no script can reset a machine.

`git clean` is never run, since a host's untracked files are its results; `checkout -f` discards tracked modifications alone. An unforced stage also refuses a checkout ahead of the pushed commit rather than dropping those commits. That check needs the fetch, so it runs on the host rather than in the probe, which is why `--force` asks for confirmation whenever it is passed; gating the prompt on the probe made the flag inert for exactly that host. Rebasing a campaign's branch puts every staged machine in that state at once.

## Launching

**`start`** launches immediately. It re-probes each host, refuses one that is unstaged, running a campaign, or holding a pending queue entry for this campaign (`--ignore-queue` overrides, naming the entry it races), then runs its phases as one detached `nohup` chain joined with `&&` and appends the launch to `experiments/<name>/launches.jsonl`. Use it when the machines are free and the campaign is next.

**`enqueue`** hands the campaign to the host's queue and returns. Use it whenever the machines might be busy, whenever the campaign is one of several, and in preference to waiting: the tick starts it within five minutes and `queue` says where it got to. A second entry for the same campaign needs `--again`, since two entries mean two runners resuming off one CSV.

Neither takes a seed range; `enqueue` freezes the split into the entry, so editing the declaration mid-flight cannot move seeds under written rows.

## Scoring phases

`2026-08-29-aurus-matched` and `2026-09-04-aurus-rematch` each hand-rolled a `score_curves.py --maximality` pass after their search from ssh'd bash scripts, at 716 and 312 worker-hours. A `score` phase runs that pass as a phase.

```toml
[[phases]]
profile = "matched"
jobs = 16

[[phases]]
kind = "score"          # scores the run phase's results directory
workers = 8             # scorers in flight, each pinned to `cores` cores
cores = 4               # cores per scorer, also score_curves.py --jobs
cuts = 20
maximal_timeout = 900   # seconds for the antichain walk, absent a deadline
compare_timeout = 600   # seconds for the compare call
deadline_s = 4500       # the walk's real budget wherever it is set
wall_cap_s = 5400       # the outer timeout on one scorer; default deadline_s + 900
maximality = "on"       # run the implication sweep over the time cuts
ideals = "on"           # label candidates against the family's ideals
epsilon = ""            # separation thresholds, e.g. "0.05,0.2,0.5"; none if empty
fingerprint_words = 256
fingerprint_seed = 0
```

The maximality stage is one `maximal --curve` walk a run (see "Running antichain" in `docs/dev/performance.md`), so `maximal_timeout` bounds the whole walk and `deadline_s` replaces it wherever set. It was a per-cut bound until the walk replaced one process per cut, and applying it unchanged would have tightened it fivefold. The walk streams its event log, so a budget that fires keeps the rows already written and the curve covers the cuts up to them.

The last five choose which curves a phase writes. `maximality = "off"` with a non-empty `epsilon` is the behavioural-separation pass (see "Behavioural fingerprints" in `docs/dev/performance.md`): it makes no solver call, where the maximality sweep over the 3000 rematch runs cost 311.7 worker-hours. `ideals = "off"` drops the `compare` call behind `ideal_solutions`, which is 7.2 s of a 7.3 s epsilon-only run on a 421-candidate directory. A phase with `maximality = "off"` and no `epsilon` is refused, having nothing to score. The word count and seed must match across any two phases whose curves are compared. The manifest records which stages a phase ran and which binaries decided them, so an epsilon-only pass names `fingerprint` and neither `maximal` nor `compare`.

`results` defaults to the profile's results directory and `out` to `curves-<stem>`. Budgets default to `score_campaign.DEFAULTS` and are always written to the command line, so the manifest records the values used rather than a default that moved.

The host runs `scripts/score_campaign.py`. Run directories are queued smallest first by `accumulated/index.tsv` length, so heavy families land last. Each worker holds `cores` cores under `taskset -c` and runs `score_curves.py --maximality` under `timeout <wall_cap_s>` in its own session, so a cap kills the `maximal` and `compare` it forked too. A curve is written as `<out>/<run>.csv.part` and moved into place only on a zero exit with a non-empty file. Beside it the scorer writes `<run>.members.tsv`, one row per (cut, surviving file), `<run>.fingerprints.tsv`, one row per candidate and its hex fingerprint, and `<run>.relations.tsv`, one row per candidate and the strongest relation `compare` found between it and any of the family's ideals. All four move or are unlinked together, so a reader never sees a sidecar whose curve was thrown away. The relation file is written whenever `ideals = "on"` and the `compare` call returned, the maximality stage having nothing to do with it; an empty map is a header alone, and a `compare` that did not run leaves no file at all, which is the difference between nothing implying an ideal and the question never being asked. It is `.tsv` rather than `.csv` because `status` counts a pass's progress by globbing `*.csv` and `collect --curves` joins that same glob: a second CSV per run would double one count and be merged as a curve with a foreign header in the other. There is no flag for the sidecars, because a flag is how membership went missing before: the 2026-09-07 pass wrote only the sizes of the survivor sets it held, so asking which repairs were maximal at 60 s cost its 311.7 worker-hours again. A failed attempt lands as `rc run` in `<out>/failures.txt`, every attempt in `<out>/timings.txt` with its budgets, and output in `<out>/warnings.log`. Startup refuses `workers x cores` above the host's CPU count, since `taskset -c` on a missing core exits 1 and drains the queue into `failures.txt`, and exits 2 when no run directory matches this host's seeds, so a tick cannot mark an empty pass done.

A host scores only run directories ending `_seed<N>` for its own seeds, so the union `collect --curves` verifies is disjoint by construction.

The runner's freshness gate covers `build-release/maximal` and `build-release/compare`. The declaration has no `--allow-stale-binary` key, because curves carry no commit, the manifest is the only record, and an override belongs at a prompt where somebody answers for it.

The pass exits 0 only when every run has a curve; otherwise the tick spends an attempt and requeues it, and the resume skips existing non-empty CSVs. To raise a budget, commit to `campaign.toml` and `enqueue` again.

`stage` and the tick refuse a score phase whose results directory is missing, unless an earlier run phase of the campaign writes it. To reproduce an archived pass, declare a campaign of one score phase.

## AuRUS scoring phases

Two passes read an AuRUS tree rather than a PEREDUR run, and a `kind = "aurus-score"` phase runs either of them. One kind, two passes, chosen by `pass`:

```toml
[[phases]]
kind = "aurus-score"
pass = "anytime"                          # scripts/score_aurus_anytime.py
results = "experiments/aurus-rerun-out"   # the raw AuRUS tree
out = "experiments/anytime-rerun-out"
jobs = 8
compare_timeout = 3600

[[phases]]
kind = "aurus-score"
pass = "wellsep"                          # scripts/check_well_separated.py
results = "experiments/results-aurus-rerun"   # the adapted tree
out = "experiments/wellsep-uncensored"
jobs = 8
ltlsynt_timeout = 60
pattern = "*.tlsf"
fast_path = "off"
```

`name`, `results`, `out`, `hosts` and `jobs` are common to both passes, and `results` is required: neither tree belongs to a runner profile, so there is nothing to derive it from. `out` defaults to `<pass>-<stem>` beside the tree, the stem being its name less a `results-` or `aurus-` prefix, which keeps the two passes over one tree out of a single directory. `jobs` is the number of repeats in flight; each scorer is given `--jobs 1`, so one knob sets the load and the manifest records what ran. The **anytime** pass then takes `compare_timeout`, the budget for one repeat's `compare` call. The **wellsep** pass takes `ltlsynt_timeout`, the budget for one candidate's `ltlsynt` call, `pattern`, the glob that picks the candidate files out of a run's `accumulated/`, and `fast_path`, which is `off` so that every verdict is `ltlsynt`'s. A budget belonging to the other pass is refused by name, because a `compare_timeout` sitting unread on a wellsep phase is a bound somebody expected to bind.

The two passes read differently shaped trees, and the shape is checked rather than assumed. The anytime pass reads the raw tree `aurus_campaign.py` writes, `<results>/<spec>/repeat-<NN>`, and it has to be that one: a solution is dated from the iteration series in its repeat's `run.log`, and `aurus_adapt.py` carries the dates it derived from that log but not the log. The wellsep pass reads the PEREDUR-shaped tree `aurus_adapt.py` leaves, `<results>/<run>_seed<NN>`, and is pointed at each run's `accumulated/` rather than at the run, so nothing beside the candidates can add a row. A phase pointed at the wrong one is refused by name; the alternative reading is an empty queue, which is what a host whose search has not run yet looks like.

The host runs `scripts/aurus_score_campaign.py`, a sibling of `score_campaign.py` rather than a third stage inside it. That file drives one scorer and its whole vocabulary is `score_curves.py`'s — cuts, an antichain deadline, a per-worker block of cores under `taskset`, the three sidecars renamed with the curve — and these passes share none of it. What they share is the shape. A repeat stands where a seed does, AuRUS being unseedable, so both tree shapes split on the number in the directory name, enforced here in the same place the runner enforces it. The queue is smallest first, by solution count for the raw tree and by `accumulated/index.tsv` length for the adapted one, with ties on the name. Rows are written to `<unit>.csv.part` and moved into place only on an accepted exit with a non-empty file, so a reader listing `*.csv` never sees an output still being written. A failed attempt lands as `rc unit-dir` in `<out>/failures.txt`, every attempt in `<out>/timings.txt` with the budgets it ran under, and output in `<out>/warnings.log`.

`check_well_separated.py` exits 1 when any candidate came back `undecided`, and that is a verdict its CSV carries rather than a failed unit: an `ltlsynt` timeout is the answer the script exists to record honestly, and treating it as a failure would retry the whole repeat on every requeue for ever. Its exit 2 — no input files, no `ltlsynt` — is a failure. The anytime pass accepts 0 alone.

The freshness gate covers `compare` for the anytime pass. The wellsep pass runs no PEREDUR binary at all: `ltlsynt` comes out of the fetched Spot tree and carries no PEREDUR commit, so there is nothing to compare against, and its path and version string go in the manifest instead. Startup refuses `jobs` above the host's CPU count and exits 2 when no repeat matches this host's seeds, so a tick cannot mark an empty pass done.

Neither pass has an outer wall cap. Both scorers bound their own solver call — `compare_timeout` per repeat, `ltlsynt_timeout` per candidate — so a unit's cost is bounded by its candidate count times that budget, and there is no forked walk for a cap to have to kill.

The pass writes `aurus-score-manifest-<host>.json` into its output directory, carrying the pass, the seeds, every budget, the binaries and both timestamps, and rewrites it when it finishes. `status` reads it as `aurus-score:<out>`, counting the CSVs in that directory against the queue the manifest froze; BINARY is `compare`'s commit for an anytime pass and `?` for a wellsep one, which names no commit rather than borrowing one its rows did not come from. The pass exits 0 only when every repeat has rows; otherwise the tick spends an attempt and requeues it, and the resume skips existing non-empty CSVs. `stage` and the tick refuse an `aurus-score` phase whose tree is missing on the same terms a score phase's is refused.

## Reading a run

`status` never caches, and progress is never derived from a CSV's length. It prints each host's checkout, one row per campaign, and the queue.

STATE is one of four:

- `done` — the runner's plan reports every row done.
- `running` — a runner names this profile *and* the newest `run.log` is under three hours old.
- `stuck` — the runner is there but the log is older. Three hours is the harness's bound on one run's silence (3600s `peredur` plus 1800s `compare`, doubled for `--jobs 1`), so investigate rather than wait.
- `stalled` — no runner names this profile; a finished-but-incomplete campaign and a rebooted host look the same, so the row keeps its counts.

Tables are coloured only on a terminal, since `tick` logs to `$HOME/.peredur-queue.log` and `status` is often piped. `NO_COLOR` and `--no-color` disable colour, `CLICOLOR_FORCE=1` forces it for `less -R`, and `--json` is never coloured.

A `~` before ROWS means the whole CSV was counted because the runner returned no plan, which overstates progress. A `!` after BRANCH means the checkout has left the manifest's branch, so a resume would produce rows from other code. A `*` after BINARY means the launch used `--allow-stale-binary`, so its rows name a commit they did not come from.

A score phase appears as `score:<out>`, read from `score-manifest-<host>.json`: ROWS counts curves against the frozen queue, STALE is the newest file's age, BINARY is `maximal`'s commit, and the same three-hour rule applies. An `aurus-score` phase appears as `aurus-score:<out>` and is read the same way, from `aurus-score-manifest-<host>.json`. `--campaign <out>` selects either.

## The binary freshness gate

`run_experiments.py`, `stage` and `start` all refuse a binary whose commit differs from HEAD, built dirty, or unable to answer `--version`. `start` checks it so a launch does not die unseen behind `nohup`.

`--allow-stale-binary` is legitimate only when rows should carry the older commit: reproducing an archive at its `PROVENANCE.json` revision, or topping up a campaign unaffected by later commits. It never bypasses a failed build or saves a rebuild before a published campaign; the `*` is permanent.

## The queue

Entries live at `experiments/queue/NNN-<name>.toml` on their host, numbered from 001, and are untracked so state rewrites never dirty the checkout.

```
*/5 * * * * cd /home/benandrew/projects/counter && python3 scripts/campaign.py tick --host av2 >> $HOME/.peredur-queue.log 2>&1
```

`campaign.py cron --host av2 --print` only prints that line, since installing it would edit somebody else's crontab from a script.

A host set up before the rename to PEREDUR has a crontab that logs to `$HOME/.counter-queue.log`, and holds its lock at `~/.counter-queue.lock`. Migrate it while no tick is running: replace the crontab line with the one above, move the old log to `~/.peredur-queue.log`, and delete the old lock file. The checkout directory keeps its name, so the `cd` in the line does not change.

A tick takes the lock at `~/.peredur-queue.lock`, recovers any `running` entry, and runs the lowest-numbered queued entry's next phase in the foreground, holding the lock throughout so a hand-typed tick cannot race the cron one into a second runner. Never wrap the tick in `flock` on that lock file: a flock lock attaches to the open file description, so the inherited descriptor denies `acquire_lock`, both exit 0, and until 2026-08-13 no phase ever ran.

| From | To | On |
| --- | --- | --- |
| — | `queued` | `enqueue` |
| `queued` | `running` | a tick picks the lowest-numbered entry |
| `running` | `queued` | the phase finished and another remains |
| `running` | `done` | the last phase finished |
| `running` | `queued` | the phase failed or the tick was interrupted, attempts left |
| `running` | `failed` | the same, with the attempt cap reached |
| `failed` | `queued` | `campaign.py requeue --host av2 001-name.toml` |
| `queued` | `cancelled` | `campaign.py dequeue --host av2 001-name.toml` |
| `running` | `cancelled` | the same, having killed the tick holding it |
| `cancelled` | `queued` | `campaign.py requeue --host av2 001-name.toml` |

`dequeue` stops a campaign by setting `cancelled`. For a running entry it lists the child processes, kills the tick, then kills the runner and `peredur`. The tick dies first because it would otherwise overwrite the cancellation from its in-memory copy at phase end; the children are listed first because they reparent to init once it dies, and killed after because the runner outlives its tick. Entries are tombstoned so their numbers are never reused, `requeue` puts one back, and `--dry-run` only names the processes it would kill. Never edit queue TOML by hand.

`running` without a lock holder means an interrupted tick. Recovery costs one attempt and resumes from the CSV. After the attempt cap (three by default, `--max-attempts`), the entry stops with `last_error` until `requeue`, after `experiments/queue/NNN-<name>.log` has been read.

## A tick stages its own branch

With nobody at a terminal to confirm `--force`, a tick on the wrong branch stages itself: it fetches the entry's commit, checks it out, builds, and reads `build-release/peredur --version` before the phase.

It stages for the branch alone and still refuses, spending an attempt with the reason in `last_error`:

- **A dirty checkout.** Uncommitted edits cannot be fetched back.
- **A live `peredur` or `run_experiments.py`.** A checkout would rebuild the binary its remaining rows name.
- **A HEAD no remote branch contains.** An unpushed commit cannot be reconstructed.

When staging moves `scripts/`, the tick re-execs once, since the old `campaign.py` would misread a newer declaration as a bad one. It passes the queue lock descriptor to the new process, spends no attempt, and sets `PEREDUR_TICK_RESTARTED` to prevent loops. The check diffs the two commits under `scripts/`, so a branch on the same scripts runs in the tick that staged it.

`stage --force` is the only way past the refusals. `tick --no-stage` restores manual staging for a host driven by hand.

`enqueue` pushes the branch and freezes its commit and build command into the entry, because a tick cannot read `campaign.toml` before checkout. Freezing also keeps every phase on one binary. A branch moving after `enqueue` does not move the entry; re-enqueue to use a new commit.

Entries run in strict numerical order, and a campaign keeps its number between phases until done. `campaign.py queue` shows each entry's branch and commit.

A tick checks its phase's configs first and runs `configs` only when something is missing; with no command declared it fails by name.

An entry with no frozen commit refuses; run `stage` by hand.

## Collecting and closing

```sh
python scripts/campaign.py collect --profile tlsf --dry-run
python scripts/campaign.py collect --profile tlsf
```

`collect` rsyncs each host's results and merges on the natural key through `merge_experiments.py`, verifying a union rather than a sum: disjoint ranges overlap on nothing, a re-collect on everything, and only the union separates either from a silent loss. A host that answers with nothing prints INCOMPLETE, since arithmetic over the hosts that answered agrees with itself. For an unmerged branch absent from `merge_experiments.PROFILE_CSVS`, give `--csv` and `--results-dir`; `collect` refuses to guess.

`collect --curves <out>` copies each host's `experiments/<out>/` into `experiments/<out>/<host>/` and joins curves into `experiments/<out>.csv`. Identical duplicates collapse, differing duplicates are MISMATCH, mixed headers are refused, and a silent host is INCOMPLETE. The joined file is written only if every curve passes.

Closing a campaign means the archive can be read without the git history. Rename the directory to `experiments/<YYYY-MM-DD>-<name>/`, then:

- Complete `PROVENANCE.json` — `status: closed`, the run window, the row counts per path, and the decision written against the decision rule `PLAN.md` pre-registered. Attribution is `recorded` for a campaign staged through `stage`, since the binary's commit was verified on both hosts before launch.
- Where the campaign ran a score phase, assemble its `maximality_pass` block from the score manifests under `experiments/<out>/<host>/`: the `invocation` template, the budgets, both scoring binaries' commits, the seeds each host scored, the start and finish stamps and the counts are all fields of `score-manifest-<host>.json`, and the failed and partial curves are read off `failures.txt` and `timings.txt` beside it. Nothing in that block is reconstructed from memory.
- Vendor `gen_configs.py`, `run_experiments.py` and `merge_experiments.py` verbatim into `experiments/<campaign>/scripts/`, and record each one's source commit and git blob sha in `vendored_scripts`. Check a copy with `git hash-object`.
- If the branch is split into pull requests or rebased rather than merged, tag its tip `provenance/<name>` and record that in `profile_commit.held_by_tag`. Every sha the file names points into that branch, and without the tag `git gc` collects them.
- If a C++ default flipped between the campaign running and the close, add it to the "Config vintage" note in `experiments/README.md`. An archived config only holds the keys a sweep overrode, so a changed default silently changes what every one of them means.

`campaign.toml` stays in the archive, keeping the seed split reproducible; queue entries do not.

## Describing a closed campaign

```sh
python scripts/campaign.py describe <campaign>
python scripts/campaign.py describe --all --json
```

`describe` derives a declaration for an archived campaign from `experiments/<campaign>/` and writes nothing. The 19 closed campaigns never had one, and none is written into them: an archive is replayed through its vendored scripts at its recorded commit, where `campaign.py` does not exist, and a file beside its own sources would be a cache that can drift. `describe --all --json` also answers whether a config knob is ever set, over 19 archives rather than roughly 65,000 configs.

The factor cross comes from the merged CSV's key columns (`sweep`, `level_name`, `selection`, `weakening`, `metric`, `repair_mode`, `spec`, `seed`), preferred over vendored `PROFILES` because the dict says what was intended and the CSV what happened. The host split comes from disjoint per-host CSVs (13 of 19), the branch from `PROVENANCE.json` (5 of 19), and phase order from a vendored shell driver, which only elitism has.

`attribution = "inferred"` follows 12 of the 19 `PROVENANCE.json` files and means each value was read from the file `derived_from` names, not recorded at the time. Unrecorded fields are listed in `not_recorded` and omitted rather than defaulted, since an absent field is true of the archive and a plausible default is not. `jobs` is always there, every run having taken its profile's `default_jobs`. A CSV header older than `selection` leaves `schemes` absent.

The output is not runnable, naming retired profiles (`wellsep-timing`) and rejected schemes (`nsga2`). `2026-07-31-replicate`, `2026-08-03-libspot-soak` and `2026-08-04-engine-comparison` never ran a factor cross and get stubs naming their vendored scripts and `kind`, with `phases` in `not_recorded`, so every printed line has a file behind it.

## Testing this

```sh
python3 scripts/test_campaign.py
```

A plain script, no pytest, that reports every failure together at the end; read the summary, not the first `FAIL`. It never touches a lab machine: remote output is captured, `collect` uses throwaway checkouts, and stage and queue paths use temporary git repositories with `PEREDUR_RUNNER_CMD` and `PEREDUR_SCORER_CMD` stubs that record their arguments. `score_campaign.py` uses a fake results tree, a `PEREDUR_SCORE_CURVES_CMD` stub, and stub binaries under `PEREDUR_BIN_DIR`; `aurus_score_campaign.py` uses one fake tree of each shape, `PEREDUR_ANYTIME_CMD` and `PEREDUR_WELLSEP_CMD` stubs, and the same stub binaries. New launch-path code must be tested the same way.

`campaign.py` parses TOML itself because av2 and av3 run python3 3.10.12 with neither `tomllib` nor `tomli`. Its subset is checked against `tomllib` on every fixture wherever that exists. Remote shell scripts never use bare globs, since zsh's `NOMATCH` aborts on an unmatched pattern and every later section vanishes in silence.
