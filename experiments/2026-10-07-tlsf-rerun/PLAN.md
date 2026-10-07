# TLSF re-run at the current engine

This campaign repeats `2026-09-14-paper-rerun`'s search, unchanged in design, at `5d117f3`. That binary carries every engine change since the paper-rerun's `57fcefb`, including the *whole-spec screen* under MRS grading (#206), the gate replaying scoring's walk (#210) and the black model probe on implication queries (#211). The design is the same 2x2 of selection (`nsga2-apportion`, `weighted`) against status grading (`mrs`, `aurus`), on 25 TLSF families at 30 seeds, with a 1000-individual budget at population 100 and a 7200 s cap. Its rows replace the PEREDUR side of the paper's RQ1 and RQ2 tables, the anytime and wall-time figures, and every TLSF row of the strength and diversity table.

The paper's strength and diversity rows rested on `2026-09-04-aurus-rematch` (`f8fbe26`) while RQ1 and RQ2 rested on the paper-rerun (`57fcefb`). Every TLSF figure now comes from one search. AuRUS is not re-run, and its archived scoring stands.

## Hosts

av2 takes seeds 0-11, av3 12-22 and av1 23-29. av1 gets fewer seeds because it finishes the FRETISH re-run's curve pass about 6.5 h after the other two. `2026-10-07-tlsf-rerun-calib` runs seed 0 of six families on av1 before its main share. av1 is the one host built with gcc 13, so its calibration runs are diffed against av2's seed-0 runs of the same cells. Output should match byte for byte, and the wall times should agree within seed noise. If the wall times disagree, av1's seeds are re-run on av2 and av3 before the wall-time figures are read.

## Cost

The paper-rerun cost 550.9 core-hours, 17.6-18.2 h on two hosts at 16 jobs. At about 18.4 core-hours a seed, this split puts about 14 h on av2, 13 h on av3 and 8 h on av1 after its queue clears. #211 cut one humanoid-742 search run from 2510 s to 1860 s, and #206 adds a solver call per scored individual under MRS. The archived figure is therefore an estimate, not a bound.

## Downstream passes

None of these run in this campaign. Each is declared once its inputs exist.

1. **Maximality scoring.** One `kind = "score"` pass with ideals, which also writes the `.members.tsv` files the later passes read. It runs on the scorer line that scored the AuRUS side (`0cfd14a`), extended with the #211 probe and a stronger fingerprint prefilter on `perf/scorer-walk`, provided its event logs match `0cfd14a`'s. The 256-word separation pass is dropped, since no printed number reads it.
2. **Separation recount.** The PEREDUR side at 65,536 words, 8/8 lassos, union distance and fingerprint seed 0, with membership taken from pass 1.
3. **Pooled strength.** Selection, grading and PEREDUR against AuRUS as one pair list. Each side's frontier is built by a running antichain walk over the union of its per-run frontiers, and cross pairs are restricted to frontier against frontier. AuRUS-against-AuRUS verdicts are reused from the archived RQ3 pass.
4. **Ball-radii analysis, the size sweep and GR(1) coverage,** rebuilt from passes 1-3.
5. **The paper,** in a new `data/` directory, copied from `rerun-2026-09-uncensored` with the PEREDUR inputs repointed.
