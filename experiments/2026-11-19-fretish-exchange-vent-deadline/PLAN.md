# 2026-11-19-fretish-exchange-vent-deadline

The paper's scale paragraph cites `largeplus-ventilator-controller`, the share of realisability queries that reach the 10 s ltlsynt timeout. That number comes from 2026-11-13-fretish-order-large-counters, whose full arm is the demotion-based one that removal replaced on 2026-11-17. 2026-11-18-fretish-exchange-rerun ran the new full (`remove-exchange`) on ventilator-controller, but under the 7200 s harness kill, which writes no run.json. This campaign runs that arm again under a 3000 s search deadline, so each run stops on its own and writes its manifest.

The other half of the counters rerun needs no new runs. `base` on the small set and the cores ran in 2026-11-18-fretish-exchange-rerun at e117a76, and its run.json (schema 68) records `final_front` {members, realizable, gutted}: 285 runs on av3 and as many on av2. lift-plus-cruise-full and rad under the new full also ran there with manifests.

## Design

`remove-exchange` from sweep D, seeds 0-9, on av3 alone. The configuration is the exchange rerun's large tree plus `genetic.max_wall_s = 3000` (`gen_configs.py` emits the same file with that one line added). The commit is e117a76 plus this declaration and its profile, so the engine is the one every other paper arm ran. 7 jobs on CPUs 0-29 give each run 4 threads, as in both earlier ventilator-controller passes.

The paper must report these numbers as measured at a 3000 s budget, as it did for the 2026-11-13 pass.

## Questions

- **Timeout share.** The share of ltlsynt queries that reach the 10 s timeout, against 40.7% (12,180 of 29,899) under the old full.
- **Front and gutting.** Whether any run finds a repair, and the final front's {members, realizable, gutted}. Under the old full no member was realisable.
- **Where the time goes.** Tool time over wall time and ltlsynt's share of it, against 3.96× and 94.8%.

## Estimate

About 2 h: two waves, each run taking 3294-3774 s of harness time at the old pass's deadline.
