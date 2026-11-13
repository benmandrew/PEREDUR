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

Pending.
