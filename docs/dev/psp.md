# PSP

The PSP (Dwyer's Property Specification Patterns) front end. `PLAN.md` on `feat/psp-front-end` fixes the language (D3) and the lowering (D4). This file gains the rest of its sections in phase P9.

## Measured orders

`scripts/psp_field_orders.py` measures every order below from the lowering and Spot 2.15.1, with black as a second solver, at counts 1 to 4. It writes `docs/dev/psp-orders.json` and exits 1 when a check fails. `src/psp/lowering.cpp` ports the script's lowering, and the `psp_lowering` suite checks the two equivalent at every pattern, scope and count from 1 to 4, reading the `lowering` table of `docs/dev/psp-orders.json` as its fixture; `scripts/test_psp_field_orders.py` checks that table still equals what the script lowers. Atoms: P is the trigger or forbidden proposition, S the target, Q the scope opener and R the scope closer. Dwyer's single proposition is S in universality, existence and bounded recurrence, and P in absence and bounded existence.

| Pattern | No boundary | Boundary B |
|---|---|---|
| absence | `G !P` | `!P W B` |
| universality | `G S` | `S W B` |
| existence | `F S` | `!B U (S & !B)` |
| precedence | `!P W S` | `!P W (S \| B)` |
| response | `G(P -> F S)` | `(P -> (!B U (S & !B))) W B` |
| bounded response n | `G(P -> F[0,n] S)` | `(P -> !B U[0,n] (S & !B)) W B` |
| bounded invariance n | `G(P -> G[0,n] S)` | `(P -> AND_{j<=n} (X^j S \| OR_{i<=j} X^i B)) W B` |
| bounded existence k | `E_k`, with `E_0 = G !P`, `E_k = !P W (P W E_{k-1})` | `E_k`, with `E_0 = !P W B`, `E_k = !P W (B \| (P W (B \| E_{k-1})))` |
| bounded recurrence c | `G F[0,c] S` | `(F[0,c] (S \| B)) W B` |

The wrappers are those of the report: `b(none)` for globally, `F R -> b(R)` for before, `!Q W (Q & b(none))` for after (the first Q), `G((Q & !R & F R) -> b(R))` for between, and `G((Q & !R) -> b(R))` for after-until. Bounded operators are expanded into `X` chains.

**Against the Dwyer table.** 24 of the 25 base rows are equivalent to the lowering. Precedence after Q is strictly stronger than its row, and `ltlfilt --accept-word` shows that the row accepts `q; p; q; cycle{∅}` and the lowering rejects it. The five published bounded-existence rows (k = 2) are each equivalent to the lowering, although they keep the old guards (`Q & F R`, or `Q` alone, with no `!R`). The missing `!R` makes no difference, because a body that starts where R holds is true at once. P1 can therefore pin all five as equivalent.

**The report reproduced.** With the prototype's bounded bodies (bounded response `F[0,n](S & !B)`, bounded invariance `G[0,n] S | (B R S)`) at counts 1 to 3, the script reproduces section 4 of the report: three pattern-order classes with 41, 36 and 35 edges, four scope-order cells, and 251 implications that change both fields, none of them a joint step.

**What the D4 bodies change.**

- *Pattern order, on the report's patterns.* There are two classes, not three: {globally, after, between, after-until} and {before}. The strict bounded response keeps `bounded response n => response` under a boundary, and the right-open bounded invariance keeps `universality => bounded invariance n`. Those are exactly the 5 edges (at counts 1 to 3) that the prototype lost under between and after-until. Before stays alone, because there `universality => existence` fails: R at the first point leaves the scope empty.
- *Pattern order, on all nine patterns.* There are three classes again, for a different reason: {globally, after} (99 edges), {between, after-until} (81) and {before} (80). Bounded recurrence alone makes the split. With no boundary, `bounded recurrence c` implies `bounded response n` for n ≥ c, `response` and `existence`. Under a boundary, B discharges the recurrence, so none of these 18 edges holds.
- *Scope order.* There are still four cells. Bounded existence and bounded recurrence join the cell of absence, universality and bounded invariance, and bounded response stays with response.
- *Polarity.* On the report's patterns, no cell differs from the report. In bounded existence, P has no polarity in any cell, since the count of P stretches is not monotone in P. Its Q and R behave as in absence: Q is antitone wherever it occurs, and R is monotone under after-until and has no polarity under before and between. In bounded recurrence, S is monotone in every cell, and Q and R behave as in bounded existence.
- *Joint steps.* There are none. Among the 105 nodes (pattern with count, scope) there are 534 implications that change both fields, and each one is reached through a single intermediate node.
- *Count chains.* Under every scope, each bounded pattern is a strict chain in its count from 1 to 4: bounded response, bounded existence and bounded recurrence weaken as the count grows, and bounded invariance strengthens. Under every scope, `bounded response n => response`, `universality => bounded invariance n`, `absence => bounded existence k` and `universality => bounded recurrence c` hold. `bounded recurrence c => existence` holds only under globally and after.

**Bounded recurrence: the body kept.** The script also measures a strict body, `(!B U[0,c] (S & !B)) W B`. It keeps the 18 edges under a boundary, so the pattern orders fall back to two classes, and its count chain is also strict. But the strict body forces S at the last point of every scope that B closes: at the point just before B, "S within c and before B" can only be met by S itself. The D4 body bounds every S-free stretch inside the scope by c, the final stretch included, which is what "P holds at least every c ticks" says. D4's body therefore stays, and its cost is the third class. The strict body is not lowered anywhere.

**Second solver.** black agrees with ltlfilt on all 16,612 queries: it proves 387, refutes 14,280, and finds no counter-model up to k = 10 for the other 1,945, which ltlfilt proves. That needs a 120 s time limit per query, the script's default. At the 10 s that `field_orders.py` uses, black left 56 bounded-existence queries at count 4 undecided, and it called two valid ones satisfiable: after-until ⇒ between for bounded existence 4, which holds because the two wrappers differ only by a guard, and the monotone polarity of R there. At 100 s, black finds no counter-model for the first of these.

## Implication rule

`psp::requirement_implies` (`src/psp/implication.cpp`) proves one requirement implies another with no solver, as the FRETISH `requirement_implies` does. It moves one of the six fields (the pattern with its count, the scope, P, S, Q and R) at a time, and every order of the steps is tried (64 states). The pattern moves along `pattern_implies` under the current scope's class, the scope along `scope_implies` in the current pattern's cell, and each formula along its polarity in the current cell (`field_polarity`), decided by `formula_implies`. No step moves two fields, since the measurement found no joint step. A field that one side does not read takes the other side's value, so it never needs a step of its own: each measured edge holds over atoms the two sides need not share, and so under any substitution of formulas for them. The rule extends each count relation from the measured counts 1 to 4 to every count.

A fixed raw-LTL requirement implies another only where the two lower identically. `formula_implies` decides only propositional formulas, so it cannot compare two LTL formulas, and an LTL check would launch a process. `spec_implies_by_rule` and `residual_destination` lift the rule slot by slot as on FRETISH: guarantees covariant, assumptions contravariant, an identical lowering anywhere in the pool or the rule on the entry in the same slot, and a tombstone read as `true`. `psp::spec_implies` (`include/psp/filter.hpp`) keeps the exact query and shortens it with `residual_destination`, as the FRETISH `spec_implies` does, so the rule changes its cost and never its verdict. These are the functions `OrderedLanguage<psp::Specification>` forwards to in P4.

The `psp_implication` suite re-derives each table with `ltlfilt` at counts 1 to 4, non-edges included: the pattern order under every scope (2,100 pairs), the scope order for every pattern (420), and the polarity of each field in each cell, which must also equal `docs/dev/psp-orders.json`. A further test checks the pattern order at counts 5 and 7 under globally, before and between. `psp_pattern_order_table` and `psp_scope_order_table`, built with the tests, print the two orders for the Lean cross-check in P6.

The Lean proofs in `proofs/Psp/` show the pattern and scope orders exact at every count from 1 up, `field_polarity` sound, the search sound given correct `formula_implies` answers, and the lowering's syntax equal to its model. `scripts/check_lean_psp_lowering.py` and `scripts/check_lean_psp_orders.py` tie them to the C++. The PSP section of `proofs/README.md` lists the theorems and what is not proved, including the absence of joint steps, which rests on the measurement alone.

`test_psp_requirement_implies_is_sound` sends a sample of pairs up to two fields apart to SPOT, and fails on any unsound or missed pair. The opt-in suite `peredur_tests psp_requirement_implies_sweep` widens the pools: 17 patterns (the bounded ones at counts 1 to 3), every scope, and four values for each formula field, one of them an unrelated atom. It pairs every 41st requirement with every requirement up to three fields apart, a field that neither side reads not counting. Of 168,362 pairs SPOT finds 7,881 implications, and the rule proves all of them and no others: 957 of 957 at one field apart (3,654 pairs), 2,499 of 2,499 at two (30,718), and 4,425 of 4,425 at three (133,990). The suite asserts no unsound and no missed pair, and takes about a minute on the debug build with four ltlfilt runs at once. `PEREDUR_PSP_SWEEP_STRIDE=7` widens the sample to 997,675 pairs: SPOT finds 35,365 implications, 0 unsound and 0 missed, with 12,754 of 12,754 at two fields apart and 17,106 of 17,106 at three.

The unrelated atoms of Q and R must differ. With one atom `w` for both, `q & !r` never holds, so a between or after-until requirement with Q = R is vacuous, and SPOT finds implications into it from every requirement that no field order reaches. That first run of the sweep missed 6,062 such pairs and found no unsound one. A rewrite of Q or R that empties a scope in this way is the gutting move P4 flags.

## Operators

`OrderedLanguage<psp::Specification>` (`include/psp/ordered_language.hpp`) gives climb, two-way mutation, maximality removal, novelty and blame the same surface they read on FRETISH. A step moves one of six fields (`psp::RequirementField`: the pattern with its count, the scope, P, S, Q and R), and a bisected step crosses the count chain of one bounded pattern. `steps_towards` (`src/psp/order_moves.cpp`) moves a guarantee towards its join with the original's slot and an assumption towards its meet, restores a tombstoned guarantee and drops an appended assumption. The join and meet bound the pattern and the scope along their orders and combine each formula by its polarity (disjunction where the field is monotone and the bound is the join), and the result is kept only where `requirement_implies` proves it.

The per-slot mutator (`psp::mutate_requirement`, `src/psp/mutation.cpp`) has seven arms, each behind a `[psp]` key read before its draw, so an arm at 0 draws nothing: `p_pattern` moves the pattern one covering edge of the order of its scope's class (the bounded patterns at the slot's count and the counts donated from the specification), `p_scope_kind` moves the scope one covering edge in its pattern's cell, `p_count` halves or doubles a count or moves it to a donated one, and `p_p`, `p_s`, `p_q` and `p_r` rewrite a formula field. A rewrite follows the field's polarity under `mutation.p_monotone`, through `monotone_rewrite`, and is free (`mutate_formula`) where the field has none or `p_monotone` declines. A field that a pattern or scope move makes the requirement read takes a formula donated from the specification. Crossover (`psp::crossover_specifications`) crosses one field of one slot pair: the pattern with its count, the count alone, the scope, or a formula through the FRETISH subformula graft.

