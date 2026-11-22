# 2026-11-20-psp-order

Declared on 2026-11-20 (PSP front-end plan, phase P8). Not launched. The only step left is to enqueue it after the user's go-ahead (see "Launch").

## What it measures

The order-directed mechanisms on PSP, as `2026-11-20-final-engine` measures them on FRETISH. The arms are sweep F's `base`, `full`, `no-climb`, `no-gate` (two-way mutation in exchange mode), `no-remove` (maximality removal) and `no-novelty`. Blame is on in every arm. Every arm runs under `nsga2-apportion` at the final engine's budget: 1,000 individuals, 500 generations, population 100, weights 0.1 0.2 0.7, weakening off, 2 threads a run, a cap of 7,200 s a run and 1,800 s for compare.

The subjects come from the registries in `scripts/run_experiments.py`, not from a list here:

- Profile `psp-order`: `PSP_SPECS` (the 5 hand-written subjects, which have fixes, so a known repair) and `PSP_SYNTECH_EVAL` (the unrealisable SYNTECH17 imports with 2 or more weakenable guarantee slots, which have no fix; PSP plan P7 drops the rest, and `PSP_SYNTECH_SPECS` keeps all 9), at seeds 0-29. On `feat/psp-front-end` that is 6 subjects (5 hand-written, 1 SYNTECH17), 1,080 runs.
- Profile `psp-order-seeded`: `PSP_SEEDED_EVAL` (the entries of `examples/psp-seeded/selected.txt`, with its base as the one ideal, less the 7 built on `syntech-Elevator_cmp_Elevator_94`), at seeds 0-4. That is 63 subjects (39 from the hand-written bases, 24 from the SYNTECH17 bases), 1,890 runs. The corpus itself keeps 70 subjects; see "Elevator_94 dropped" below.

The counts are read from the registries whenever the runner plans, so a subject added to a registry before enqueue joins the campaign. Check them with `python3 scripts/run_experiments.py --profile psp-order --dry-run` and `--profile psp-order-seeded --dry-run`.

Seeds are split over the hosts as the final engine splits them, so every arm of a (subject, seed) pair runs on one host: the specifications at seeds 0-14 on av2 and 15-29 on av3, the seeded corpus at seeds 0-2 on av2 and 3-4 on av3. Each phase runs 15 jobs on CPUs 0-29 under 48 GB. CPUs 30-31 stay free for the Ollama server. Set the windows against whatever else is running at enqueue (`campaign.toml` says how).

## Smoke runs (2026-11-20, local, at b5e93006)

`realize` on every subject of both profiles (53 on this branch), one at a time under the 7,200 s cap: all 53 are unrealisable. The slowest took 18.3 s (`syntech-AirportShuttleProject_AirportShuttle1_AirportShuttle_176`), and 51 took under 2 s. The 31 SYNTECH-based seeded mutants merged later were each checked unrealisable by `seed.py` under its 300 s timeout.

One run of each arm on `psp-crossing` at seed 0, through `run_experiments.py --profile psp-order --specs psp-crossing --seeds 0 --jobs 2`: every arm found repairs (21 to 73), in 33-44 s of wall time, and only `no-gate` found one that implies a fix. The output is in `experiments/results-psp-order.csv` and `experiments/results-psp-order/` of the P8 worktree, where the paper's `scripts/import_data.py --psp` reads it. One run of each arm on the seeded subjects `psp-crossing__m10` and `psp-arbiter__m0` at seed 0 (`--profile psp-order-seeded`) is in `results-psp-order-seeded*` there: all 12 runs found repairs, in 34-69 s, and `full` and `no-climb` found the base on `psp-arbiter__m0`.

At about 60 s a run, each host's specifications phase is about 9 slot-hours, which is about 0.6 h at 15 jobs. The SYNTECH17 import has not run beyond `realize`, so this estimate is uncertain by about 50%.

## Trivial-reversion probe

