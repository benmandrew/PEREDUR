# 2026-11-17-fretish-gate-smoke

Two mechanisms of the full arm pull a realisable parent back towards the original. Under `blame_targeted_slots`, a realisable parent re-edits only the slots that already differ from the original, which halved the number of distinct candidate signatures. Under `direction_gated`, it strengthens only along `steps_towards` the original, which cut the number of distinct ways candidates weaken the original by 45%. This smoke test asks whether letting a realisable parent move elsewhere widens the spread of pre-climb candidates, and whether it changes the found-repair rate, the known-repair rate and the seeded recovery rate.

## Design

Three levels of sweep O, each built from `full` without step scaling (see "Blame-targeted slot choice" and "Status-gated mutation direction" in `docs/dev/operators.md`):

- `blame-unreal`: full with scale off (the `no-scale` override, `step_scaling = 0`) and `blame_realizable = false`, so blame narrows only an unrealisable parent's slot draw. This is the control for the two gate modes.
- `gate-free`: blame-unreal with `gate_realizable = "free"`. A realisable parent takes no step towards the original and strengthens with the directed arms on, so a slot may move above the original's value.
- `gate-exchange`: blame-unreal with `gate_realizable = "exchange"`. A realisable parent takes one step towards the original in a slot that differs from it, and weakens one slot still equal to it, in the same mutation.

Step scaling was cut from the paper's mechanisms on 17 Nov, so all three arms run without it. They therefore compare with each other, not with the archived `full` arm, which ran with scale on. A comparison with earlier campaigns goes through their `no-scale` arm, and it still crosses commits.

The configs use the generator flags of 2026-11-05-fretish-order (nsga2-apportion, weakening off, weights 0.1 0.2 0.7, 1000 bred individuals at population 100, 500 generations, `--pin-vintage`), in their own tree, `configs-fretish-gate-smoke`. The two phases run in order:

1. `small`: the small set's nine specifications at seeds 0-9, 270 runs (`fretish-gate-smoke`).
2. `seeded`: the 110 subjects of the seeded corpus (2026-11-13-seeded-corpus) that are both in `selected.txt` and in the headline stratum of `subjects.csv`, at seeds 0-1, 660 runs (`fretish-gate-smoke-seeded`).

The readings are: the number of distinct pre-climb candidate signatures and of distinct weakening patterns, the found-repair rate (`found_repair`), the known-repair rate (`implies_ideal`), and on the seeded corpus the rate of recovering the base. Each gate arm is compared with blame-unreal per (spec, seed) cell.

This is a smoke test. Ten seeds on the small set and two on the seeded corpus are too few to resolve differences of 1-2 percentage points, so a null result says nothing about small effects. The run checks that the modes work at campaign scale and shows which, if any, deserves a full campaign.

## Hosts

av2 only, CPUs 0-15 under a 32 GB cap, at 16 jobs. veribee-33 holds av2 CPUs 16-31 and av3 CPUs 0-15 until about 06:00 on Wed 18 Nov. fret-collab-a5 holds av3 CPUs 16-31 until about 16:30. All of them were told before enqueueing. peredur-extension-30 ran a reduction on av2 CPUs 0-2 until 16:46 UTC, after which all of CPUs 0-15 were free. The user approved the run on Tue 17 Nov.

The seeded corpus's `subjects/`, `selected.txt` and `subjects.csv` are untracked, so a checkout of the branch lacks them. They were copied into the same path in av2's checkout with `rsync -a`, as "Large outputs" in `docs/dev/campaigns.md` prescribes. A stage never runs `git clean`, so the copy survives the tick's checkout.

## Estimate

The full arm took a mean of 94 s per run on the small set and 46 s on the seeded corpus. At 16 jobs, with the compare step on top, expect about 1 h for the small phase and under 1 h for the seeded phase, so about 1.5-2.5 h in all.

## Result

Not run yet.
