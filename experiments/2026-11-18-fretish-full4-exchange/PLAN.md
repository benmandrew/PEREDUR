# 2026-11-18-fretish-full4-exchange

Registered 2026-11-17, before launch. The paper's four-mechanism full (`full4`: climb, gate, blame and demote) with the gate's exchange mode for realisable parents (`gate_realizable = "exchange"`). The arm runs on the small set and on the seeded corpus's headline subjects, and `full4-no-blame` gets the same mode on the seeded subjects. The 2026-11-17 smoke test (`2026-11-17-fretish-gate-smoke`) suggested exchange widens the spread of candidates. This campaign measures it against `full4` at full seed counts. The user approved the plan and the timer launch in the peredur-extension session on 17 Nov.

## Arms

- `full4-exchange`: sweep D's `full4` (= `lean-demote`) with `gate_realizable = "exchange"`. Required.
- `full4-no-blame-exchange`: sweep D's `full4-no-blame` with `gate_realizable = "exchange"`. Optional, seeded corpus only.

The configs come from the generator line of `2026-11-17-fretish-order-full4-no-f` and `2026-11-17-fretish-extension-share` (sweep D, `--parallel 2`, the order campaign's flags, `--pin-vintage`), in `configs-fretish-full4-exchange`. Regenerated, `full4-exchange` differs from `full4` only by the line `gate_realizable = "exchange"`, and `full4-no-blame-exchange` from `full4-no-blame` likewise. The configs state `parallel = 2`, as full4's do. Here each run is pinned to one CPU, so its two threads share that CPU, and wall times do not compare with full4's.

## Runs

| Block | Profile | Subjects | Seeds, av2 | Seeds, av3 | Runs |
|---|---|---|---|---|---|
| required | `fretish-full4-exchange` | small set (9) | 0-14 | 15-29 | 270 |
| required | `fretish-full4-exchange-seeded` | seeded headline (110) | 0-2 | 3-4 | 550 |
| optional | `fretish-full4-no-blame-exchange-seeded` | seeded headline (110) | 0-2 | 3-4 | 550 |

The small set splits by seed as `full4` did in `2026-11-17-fretish-order-full4-no-f`. The seeded corpus splits as `2026-11-17-fretish-extension-share` split it (seeds 0-2 on av2, 3-4 on av3). Each host takes its items in the order of the table, seed-major within a block, so a partial block covers whole seeds. The headline subjects are those in both `selected.txt` and the headline stratum of `subjects.csv`. Those files are untracked, so they were copied into each host's checkout with `rsync -a`.

The smoke test's exchange runs took about 126 s on the small set and 69 s on the seeded corpus. Capacity is about 16 CPU-hours on av2 and 14.5 on av3, against about 11 and 9 CPU-hours of required work. The required blocks should therefore finish on both hosts, and the optional block will only partly finish. Whatever is not done at the hard stop is lost.

## Hosts

The CPU allotment was set by peredur-extension-d5 for the morning of Wed 18 Nov. lift-plus-cruise-full has priority over this campaign. Every time is UTC.

- av2: CPUs 20-23 from 06:00, taking no new run after 08:30. CPUs 30-31 from 09:00 to 12:00.
- av3: CPUs 28-31 from 06:00. CPUs 28-29 take no new run after 07:30, and CPUs 30-31 continue to 12:00.
- Nothing of ours runs after 12:00. The driver kills every running item at 11:59:30, and the unit's `RuntimeMaxSec` stops it at 11:59:40 at the latest.
- The optional block runs only on CPUs 30-31, after the host's required items are taken.
- Memory is capped at 8G per host, with no swap, for the whole unit (build, runner, engines).
- The campaign queue is not used, since `peredur-extension-26`'s entry holds it. `~/projects/counter` is not touched on either host.

## Driver

`scripts/driver.py`, with the per-host lanes in `scripts/schedule.py`, runs in `~/projects/peredur-full4-exchange` on each host. A systemd user timer that `scripts/install.sh` installs starts it at 06:00. It first builds `build-release` on the 06:00 CPUs (`taskset -c`, 4 jobs) and generates the configs. It then runs one lane per CPU. A lane waits for its start time, takes the next item it may take, and runs `run_experiments.py --profile P --specs S --seeds N --jobs 1` under `taskset -c <cpu>`. It takes no new item after its cutoff. A run counts as done when the profile's CSV holds its row. A failed run is run once more (the runner resumes), and a second failure skips it. Every event is one line, with a UTC time, in `experiments/2026-11-18-fretish-full4-exchange/run-<host>/status.log`. The events are `start`, `build-start`, `build-done`, `configs-done`, `work-list`, `lane-start`, `item-start`, `item-done`, `item-failed`, `item-skipped`, `item-killed`, `lane-stopped`, `finished` and `hard-stopped`. Each attempt's output is in `run-<host>/logs/`.

## Result

Not run yet.