`scripts/probe.py` (a port of the FRETISH corpus's probe) estimates, for each seeded mutant O with base B, the share of chains of one (p1) and two (p2) undirected mutations from O that give a realisable specification that implies B. Such a draw is a trivial reversion: blind mutation repairs O without the search. A subject is headline when p1 and p2 are both below 0.01; the others are trivial. The paper reports the ideal rate over the headline subjects beside the rate over all of them.

The probe runs locally, not through the queue, after `feat/psp-front-end` is merged (so it sees every mutant in `selected.txt`) and before the analysis. It uses `sample-mutants`, `compare` and `realize` from `build-release` at the campaign's commit, under the `base` arm's config, so first generate the configs with the `configs` command of `campaign.toml` (they are untracked):

```
systemd-run --user --scope -q -p MemoryMax=7G -p MemorySwapMax=0 nice -n 10 nix develop -c python3 experiments/2026-11-20-psp-order/scripts/probe.py --jobs 4
```

It writes `experiments/2026-11-20-psp-order/probe.csv` (columns subject, distinct, implies_base, implies_mutant, reversions, p1, p2) and resumes from `probe-work/`. A smoke run at 500 draws on two subjects gave p1 = 0.002 and p2 = 0 on `psp-arbiter__m0`, and p1 = p2 = 0.002 on `psp-crossing__m10`. The full probe ran locally on 2026-11-20 at 7f356f56's binaries (5,000 draws, 4 jobs): 66 of the 70 subjects are headline (35 of the 39 from hand-written bases, all 31 from SYNTECH17 bases). The trivial ones are `psp-arbiter__m24` (p1 = 0.0106), `psp-minepump__m33` (0.0308), `psp-patrol__m57` (0.0124) and `psp-patrol__m64` (p1 = 0.0100). `probe.csv` is untracked, as all experiment data is, so it lives in `~/projects/peredur-psp`.

## Launch

Before enqueue, point the local branch `campaign/psp-order` at the commit to run (the merge of this branch and `feat/psp-front-end`), since `enqueue` pushes that branch to both hosts. Then, after the user's go-ahead, and not before:

```
python3 scripts/campaign.py enqueue 2026-11-20-psp-order
```

This queues one entry on each of av2 and av3.

## Rerun of the seeded phases (2026-11-21)

The first launch, at 392a5539 (av2 103, av3 101), ran the specification phases to completion on both hosts, and their rows stand. On av3 the seeded phase was OOM-killed twice at its 28G cap (18:44Z and 18:59Z) and was dequeued. The cause was in scoring, not in the search: `compare` buffered the output of `ltlfilt --remove-wm` with no bound, and on the no-twoway arm's repair 61 of `psp-cruise__m81` at seed 3 (a `bounded-existence` guarantee with `count: 16` under `before`) that output doubles with each level of W nesting, 739 MB in 8 s. 42a10e03 (merged at b7376a94) caps that one capture at 4 MiB and reports the rewrite as unresolved, so the pair reads `?`. Every other capture is unchanged, and on the 65 other repairs of that run the old and new `compare` print identical verdicts.

av2 103 was dequeued once its specification phase finished, before its seeded phase began. The 233 seeded rows that av3 wrote at 392a5539 were set aside on av3 as `results-psp-order-seeded-392a553.csv` and `results-psp-order-seeded-392a553/`, so every seeded row comes from the fixed commit. The campaign was enqueued again at the fixed commit. Its specification phases find every run done and run nothing, since the resume key carries no commit. The specification CSV is therefore entirely at 392a5539, and the seeded CSV is entirely at the later commit.

## After the runs

1. `python3 scripts/campaign.py collect --profile psp-order --host av2`, then `--host av3`, and likewise `--profile psp-order-seeded`.
2. Run the probe (above), if it has not run.
3. Locally, one job at a time under a memory cap: `python3 experiments/2026-11-20-psp-order-pooled/scripts/make_design.py`, then `python3 scripts/pool_members.py experiments/2026-11-20-psp-order-pooled/design.toml experiments/2026-11-20-psp-order-pooled/members --root .`. Rerun `make_design.py --drop ...` for each subject where an arm holds 3 or fewer members (a SYNTECH17 import where no arm finds a repair holds none), and `--sampled ...` for any pool too large to compare in full, then pool again. Record the SHA-256 sums of the inputs in `2026-11-20-psp-order-pooled/`.
4. `rsync -a` the pooled campaign's `design.toml`, `subjects-*.txt` and `members/` to both hosts' checkouts. After the user's go-ahead: `python3 scripts/campaign.py enqueue 2026-11-20-psp-order-pooled`.
5. `python3 scripts/campaign.py collect --outputs psp-order-pooled-reduced --host av2`.
6. In the paper (`scripts/import_data.py --psp <this checkout>`, then `scripts/summarise.py`), the results reach `data/stats.json` under `psp-*` keys.

## Elevator_94 dropped (2026-11-22)

The 7 seeded subjects built on `syntech-Elevator_cmp_Elevator_94` hit the 7,200 s run cap: of their first 28 runs (seed 0 on av2, seed 3 on av3, at ed84849d), 27 timed out and 1 (`no-remove` on `__m653`) found a repair at 7,090 s. Every other seeded subject takes about a minute a run (mean 59 s over 468 runs). At 7 jobs, the Elevator runs alone would have kept av2 busy until about Mon 23 Nov 13:00Z, past the slot that ends at 06:00Z. With the user's agreement (22 Nov), `PSP_SEEDED_EVAL` leaves them out, and the campaign was enqueued again at the commit that drops them. Their rows already in `results-psp-order-seeded.csv` stay there. The pooled design reads the profile's subject list, so it leaves them out, and the analysis reports Elevator_94 as out of budget from those rows.
