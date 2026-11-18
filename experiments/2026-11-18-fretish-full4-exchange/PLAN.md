# 2026-11-18-fretish-full4-exchange

Registered 2026-11-17, before launch. The paper's full (sweep D's `lean-remove`: climb, gate, blame and maximality removal) with the gate's exchange mode for realisable parents (`gate_realizable = "exchange"`). The arm runs on the small set and on the seeded corpus's headline subjects, and `remove-no-blame` gets the same mode on the seeded subjects. The 2026-11-17 smoke test (`2026-11-17-fretish-gate-smoke`) suggested exchange widens the spread of candidates. This campaign measures it against the paper's full at full seed counts. The directory keeps its first name, `fretish-full4-exchange`, from when the paper's full was `full4` (lean-demote), because the installed timer unit names the driver's path in it. The user approved the plan and the timer launch in the peredur-extension session on 17 Nov.

## Arms

- `remove-exchange`: sweep D's `lean-remove` with `gate_realizable = "exchange"`. Required.
- `remove-no-blame-exchange`: sweep D's `remove-no-blame`, as `580a6cc7` defines it, with `gate_realizable = "exchange"`. Optional, seeded corpus only.

The configs come from the generator line of the removal campaigns (sweep D, `--parallel 2`, the order campaign's flags, `--pin-vintage`), in `configs-fretish-remove-exchange`. Regenerated, `remove-exchange` differs from `lean-remove` only by the line `gate_realizable = "exchange"`, and `remove-no-blame-exchange` from `remove-no-blame` likewise. The configs state `parallel = 2`, as the controls' do. Here each run is pinned to one CPU, so its two threads share that CPU.

## Controls

- Small set: `remove-full`, the rerun of `lean-remove` in `2026-11-17-fretish-order-remove-no-f`, seeds 0-29.
- Seeded, against `remove-exchange`: `lean-remove` from fret-collab's seeded removal run (`results-fretish-seeded-removal.csv` in `~/projects/peredur-wt-demote`, commit `612876c`, seeds 0-4).
- Seeded, against `remove-no-blame-exchange`: `remove-no-blame` from peredur-extension-26's profile `fretish-seeded-remove` (commit `739edd4` on `campaign/fretish-extension-share`, all 120 subjects, seeds 0-4). Its config is byte-identical to the one in `configs-fretish-order-remove-no-f`.

The outcomes pair across these commits, because `gate_realizable = "towards"` keeps the old path byte for byte (the determinism tests check this). The wall times do not pair, since load, host and threads per CPU differ. Both arms are kept.

## Runs

| Block | Profile | Subjects | Seeds, av2 | Seeds, av3 | Runs |
|---|---|---|---|---|---|
| required | `fretish-remove-exchange` | small set (9) | 0-14 | 15-29 | 270 |
| required | `fretish-remove-exchange-seeded` | seeded headline (110) | 0-2 | 3-4 | 550 |
| optional | `fretish-remove-no-blame-exchange-seeded` | seeded headline (110) | 0-2 | 3-4 | 550 |

The small set splits by seed as `2026-11-17-fretish-order-remove-no-f` did. The seeded corpus splits as `2026-11-17-fretish-extension-share` split it (seeds 0-2 on av2, 3-4 on av3). Each host takes its items in the order of the table, seed-major within a block, so a partial block covers whole seeds. The headline subjects are those in both `selected.txt` and the headline stratum of `subjects.csv`. Those files are untracked, so they were copied into each host's checkout with `rsync -a`.

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

## Log

Times are UTC on 2026-11-18.

- 06:00:00. Both timers fired. The release build started on av2 CPUs 20-23 and av3 CPUs 28-31.
- 06:08:03 (av3) and 06:09:16 (av2). Build and configs done. Work lists: av2 795 items (465 required, 330 optional), av3 575 items (355 required, 220 optional). Four lanes started on each host.
- 06:20. 21 items done on av2 and 22 on av3, with no failures.
- 06:00. veribee-ab released av2 CPUs 16-23 and av3 CPUs 8-15 and raised its own memory cap to 20G per host. This run's CPUs and 8G cap are unchanged.
- 07:31 and 07:39 (av3). CPUs 29 and 28 stopped at their 07:30 cutoff, as planned. Both hosts finished the small set (135 runs each) by 07:42.
- 08:30 (av2). CPUs 20-23 stopped at their cutoff, as planned, with 280 of 465 required items done. CPUs 30-31 took over at 09:00.
- 10:40:52 (av2). Required block done. CPUs 30-31 moved on to the optional block.
- 10:49 (av3). Finished: all 575 items done (355 required, 220 optional), none untaken.
- 11:59:30 (av2). Hard stop. The driver killed one running optional item (`rad__s22-23-26-51-52__m07`, seed 1), which has no row and is excluded. av2 finished 670 items (465 required, 205 optional: seed 0 complete, seed 1 95 of 110, seed 2 not started). Both units are inactive.
- Totals: 1245 items done, no failures, no retries, no skips. Mean time per item 83 s on av2 and 77 s on av3.

## Result

See `REPORT.md`. The merged arm results are in `data/`, the analysis scripts are in `scripts/`, and their output is in `analysis/`, untracked.
