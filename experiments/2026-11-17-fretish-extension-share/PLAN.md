# 2026-11-17-fretish-extension-share

Registered 2026-11-17, before launch. The part of the paper's new four-mechanism ablation that `2026-11-17-fretish-order-full4-no-f` does not run, on the CPUs that peredur-extension-d5 allocated to peredur-extension for the night of 17–18 November. The user approved the schedule in the peredur-extension session.

## Arms

These use the arm names of `2026-11-17-fretish-order-full4-no-f`. `full4` is the paper's `full` (climb, gate, blame and demote, the same as sweep D's `lean-demote`). The three `full4-no-<f>` arms are that `full` without one of its other features. Sweep D's `lean` is `full` without demote, so it is the paper's `no-demote`. Sweep D's `base` matches sweep O's `base` byte for byte. `only-<f>` is sweep O's, and the cut features are off in those arms.

## Runs

| Phase | Profile | Subjects | Arms | Seeds | Host, CPUs, jobs | Runs |
|---|---|---|---|---|---|---|
| seeded-av2 | `fretish-seeded-full4` | 120 seeded | full4, 3 × full4-no-f, lean, base | 0–2 | av2, 0–15, 8 | 2,160 |
| seeded-av3 | `fretish-seeded-full4` | 120 seeded | the same | 3–4 | av3, 16–31, 8 | 1,440 |
| rad-av2 | `fretish-order-large-full4` | rad | full4, 3 × full4-no-f, lean, 4 × only-f | 0–4 | av2, 0–15, 4 | 45 |
| rad-av3 | `fretish-order-large-full4` | rad | the same | 15–19 | av3, 16–31, 4 | 45 |
| lpc-night-av2 | `fretish-order-large-full4` | lift-plus-cruise-full | sweep D's five | 0 | av2, 0–9, 1 | 5 |
| lpc-night-av3 | `fretish-order-large-full4` | lift-plus-cruise-full | sweep D's five | 15 | av3, 16–25, 1 | 5 |

The counts come from `run_experiments.py --dry-run`. `2026-11-18-fretish-extension-lpc` adds the rest of lift-plus-cruise-full on whole hosts from 06:00, once veribee-ab releases its halves: seeds 0–1 on av2 and 15–16 on av3 (13 runs per host after the night phase), then one stretch seed per host (2 and 17), on CPUs 0–29 at 3 jobs. If a stretch seed has not finished all 9 arms by 12:00, it is dequeued and left out of the contrasts.

Memory is capped at 80G per half-host and 110G per whole host, with no swap. Per-run peak memory has never been measured.

## Threads and pairing

- The seeded configs state `parallel = 2`. That is the thread count of the seeded campaign's sweep O arms, which ran at 20 CPUs over 8 jobs on `ben`. Seeded outcomes do not depend on load or host (`2026-11-13-seeded-corpus/PLAN.md`, 2026-11-16), so the seeds split by when each host becomes free. All six arms rerun under one load, so the wall-time contrasts pair within this campaign. Sweep P's `lean` ran at 2 jobs, so it has a different thread count.
- The large tree has no `parallel` line, which is how `fretish-order-large`'s `base` ran. The runner gives each run the phase's CPUs divided by its jobs: 4 on rad, as in base's 7 jobs over 32 CPUs, and 10 on lift-plus-cruise-full. A phase with one job sets nothing, and peredur reads its own 10-CPU affinity. The sweep O `only-<f>` and `base` configs match `configs-fretish-order` byte for byte.
- rad and lift-plus-cruise-full keep `base`'s seed-to-host split: seeds 0–14 on av2 and 15–29 on av3.
- Load is not matched for lift-plus-cruise-full. Base ran 3 jobs per host. The night phase runs 1 job beside veribee, and the whole-host phase runs 3. Compare its wall times with care.

## Queue

- av2: behind `2026-11-17-fretish-order-full4-no-f`'s entry 081.
- av3: behind that campaign's av3 entries.
- Each host is enqueued with `--host`, and a phase with no seeds on a host is skipped there.
