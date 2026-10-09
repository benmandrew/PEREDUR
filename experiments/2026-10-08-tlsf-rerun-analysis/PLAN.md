# 2026-10-08-tlsf-rerun-analysis

Downstream passes 3 and 4 of `2026-10-07-tlsf-rerun`'s PLAN: pooled TLSF strength (selection, grading, and PEREDUR against AuRUS), the TLSF ball radii with their permutation passes, the size sweep, and GR(1) coverage against GLASS, JVTS-Repair and AMT13. Each pass reproduces its archived method, and only the PEREDUR input changes: the rerun's frontiers (`results-tlsf-rerun`, membership from `curves-tlsf-rerun`, 65,536-word prints from `separation-recount-tlsf-rerun-s0`).

## Inputs and planning

All three inputs were collected on 2026-10-09: 3000 runs, of which 2920 have a frontier. The 80 without one are AuRUS-graded runs that accumulated no repair (lily16, humanoid-503, prioritized-arbiter, full-arbiter, pcar-v2-888); the planners skip them as `members.py` does.

`scripts/plan.py` runs locally after collect and writes `experiments/analysis-tlsf-rerun-work/`. It packs the tree the hosts read into `data/{strength,rq3,coverage}.tar.xz`, with digests in `data/bundles.json`. The build step runs `scripts/unpack.py`, which checks each digest and extracts the bundle once per digest. It never touches a phase's outputs. The bundled `prints.tsv` keeps the first 4096 words of each print. The host reads it only to drop fallback pairs that a word refutes both ways, which stays sound on fewer words. The full prints would make a 1.5 GB bundle.

- **strength**: `pooled_frontier.py plan` over sides `mrs-nsga2-apportion`, `mrs-weighted` and `aurus-nsga2-apportion`, with comparisons selection and grading, on all 25 families. That makes 97,102 classes, dealt as whole families: av1 takes humanoid-742 plus 4 small families, av2 takes humanoid-531 plus 7, and av3 takes the other 12.
- **rq3**: PEREDUR's `mrs-nsga2-apportion` pool against AuRUS's archived pooled frontier. `scripts/aurus_frontier.py` rebuilds that frontier from the archived RQ3 verdicts (provenance/pooled-rq3) into `data/aurus-frontier.csv`; its per-family sizes equal the archived front_B. Of 3,627,686 class pairs, 3,428,043 are refuted both ways by one of the 65,536 words, which leaves 199,643 to compare. Each pair runs on the host that holds its AuRUS body: 98,944 on av2 and 100,699 on av3.
- **coverage**: `maoz_score.py`'s pool pass over the rerun covers 9276 frontier rows and 9169 distinct repairs. They are compared against the 55 archived tool frontier representatives (provenance/maoz-coverage), 39,021 pairs in all, dealt round-robin with 13,007 per host.

## Phases and cost

The campaign declares four phases, and each host runs them in order.

| Phase | Kind | Hosts | Work | Estimate per host | Basis |
|---|---|---|---|---|---|
| coverage | compare, jobs 16, 20 s / 120 s | av1, av2, av3 | 13,007 pairs | about 10 min | archive: 27,220 compare-seconds for 39,034 pairs |
| rq3-cross | compare, jobs 16, 300 s / 700 s | av2, av3 | about 100k pairs | about 10 min | 120 sampled unrefuted PEREDUR pairs, 6 per rq3 family, run locally: 0.02-0.13 s each, about 3 core-hours in all; the archived all-pairs RQ3, 2.16M pairs in about 40 min on 3 hosts, agrees |
| strength | frontier, 4 workers x 4 solver jobs, 300 s | av1, av2, av3 | 5 / 8 / 12 families | 1-3 h, not measured | `maximal` is not run locally (it has OOMed this box). A family's pooled walk is about one per-run maximality walk; the scoring phase did 500 of those in 2.2 h on av1 at 8 workers. The humanoid-742 and humanoid-531 walks set the tail. The archived all-pairs pooled-rq2, about 10 h on 3 hosts, is an upper bound |
| strength-fallback | compare, jobs 16, 300 s / 700 s | av1, av2, av3 | undecided walk pairs only | minutes, or nothing | the list holds only a header where every walk decided |

