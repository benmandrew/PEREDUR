# Pooled-frontier strength, PEREDUR against AuRUS (RQ3)

A measurement pass run by hand on av1, av2 and av3 on 2026-10-06, outside `scripts/campaign.py`. It compares the strength of PEREDUR's default configuration (`mrs-nsga2-apportion`) and AuRUS under the implication order, family by family, in the pooled design the paper's PLAN.md set at decdc10. It gives the paper's `\QAurSub` and `\QAurSubP`.

## Design

For each family and side, the pool is every member of every run's last-cut frontier over the 30 seeds. PEREDUR's runs are the rematch sidecars (`counter-wt-fingerprint/experiments/separation-recount-rematch-s0/av{2,3}`). AuRUS's are the well-separation screened sidecars (`../ball-radii/out/ws/aurus/av{2,3}`, from the 2026-08-14-aurus-h2h verdicts). `members.py` lists them in `members.csv`.

Members whose bodies match once comments and whitespace are removed (`hash.py`, run on av2 and av3 as `~/subsum/pooled-hash.py`, same md5) form one class, which may hold members of both sides. `plan_pooled.py` lists every pair of classes in a family that survives the fingerprint prefilter, except pairs whose classes both come from one run alone, since a run's frontier is an antichain. It deals them round-robin to the three hosts in a seeded shuffle and writes `plan/classes.csv`.

`score_pooled.py` merges classes that compare as equivalent into groups. A side's pooled frontier is its groups that no other group of that side strictly implies. sub(A by B) is the share of A's frontier strictly implied by some group of B, and net = sub(B by A) - sub(A by B), positive when PEREDUR's repairs are the stronger. A pair absent from the results reads as incomparable. The test is the exact Wilcoxon signed-rank over families.

## Run

`plan/chain-pooled-<host>.sh` waited for the humanoid-742 per-run split on each host to exit, then ran `../run.py` over the host's third of the pairs:

| Host | Started | Finished | Pairs |
|---|---|---|---|
| av1 | 20:14:52 | 20:56:31 | 721,199 |
| av2 | 20:13:52 | 20:54:02 | 721,308 |
| av3 | 20:11:53 | 20:47:15 | 720,892 |

Every call ran a `compare` built from e04317d: `~/subsum/bin/compare` on av2 and av3, and on av1 `~/subsum/av1/bin/compare`, a wrapper around a Nix-library bundle of the same build. The budgets are `run.py`'s: `compare --timeout 300` per solver call, 700 s and `ulimit -v 8000000` per pair, 28 jobs (30 on av1).

The pass decided all 2,163,399 pairs: 1,863,226 incomparable, 149,584 stronger, 142,868 weaker and 7,721 equivalent. None was undecided.

## Result

`report-pooled.txt`, from `score_pooled.py plan results/results-pooled20-*.csv`. Over 20 families, PEREDUR's repairs are the stronger on 12 and AuRUS's on 8. The median net is +0.079 and the mean +0.053, exact Wilcoxon p = 0.2455. Both readings of an undecided pair agree, since there are none. Per-family figures are in `families-pooled.csv`.

## Excluded families

- `humanoid-742`: not run. Its plan held 199,983 pairs at about 21 s each.
- The other four of the 25 RQ3 families have no screened AuRUS run, so `members.py` has no AuRUS side for them.

## Files

The scripts, `members.csv`, `plan/classes.csv`, the three host scripts, `families-pooled.csv` and `report-pooled.txt` are tracked. The pair lists (`plan/pairs-<host>.csv`, 195 MB each) and the results stay untracked:

| File | Rows | md5 |
|---|---|---|
| `results/results-pooled20-av1.csv` | 721,199 | `8d41eafe7973f6375f5b45146d2dd8ca` |
| `results/results-pooled20-av2.csv` | 721,308 | `0d1f315deeb61094c3aa91ecb8800d34` |
| `results/results-pooled20-av3.csv` | 720,892 | `dea7ca394b7ddcfc8fba4af716343beb` |

`members.py`, `plan_pooled.py` and `score_pooled.py` import `../plan.py` and `../../ball-radii/tlsf_prc.py`. `../plan.py` and `../run.py` are tracked with this pass. `tlsf_prc.py` is tracked at 4bd4b2d. The copy that ran carried uncommitted edits (XSEED, CUT and an optional cut argument to `frontier()`), which these scripts do not use. counter's `experiments/2026-10-06-pooled-rq2/scripts/tlsf_prc.py` on `campaign/pooled-rq2` holds that copy.
