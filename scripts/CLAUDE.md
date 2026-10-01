# scripts

Operating manual for `campaign.py`. `README.md` beside it is the reference for every flag and for the rest of the harness; this file is what to do, in what order, and what each reading means. Campaign work is driven from here rather than typed by hand: the prohibitions in the root `CLAUDE.md` hold, and the detail below is why each of them exists.

## The declaration

A campaign is one directory, `experiments/<name>/`, holding `campaign.toml` and, where the campaign answers a question, the pre-registered `PLAN.md`:

```toml
name = "arbiter-probe"          # must equal the directory name
branch = "feat/arbiter-probe"   # the branch every host runs from
profile = "arbiter-probe"       # default profile for phases that omit one
build = "cmake --build build-release"   # optional, this is the default
configs = "python3 scripts/gen_configs.py --sweeps C --out-dir experiments/configs-arbiter-probe"  # optional, no default
hosts = { av2 = "0-99", av3 = "100-199" }
phases = [ { profile = "arbiter-probe", jobs = 4 } ]
```

`configs` is the `gen_configs.py` line the campaign needs, run on the host during `stage`, after the build and before the version check so a generator that fails reports as itself. It has no default, because no line is right for every campaign and the wrong flags are worse than nothing: a config tree is untracked, so the branch carries the declaration and not the configs, and until this key existed the invocation lived only in somebody's shell history. It runs in a subshell, so two generator calls joined with `&&` behave as written.

A phase takes `profile`, `jobs`, and optionally `name`, `sweeps`, `specs` and `hosts`; `[[phases]]` headers mean the same thing as the inline array. Seed ranges are inclusive and may be comma-separated (`"0-9,20-29"`). Phases run in order on each host and stop at the first failure, so a phase that depends on an earlier one is safe to declare.

A phase also has a `kind`: `run`, the default and what every phase was before the key existed, or `score`. A score phase is the offline maximality pass over a results directory rather than a search, and reads its own keys (`results`, `out`, `workers`, `cores`, `cuts`, `maximal_timeout`, `compare_timeout`, `deadline_s`, `wall_cap_s`) in place of `jobs`, `sweeps` and `specs`, each of which it refuses by name. "Scoring phases" below is the whole of it.

A phase's own `hosts` table overrides the campaign-level split for that phase alone. It is held to exactly the campaign-level rules (known hosts, well-formed inclusive ranges, and no two hosts sharing a seed) and may only narrow, since every host it names must already be declared at the top level — `stage` staged no other, and no phase would run there. A host the table omits runs nothing for that phase, contributing no command to that host's `&&` chain under `start` and being skipped by a tick, which advances past it. The case for it is a campaign whose paths have different sample sizes, which cannot share one range because `run_experiments.py --seeds` replaces a profile's own seed list rather than intersecting with it. Forcing 70 FRETISH seeds over 4 specs onto TLSF phases sized at 25 over 20 families would run 4,200 TLSF rows where the plan says 1,500, and nothing in the declaration would say the row count had changed. `enqueue` freezes the per-phase ranges into the queue entry as `phase_seeds`, for the reason it already freezes the campaign range, and an entry written before that field existed falls back to the campaign split.

`campaign.toml` names a profile that `run_experiments.py` defines and never redefines one. Migrating `PROFILES` out of the runner would break the eighteen archived campaigns that vendor a verbatim copy of that file, so the declaration is validated against whatever the current checkout defines and fails loudly on a name it does not find. Four other things fail the load: a `name` that disagrees with its directory, a malformed range, an unknown key, and two hosts whose ranges overlap. The overlap is the one worth the noise, because it is invisible afterwards — both hosts run the shared seeds, the merge keeps one row per key, and the campaign quietly costs more machine time and returns fewer rows than the plan says.

The declaration is tracked in git through a `.gitignore` negation. The lab machines get it by checking out the campaign's branch, which is also what makes `tick` able to read it there.

## Staging a host

```sh
python scripts/campaign.py stage arbiter-probe --dry-run   # probe only
python scripts/campaign.py stage arbiter-probe
```

`stage` pushes the branch to origin, then on each declared host fetches it, checks it out at the pushed commit, runs the build command, runs the `configs` command where the campaign declares one, checks the configs directory of every profile its phases name, and reads `build-release/counter --version` back to confirm the binary was built from that commit with `dirty=0`. A host that fails any of it is reported and nothing is launched.

