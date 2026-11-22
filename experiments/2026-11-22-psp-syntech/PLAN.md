# 2026-11-22-psp-syntech

Declared on 2026-11-22. Not launched. The only step left is to enqueue it after the user's go-ahead (see "Launch").

## Why

`2026-11-20-psp-order` ran the SYNTECH17 subjects from an import that made every constraint outside the three library patterns a fixed entry, so only 7% of the slots of the unrealisable imports were weakenable, and that kept tautologies. A valid guarantee makes the vacuity filter (`psp::specification_is_vacuous`) discard every offspring that keeps it, so the runs scored only their input: 0 of 180 runs repaired `SimpleVehicle_VehicleSpec_31`, and 0 of 96 repaired the seeded subjects built on `SimpleVehicle_VehicleSpec_24`. `scripts/import_spectra.py` now lifts each state-only safety and justice constraint to the pattern it equals and drops valid requirements (`docs/dev/psp.md`, "SYNTECH17"). This campaign measures the mechanisms again on the SYNTECH17 subjects only. The hand-written subjects keep their `psp-order` rows.

## What it measures

The arms, budget and configs are `2026-11-20-psp-order`'s: sweep F's `base`, `full`, `no-climb`, `no-gate` (two-way mutation in exchange mode), `no-remove` (maximality removal) and `no-novelty`, blame on in every arm, under `nsga2-apportion` at 1,000 individuals, 500 generations, population 100, weights 0.1 0.2 0.7, weakening off, 2 threads a run and 1,800 s for compare. The configs are generated into `experiments/configs-psp-syntech`. The cap is 1,800 s a run (`PSP_SYNTECH_CAP`), not 7,200 s: the subjects here finish in 1.5 to 13 minutes or not in hours (see "Smoke runs").

The subjects come from the registries in `scripts/run_experiments.py`:

- Profile `psp-syntech`: `PSP_SYNTECH_EVAL` (the unrealisable lifted imports with 2 or more weakenable guarantee slots, all 9) less `PSP_SYNTECH_OUT_OF_BUDGET`, at seeds 0-9. That is 5 subjects: SimpleVehicle 31, 78, 79 and 134 and AirportShuttle 177. None has a known repair. Results go to `results-psp-syntech/` and `results-psp-syntech.csv`. The name `syntech-SimpleVehicle_VehicleSpec_31` repeats a `psp-order` subject, but the file is the lifted import, and the two campaigns write separate CSVs.
- Profile `psp-syntech-seeded`: `PSP_SEEDED_SYNTECH` (the `syntech-lift-` entries of `examples/psp-seeded/selected.txt`, with the base as the one ideal) less those on `PSP_SEEDED_SYNTECH_OUT_OF_BUDGET_BASES`, at seeds 0-1. That is 49 subjects from 13 bases. Results go to `results-psp-syntech-seeded/` and `results-psp-syntech-seeded.csv`.

Out of budget, from the smoke runs, and left in the registries so the analysis can report them:

- `AirportShuttle_182`, `AirportShuttle_183` and `AirportShuttle_176`: both arms reached the 1,200 s smoke cap. On 183 the run did not finish generation 0, since each realisability query costs seconds (`realize` 5.7 s on the whole specification). `AirportShuttle_177`, from the same project, took 65 s, so the cost does not follow the project.
- `Elevator_cmp_Elevator_88`: both arms reached the 1,200 s cap. A run that times out writes no repair, so a longer cap is the only way to score it, at about an hour a run.
- The 6 seeded subjects on `roboticarm_Robot_82` and `roboticarm_Robot_87`: both arms on `Robot_87__m3324` reached the 1,200 s cap, and `compare` timed out at 3,600 s in the probe on 3 of the 6.

## Smoke runs (2026-11-22, local, at d2ec7828's binaries)

One seed (0), arms `base` and `full`, cap 1,200 s, 2 threads a run, under the memory cap, through `run_experiments.py` with a scratch profile. Wall time a run, `base` / `full`:

| Subject | base | full |
|---|---|---|
| `syntech-SimpleVehicle_VehicleSpec_31` | 543 s (40 repairs) | 790 s (46) |
| `syntech-SimpleVehicle_VehicleSpec_79` | 532 s (26) | 518 s (46) |
| `syntech-AirportShuttle_177` | 65 s (0 repairs) | 65 s (0) |
| `syntech-AirportShuttle_182` | cap | cap |
| `syntech-AirportShuttle_176` | cap | cap |
| `syntech-Elevator_cmp_Elevator_88` | cap | cap |
| `syntech-AirportShuttle_183` | cap | cap |
| `syntech-lift-SimpleVehicle_VehicleSpec_21__m161` | 86 s | 84 s |
| `syntech-lift-AirportShuttle_AirportShuttle_49__m0` | 99 s | 96 s |
| `syntech-lift-ParkingModule_ParkingSpec_168__m246` | 180 s | 138 s |
| `syntech-lift-SimpleVehicle_VehicleSpec_24__m1137` | 207 s | 240 s |
| `syntech-lift-SimpleVehicle_VehicleSpec_33__m149` | 561 s | 595 s |
| `syntech-lift-roboticarm_Robot_87__m3324` | cap | cap |

