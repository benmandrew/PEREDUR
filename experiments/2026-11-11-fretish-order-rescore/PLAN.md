# 2026-11-11-fretish-order-rescore

The anytime curves of 2026-11-05-fretish-order's small set missed every repair the output climb made, because the climb never accumulated its results and `score_curves.py` read only `accumulated/index.tsv` and the files it names. For random+climb at 100 s, the ideal curve showed 36% of runs against 45% at the stop. `6e5aa655` makes the scorer add each `repair_N.json` that a climb-on run never accumulated, timed at the run's `wall_s`.

This campaign re-scores the existing run directories with that scorer, at the first pass's budgets, into `experiments/curves-fretish-order-v2`. Nothing is re-run. Each host scores its own seeds (av2 0-14, av3 15-29), since each holds only those run directories. The first pass took 164,660 worker-seconds over both hosts at 8 workers of 4 cores, so about 3 h of wall time a host. A second phase re-scores the large set into `experiments/curves-fretish-order-large-v2` the same way (217,089 worker-seconds in the first pass, about 3.8 h a host). It was added on 2026-11-11 after the small phase was enqueued on av2, so av2 has a second entry for it; the small phase there skips the curves it already wrote.

Collect with `campaign.py collect --curves curves-fretish-order-v2`. Arms without the climb should reproduce the first pass's curves exactly, which is the check that nothing else moved.