The structural actions (add an assumption, tombstone a guarantee) and the whole-specification mutation, with two-way mutation in every `gate_realizable` mode and blame narrowing, are one template (`mutate_specification_impl` in `src/genetic/mutation.cpp`) that FRETISH and PSP both instantiate. An added PSP assumption is a globally response over the inputs.

The objectives are those of FRETISH over the lowering: syntactic similarity is the mean of the keyword token similarity and the formula similarity of each field that either side reads, semantic similarity is the mean of the model-count similarity of each changed slot's lowering and `keyword_similarity` (pattern and scope downset Jaccard), and status is the MRS walk over the live guarantees, each subset checked by `ltlsynt` over `to_ltl()` with the well-separation test.

**Gutting.** A rewrite of Q or R can empty a scope (R valid under before, Q unsatisfiable under after, Q implying R under between and after-until), after which the requirement holds vacuously. The search does not forbid it. `repair_N.fitness.json` carries `gutted`, the guarantee slots that the repair emptied and the original did not, the run prints a warning naming them, and `run.json` counts the gutted repairs under `psp.gutted_repairs`.

## Mechanisms on PSP

Each arm of sweep F (`base`, `full` and every `no-<f>`, with blame on in every arm and two-way mutation in exchange mode) ran to completion on `examples/psp-arbiter` with seed 7 on a release build, in 50 s to 75 s. The table lists the `run.json` counter that shows each mechanism acting, and its value in `full` and in the arm that turns the mechanism off.

