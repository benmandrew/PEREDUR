# 2026-11-24-one-way

The peredur-extension paper drops two-way mutation (user, 2026-11-24): in a local test on 7 subjects × 5 seeds, `no-gate`'s pooled repairs were stronger than `full`'s on all 7, and no repair to the exchange beat `no-gate`. The paper's full search becomes sweep F's `no-gate`, and its leave-one-out arms must be `no-gate` with one more feature off. base and the `only-<f>` of the other three features already have the gate off, so they stay as sweep F ran them.

## Engine

Sweep T of `scripts/gen_configs.py`: `full` is sweep F's `no-gate` byte for byte (comments aside), and `no-climb`, `no-remove` and `no-novelty` differ from it in that feature's keys alone. Every arm has `direction_gated = false` and `bisect_steps = false`.

The branch head is e127243c, past sweep F's 8948138. The only source change between them is 3f13f457 (a clang-tidy split of `breed_offspring` and loop renames in nsga2, with no change to draws). Four sweep F `no-gate` runs (fsm, fsm-timing, mode-arbiter and takeoff, seed 3), rerun at e127243c, gave byte-identical `repair_*.json` and accumulated sets; only timings and the climb's memo-hit counts differ.

## Arms and phases

| Campaign | Phase | Subjects | Arms | Seeds av2 / av3 | Jobs × threads | Slot-h (both hosts) |
|---|---|---|---|---|---|---|
| one-way | small | 9 specs + 10 rad cores | no-climb, no-remove, no-novelty | 0–14 / 15–29 | 7 × 2 on 0–13 / 15 × 2 on 0–29 | about 90 |
| one-way | rad | rad | the same 3 | 0–4 / 15–19 | 3 × 4 / 7 × 4 | about 6 |
| one-way | seeded | 130 sideways mutants | the same 3 | 4 / 0–3 | 7 × 2 / 15 × 2 | about 175 |
| one-way (av3), one-way-lpc (av2) | lpc | lift-plus-cruise-full | full | 0–4 / 15–19 | 3 × 10 | 100–170 |

Estimates scale each subject's sweep F `no-<f>` wall time by its `no-gate`/`full` ratio (1.9 on the specifications, 2.5 on the seeded corpus), times threads a run. lift-plus-cruise-full has no `no-gate` run, so its estimate takes a ratio of 1.5–2.5. `no-climb` is likely overstated, since the extra time without the gate is spent mostly in the climb.

Hosts (mailbox, 2026-11-24 17:19Z): av3 is free on CPUs 0-29. On av2, veribee-v0split holds CPUs 14-29 until about 23:30Z, so `2026-11-24-one-way` runs on CPUs 0-13 there and `2026-11-24-one-way-lpc` is enqueued after "released av2". CPUs 30-31 stay free for Ollama on both hosts.

## After the runs

Collect the four profiles, then rerun both pooled passes on 8 nsga2 arms: base, full (sweep F `no-gate`, or sweep T `full` on lift-plus-cruise-full), the three sweep T `no-<f>` and sweep F's `only-climb`, `only-remove` and `only-novelty`.

## Log

- 2026-11-24: prepared and launched at e127243c+ (this commit).
- 2026-11-25 01:34Z: av3 107 done; av2 109 done at 22:30Z on 24 Nov. The lpc entry was not enqueued on "released av2" (no watcher); enqueued at 12:27Z as av2 110, killed at 12:36Z when the lab hosts restarted, and rerun by the tick from 12:40Z (try 2 of 3).
- 2026-11-25 13:15Z: collected one-way (1,710 rows), one-way-large (30) and one-way-seeded (1,950); every key once.
- 2026-11-25 14:40Z: the two 8-arm pooled passes, `2026-11-25-one-way-pooled` and `2026-11-25-one-way-seeded-pooled`, written by `experiments/2026-11-25-one-way-pooled/scripts/make_design.py` (full from sweep F's `no-gate` and each no-<f> from sweep T, through `[groups.sources]`), on CPUs 0-29 of both hosts. Pooled locally under MemoryMax=20G: specifications 18 subjects, 244,777 nodes (the ten-arm pool had 235,515), host split and sampled subjects as before; seeded 126 subjects, 145,461 nodes (largest 4,571), after dropping the same 4 fsm-lmcps mutants, where every arm holds 0 members. `make_design.py random` also writes `2026-11-25-one-way-pooled-random` (random/base against base on this pass's words), not yet a campaign. Input sums are in each campaign's INPUTS.sha256 (untracked, with the inputs).