The configs check runs whether or not the key is declared, which is the half that matters: the declarations written before the key have none, and `run_experiments.py` exits 1 on a directory holding no `.toml`. A queued campaign discovers that one tick at a time and spends its three attempts on it — which is what aurus-h2h did on av3, av2 having had a calibration run's configs lying around. `stage` now refuses the host by name and names the directory. The path comes from `run_experiments.PROFILES[…]["configs_dir"]` made relative to the runner's own `REPO_ROOT`: the value there is absolute against *this* checkout, and the script that reads it runs under somebody else's home directory. Every phase's profile is checked, not only the campaign-level default, since a second phase's directory is the one nothing else would look at.

Staging **refuses by default**, and the three refusals are the three ways to destroy work that cannot be recovered from this side: a dirty checkout, a live `counter` or `run_experiments.py` process, and a checkout on any branch other than the campaign's. All three are reported at once rather than one per attempt. The hosts normally sit on somebody's in-flight branch, so switching them is the expected case and `--force` is the expected answer; what matters is that the answer is deliberate. Through the queue there is no such answer to give, which is why a tick stages the branch itself and refuses the other two — see "A tick stages its own branch" below. `--force` prints the modified files by name, the branch and head it is about to leave, and then requires the host name typed back at a terminal. Without a terminal it refuses outright, which is what keeps a scripted or cron-driven stage from resetting a machine.

`git clean` is never run. A host's untracked files are its results, and the campaign staged over them would delete the previous campaign's output; `checkout -f` discards tracked modifications and nothing else. An unforced stage also refuses a checkout that is ahead of the pushed commit rather than dropping those commits, and `--force` is what discards them.

That fourth refusal lives on the host rather than in the probe, because detecting it needs the fetch: the machine may not hold the target commit until the apply script has run. So it does not appear in the probe's list, and a host can be clean, idle, on the campaign's own branch — no refusal at all from this side — and still decline over there. This is why `--force` asks for confirmation whenever it is passed rather than only when the probe found something: gating the prompt on a probe-level refusal made `--force` inert for exactly that host and skipped the confirmation with it, so the flag appeared to do nothing and the stage failed with the same message it gives without it. Rebasing a campaign's branch puts every staged machine in that state at once, since their commits are then unreachable from the new tip and read as work unique to the host.

## Launching

Two paths, and the choice is about attention rather than mechanism.

**`start`** launches immediately: it re-probes each host, refuses one that is unstaged, already running a campaign, or holding a pending queue entry for this campaign, and then runs that host's phases as a single detached `nohup` chain joined with `&&`. Each launch is appended to `experiments/<name>/launches.jsonl` — the record of what was asked for, from the side that asked, which is the half that disappears when an ssh session closes. Use it when the machines are free and the campaign is the next thing to run.

**`enqueue`** hands the campaign to the host's queue and returns. Use it whenever the machines might be busy, whenever the campaign is one of several, and always in preference to waiting: the tick will start it within five minutes and `queue` will say where it got to. A campaign already queued on a host is refused a second entry unless `--again` is given, because two entries mean two runners resuming off one results CSV. `start` refuses on the same ground where a pending entry exists, and `--ignore-queue` is the override; it names the entry it is racing rather than launching quietly beside it.

Neither verb takes a seed range. Both read the split from `campaign.toml`, and `enqueue` freezes it into the queue entry, so editing the declaration mid-flight cannot move a host's share of the seeds underneath rows already written against the old split.

## Scoring phases

