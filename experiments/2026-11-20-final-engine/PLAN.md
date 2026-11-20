# 2026-11-20-final-engine

Prepared 2026-11-20, not launched. Every arm of the peredur-extension paper is rerun on the final engine at one commit of `campaign/final-engine`, so that the paper's `\tbd` values come from one engine. Four campaigns share the branch, the commit and the config trees: `2026-11-20-final-engine` (small set, rad's cores, seeded corpus, rad), `2026-11-20-final-engine-large` (lift-plus-cruise-full, then ventilator-controller), and the pooled passes `2026-11-20-final-engine-pooled` (specifications) and `2026-11-20-final-engine-seeded-pooled` (seeded corpus).

## Engine

Sweep F of `scripts/gen_configs.py`. Four features: climb (`climb_output`, `climb_orders = 1`), twoway (`direction_gated`, `bisect_steps`, `gate_realizable = "exchange"`), remove (`maximality_demotion` and `maximality_removal`) and novelty (`novelty`). `full` is sweep K's `full-k1` plus `novelty = true`. In every arm, blame-targeted slots are on (paper bennote item 1), and the controller store's screen gate is `controller_reuse_min_screen_ms = 150` with `controller_reuse_min_guarantees = 10` (bennote item 2): in 2026-11-18-fretish-exchange-rerun the screen took 441–942 ms on lift-plus-cruise-full and at most 47 ms on every other subject, so the store is on for lift-plus-cruise-full and off elsewhere, and host load moves no subject across the gate. Every arm states every mechanism key, on or off, and `--pin-vintage` states the vintage keys.

Against `full-k1`, `full` adds `novelty = true`, `controller_reuse_min_screen_ms = 150`, and lines that state keys at the values full-k1 inherits (`elitism_rate`, `crowding`, `novelty_tiebreak`, `spread_init`, `p_stop`, `p_add_assumption`, `p_remove_guarantee`, `blame_realizable`, `slot_coverage`, `flip_tabu`, `flip_sideways`, `controller_reuse_min_guarantees`). `base` differs from `full` in the seven feature keys alone.

## Arms

| Arm | Config |
|---|---|
| base, full, no-climb, no-gate, no-remove, no-novelty, only-climb, only-gate, only-remove, only-novelty | `nsga2-apportion/…/sweep_F_<arm>.toml` |
| random | `random/…/sweep_F_base.toml` |
| random+climb | `random/…/sweep_F_only-climb.toml` |

`only-<f>` is base plus f's keys; `no-<f>` is full with f's keys at base's values. The gate feature prints as twoway in the paper.

## Phases, seeds and estimates

Every arm of a (spec, seed) runs on one host. Slot-hours are wall time × threads a run, from the per-run mean wall times of the nearest earlier arm (exchange rerun, first pass, removal campaigns, sideways corpus), × 1.3 for the arms with novelty on. Where blame was off in the earlier arm (base, random, the only-<f> arms on rad), its time is used unchanged, which on rad overstates by up to 5×, since blame cut rad's base from 754 s to 142 s. Wall time assumes 30 CPUs a host and perfect packing.

| Campaign | Phase | Subjects | Arms | Seeds av2 / av3 | Jobs × threads | Slot-h (both hosts) | Wall a host |
|---|---|---|---|---|---|---|---|
| final-engine | small | 9 specs + 10 rad cores | 12 | 0–14 / 15–29 | 15 × 2 | 310 | 5.2 h |
| final-engine | seeded | 130 sideways mutants | 10 (no random) | 0–2 / 3–4 | 15 × 2 | 227 | 4.5 h / 3.0 h |
| final-engine | rad | rad | 12 | 0–4 / 15–19 | 7 × 4 | 53 | 1–2 h |
| final-engine-large | lpc | lift-plus-cruise-full | 12 | 0–2 / 15–17 | 3 × 10 | 605 | 9–10 h |
| final-engine-large | vent | ventilator-controller | full, base, random | 0–4 / 15–19 | 7 × 4 | 240 | 6 h (3 rounds at the cap) |
| final-engine-pooled | pool, fingerprint, compare, reduce | 18 | 10 nsga2 arms | all 30 | 8 draws, 12 compares | about 35 CPU-h | 2–4 h |
| final-engine-seeded-pooled | the same | 130, less any with an arm of ≤ 3 members | 10 nsga2 arms | all 5 | the same | 25–70 CPU-h | 1–2 h |

All estimates carry at least ±50%. Each phase names its host, seeds, CPUs (0–29; 30–31 stay free for Ollama) and memory cap on one line, at the exchange rerun's caps (64G small, 96G rad and vent, 110G lpc; 48G for the seeded corpus at 15 jobs, where 8 jobs ran under 24G). Another agent shares the hosts, so the windows are the user's to set; see the comment in each `campaign.toml`.

## Pooled passes

Both designs come from `experiments/2026-11-20-final-engine-pooled/scripts/make_design.py`, which writes `design.toml` and `subjects-{av2,av3}.txt` (untracked, as every pooled pass's inputs are). Every pair of the ten nsga2-apportion arms is a contrast: family `base` holds full, every only-<f> and no-climb against base; `ablation` each no-<f> against full; `joint` the other 35 pairs. The specification pass pools the 18 subjects of 2026-11-19-fretish-exchange-pooled (`POOLED_EXCLUDED` drops fsm-lmcps) with its host split and its two sampled subjects. The seeded pass pools all 130 subjects, split alternately; after pooling, rerun `make_design.py seeded --drop …` for any subject where an arm holds 3 or fewer members, and `--sampled …` for any pool too large to compare exhaustively.

The reduce phase runs on av2 alone (av3 cannot ssh to av2) once av3's entry has passed its compare phase: `pooled_reduce.py --pooled-balls --ball-k 3` (strength, containment with each arm's seed-half control) and `joint_measures.py --half-control` (share, joint-front reach, and each arm's odd-against-even reach control, bennote item 6).

## Order of work

1. Copy nothing for the runs: the sideways corpus (`selected.txt`, `subjects/`) is already in both hosts' checkouts (checked 2026-11-20). Enqueue `2026-11-20-final-engine` on av2 and av3, then `2026-11-20-final-engine-large`.
2. When both hosts finish `2026-11-20-final-engine`: `campaign.py collect --profile final-engine` and `--profile final-engine-seeded`, and add both sets to `experiments/catalogue.toml`.
3. Locally, one at a time: `make_design.py specs` and `make_design.py seeded`, then `scripts/pool_members.py <design> <campaign>/members --root .` for each pooled campaign, and record the SHA-256 sums of the inputs in that campaign's directory.
4. `rsync -a` each pooled campaign's `design.toml`, `subjects-*.txt` and `members/` to both hosts' checkouts, then enqueue both pooled campaigns on both hosts. A host runs its entries in queue order, so a pooled entry enqueued after `2026-11-20-final-engine-large` waits for it; enqueue the pooled passes first, or dequeue and re-enqueue the large entry, to run them sooner.
5. `campaign.py collect --outputs final-engine-pooled-reduced --host av2`, and likewise for the seeded pass, then `collect --profile final-engine-large` and `--profile final-engine-vent`.
