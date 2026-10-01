# Separation and maximality curves for the un-censored AuRUS arm: 708 of 708 runs scored, 48 maximality curves partial

`2026-09-22-aurus-rerun` re-ran the AuRUS baseline at the fork `e1cfadf`, which keeps the solutions of a run killed at the cap. Its `aurus` phase left 708 scorable run directories under `experiments/results-aurus-rerun` on the two hosts, 354 a host, holding 296,300 candidate files. The 72 runs with no solution were skipped by name. This campaign scored those 708 runs with two `kind = "score"` phases at the budgets `2026-09-14-paper-rerun-curves` gave both of its arms. `2026-09-22-aurus-rerun/PLAN.md` section 8 registers that reuse.

The PEREDUR side was not re-scored. Its 3000 curves are `2026-09-14-paper-rerun-curves`', scored at `0cfd14a`, and `git diff --stat 0cfd14a 3cd6efb` lists one changed file, this campaign's `campaign.toml`. The `fingerprint`, `maximal` and `compare` binaries here are therefore source-identical to the ones that scored PEREDUR, and every score manifest records them at `3cd6efb` with `dirty = 0`. The output directories carry an `-uncensored` suffix, because `curves-aurus-rerun` and `separation-aurus-rerun` on both hosts already hold the earlier pass over the censored archive.

The campaign ran from `campaign/aurus-rerun-curves` at `3cd6efb` through queue entries av2 034 and av3 032. Two earlier entries at the same commit, av2 032 and av3 030, are recorded as cancelled with no attempt. av2 scored repeats 0 to 14 and av3 15 to 29, each the repeats it had run.

## Separation

The *behavioural-separation* pass greedily builds an epsilon-net over each run's solutions, one *fingerprint* bit per sampled lasso word, and makes no solver call. It ran at epsilon 0.05, 0.2 and 0.5, 256 fingerprint words, fingerprint seed 0 and 20 cuts, with `compare_timeout` 600 s, `deadline_s` 900 s and `wall_cap_s` 1200 s, on 8 workers a host at 1 core each. Those values match `2026-09-14-paper-rerun-curves` exactly, since a fingerprint compares only against one drawn from identical sampling.

It queued 354 runs a host and scored 354, with 0 failed and empty `failures.txt` files. `timings.txt` sums to 17 s of worker time on av2 and 16 s on av3. av2 ran from 12:07:05 to 12:07:18 on 2026-09-23 and av3 from 11:57:07 to 11:57:19. No `warnings.log` line is a warning.

## Maximality

The maximality pass walks a running antichain once a run and checks each candidate against the ideals with `compare`. It ran at 20 cuts with `maximal_timeout` 900 s, `compare_timeout` 3000 s, `deadline_s` 7500 s and `wall_cap_s` 8400 s, on 8 workers a host at 4 cores each. av2 ran from 12:10:01 to 20:36:16 and av3 from 12:00:01 to 19:51:35 on 2026-09-23. Each host queued 354 runs and scored 354, with 0 failed and empty `failures.txt` files.

| host | worker-hours | median run | longest run | partial curves |
|---|---|---|---|---|
| av2 | 56.45 | 7 s | 7500 s | 25 |
| av3 | 55.42 | 6 s | 7499 s | 23 |

The pass cost 111.9 worker-hours, against 67.1 for the pass over the 596 archived AuRUS runs. Every run that took over 300 s is one of the 30 `humanoid-741` or 30 `humanoid-742` runs. 48 curves are *partial*, the walk having hit its 7500 s deadline and kept the 61 to 546 rows it had written: all 30 `humanoid-742` runs and 18 of the `humanoid-741` runs. The family attribution comes from `timings.txt`, because `warnings.log` interleaves eight workers and its run headers do not bind the lines that follow. The earlier pass left 30 of its 596 AuRUS curves partial.

`warnings.log` also records `no ideals directory` 30 times, 15 a host, all for `examples/humanoid-741/fixes`. That family has no ideal, so its curves carry solution and maximal metrics and no ideal ones.

## Rows

| file | rows | curves | av2 rows | av3 rows | `humanoid-741` rows |
|---|---|---|---|---|---|
| `separation-aurus-uncensored.csv` | 339,875 | 708 | 170,393 | 169,482 | 7,357 |
| `curves-aurus-uncensored.csv` | 328,856 | 708 | 164,713 | 164,143 | 6,299 |

Both were collected on 2026-09-24 with `campaign.py collect --curves`, and neither holds an identical duplicate row. The joined files stay untracked in the `campaign/aurus-rerun-anytime` worktree.

## Reading these against PEREDUR

Three rules apply before these files meet the PEREDUR curves. First, `humanoid-741` comes out of both files, leaving 678 curves over the 25 endpoint families. Second, the 72 runs with no solution go back in as flat zeros, for a denominator of 750. Third, cap status comes from the search campaign's `killed` column. A curve exists for 118 of the 175 killed runs, and its `run_wall_s` reads 7500.0 or 7500.01 s, the 7200 s budget plus the harness's 300 s grace, so a reader that infers a cap from a missing curve, as the archive's consumers did, gets it wrong.

The paper's staging applies all three. After it, the AuRUS curve covers 750 runs against the archive's 678, its mean solution count at the cap is 387.9 against 423.3, and its mean maximal antichain is 35.6 against 38.6. Both means fell because the denominator grew by runs that hold little: the six capped families hold 5,908 of the 296,300 candidates.

Every ideal count is a lower bound, since an implication that times out reads as a non-implication. These curves add no decision of their own. They put the AuRUS side on the same scorer, budgets and clock resolution as the PEREDUR side it is drawn beside.
