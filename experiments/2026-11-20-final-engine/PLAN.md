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

## Random against base

Added 2026-11-23 at the user's request: the paper measures no strength or diversity for `random`. `2026-11-23-final-engine-pooled-random` pools `random/base` and `nsga2-apportion/base` alone on the specification pass's 18 subjects and host split, with the one contrast random against base (`make_design.py random`). Strength and pooled balls read one pair of arms and share reads one arm's frontier, so on the same words they equal what an eleventh arm in the ten-arm pass would give, and the ten-arm pass's outputs stay as they are. The words are drawn from the pool's members, so a fresh draw would differ: before enqueue, each host copies `words` and `words.meta` from `experiments/final-engine-pooled/fp/<subject>/` into `experiments/final-engine-pooled-random/fp/<subject>/`, and the fingerprint phase draws none. Joint-front reach in its output is over two arms and is not the paper's measure. Order of work: pool locally, rsync the inputs, copy the words on each host, enqueue on both hosts (CPUs 0-13), collect the reduce from av2.

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
- 2026-11-21 12:40Z: pooled locally under MemoryMax=20G. The specification pass has 18 subjects (15 on av2, 3 on av3; valu3s-uc6 and lift-plus-cruise-mini sampled) and 45 contrasts, every pair of the ten nsga2-apportion arms. The seeded pass first pooled all 130 subjects. Four subjects had an arm holding 3 or fewer members, all fsm-lmcps mutants (`sw-fsm-lmcps__paper-v2__m01`, `m02`, `m06`, `m07`), so the design was rewritten with `--drop` for them and pooled again: 126 subjects, 63 a host. Its largest pool is 4,423 nodes (`sw-rad__s3-9-32-56-71-72__m07`), so none is sampled. The inputs were copied to both hosts by rsync and checked there against these sums:

```
3fa1f043197ebc27a059035b6b40e4c85d2a9d264cf100ac5ef13ddfc9d77a7d  2026-11-20-final-engine-pooled/design.toml
20896dc9b89dde698d76254432975e6e4ce4b7370d33d50f7cd9f6915a0bb785  2026-11-20-final-engine-pooled/subjects-av2.txt
855f78e4c637cbe7837c5da9cc7c40be51c4afd02754f6d6d7f9ea8567b4abf8  2026-11-20-final-engine-pooled/subjects-av3.txt
0e170061696bdc5e9156792029c5632e1229a88941480d1109ac59381a8ebbbd  2026-11-20-final-engine-pooled/members/index.csv
48b2b9d11bfc058e015e187a9c80e65909d33721dc7f30ce7a99876b8ef1578b  2026-11-20-final-engine-pooled/members/done.csv
aa0da1329ac9f77f2e5dfb5b19c34f189e6fa95db10edef45d4caf4ba1d76c25  2026-11-20-final-engine-seeded-pooled/design.toml
ce1f64aaca4ca1112961c82f668036822b8431375c38f558b262f602173d8216  2026-11-20-final-engine-seeded-pooled/subjects-av2.txt
045c6a17455f778c4a7c00c69f7093fb76253390febbba06fdf871357cca130e  2026-11-20-final-engine-seeded-pooled/subjects-av3.txt
ad3dfa029274b7f7d91cca14152d9705002bed1ba2b6ed09c56a1d0d10d6802b  2026-11-20-final-engine-seeded-pooled/members/index.csv
5e60e69f6fb0cbe7c6d658a214faca34debc65a0e8507a4fe6aaeaf6da19b7cd  2026-11-20-final-engine-seeded-pooled/members/done.csv
```
- 2026-11-21 16:35Z: the specification pass ended on av3, after its compare (2.37 M rows), and reached reduce on av2 at 16:45Z, after 8.0 M rows. The seeded pass's fingerprint phase failed twice on av3 (16:2xZ), because `fingerprint_members.py` read every spec from `examples/<subject>/spec.json`, where no seeded mutant has one. It was fixed in acb3231d. Both seeded entries were dequeued (av3 100, av2 102) and enqueued again at acb3231d as av3 102 and av2 104, behind peredur-psp's `2026-11-20-psp-order` (av3 101, av2 103), which was agreed in the mailbox.
- 2026-11-21 18:30Z: the specification pass's reduce ended on av2 (pooled at 18:21Z, joint at 18:30Z), and `campaign.py collect --outputs final-engine-pooled-reduced --host av2` copied its 11 files into `experiments/final-engine-pooled-reduced/av2/` (strength, ball radii, pooled ball radii and joint measures over 18 subjects). The seeded entries wait behind peredur-psp's: av3 102 behind psp's av3 101 (182 of 840 runs at 18:43Z), and av2 104 behind psp's av2 103, which started at 18:3xZ. Sums of the main outputs:

```
9a89431010d8061ce1c556ef0eda4d1558991b2dfa3eb2583fba6b5f930dfd3d  strength/contrasts.csv
cedc8be2b37726421e885da382dd12c9aaf4dd67671b5bce1ccfbbaae5be015f  joint/per-subject.csv
f8818df380aa468857fb71233258bacf8abb1d32016978e441fe0bbf00eea8db  ball-radii-pooled/pooled.csv
```
- 2026-11-21 22:05Z: the seeded pass ended on av3 (entry 102: fingerprint from 19:10Z, compare of 1.45 M pairs from 21:10Z). peredur-psp's av3 entry had been dequeued at 19:02Z after its own compare ran out of memory (an unbounded `ltlfilt --remove-wm` capture, fixed on psp's branch in 42a10e03), so our 102 started early. That fix is not on this branch: no compare of ours ran out of memory.
- 2026-11-22 00:05Z: the seeded pass ended on av2 (entry 104: fingerprint from 19:45Z, compare of 1.54 M pairs from 21:35Z, reduce from 23:40Z to 23:53Z). Its compare slowed to about 18 pairs a second on one subject, `sw-fsm-timing__add-not-supported__m00` (60,041 pairs), and ran at its usual rate on the rest. `campaign.py collect --outputs final-engine-seeded-pooled-reduced --host av2` copied its 11 files into `experiments/final-engine-seeded-pooled-reduced/av2/`: 126 subjects, 45 contrasts each (5,670 contrast rows). Both pooled sets are now in `experiments/catalogue.toml`. Every step of the order of work is done. Sums of the main outputs:

```
056fff8c291d0d8902a04270427c1b62cd3eea7cae398364e59678a13083e9d0  strength/contrasts.csv
520797b04cdcfe9950b60a64c1fa1a8fac75134f898009c7663c9a0a0c71c882  joint/per-subject.csv
dba80150ee7656fd1e725927eb328e435666c9556074c65c40ff9715923ece20  ball-radii-pooled/pooled.csv
```
- 2026-11-23 18:30Z: pooled `2026-11-23-final-engine-pooled-random` locally under MemoryMax=20G: 18 subjects, 75,721 nodes (32% of the ten-arm pool's 235,515). random holds 10 to 5,202 members a subject; on rad-core-10, 23 of its 30 runs are empty and 10 members remain. Sums of the inputs:

```
8eb626cb25c87546edadf0daeb18541e5f9a51bb6bdd48479a4ffa9018f2e3ae  design.toml
20896dc9b89dde698d76254432975e6e4ce4b7370d33d50f7cd9f6915a0bb785  subjects-av2.txt
855f78e4c637cbe7837c5da9cc7c40be51c4afd02754f6d6d7f9ea8567b4abf8  subjects-av3.txt
a2937dd5d09b377da42327080f4d24413df3b58f8e46700cd306bb6727db56e8  members/index.csv
7125856c24fb4fd9a1eacaa198a00e28fbb5988717b42cf1559fa45dbb6dddbf  members/done.csv
```
- 2026-11-23 19:56Z: `2026-11-23-final-engine-pooled-random` ran as av2 108 and av3 106 from 19:15Z (fingerprint about 10 min with no word drawn, compare of 353,067 pairs on av2 and 224,244 on av3, reduce on av2) and ended at 19:55Z. `campaign.py collect --outputs final-engine-pooled-random-reduced --host av2` copied its 11 files. `joint_measures.py` labels both arms `base`, since it prints the level alone; in `joint/per-subject.csv` the first row of each subject is nsga2-apportion/base, whose frontier and share equal the ten-arm pass's on all 16 exhaustive subjects. random against base: net strength median -0.033 (5 of 18 subjects random stronger, p = 0.39); share HL -0.080 [-0.122, -0.034] over 18 subjects (random weaker on 14, p = 0.0034); pooled balls, random's halves against base's, +0.027 (p = 0.55). Sums of the main outputs:

```
7ea5c056da4f2aef8cf1c5cd24f4fa350140647b532913e2185344bd0931bce6  strength/contrasts.csv
3a3422b2233dc463592f4dcc956c48a160d651c226cf2f65e34d7574e9fb91a8  joint/per-subject.csv
519f829b606e148dd7699df9ce1bc29e42be3d97e20d6eb74627deb7a657cee7  ball-radii-pooled/pooled.csv
```
