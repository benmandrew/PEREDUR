# Pooled-frontier strength, FRETISH directed against uniform

A measurement pass run by hand on av3 (and briefly av2) on 2026-10-05, outside `scripts/campaign.py`. It compares the strength of the two FRETISH mutation arms under the implication order, subject by subject, in the same pooled design as the TLSF overlap pass (`experiments/2026-09-25-overlap`).

## Design

For each subject and arm, the pool is every member of every run's `accumulated/maximal.tsv` over the 30 seeds of `results-fretish-ablation` (profile `sweep_O_{directed,uniform}_nsga2-apportion_wkoff_log_<subject>_seed<NN>`; seeds 0–14 copied from av2, seeds 15–29 from av3). Members with identical normalised JSON are one node. Within each arm, a node is dominated when another node of that arm strictly implies it; the undominated nodes, merged by equivalence, are the arm's frontier classes. Every directed class then meets every uniform class in both directions.

- `sub_d` is the share of directed's frontier classes strictly implied by some uniform class, and `sub_u` the reverse.
- `net = sub_u − sub_d`. A negative net means uniform's repairs subsume directed's more often than the reverse, so uniform's are the stronger.
- `_imp` and `_non` columns read every undecided direction as an implication and as a non-implication respectively.

Fingerprints on each subject's `fmix-front` word set prune the queries: a word that one side accepts and the other rejects refutes that direction, which is the answer the solver would give. Words decide cost and never a verdict. `scripts/pooled.py` holds the method; its docstring gives the detail. Subjects whose within-arm candidate pairs exceed 50,000 build their frontier by antichain insertion in ascending order of accepted words (`method_*` = `incremental`).

## Binaries

`compare` built from 5dc3472 (after the glued-X fix) plus `scripts/compare-directions.patch`, which skips a word-refuted direction and prints each direction's verdict. Hence `dirty=1` in `compare.version`. The per-direction budget is compare's fixed 20 s.

## Result

17 subjects. net is negative on 16 and positive on 1 (`fsm`, +0.031). Median −0.107, mean −0.171, Wilcoxon signed-rank p = 4.6e-05. The verdict is the same with undecided directions read as implications or as non-implications (p = 4.6e-05 both ways). Per-subject figures are in `subjects.csv`; every comparable cross pair is in `relations.csv`.

## Excluded subjects

- `valu3s-uc6`: 1,364 queried directions timed out and none was decided as an implication before the run was stopped, so no frontier can be built. Log in `logs/av2/run-valu3s.log`.
- `lpc-full-core1`: a sampled estimate (`scripts/sample_pooled.py`) was tried on av2. Of 2,308 directions, 2,130 timed out and 4 were implications; every sampled frontier row was undecided against the other arm. Stopped after 9 rows (`logs/av2/`).
- `fsm-lmcps`: uniform returned no repairs.

## Caveats

- `rad-core-33` has 355 undecided directions, `lpc-mini-core1` 162; both are inside the sensitivity reading.
- A timed-out direction within an arm reads as no implication when building the frontier, as in `maximal`, so a frontier can hold a class another member would dominate.
- The two excluded heavy subjects are the specifications with the largest pools; the result says nothing about them.
