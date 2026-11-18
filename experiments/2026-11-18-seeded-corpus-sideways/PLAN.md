# 2026-11-18-seeded-corpus-sideways

Draft, not registered. The sideways classes are `swap-signal`, `flip-literal` and `reverse-implication`, approved and checked for reachability. `swap-timing-kind` was dropped.

## Question

The seeded corpus of `2026-11-13-seeded-corpus` builds every mutant O by strengthening a realisable base B, so B always lies below O and recovering it always needs weakening. The order-directed mechanisms weaken an unrealisable parent, so that corpus suits them by construction (`paper/evaluation.tex`, construct threat on the narrow fault model). This campaign widens the fault model. Its question is whether the per-feature verdicts on the seeded corpus hold once some faults are sideways edits, which neither strengthen nor weaken the requirement they change.

## Design

Everything follows `2026-11-13-seeded-corpus` except the fault classes and the admission rule.

- **Bases.** The same bases (`bases.csv`) and the same single-requirement screen.
- **Fault classes.** The draw table of `scripts/seeded/seed.py` gains the sideways operators approved from the candidates below. Each edit of a compound seed of 2 to 4 still draws its class uniformly from one table, which then holds the 9 strengthening guarantee edits and the new operators, with `drop-assumption` at its current 0.1. There is no separate stratum. With the k = 3 sideways classes, the share of mutants with no sideways edit is (9/12)^n for n edits, ignoring `drop-assumption`: 0.56 at n = 2 and 0.32 at n = 4.
- **Admission.** O must be satisfiable and unrealisable, every live guarantee of O must be realisable alone, and `compare` must find O `strictly stronger than` B or `incomparable` with it. The old rule kept only `strictly stronger than`. By closure under weakening an unrealisable O cannot be weaker than B, so these two relations are the only ones left, and the rule drops only `equivalent` and timeouts.
- **Relation column.** `mutants.csv` records each mutant's relation to B, so the analysis can say how much of the corpus needs a sideways move. It is a column, not a stratum.
- **Same-slot edits.** `seed_once` draws each edit's slot independently, so two edits can land on one guarantee. The rule is kept unchanged, so the only difference from the old corpus is the fault classes.
- **Recovery.** Unchanged: B is O's only ideal, and `implies_ideal` counts repairs R with B ⊑ R.
- **Probe and selection.** The trivial-reversion probe and the headline threshold (p1 and p2 both below 0.01) run unchanged, with the same cluster caps.
- **Arms.** Profile `fretish-seeded-sideways`, at one commit (`campaign/fretish-extension-share` at `e664c376` plus this campaign's scripts). From sweep D: `lean-remove` (the paper's `full`), `lean` (`no-remove`), `remove-no-climb`, `remove-no-gate`, `remove-no-blame`, `only-remove` and `base`. From sweep O: `only-climb`, `only-gate` and `only-blame`. All under `nsga2-apportion` at `--parallel 2`, seeds 0-4, the old budget. There is no `random` arm, since the paper reports none on the seeded corpus. Spec names carry the prefix `sw-`, because 56 of the 130 subject ids repeat the old corpus's.

## Candidate operators

Each operator models a plausible requirement error and is in general neither a strengthening nor a weakening. `compare` decides each compound mutant's relation, so no single edit is trusted. *Undo* names the engine move that reverses the edit, from `src/genetic/mutation.cpp` at `77a12dc9`. A free rewrite is admitted on an unrealisable parent unless it is proved stronger, since `p_sideways` defaults to 1.0.

| Operator | Edit | Fault it models | Undo in the engine | Applies to |
|---|---|---|---|---|
| `swap-signal` | One atom in a condition or response replaced by another atom of the same kind (input for input, output for output) that occurs in B | Wrong signal named | One free rewrite: atom rename from the spec's atom pool | Every guarantee |
| `flip-literal` | One literal in a condition or response negated, or its negation removed | Wrong polarity | One free rewrite: add or remove a negation | Every guarantee |
| `reverse-implication` | A response `p -> q` written as `q -> p` | Implication read backwards | No single move; two renames or a subtree rewrite, to be measured | Responses with an implication |
| `swap-timing-kind` | `within k` written as `for k`, or the reverse | Deadline and duration confused | No sideways timing move; directed steps through a common point, if one exists | 285 of 1,264 base slots |
| `swap-mode` | The mode of a scope replaced by another mode of the spec (`in m` to `in m'`) | Wrong mode named | No direct move; a scope edge keeps its mode, so the path goes through global scope | 41 of 1,264 base slots |
| `swap-scope-kind` | `before m` written as `after m`, or `in m` as `notin m` | Scope relation confused | Directed scope edges only, if a path exists | 41 of 1,264 base slots |

The last two rarely apply, because only 41 base slots name a mode.

## Reachability check

This runs after approval, on the approved operators only. For each operator, sample single edits on the bases. For each edit, measure whether `mutate_specification` under the `base` and `full` configs can return B, the unedited requirement, from the edited one within 1, 2 and 3 mutations, with the corpus probe's machinery (`sample-mutants`, `scripts/seeded/probe.py`). An operator with zero reversions in 3 mutations over its sample is unreachable. It is dropped, or the engine gains a move first, by the user's choice. Recovery failures on it would otherwise measure the operator set rather than direction. The check reports, for each operator, the reversion rate at each depth and the share of edits never reversed.

### Result, 18 November

