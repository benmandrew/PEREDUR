# 2026-11-27-seeded-random-extra

After 2026-11-27-seeded-random, random's seeded pooled pass covered 125 mutants and the Pareto arms' 129. Three of the four missing (fsm-lmcps paper-v2 m01, m02, m07) are pooled at seeds 0-29 for the Pareto arms (2026-11-26-extra-seeds), but random ran them at seeds 0-4 only, where no arm repairs them. On the fourth, m09, random holds 1 repair at seeds 0-4, under the join rule's 4. The user asked for no difference (2026-11-27) and chose: random at seeds 5-29 on the four mutants, every arm at seeds 5-29 on m09, and m09 pooled at seeds 0-29 in both passes, as the extra-seeds mutants are. A mutant where random still holds 3 or fewer repairs leaves both passes. The runs feed the pooled passes only; the paper's per-run seeded measures keep seeds 0-4.

## Design

Profiles `seeded-random-extra` (random/base, the four mutants) and `seeded-m09-extra` (the 8 Pareto arms on m09: sweep F's base, only-<f> and no-gate, sweep T's no-<f>), with the configs of 2026-11-26-extra-seeds, byte-identical to sweeps F and T. Run directories keep sweeps F and T's names. av2 alone, since the GAWAIN session holds av3.

| Phase | Runs | Jobs x threads, CPUs | Estimate |
|---|---|---|---|
| random | 4 mutants x 25 seeds = 100 | 15 x 2 on 0-29 | about 10 min |
| m09 | 8 arms x 25 seeds = 200 | 15 x 2 on 0-29 | about 20 min |

## After the runs

1. Collect both profiles (100 and 200 rows).
2. `scripts/make_design.py count`: members over seeds 0-29 per arm; then `write` builds the farm and the two passes, less any mutant that fails the join rule.
3. Pool both locally under MemoryMax=20G, write INPUTS.sha256, rsync to av2. Enqueue `2026-11-27-extra-m09-pooled`. After its fingerprint phase, copy the words (m01, m07 from av2's extra-seeded-pooled/fp, m02 from av3's, m09 from extra-m09-pooled/fp) into extra-seeded-random-pooled/fp and enqueue `2026-11-27-extra-seeded-random-pooled`.
4. Collect both reduced outputs; hand av2 back to the GAWAIN session.
5. Paper: in `import_data.py`, add the m09 pass to `POOLED_EXTRA["seeded-final"]` and the random pass to `POOLED_EXTRA["random-seeded-final"]`; update the Units text; `make import` and `make`.

## Log

- 2026-11-27: prepared. av2 promised by the GAWAIN session for about 23:55Z.
