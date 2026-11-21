# 2026-11-20-final-engine

Prepared 2026-11-20, not launched. Every arm of the peredur-extension paper is rerun on the final engine at one commit of `campaign/final-engine`, so that the paper's `\tbd` values come from one engine. Four campaigns share the branch, the commit and the config trees: `2026-11-20-final-engine` (small set and rad's cores, on CPUs 0-13 beside veribee's re-run), `2026-11-20-final-engine-large` (lift-plus-cruise-full, rad and the seeded corpus, on CPUs 0-29 once veribee's re-run ends), and the pooled passes `2026-11-20-final-engine-pooled` (specifications) and `2026-11-20-final-engine-seeded-pooled` (seeded corpus).

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

lift-plus-cruise-full runs full, base and random alone, at 10 seeds (user, 2026-11-20: option B). A run costs about 8.4 slot-h, so the twelve arms at 10 seeds would cost about 1,010; the ablation leaves it out, as it adds about 3% of each contrast's pairs. ventilator-controller is not run. No realisability query on it finishes, so it is taken as a timeout for every arm (user, 2026-11-20).

## Phases, seeds and estimates

Every arm of a (spec, seed) runs on one host. Slot-hours are wall time × threads a run, from the per-run mean wall times of the nearest earlier arm (exchange rerun, first pass, removal campaigns, sideways corpus), × 1.3 for the arms with novelty on. Where blame was off in the earlier arm (base, random, the only-<f> arms on rad), its time is used unchanged, which on rad overstates by up to 5×, since blame cut rad's base from 754 s to 142 s. Wall time assumes 30 CPUs a host and perfect packing.

| Campaign | Phase | Subjects | Arms | Seeds av2 / av3 | Jobs × threads | Slot-h (both hosts) | Wall a host |
|---|---|---|---|---|---|---|---|
| final-engine | small | 9 specs + 10 rad cores | 12 | 0–14 / 15–29 | 7 × 2 on CPUs 0–13 | 310 | 11 h |
| final-engine-large | lpc | lift-plus-cruise-full | full, base, random | 0–4 / 15–19 | 3 × 10 | 250 | about 5 h |
| final-engine-large | rad | rad | 12 | 0–4 / 15–19 | 7 × 4 | 53 | 1–2 h |
| final-engine-large | seeded | 130 sideways mutants | 10 (no random) | 3–4 / 0–2 | 15 × 2 | 227 | 3.0 h / 4.5 h |
| final-engine-pooled | pool, fingerprint, compare, reduce | 18 | 10 nsga2 arms | all 30 | 8 draws, 12 compares | about 35 CPU-h | 2–4 h |
| final-engine-seeded-pooled | the same | 130, less any with an arm of ≤ 3 members | 10 nsga2 arms | all 5 | the same | 25–70 CPU-h | 1–2 h |

All estimates carry at least ±50%. Each phase names its host, seeds, CPUs (0–29; 30–31 stay free for Ollama) and memory cap on one line, at the exchange rerun's caps (64G small, 96G rad, 110G lpc; 48G for the seeded corpus at 15 jobs, where 8 jobs ran under 24G). Another agent shares the hosts, so the windows are the user's to set; see the comment in each `campaign.toml`.

## Pooled passes

Both designs come from `experiments/2026-11-20-final-engine-pooled/scripts/make_design.py`, which writes `design.toml` and `subjects-{av2,av3}.txt` (untracked, as every pooled pass's inputs are). Every pair of the ten nsga2-apportion arms is a contrast: family `base` holds full, every only-<f> and no-climb against base; `ablation` each no-<f> against full; `joint` the other 35 pairs. The specification pass pools the 18 subjects of 2026-11-19-fretish-exchange-pooled (`POOLED_EXCLUDED` drops fsm-lmcps) with its host split and its two sampled subjects. The seeded pass pools all 130 subjects, split alternately; after pooling, rerun `make_design.py seeded --drop …` for any subject where an arm holds 3 or fewer members, and `--sampled …` for any pool too large to compare exhaustively.

The reduce phase runs on av2 alone (av3 cannot ssh to av2) once av3's entry has passed its compare phase: `pooled_reduce.py --pooled-balls --ball-k 3` (strength, containment with each arm's seed-half control) and `joint_measures.py --half-control` (share, joint-front reach, and each arm's odd-against-even reach control, bennote item 6).

## Order of work

1. Copy nothing for the runs: the sideways corpus (`selected.txt`, `subjects/`) is already in both hosts' checkouts (checked 2026-11-20). Enqueue `2026-11-20-final-engine` on av2 and av3 (CPUs 0-13).
2. Enqueue `2026-11-20-final-engine-large` on each host only once veribee has posted in the mailbox that it has released CPUs 14-29 there (expected Sat 01:00Z on av3, Sat 04:00Z on av2). campaign.py has no start time, and the queue would start the entry as soon as the first ends.
3. When both entries finish on both hosts: `campaign.py collect --profile final-engine`, `--profile final-engine-seeded`, `--profile final-engine-large` and `--profile final-engine-lpc`, each with `--host av2` then `--host av3`, and add the sets to `experiments/catalogue.toml`.
4. Locally, one at a time under a memory cap: `make_design.py specs` and `make_design.py seeded`, then `scripts/pool_members.py <design> <campaign>/members --root .` for each pooled campaign, and record the SHA-256 sums of the inputs in that campaign's directory.
5. `rsync -a` each pooled campaign's `design.toml`, `subjects-*.txt` and `members/` to both hosts' checkouts, then enqueue both pooled campaigns on both hosts.
6. `campaign.py collect --outputs final-engine-pooled-reduced --host av2`, and likewise for the seeded pass.

## Log

- 2026-11-20 14:15Z: `2026-11-20-final-engine` enqueued at 8948138 as av2 099 and av3 097, on CPUs 0-13 beside veribee's re-run. Done at 23:45Z on av2 and 00:03Z on av3, about 9.5 h a host against the 11 h estimate.
- 2026-11-21 00:09Z and 03:45Z: veribee released av3 and then av2. `2026-11-20-final-engine-large` enqueued as av3 098 (01:00Z) and av2 100 (03:52Z), on CPUs 0-29. Done at 11:35Z on av3 (10.4 h) and 11:59Z on av2 (8.1 h). lift-plus-cruise-full took about 3.2 h on av3 against the 5 h estimate; the seeded phase took 6.8 h on av3 against 4.5 h.
- 2026-11-21 12:34Z: collected `final-engine` (6,840 rows), `final-engine-large` (120), `final-engine-lpc` (30) and `final-engine-seeded` (6,500) from av2 and then av3. Every source row is present once. All rows are at 8948138, clean, and none timed out. Found-repair counts: 6,253 of 6,840 on the small set and rad's cores, 5 of 120 on rad (as earlier campaigns, about 1 in 10 for the stronger arms and none for base), 30 of 30 on lift-plus-cruise-full, and 6,280 of 6,500 on the seeded corpus. Catalogue entries added.
- 2026-11-21: veribee holds CPUs 14-29 (and up to 32G) on each host from the end of `-large` there until Mon 23 Nov 06:00Z (mailbox, 01:30Z), so both pooled passes run on CPUs 0-13 (d2df1e5b).
