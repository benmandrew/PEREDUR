# 2026-11-17-fretish-extension-share

Registered 2026-11-17, before launch. The part of the paper's new four-mechanism ablation that `2026-11-17-fretish-order-remove-no-f` does not run, on the CPUs that peredur-extension-d5 allocated to peredur-extension for the night of 17–18 November. The user approved the schedule in the peredur-extension session.

## Arms

Rebuilt at 22:30 on 17 Nov, after the user switched the paper's fourth mechanism from demotion to removal (see the Log). The first version ran `full4` (= `lean-demote`) and its `full4-no-<f>`; entries 082 and 079 were cancelled before any results were kept.

These use the arm names of `2026-11-17-fretish-order-remove-no-f`. `lean-remove` is the paper's `full` (climb, gate, blame and removal; its config sets demotion on as well). The three `remove-no-<f>` arms are that `full` without one of climb, gate or blame, and differ from it only in that feature's keys. Sweep D's `lean` is `full` without removal, so it is the paper's `no-remove`. `only-remove` is sweep O's `base` with demotion and removal on. Sweep D's `base` matches sweep O's `base` byte for byte. `only-climb`, `only-gate` and `only-blame` are sweep O's. On the seeded corpus, `full` is `fretish-seeded-removal`'s 600 `lean-remove` runs (seeds 0–4, at `612876c`), which this campaign does not rerun.

## Runs

| Phase | Profile | Subjects | Arms | Seeds | Host, CPUs, jobs | Runs |
|---|---|---|---|---|---|---|
| seeded-av2 | `fretish-seeded-remove` | 120 seeded | 3 × remove-no-f, lean, base | 0–2 | av2, 0–15, 8 | 1,800 |
| seeded-av3 | `fretish-seeded-remove` | 120 seeded | the same | 3–4 | av3, 16–31, 8 | 1,200 |
| rad-av2 | `fretish-order-large-remove` | rad | lean-remove, 3 × remove-no-f, lean, only-remove, 3 × only-f | 0–4 | av2, 0–15, 4 | 45 |
| rad-av3 | `fretish-order-large-remove` | rad | the same | 15–19 | av3, 16–31, 4 | 45 |
| lpc-night-av2 | `fretish-order-large-remove` | lift-plus-cruise-full | sweep D's six | 0 | av2, 0–9, 1 | 6 |

The counts come from `run_experiments.py --dry-run`. av3 has no night phase, because its share starts after fret-collab's 076–078 and the phase would still be running at about 08:30. Two more campaigns on this branch finish lift-plus-cruise-full:

- `2026-11-18-fretish-extension-lpc-2job`, enqueued at 06:00: sweep D at 2 jobs on av2 CPUs 0–19 (seeds 0–1, 6 runs after the night phase) and on av3 CPUs 8–27 (seed 15, 6 runs). veribee-ab keeps av2 24–31 and av3 0–7 until its release, and peredur-extension-55 holds av2 20–23 and av3 28–31.
- `2026-11-18-fretish-extension-lpc`, enqueued per host at its full release (av3 07:45, av2 09:00, or earlier on veribee-ab's message): every remaining arm of seeds 0–1 and 15–16 on CPUs 0–29 at 3 jobs, then stretch seeds 2 and 17. The user allowed the stretch seeds to run past 12:00 until all 9 arms finish. Everything else must be off both hosts by 12:00.

A phase that fails is resumed once. If it fails again, it is skipped and the loss is logged below.

Memory is capped at 80G per half-host and 110G per whole host, with no swap. Per-run peak memory has never been measured.

## Threads and pairing

- The seeded configs state `parallel = 2`. That is the thread count of the seeded campaign's sweep O arms, which ran at 20 CPUs over 8 jobs on `ben`. Seeded outcomes do not depend on load or host (`2026-11-13-seeded-corpus/PLAN.md`, 2026-11-16), so the seeds split by when each host becomes free. The five arms rerun under one load, so their wall-time contrasts pair within this campaign. `full`'s 600 runs ran in 076 on av3 CPUs 16–31 at 8 jobs and 2 threads a run (the runner writes `parallel = 2` from 16 CPUs over 8 jobs), 18:50–20:07 on 17 Nov, with veribee-33 on CPUs 0–15. seeded-av3 matches that host, CPU set, jobs and threads; seeded-av2 runs on the other host. Wall-time contrasts against `full` therefore pair only approximately, through the load on the other half of the host. Sweep P's `lean` ran at 2 jobs, so it has a different thread count.
- The large tree has no `parallel` line, which is how `fretish-order-large`'s `base` ran. The runner gives each run the phase's CPUs divided by its jobs: 4 on rad, as in base's 7 jobs over 32 CPUs, and 10 on lift-plus-cruise-full. A phase with one job sets nothing, and peredur reads its own 10-CPU affinity. The sweep O `only-<f>` and `base` configs match `configs-fretish-order` byte for byte. The sweep D configs match `configs-fretish-order-remove-no-f`'s except for its `parallel = 2` line, and the seeded tree's match it exactly.
- rad and lift-plus-cruise-full keep `base`'s seed-to-host split: seeds 0–14 on av2 and 15–29 on av3.
- Load is not matched for lift-plus-cruise-full. Base ran 3 jobs per host. The night phase runs 1 job beside veribee, and the whole-host phase runs 3. Compare its wall times with care.

## Queue

- av2: behind `2026-11-17-fretish-order-remove-no-f`'s entry 083.
- av3: behind that campaign's entry 080.
- Each host is enqueued with `--host`, and a phase with no seeds on a host is skipped there.

## Log

- 2026-11-17 18:14 UTC. av2 entry 082 queued behind fret-collab's 081 at `510412d`.
- 2026-11-17 19:10 UTC. av3 entry 079 queued behind fret-collab's 078 at `6ddf36d`, with at most 2 attempts per phase. The user, in the peredur-extension session, approved the av3 half and approved enqueuing both lift-plus-cruise-full campaigns overnight. Local timers enqueue `-lpc-2job` on both hosts at 06:00, `-lpc` on av3 at 07:45 and `-lpc` on av2 at 09:00. A veribee-ab release message brings a timer's step forward. A second enqueue of a queued campaign is refused, so the timers cannot double-queue.
- 2026-11-17 19:20 UTC. peredur-extension-d5 closed and handed the 12:00 check to this session. A local timer wakes at 11:45 on 18 Nov to check that every PEREDUR entry except the stretch seeds (2 on av2, 17 on av3) is off both hosts by 12:00.
- 2026-11-17 22:15 UTC. av2 entry 082 started (phase seeded-av2) after fret-collab's 081 finished at 22:11 with no failures. On av3, 077 finished at about 21:30 and 078 is running, so 079 should start at about 00:25.
- 2026-11-17 22:16 UTC. fret-collab-86 reported that the user, in its session at about 22:15, switched the paper's fourth mechanism from demotion to removal, so `full` is now `lean-remove`. The user confirmed in this session to rebuild the share on `lean-remove` and to let fret-collab's `remove-no-f` reruns go first. 082 (av2, about one minute into `seeded-av2`, 12 processes killed) and 079 (av3, not started) were cancelled. The 06:00, 07:45 and 09:00 timers were stopped. fret-collab queued 083 (av2) and 080 (av3) at 22:17, due to end at about 00:40 (range 23:30–01:50).
- 2026-11-17 22:20 UTC. The rebuilt share (`739edd4`, signed) was queued as av2 084 behind 083 and av3 081 behind 080, each with at most 2 attempts per phase. 083 started at 22:20. The 06:00, 07:45 and 09:00 timers were restarted, unchanged.
- 2026-11-18 01:05 UTC. fret-collab's 083 (av2) and 080 (av3) finished at 00:53 and 00:56, 1,140/1,140 runs each with no failures. Its `remove-full` reproduced the removal campaign's `lean-remove` on 569/570 cells (valu3s-uc6 seed 19, 68 → 69 repairs). av2 084 started at 01:00 (seeded-av2, estimated 3 h 19 min at 01:05) and av3 081 at 01:05 (seeded-av3).