Two campaigns, `2026-08-29-aurus-matched` and `2026-09-04-aurus-rematch`, needed a second pass after their search: `score_curves.py --maximality` over every run directory, producing the `maximal_solutions` and `maximal_ideal_solutions` anytime curves that are the whole of the RQ3 result. The first pass cost 716 worker-hours and the second 312, and both were hand-rolled, as a pair of bash scripts ssh'd onto each host, a budget file edited over ssh while the queue ran, and a `maximality_pass` provenance block assembled by hand afterwards. That is every failure `campaign.py` was written to remove, arriving one campaign later. A `score` phase is the pass as a phase.

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
```

`results` defaults to the phase's profile's results directory, made relative to the checkout the way the configs directory is, and `out` to `curves-<stem>`, so `results-rematch` scores into `experiments/curves-rematch/`; a phase may name a directory in `results` and no profile at all. Every budget defaults to `score_campaign.DEFAULTS`, which is the one place they are written, and every one is stated on the scorer's command line whatever the declaration said, so the manifest the host writes records the declaration's values rather than a default that moved.

On the host the phase runs `scripts/score_campaign.py`, the scoring twin of `run_experiments.py`, and its mechanics are the hand-rolled scripts' mechanics. The run directories directly under `results` are queued smallest first by the line count of `accumulated/index.tsv`, so the heavy families land last; `workers` threads each own a block of `cores` cores under `taskset -c`, and each item is one `score_curves.py --maximality` invocation over one run directory under coreutils `timeout <wall_cap_s>`, which kills the scorer's whole process group when it fires. The curve is written to `<out>/<run>.csv.part` and moved into place only on a zero exit with a non-empty file, so a reader listing `*.csv` never sees a curve in flight; a failed attempt is deleted and lands as `rc run` in `<out>/failures.txt`. Every attempt appends `accumulated elapsed_s rc run maximal_timeout/compare_timeout/deadline_s` to `<out>/timings.txt`, which is what makes a curve readable against the budgets it was scored under, and every scorer's output goes to `<out>/warnings.log`. Each scorer runs in a session of its own, and a cap that fires kills that whole session, since `score_curves.py` forks `maximal` and `compare` and killing it alone would leave those running on the cores the next item is pinned to; where `taskset` or `timeout` is absent the pass runs unpinned or under a Python cap that kills the session the same way, and the manifest says so. Two things are refused at startup rather than discovered one run at a time: a `workers x cores` past the host's CPU count, since `taskset -c` on a core the host does not have exits 1 at once and drains the queue into `failures.txt` in milliseconds, and a queue with no run directory on this host's seeds, which exits 2 naming the directory and the seeds, because an empty pass exiting 0 would let a tick mark the phase done over nothing.

A host scores its own seeds and nothing else. The phase passes the same `--seeds` a run phase would, and the scorer keeps only the run directories whose name ends `_seed<N>` with N in that list, which is the split enforced in the same place the runner enforces it: two hosts holding the same results tree layout never score one run twice, and the union `collect --curves` verifies is disjoint by construction.

The scorer runs the runner's freshness gate over `build-release/maximal` and `build-release/compare`, refusing a binary whose commit is not the checkout's HEAD, one built dirty, or one that cannot answer `--version`. The declaration has no key for `--allow-stale-binary`, deliberately: a curve names no commit in its rows, so the manifest is the only record of what scored it, and an override belongs at a prompt where somebody is answering for it. The 2026-08-29 pass ran two builds of `maximal` and one `compare` from a commit the campaign's binary did not carry, and the `scoring_binaries` note in that archive's `PROVENANCE.json` is what it costs to reconstruct that afterwards.

It exits 0 only when every queued run has a curve, so a pass with failures fails its phase, the tick spends an attempt and requeues it, and the resume makes the retry cheap: a run whose `<out>/<run>.csv` exists non-empty is skipped, and a requeued phase re-scores only what failed. Raising a budget for the heavy families is a commit to `campaign.toml` and a fresh `enqueue`, which is the replacement for the flags file: the tick reads the declaration at the entry's frozen commit, the resume skips every curve already written, and the timings lines record which budgets each attempt ran under, so a directory scored under two settings stays readable.

`stage` checks a score phase's results directory the way it checks a run phase's configs directory, on the hosts the phase runs on, and refuses the host by name where it is absent, except where an earlier run phase of the same campaign writes it there, the common case, since that directory does not exist until the search has run. A tick makes the same check when the phase's turn comes and refuses with the directory in `last_error`, spending an attempt rather than the scorer's whole startup. A score phase over a results directory another campaign produced is the case the stage check is for, and reproducing an archived pass is exactly that: a campaign of one score phase over the archived results.

A separation recount never re-runs the maximality sweep. `maximality = "off"` with `members_from` naming an earlier pass's `out` reads each run's antichains from that pass's `<run>.members.tsv` sidecars and fingerprints the candidates afresh, so the word count, lasso shape (`fingerprint_max_prefix`, `fingerprint_max_cycle`) and distance (`fingerprint_distance`) can all move at the cost of one `fingerprint` call a run. The scripts/README.md score-phase section has the keys and their defaults.

`status` reports a score phase as a `score:<out>` row (see "Reading a run"), and `collect --curves <out>` brings the curves home (see "Collecting and closing").

## Reading a run

`campaign.py status` is read-only, takes about a second, and is the source of truth. Re-run it; never cache what it said, and never re-derive progress from the length of a results CSV. It prints the checkout each host is on, one row per campaign, and the queue where there is one.

STATE is one of four:

- `done` — the runner's own plan reports every planned row already done.
- `running` — a runner process names this profile *and* the newest `run.log` under the campaign's results directory is fresher than three hours.
- `stuck` — the runner process is there and the log is older than that. Three hours is the harness's own bound on one run's silence (a 3600s `counter` timeout plus an 1800s `compare` timeout, doubled for a `--jobs 1` host), so `stuck` means the process is alive and producing nothing. Investigate it; do not wait for it.
- `stalled` — no runner process names this profile. A finished-but-incomplete campaign and a machine that was rebooted look identical here, which is why the row still carries its counts.

On a terminal every table is coloured — STATE by what it says, TREE and PROCESSES on the checkout table, the `!` and `*` marks themselves, and `queue` and the `stage` result table through the same renderer — and nowhere else: colour is off wherever stdout is not a terminal, since `tick` runs from cron into `$HOME/.counter-queue.log` and a `status` is as often piped as read, and escapes in either are the 59KB-of-escape-codes failure the root `CLAUDE.md` records against the C++ status line. `NO_COLOR` and `--no-color` turn it off on a terminal too, `CLICOLOR_FORCE=1` forces it back on for `less -R`, and `--json` is never coloured either way.

Two marks qualify a row. A `~` before the ROWS count means the number is the whole CSV's length rather than this launch's share, used only where the runner returned no plan; it overstates progress wherever profiles share a CSV or a relaunch topped one up. A `!` after BRANCH means the campaign's manifest names a branch the checkout has since left, so a resume there would produce rows from other code. A `*` after BINARY means that launch used `--allow-stale-binary`, so its rows name a commit they did not come from.

A score phase is a `score:<out>` row, read from the `score-manifest-<host>.json` the scorer writes inside its output directory. ROWS is the count of curve CSVs under that directory against the queued count the manifest froze, STALE is the age of the newest file under it, BINARY is `maximal`'s commit, and the same three-hour rule applies with a `score_campaign.py` naming that directory standing where a runner naming a profile does. `--campaign <out>` selects it as `--campaign <profile>` selects a run.

## The binary freshness gate

`run_experiments.py` reads `counter --version` at startup and refuses to launch when the binary's commit differs from the working tree's HEAD, when it was built dirty, or when it cannot answer at all. `stage` checks the same thing one step earlier, and `start` refuses on it too, because a launch that dies on the far side of a `nohup` leaves its message in a log nobody is reading yet.

`--allow-stale-binary` is legitimate only where the mismatch is the intention and the rows are meant to carry the older commit: reproducing an archived campaign at the revision its `PROVENANCE.json` names, or topping up a campaign whose earlier rows came from a binary a later commit did not affect. It is never the way past a failed build, and it is never used to save a rebuild before a campaign that will be published — the `*` on BINARY is permanent, and `PROVENANCE.json` records how little of a wrong attribution is recoverable afterwards.

## The queue

Entries live at `experiments/queue/NNN-<name>.toml` on the host that runs them, numbered per host from 001. They are deliberately untracked: a tick rewrites the state on every transition, and a tracked file doing that would leave the checkout permanently dirty, which is the first thing `stage` refuses to touch.

```
*/5 * * * * cd /home/benandrew/projects/counter && python3 scripts/campaign.py tick --host av2 >> $HOME/.counter-queue.log 2>&1
```

`campaign.py cron --host av2 --print` emits that line. Printing is all it does: installing a cron entry on a lab machine would be editing somebody else's crontab from a script.

A tick takes the lock, recovers any entry left `running`, then runs the next phase of the lowest-numbered queued entry in the foreground. The phase holds the lock for its whole duration, so every tick that lands during it exits at once, which is what stops a tick typed by hand racing the cron one into a second runner over one CSV.

The crontab line must not wrap the tick in `flock` on that same lock file. It did until 2026-08-13, and no tick could ever run a phase: flock locks attach to the open file description, the wrapper's descriptor survives the exec, and `acquire_lock` opening the path again is denied by the lock its own parent holds. The wrapper and the tick both exit 0, so 299 consecutive failures on each host logged one repeated line and nothing read as an error. `acquire_lock` is the only guard and covers the hand-typed case the wrapper never did.

| From | To | On |
| --- | --- | --- |
| — | `queued` | `enqueue` |
| `queued` | `running` | a tick picks the lowest-numbered entry |
| `running` | `queued` | the phase finished and another remains |
| `running` | `done` | the last phase finished |
| `running` | `queued` | the phase failed or the tick was interrupted, attempts left |
| `running` | `failed` | the same, with the attempt cap reached |
| `failed` | `queued` | `campaign.py requeue --host av2 001-name.toml` |

`running` with no tick holding the lock means an interrupted tick rather than a live phase, since a tick runs its phase in the foreground. Recovery costs one attempt and re-runs the same phase over the same seeds; `run_experiments.py` resumes off the results CSV, so a tick killed mid-phase costs nothing but the runs that were in flight. The attempt cap (three by default, `--max-attempts`) is what separates a slow phase from a broken one: past it the entry stops moving and holds the exit status in `last_error`, and only `requeue` restarts it, after somebody has read `experiments/queue/NNN-<name>.log`.

## A tick stages its own branch

Campaigns queue up on different branches, and the only thing standing between one entry and the next is a checkout. `stage` cannot answer that: switching a host's branch is one of its three refusals, `--force` prints what it will discard and requires the host name typed back at a terminal, and there is nobody at one when the tick fires at 03:05. So a tick that finds the checkout somewhere other than its entry's branch fetches the entry's commit, checks it out, runs the build command and reads `build-release/counter --version` back — `stage`, performed from inside the host, before the phase runs.

It stages for the branch alone. The other two refusals hold, and a tick that meets either spends an attempt and stops with the reason in `last_error`:

- **a dirty checkout** — somebody's uncommitted edits, which no fetch brings back;
- **a live `counter` or `run_experiments.py`** — checking out under a running campaign rebuilds the binary its remaining rows will name, so every row after the swap carries a commit it did not come from;
- **a HEAD no remote branch contains** — an unpushed commit, the third thing the checkout can hold that this side cannot reconstruct. `stage` guards the same case by refusing a checkout ahead of the pushed commit, a question that cannot be asked across two unrelated branches.

A stage that moves `scripts/` restarts the tick, once. The declaration is tracked on the campaign's own branch and is therefore unreadable until the checkout has moved, and the process that moved it keeps the `campaign.py` it imported beforehand — so a declaration using keys that older revision does not define fails to parse, and a phase kind it does not define cannot be rendered even where it does parse. Both read as a bad declaration rather than as a stale parser, and the obvious response, editing the file, is wrong: `aurus-curves` spent an attempt on exactly that on 2026-09-08, its score phase's keys postdating the revision both hosts were standing on. The tick re-execs itself after such a stage, hands the queue lock's descriptor to the new process rather than dropping it for the length of the exec, and runs the phase there; `ensure_staged` has already put the entry back in `queued`, so no attempt is spent. `COUNTER_TICK_RESTARTED` marks the restarted process, so scripts that move again cannot loop it, and the check is a diff of the two commits under `scripts/`, so a campaign on another branch of the same scripts still runs its phase in the tick that staged it.

`stage --force` remains the only way past any of the three, and `tick --no-stage` restores the old behaviour — an entry on another branch burns attempts and says `stage it first` — for a host being driven by hand.

`enqueue` is what makes this possible: it pushes the campaign's branch to origin and freezes the branch's **commit** and the campaign's **build** command into the entry, beside the seed ranges it already froze. Both are there because the declaration is tracked on the campaign's own branch, so a tick standing anywhere else cannot read a word of `campaign.toml` until after the checkout it is about to perform; the commit and the build command are exactly what that checkout needs. Freezing the commit has the same force as freezing the seeds: a campaign's phases run over hours and requeue between them, and one whose phases straddled two commits would write rows under a single `commit` column that came from two binaries. A branch that moves after `enqueue` therefore does not move the entry — re-enqueue it to pick the new commit up.

Entries are still taken in strict numerical order, which is what keeps the checkout still: an entry requeues at its own number between phases, so it stays the lowest-numbered one until it is `done` and a campaign runs to completion before the next branch is staged. `campaign.py queue` prints the branch and commit each entry needs, beside its state.

A tick also generates the configs its phase needs. `stage` runs the campaign's `configs` command and then checks every directory its phases read; a tick that did neither launched straight into the empty directory `run_experiments.py` exits 1 on, which is the failure the check was written for, on the path with nobody watching. The check comes first and the command runs only where something is missing, so a host already carrying its configs pays one directory scan and runs no generator. It sits after the declaration is loaded rather than inside the checkout, because the command and the directories to check are keys of `campaign.toml`, which nothing can read until the checkout has moved. A campaign that declares no command fails here by name, in the words `stage` uses.

An entry written before this existed names no commit. There is nothing safe to guess — the branch has moved since, or the entry would not be waiting — so it refuses exactly as every entry used to, and the fix is `stage` by hand.

## Collecting and closing

```sh
python scripts/campaign.py collect --profile tlsf --dry-run
python scripts/campaign.py collect --profile tlsf
```

`collect` rsyncs each host's per-run tree and CSV back, merges on the natural key through `merge_experiments.py`, and verifies the result as a union rather than a sum: two hosts on disjoint seed ranges overlap on nothing, a re-collect overlaps on everything, and only the union separates either from a silent loss. A host that answers with nothing is carried into the check explicitly and prints INCOMPLETE, because arithmetic over the hosts that did answer agrees with itself perfectly. A campaign on an unmerged branch has no entry in `merge_experiments.PROFILE_CSVS`, which is what `--csv` and `--results-dir` are for; `collect` refuses to guess the file name.

`collect --curves <out>` is the score phase's half. It pulls each host's `experiments/<out>/` whole into `experiments/<out>/<host>/`, so `timings.txt`, `failures.txt`, `warnings.log` and `score-manifest-<host>.json` stay beside the curves they describe and apart per host, then joins every per-run CSV into `experiments/<out>.csv` with the header written once. The verification is the results merge's, over run names rather than keys: a run on both hosts is one curve where the copies are byte-identical and a MISMATCH where they differ, two headers among the curves are two `score_curves.py` vintages and are refused naming both rather than concatenated under one set of columns, and a host that answers nothing is INCOMPLETE. The joined file is written only when every curve was accepted, so a bad collect leaves the last good `experiments/<out>.csv` standing.

Closing a campaign means the archive can be read without the git history. Rename the directory to `experiments/<YYYY-MM-DD>-<name>/`, then:

- Complete `PROVENANCE.json` — `status: closed`, the run window, the row counts per path, and the decision written against the decision rule `PLAN.md` pre-registered. Attribution is `recorded` for a campaign staged through `stage`, since the binary's commit was verified on both hosts before launch.
- Where the campaign ran a score phase, assemble its `maximality_pass` block from the score manifests under `experiments/<out>/<host>/`: the `invocation` template, the budgets, both scoring binaries' commits, the seeds each host scored, the start and finish stamps and the counts are all fields of `score-manifest-<host>.json`, and the failed and partial curves are read off `failures.txt` and `timings.txt` beside it. Nothing in that block is reconstructed from memory.
- Vendor `gen_configs.py`, `run_experiments.py` and `merge_experiments.py` verbatim into `experiments/<campaign>/scripts/`, and record each one's source commit and git blob sha in `vendored_scripts`. Check a copy with `git hash-object`.
- If the branch is split into pull requests or rebased rather than merged, tag its tip `provenance/<name>` and record that in `profile_commit.held_by_tag`. Every sha the file names points into that branch, and without the tag `git gc` collects them.
- If a C++ default flipped between the campaign running and the close, add it to the "Config vintage" note in `experiments/README.md`. An archived config only holds the keys a sweep overrode, so a changed default silently changes what every one of them means.

The campaign's `campaign.toml` stays in the archive, which is what makes the seed split reproducible; its queue entries do not, having been on the hosts.

## Describing a closed campaign

```sh
python scripts/campaign.py describe <campaign>
python scripts/campaign.py describe --all --json
```

`describe` prints a campaign declaration for an archived campaign, derived on demand from `experiments/<campaign>/`. `--all` covers every archive and `--json` is machine-readable. It is read-only and writes nothing.

The 19 closed campaigns under `experiments/` never had a declaration: their seed split was a hand-typed ssh range, and the factor cross lived only in a `PROFILES` entry in the vendored `run_experiments.py`. Writing a `campaign.toml` into each archive was considered and rejected on two grounds. An archive is reproduced at the commit its `PROVENANCE.json` names, through the vendored `scripts/`, and `campaign.py` does not exist at that revision, so a declaration written today would be unreadable in the only place the archive is ever replayed. The derivation's sources also sit in the same directory the output would, which makes the file a cache that can drift from what it caches. The precedent is in the root `CLAUDE.md`: when a C++ default flipped and silently changed what every archived config meant, the fix was a note in `experiments/README.md` rather than an edit to the archives.

Auditing whether a config knob earns its keep meant counting how many configs under `experiments/` set it, across roughly 65,000 files. `describe --all --json` answers the same question over 19 archives in one command, and stays correct because it reads the archive rather than a snapshot of it.

The factor cross comes from the merged results CSV, whose key columns — `sweep`, `level_name`, `selection`, `weakening`, `metric`, `repair_mode`, `spec`, `seed` — are exactly the cross that ran. The CSV is preferred over the vendored `PROFILES` dict throughout, because the dict says what was intended and the CSV says what happened. The host split comes from the per-host CSVs where their seed blocks are disjoint, which 13 of the 19 archives carry; only one `PROVENANCE.json` (elitism) records the split directly. The branch comes from `PROVENANCE.json`, where 5 of 19 state one. Phase order comes from a vendored shell driver naming its profiles in order, and only the elitism campaign has one.

`attribution = "inferred"` mirrors the convention 12 of the 19 `PROVENANCE.json` files already use, and it licenses exactly one thing: that the value was read back out of a named file in the archive, which `derived_from` names per field, so any claim is checkable against its source. It does not license treating the output as a record kept at the time. A `not_recorded` list names every field no file in the archive records, and those fields are omitted rather than defaulted, because an absent field is a true statement about the archive and a plausible default is not. `jobs` is in every campaign's `not_recorded` list: the one launch script in the whole archive passes no `--jobs`, so every run took its profile's `default_jobs`, which is the runner's value rather than the campaign's. Older CSVs are narrower, 12 columns in July against 21 by August, and a campaign whose header predates the `selection` column cannot have recorded a selection scheme, so `schemes` is absent there rather than filled in with the legacy default `merge_experiments.fill_defaults` would supply.

The output is not runnable. It names profiles this checkout has retired (`wellsep-timing` went with the `[filters.intervals]` key) and selection schemes it rejects (`nsga2`, renamed to `nsga2-truncate` on 2026-08-06). Reproducing an archived campaign goes through `experiments/<campaign>/scripts/` at the commit `PROVENANCE.json` names, which is what those vendored copies exist for.

Three archives — `2026-07-31-replicate`, `2026-08-03-libspot-soak` and `2026-08-04-engine-comparison` — have no merged results CSV and never ran a factor cross, the last two driving `soak.py` and `compare_engines.py` rather than the runner. `describe` prints a stub for those, naming the vendored scripts and the `kind` the archive records, with `phases` in `not_recorded`. Forcing a cross onto an archive that never ran one would be the first inferred field nothing in the directory supports, and the whole value of the verb is that every line it prints has a file behind it.

## Testing this

```sh
python3 scripts/test_campaign.py
```

A plain script, no pytest, matching `test_experiment_paths.py` in everything but its failure handling: this suite records every failure and reports them together at the end, exiting non-zero after the whole run rather than at the first one. Read the closing summary, not the first `FAIL` line — exiting early hid 79 of the 227 assertions behind one ambient-state failure, which is why the two suites differ here. It never touches a lab machine: the remote protocol runs against captured marker output, `collect` runs against throwaway checkouts, and the stage and queue paths run against temporary git repositories with `COUNTER_RUNNER_CMD` and `COUNTER_SCORER_CMD` pointed at stubs that record their arguments. `score_campaign.py` itself runs against a fake results tree, with `COUNTER_SCORE_CURVES_CMD` pointed at a stub scorer and `COUNTER_BIN_DIR` at two stub binaries answering `--version`. Anything added to the launch paths is testable the same way, and must be — the alternative is testing it on a machine with somebody's campaign on it.

`campaign.py` parses TOML itself rather than through `tomllib`, because `tick` runs on av2 and av3, whose python3 is 3.10.12 with neither `tomllib` (3.11) nor `tomli` installed. The subset it accepts is checked against `tomllib` on every fixture wherever `tomllib` exists, so the two cannot drift apart in what they accept. Remote shell scripts never contain a bare glob: the lab login shell is zsh, whose default `NOMATCH` aborts the script where a pattern matches nothing, and every section after the glob then vanishes in silence.