The user approved `swap-signal`, `flip-literal`, `reverse-implication` and `swap-timing-kind`. They are in `seed.py` as `SIDEWAYS_EDITS`, behind `--sideways`, which also admits `incomparable`. `reach.py` ran 2 edits for each base on the 112 kept bases with 3 to 20 guarantees, 2,000 chains of depth 3 for each edit, at `3ba397fe` (the scratch build). `full` is `lean-remove` with an unrealisable parent at status 0.5. A chain reverts the edit when the edited guarantee equals B's, with formulas compared by truth table. The run took 8 min 51 s at 8 jobs.

| Operator | Edits | Reverted by depth 1 / 2 / 3, `base` | `full` | Reverting chains at depth 1, `base` |
|---|---|---|---|---|
| `swap-signal` | 150 | 49% / 61% / 64% | 47% / 57% / 60% | 0.23% |
| `flip-literal` | 151 | 95% / 97% / 97% | 96% / 97% / 97% | 0.62% |
| `reverse-implication` | 54 | 17% / 20% / 22% | 17% / 19% / 22% | 0.01% |
| `swap-timing-kind` | 40 | 0% / 0% / 0% | 0% / 0% / 0% | 0 |

`base` and `full` agree within 4 points, so the gate does not block these undos. `swap-signal` and `flip-literal` are each undone by one move (an atom rename, or adding or removing a negation). The edits never reverted in 6,000 chains are those where the right slot, occurrence and atom together are rarer than about 1 in 2,000 draws. `reverse-implication` is undone in one move only where the rewrite happens to land on the original, and otherwise needs two or more moves. That covers 22% of its edits by depth 3.

`swap-timing-kind` is not a sideways edit. `for k p` implies `within k p`, and `compare` confirms it on `fsm-combined__add-not-limits-and-not-standby`: `within 5` to `for 5` is strictly stronger, and `for 5` to `within 5` strictly weaker. The engine has no move between the two kinds. The path goes through `immediately`: `for 5`, `for 2`, `for 1`, `immediately`, `within 1`, `within 2`, `within 4`, `within 5`, which is 7 directed steps on one slot. Its zero reversion rate at depth 3 measures path length, so the operator is reachable by a run but not by a short chain. Its `within`-to-`for` half is a timing strengthening, like `lengthen-for`, and its `for`-to-`within` half is a weakening.

## Cost

The seeded results now in the paper, the removal controls and the 18 Nov exchange arm stay valid as results on the strengthening-only corpus. This campaign runs 130 subjects x 10 arms x 5 seeds = 6,500 runs. `fretish-seeded-remove`'s 3,000 runs averaged 50 s each at 2 threads. On 16 CPUs (8 jobs) per host, av2's seeds 0-2 take about 6.8 h and av3's seeds 3-4 about 4.5 h.

## Log

- 2026-11-18 (morning). Draft written. Worktree `.claude/worktrees/seeded-corpus-sideways` on `campaign/seeded-corpus-sideways`, off `77a12dc9`.
- 2026-11-18. Operators approved: swap-signal, flip-literal, reverse-implication, swap-timing-kind. Added to `seed.py` behind `--sideways`. Reachability check run (`reach.py`, result above). swap-timing-kind is a strengthening or a weakening, so it is not sideways; awaiting the user's decision on it.
- 2026-11-18. `swap-timing-kind` dropped at the user's decision and removed from `seed.py` and `reach.py`. Its row in the result table and `reach.csv` stays as the record.
- 2026-11-18 15:22. Generated with `seed.py --sideways` (both passes of the old corpus, same seeds and options), at `349acc85` plus a fix to `relation()`: compare prints `o.json : incomparable` without the ideal's name, which the old regex read as `error`. The old corpus's 286 `error` rows are most likely incomparable pairs, which its rule dropped anyway. 1,710 candidates, 247 kept: 169 `incomparable` with B and 78 `strictly stronger than`, and 183 of the 247 hold a sideways edit. The main pass kept 246 of 1,140 and the extra pass 1 of 570.
- 2026-11-18 15:24–18:09. Probe (`probe.py`, `base` config, 5,000 draws, depth 2) on all 247, local, under a 16 GB scope. It ran at 6 jobs, then 12 (slower: 1.7 against 2.1 subjects a minute, from contention), then 6 again, on cores 0–9, and for the last part of the run on cores 0–3 at the user's request. `probe.py` now writes `probe-work/<subject>/row.json` and reuses it, so a restart keeps finished subjects. 233 subjects are headline (p1 and p2 below 0.01).
- 2026-11-18 18:09. `choose.py` (cap 40, 10 trivial): 130 subjects, 120 headline and 10 trivial, in `selected.txt`. Headline by cluster: fsm 40, lmcps 40, liquid-mixer 16, rad 15, mode-arbiter 8, valu3s 1. Of the 130, 85 are incomparable with B and 45 strictly stronger, and 95 hold a sideways edit. The old corpus selected 120 (110 headline).
- 2026-11-18 21:30. Reachability script moved to `scripts/reach.py`. `reach.csv` stays untracked, and its numbers are in the table above. The branch was rebuilt on `campaign/fretish-extension-share` (`e664c376`), which holds the removal arms, and the earlier commits are kept locally as `backup/seeded-corpus-sideways-77a12dc9`. `scripts/seeded/` is identical on the two bases. Hosts were agreed with the other sessions: veribee-7a holds av2 CPUs 16-31 and av3 CPUs 0-15 until about 23:30, so this campaign takes av2 0-15 and av3 16-31, each under 24G, for its whole run. peredur-22, peredur-extension-6b and peredur-extension-7b need nothing on either host tonight.
