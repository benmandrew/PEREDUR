# 2026-11-13-fretish-order-large-counters

The paper's RQ3 (the three largest specifications) has three gaps that the 2026-11-05-fretish-order large set cannot fill, because its run.json predates the counters of PR #226 (be57f58f) and every ventilator-controller run met the 7200 s harness cap, which writes no run.json at all.

- **Climb's share of wall time on large specifications.** The paper gives it for the small set only (9.7% of full's summed wall time, 18.5% of only-climb's). This campaign reads `climb.wall_s` over `wall_s` for full on lift-plus-cruise-full and rad.
- **Gutting on ventilator-controller.** Its output modes give the system a gutting move. This campaign reads `final_front` {members, realizable, gutted} there.
- **Where ventilator-controller's time goes.** The tool-time split in the paper covers only runs that wrote a manifest, and none of ventilator-controller's did.

## Design

Full arm only (nsga2-apportion, sweep O level full), seeds 0-9, on av2. lift-plus-cruise-full and rad use the first pass's config tree unchanged. ventilator-controller uses the same configuration plus `genetic.max_wall_s = 3000`, in its own tree (`configs-fretish-order-deadline`), so each run stops on its own deadline and writes its repairs and run.json. That is a smaller budget than the first pass's 7200 s kill, and the paper must report its numbers as measured at that budget. The longer-cap re-run of ventilator-controller was ruled out, and this campaign is not that.

The runs share av2 with GAWAIN's reruns (CPUs 24-31) after veribee's hlwide-run ends, about 17:30 UTC on 2026-11-13. The agreed terms are CPUs 0-15 and one unit capped at 24 GB with no swap, so that a run that grows kills only this campaign's work. The host's user systemd lacks the cpuset controller, so the CPUs are set with taskset inside a memory-capped scope (the campaign.py change of the same day).

## Estimate

About 8-9 h: rad a few minutes, lift-plus-cruise-full 3.5-4 h at 2 jobs of 8 threads, ventilator-controller 3-5 h at 4 jobs of 4 threads. Tell GAWAIN when it ends, since it needs an unloaded host for a timing run.

## Result

Queue entry 073 ran on av2 from 17:15 to 23:22 UTC on 2026-11-13, 6 h 7 min, inside the estimate. All 30 runs wrote run.json at f557c5d, and `collect` merged 20 rows into `results-fretish-order-large-counters.csv` and 10 into `results-fretish-order-vent-deadline.csv`.

- **Climb's share of wall time.** On lift-plus-cruise-full every run found a repair, and the climb took 4.0% of the summed wall time (median 3.3% per run), against 9.7% on the small set. It climbed 1,257 repairs over the 10 runs. On rad no run found a repair, each stopping in 82-98 s, so the climb had nothing to do and took no measurable time.
- **Gutting on ventilator-controller.** At the 3000 s deadline no run found a repair. Every run stopped on its own deadline: the engine's wall_s is 1.01-1.18x the 3000 s, and the harness's wall time, which adds start-up and output, is 3294-3774 s. The final fronts hold 7-20 members, none of them realisable, so none is gutted, and gutting cannot be checked at this budget.
- **Where ventilator-controller's time goes.** A run completes 4 or 5 generations, breeding a median of 150 individuals. Summed tool time is 3.96x the summed wall time. ltlsynt takes 94.8% of it and the stored-controller check 4.4%; Ganak is never called, because nothing is scored past the status stage. Of 29,899 ltlsynt queries, 12,180 (40.7%) hit the 10 s timeout, at a mean of 4.2 s per query. The budget screen on the original specification itself times out at 10 s. The bottleneck is the realisability query, which at this size mostly fails to decide.

Peak memory: the ventilator phase's scope peaked at 13.6 GB (with page cache) under the 24 GiB cap. The first two phases' scopes had unloaded before they could be read, and no run records its peak RSS.
