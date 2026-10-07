# 2026-10-07-tlsf-rerun-scoring

## Purpose

Scores 2026-10-07-tlsf-rerun's search output for the paper's TLSF figures: the anytime maximality curves with their `.members.tsv` sidecars, then the separation recount at seed 0 over those sidecars. Each host scores the seeds it searched, so the split equals the search's: av2 0-12, av3 13-25, av1 26-29. The entry on each host is queued behind that host's search entry and starts when the search ends.

## Scorer

The branch is 0cfd14a, the scorer behind 2026-09-14-paper-rerun-curves and the archived AuRUS curves, plus two changes to the implication checker and the walk's prefilter. 032c2dc ports main's aaae1a8: an implication query is tried by SPOT for 200 ms, then by black for 300 ms taking only SAT, then by SPOT at the full budget. 1a1e86f makes the walk's fingerprint sampling configurable, and this campaign sets 4096 words. Neither change can turn a refutation into an implication or the reverse: black's probe accepts only SAT, and the prefilter only skips queries whose implication a sampled word already refutes.

Measured on 11 archived paper-rerun runs locally (4 humanoid-742, 3 humanoid-531, 2 pcar-v2-888, lift, gyro-var2), each A/B pair interleaved on the same 4 cores: the old binary took 4435 s in total, the probe alone 892 s (5.0x), and the probe with 4096 words 637 s against 850 s for the probe alone on a paired re-run. Every event log from the probe and from the probe with 4096 words is byte-identical to the old binary's. A 65536-word, 8/8 prefilter was slower (1032 s, fingerprinting serial at 22-61 s a run) and lost one implication to a 20 s SPOT timeout on humanoid-742 seed 2, so it is not used.

The branch also carries main's campaign.py (av1, load verb, rebuild before a phase), the recount tooling from 16381dc, and scripts/pooled_frontier.py with a `kind = "frontier"` phase for the pooled strength pass.

## Settings

The maximality phase keeps 2026-09-14-paper-rerun-curves' maximality-peredur settings: 8 workers of 4 cores, 20 cuts, `maximal_timeout` 900, `compare_timeout` 3000, `deadline_s` 7500. The recount keeps 2026-10-01-separation-recount's counter-s0 settings: 65536 words, seed 0, 8/8 lassos, union distance, 32 workers of 1 core, `deadline_s` 900. The AuRUS side is not re-scored; its archived curves and recount came from the same scorer and settings.

## Cost

The archived maximality pass took 183.9 worker-hours on av2 for 15 seeds, of which humanoid-742 and humanoid-531 were 81%, and 57 runs stopped at the 7500 s deadline. At the measured 7.0x on the walk, and with `compare` against the ideals unchanged, this is an estimate of 3-5 h a host at 8 workers, not a measurement on a full host. The archived recount took about 20 core-hours over both hosts.

## Afterwards

The pooled strength pass needs every host's sidecars and bodies in one place, so it follows `collect` as its own campaign: `pooled_frontier.py plan` over the collected sidecars writes the work tree and per-host job lists, and a `kind = "frontier"` phase runs it. Then the ball radii, the size sweep, the Maoz coverage and the paper's data directory, as in 2026-10-07-tlsf-rerun's PLAN.md.
