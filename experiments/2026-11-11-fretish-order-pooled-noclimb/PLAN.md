# 2026-11-11-fretish-order-pooled-noclimb

The pooled re-run of 2026-11-09-fretish-order-pooled found full stronger than base (median net +0.371) and only-climb stronger still (+0.920), which leaves open whether full's gain over base is the climb's alone. This pass adds the no-climb arm, every feature but the climb, with two contrasts in one Holm family: no-climb against base, and full against no-climb. If full against no-climb accounts for the whole of full's gain, the climb carries it; if no-climb beats base too, the other features add to it.

1. Done on 2026-11-11: `pool_members.py` pooled all 19 subjects locally into `members/` in 55 s from the collected `results-fretish-order` and `-cores`, with no missing run. Base's and full's node and member counts match the first pass's pool on every subject.
2. Done on 2026-11-12: enqueued on av2 at 04:05, after the other sessions' batches there. The pool phase copied `members/`, then fingerprint and compare ran on every subject with 16 compare jobs, finishing at 05:29.
3. Done on 2026-11-12: `campaign.py collect --outputs fretish-order-pooled-noclimb --host av2` brought home 545,014 compare rows, every one with rc 0. `pooled_reduce.py` on this design, then again with `--pooled-only --pooled-draws 20 --jobs 10`, wrote `reduced/` in 11 min.

## Result

Strength (`reduced/strength/summary.txt`): no-climb against base has median net +0.191, no-climb stronger on 17 of 18 subjects (Holm p 1.5e-05). Full against no-climb has median net +0.738, full stronger on 18 of 19 with one tie (Holm p 1.5e-05). Both the climb and the other six features add strength, the climb the larger share.

Diversity (`reduced/ball-radii-pooled/`): no-climb's nodes fall inside base's region less often than base's control at K = 3 (0.588 against 0.636, 14 of 18 below, Holm p 0.018) and K = 5, not at K = 1, while base's fall inside no-climb's as often as inside its control at every K. Between full and no-climb, each falls inside the other's region less often than inside its control at K = 3 (Holm p 0.008 and 0.007), and one direction each passes at K = 1 and K = 5. Full's spread outside base's region is therefore not the climb's alone.