The four specification runs ran 3 at a time on CPUs 0-5, AirportShuttle 177 and 182 4 at a time on CPUs 0-7, AirportShuttle 176 2 at a time on CPUs 0-3, the seeded ones 6 at a time on CPUs 0-11. Every finished run found repairs except on AirportShuttle 177, where both arms used the 1,000 individuals by generation 41 and none was realisable. It stays in the campaign: it is cheap, and a mechanism may change that. The SimpleVehicle runs show that the lifted imports can be repaired, where the earlier ones could not.

## Size

Run time grows with the number of weakenable guarantee slots, since each score walks subsets of them. The estimate takes each subject's time from the smoke run of its family:

- Specifications: about 600 s a run on the 4 SimpleVehicle subjects (the mean of the 4 SimpleVehicle smoke runs; 78 and 134 were not run) and 65 s on AirportShuttle 177, so 6 arms make about 4.1 slot-h a seed.
- Seeded: by base, 580 s for the bases with 15 weakenable guarantees (SimpleVehicle 33, 39 and 52; 12 subjects), 230 s for those with 9 to 11 (SimpleVehicle 22, 24, 56, 63 and 101, SimpleVehicle2 103; 24 subjects), 120 s for SimpleVehicle 21 and 70 (8 subjects), 160 s for ParkingModule 168 (4) and 100 s for AirportShuttle 49 (1). That is about 14,200 s a subject over one arm, so 6 arms make about 23.4 slot-h a seed.

The hosts ran the earlier PSP campaign about 1.3 times slower than the local smoke runs (psp-order seeded: 59 s a run on the hosts, 34-69 s locally), so each figure is scaled by 1.3. At 7 jobs (CPUs 0-13), each host runs:

- 5 specification seeds: 5 x 4.1 x 1.3 = 26.7 slot-h;
- 1 seeded seed: 23.4 x 1.3 = 30.4 slot-h;

about 57 slot-h, or 8.2 h at 7 jobs. The figure rests on one seed of two arms per family, so it is uncertain by about 30%; the `no-*` arms may differ from `base` and `full`. If the hosts free CPUs 14-29 before enqueue, widen the windows and jobs (`campaign.toml`) rather than adding seeds, and the phases then take about half as long.

## Trivial-reversion probe

`2026-11-20-psp-order/scripts/probe.py` ran locally on 2026-11-22 on the 55 new seeded subjects only, at 5,000 draws, 12 jobs, with this campaign's `base` config:

```
systemd-run --user --scope -q -p MemoryMax=14G -p MemorySwapMax=0 nice -n 10 python3 -I experiments/2026-11-20-psp-order/scripts/probe.py --jobs 12 --out experiments/2026-11-22-psp-syntech --tag=-syntech-lift --config experiments/configs-psp-syntech/nsga2-apportion/wkoff/log/sweep_F_base.toml --subjects <the syntech-lift- entries of selected.txt>
```

It writes `probe-syntech-lift.csv` here (untracked) and `probe-work-syntech-lift/`. 52 of the 55 subjects were probed, and 48 are headline (p1 and p2 below 0.01). The trivial ones are the three probed `ParkingModule_ParkingSpec_168` subjects (`__m246`, `__m264`, `__m77`; p1 0.12-0.14, p2 0.15-0.17) and `SimpleVehicle_VehicleSpec_101__m1149` (p1 = p2 = 0.017). `compare` reached the probe's 3,600 s limit on `roboticarm_Robot_82__m5778`, `roboticarm_Robot_87__m3499` and `roboticarm_Robot_87__m5614`, which then raised in `probe.py`, so their rows are missing and the CSV was written from the finished subjects' `row.json`. All three are on out-of-budget bases.

## Launch

Before enqueue, point the local branch `campaign/psp-syntech` at the commit to run (this branch merged into `feat/psp-front-end`), since `enqueue` pushes that branch to both hosts. Then, after the user's go-ahead, and not before:

```
python3 scripts/campaign.py enqueue 2026-11-22-psp-syntech
```

This queues one entry on each of av2 and av3. Check the subject counts first with `python3 scripts/run_experiments.py --profile psp-syntech --dry-run` and `--profile psp-syntech-seeded --dry-run`.

## After the runs

1. `python3 scripts/campaign.py collect --profile psp-syntech --host av2`, then `--host av3`, and likewise `--profile psp-syntech-seeded`. Then add the result sets to `experiments/catalogue.toml` (this campaign, its pooled pass and `configs-psp-syntech` are not yet catalogued, as `psp-order` is not) and run `scripts/catalogue.py render`.
2. The pooled pass is `2026-11-22-psp-syntech-pooled`, built as `2026-11-20-psp-order/PLAN.md` step 3 describes, with `make_design.py --profiles psp-syntech psp-syntech-seeded --out experiments/2026-11-22-psp-syntech-pooled`. It holds two groups, one per profile, with every pair of the six arms planned. Drop a subject where an arm holds 3 or fewer members.
3. In the paper, the SYNTECH17 rows come from this campaign, and the hand-written rows from `psp-order`. The trivial subjects of the probe are reported apart, as on FRETISH.