| Mechanism | Counter in `run.json` | `full` | Arm without it |
| --- | --- | --- | --- |
| climb | `climb.steps_accepted` (of `climb.steps_tried`) | 14 (of 39) | 0 in `no-climb` |
| two-way mutation (exchange) | `direction_gate.steps`, `direction_gate.forbidden` | 108, 25 | 0, 0 in `no-gate` |
| maximality removal | `n_maximality_removals` | 175 | 0 in `no-remove` |
| novelty | `novelty.implied`, `novelty.covered` | 299, 2699 | 0, 0 in `no-novelty` |
| blame narrowing | `n_targeted_mutations` | 607 | not ablated (1092 in `base`) |

The `psp` block counts the PSP arms: in `full`, 112 pattern moves, 93 scope moves, 109 count moves, 303 monotone and 959 free rewrites, and 176 crossovers that changed a parent. No repair in any arm is gutted. `donated_fields` is 0 in every arm, because on this example the pattern and scope moves stay among patterns and scopes that read the same fields; `psp_mutator_donates_the_fields_a_move_newly_reads` covers donation. Climb runs on the output, so `no-climb` searches exactly as `full` does and differs only in the repairs climbed afterwards.

`full` writes 68 repairs. `compare` against `fixes/grant-follows-request.psp` finds `repair_1.psp` strictly stronger than the known repair and none equivalent, so the known repair is found in the sense of the evaluation (some repair implies it), not as an exact output.

## Seeded corpus

`psp::seeded_edits` (`src/psp/seeding.cpp`) lists the single-slot faults of a realisable specification, and `sample-mutants --psp-seeded` writes them. A fault is a step up the pattern, scope or count order, a literal added to a formula field against its polarity, or a sideways move: an incomparable pattern or scope, swapped delimiters, a flipped literal or a swapped signal. Edits that make a requirement vacuous are left out: an emptied scope, a donated delimiter that shares an atom with P or S, and a literal over a delimiter's atom. With these exclusions, SPOT finds 2 of the 116 faults on the cruise fix equivalent to their slot or weaker. Without them it found 34 of 176. `experiments/2026-11-20-psp-seeded-corpus/scripts/seed.py` filters the faults as the FRETISH sideways corpus does and writes `examples/psp-seeded/`.
