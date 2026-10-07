# Ball-radii precision on the TLSF frontiers

Fingerprint precision and coverage between the paper's TLSF frontiers, the TLSF counterpart of the FRETISH `fp_prc.py` pass. Inputs are the separation-recount sidecars (`*.members.tsv`, `*.fingerprints.tsv`): rematch from av2 and av3, and AuRUS from `separation-recount-aurus-s0` on both hosts. Runs were on av3 in `~/tlsf-prc` on 2026-10-05.

```sh
K=3 python3 tlsf_prc.py out.csv rematch/av2 rematch/av3 aurus/av2 aurus/av3
python3 tlsf_prc_report.py out.csv
```

`MATCH=1` subsamples both cross pairs and both same-arm controls of a seed block (2j, 2j+1) to the smallest of the four frontiers.

| File | Run |
|---|---|
| `out/k{1,3,5}.csv`, `out/report_k{1,3,5}.txt` | unmatched, K = 1, 3, 5 |
| `out/match_k{1,3,5}.csv`, `out/report_match_k{1,3,5}.txt` | size-matched, K = 1, 3, 5 |

Cross pairs compare two configurations at the same seed, while controls compare seeds 2j and 2j+1 of one configuration. Two PEREDUR configurations at the same seed share an RNG stream, so their cross figure can exceed the control for that reason alone; the MRS-vs-AuRUS-grading row does so at every K, matched or not.

## FRETISH

`fretish/fp_prc.py` is the same measure on the fretish-mixed frontiers: directed, uniform and mixed mutation, seeds 0–29, on 15 FRETISH subjects. Inputs are the per-subject fingerprint files (`<subject>.tsv`, one row per member: run path and hex fingerprint), one directory per host. Runs were on av3 in `~/fret-prc` on 2026-10-05.

```sh
DIST=jaccard K=3 MATCH=0 python3 fp_prc.py j3m.csv av2/*.tsv av3/*.tsv
python3 fp_prc_report.py j3m.csv
```

A `triple` unit holds the three arms at one seed. An `xtriple` unit draws each arm from a different seed of one control block, so no cross pair shares an RNG stream. `MATCH=<seed>` subsamples each unit to its smallest group. `DIST=jaccard` is the union distance, and the default is the Hamming count.

| File | Run |
|---|---|
| `fretish/out/j{1,3,5}m.csv`, `fretish/out/report_j{1,3,5}m.txt` | Jaccard, size-matched, K = 1, 3, 5 |
| `fretish/out/j3.csv`, `fretish/out/report_j3.txt` | Jaccard, unmatched, K = 3 |
| `fretish/out/h3m.csv`, `fretish/out/report_h3m.txt` | Hamming, size-matched, K = 3 |

At K = 3, matched and cross-seed, directed and uniform overlap by 0.607 against a same-arm control of 0.613 (p = 0.45). Coverage puts the cross figure below the control, 0.584 against 0.638 (p = 0.003). Directed's repairs sit inside uniform's region more often than the reverse: 0.680 against 0.534, with uniform-in-directed larger on 3 of 15 subjects (p = 0.010). Mixed reads like uniform. Same-seed triples raise the cross figures by about 0.02. The asymmetry and the coverage gap hold at K = 1 and 5 and under the Hamming count. Precision's cross figure matches the control at K = 1 and under Hamming, but falls below it at K = 5 (0.688 against 0.708, p = 0.049). `fsm-lmcps` has 7 fingerprints and fills no unit. The permutation null and the P-precision pass have not been run on these frontiers.

## Size sweep

`size_sweep.sh` reruns the screened PEREDUR-against-AuRUS pass (`K=3 MATCH=1 XSEED=1`, 2 h cap) with `SIZE=20`, which subsamples every frontier of a block to exactly 20 members and skips blocks whose smallest frontier holds fewer. Räisä et al. (2025) find that k-NN precision does not converge with set size, so the sweep checks that the verdict does not depend on how many repairs each frontier holds. Larger sizes have no data: the smallest frontier of any block is at most 63 repairs (`round-robin-arbiter-aurus`), only that family has a block of 50, and none has one of 100.

```sh
./size_sweep.sh ~/projects/counter-wt-fingerprint/experiments/separation-recount-rematch-s0 <work-dir>
```

Inputs are the rematch sidecars of `separation-recount-rematch-s0` and the screened AuRUS sidecars in `out/ws/aurus/`. It ran locally on 2026-10-07 in 26 s on 10 workers. The outputs are `out/size20/size20.csv` and `out/size20/report_size20.txt`, reproduced byte for byte by a second run.

Over the 14 families that enter, PEREDUR's region holds 0.836 of AuRUS's repairs and AuRUS's holds 0.629 of PEREDUR's, larger on 11 of 14 (p = 0.0134). The cross figure, 0.732, is below both controls (0.820 and 0.833). The screened headline run (`out/ws/screened.csv`) restricted to the same 14 families gives 0.825 against 0.668, 12 of 14, p = 0.0052, with the cross figure 0.746 against 0.802 and 0.838. The verdict is unchanged. Coverage loses significance at this size (0.627 against 0.573, p = 0.24).
