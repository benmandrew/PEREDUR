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