## Collect and reduce

```sh
python3 scripts/campaign.py collect --outputs analysis-tlsf-rerun
python3 scripts/campaign.py collect --outputs analysis-tlsf-rerun-work
systemd-run --user --scope -q -p MemoryMax=8G -p MemorySwapMax=0 choom -n 1000 -- \
    nice -n 19 sh experiments/2026-10-08-tlsf-rerun-analysis/scripts/post_collect.sh
```

`post_collect.sh` takes the following steps:

- It files each host's `strength/<spec>/result.json` beside the local plan, splits the fallback results back per family, and runs `pooled_frontier.py score`.
- It runs `score_rq3.py`, which applies the archived measure frontier against frontier. PEREDUR's frontier is the strength walk's; AuRUS's is the archived one.
- It rebuilds `relations.csv` and runs `maoz_score.py --pass report`.
- It runs `tlsf_prc.py` at K = 1, 3 and 5 (MATCH=1, XSEED=1), and `perm_null.py` at R = 1000 at the cap and at `CUT=equal`. These feed `perm_report.py`, `direction_ctrl.py`, `hl_ctrl.py` and `effect_sizes.py`.
- It runs the n = 20 size sweep.

The ball radii and the size sweep read only the collected sidecars, so `STEPS="ball size"` runs them now.

## Vendored scripts

| Script | Source | Changes |
|---|---|---|
| `tlsf_prc.py`, `tlsf_prc_report.py`, `perm_null.py`, `direction_ctrl.py`, `stats.py` | repair-diversity worktree at 22ab586, the committed blobs | none |
| `perm_null_dyn.py`, `perm_report.py`, `hl_ctrl.py`, `effect_sizes.py`, `refp.sh`, `union_by_n.py` | the same directory, working copies that are untracked there | `effect_sizes.py` imports the paper's `tables.py` from `rerun-2026-09-uncensored`, because `rematch-2026-09` is gone |
| `size_sweep.sh` | blob d8c6bd0 at 22ab586 | PEREDUR is read from av1, av2 and av3; AuRUS from `AURUS_SIDECARS`; it runs 8 workers instead of 10 |
| `maoz_score.py` | blob 8682503 at provenance/maoz-coverage | `REPO_ROOT` points three levels up |

The paper's `tables.py` is read, never written.

## Deviations from the archived methods

- **Strength** is built frontier against frontier by a running antichain walk, where the archive compared all pairs. By transitivity the measure is the same.
- **RQ3's cross** is PEREDUR's pool against AuRUS's archived frontier, with prints refuting the clearly incomparable pairs. The AuRUS-against-AuRUS verdicts are the archive's.
- **humanoid-742** stays out of RQ3, as it was in the archive. humanoid-503, humanoid-531, full-arbiter and prioritized-arbiter still have no screened AuRUS run. RQ2, selection and grading, now covers every family, including the humanoids that the archive sampled.
- **Coverage** runs every pair alone at the archive's single-pair budget, 20 s for black and 120 s of wall time. The archive first ran chunks of 50 under 900 s.
- **The `humanoid-458` substitution is unresolved.** The archived `cap_merged.csv` replaced humanoid-458's rows with a 2^20-word pass (`perm_null_dyn.py`). The AuRUS side of that pass is on av2 and av3 only (`~/tlsf-dyn/out`), so this pass reports humanoid-458 at 65,536 words. To match the archive, fetch those prints, re-fingerprint the rerun's humanoid-458 with `refp.sh` at 1,048,576 words, and run `perm_null_dyn.py` on that family.
- **Local tools.** On this box, `build-release/third_party/black/install/bin/black` cannot load `libblack.so`. `compare_pairs.py` prefers that path when it exists, so the local cost sample set `PEREDUR_BLACK_PATH` to the build tree's `black`. The hosts use their own working builds.
