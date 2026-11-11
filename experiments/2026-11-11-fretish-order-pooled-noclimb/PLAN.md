# 2026-11-11-fretish-order-pooled-noclimb

The pooled re-run of 2026-11-09-fretish-order-pooled found full stronger than base (median net +0.371) and only-climb stronger still (+0.920), which leaves open whether full's gain over base is the climb's alone. This pass adds the no-climb arm, every feature but the climb, with two contrasts in one Holm family: no-climb against base, and full against no-climb. If full against no-climb accounts for the whole of full's gain, the climb carries it; if no-climb beats base too, the other features add to it.

1. Done on 2026-11-11: `pool_members.py` pooled all 19 subjects locally into `members/` in 55 s from the collected `results-fretish-order` and `-cores`, with no missing run. Base's and full's node and member counts match the first pass's pool on every subject.
2. Enqueue on av2 once its other sessions' batches have finished (agreed: after veribee's ~04:30 window). The pool phase copies `members/`, then fingerprint and compare run on every subject with 16 compare jobs.
3. Collect with `campaign.py collect --outputs fretish-order-pooled-noclimb --host av2`, concatenate the subsumption CSV, and reduce locally with `pooled_reduce.py` on this design, as in the first pass.
