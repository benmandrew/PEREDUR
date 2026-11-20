# PSP

The front end for Dwyer's *Property Specification Patterns* (PSP), the third specification language beside FRETISH and TLSF. A PSP requirement is a pattern under a scope, and its lowering is a body per pattern inside a wrapper per scope. `PLAN.md` on `feat/psp-front-end` (worktree `~/projects/peredur-psp`) records the decisions: the language (D3), the lowering (D4), the fields (D5) and the file format (D6). This file describes what was built from them, and the measurements and proofs behind it.

## Language

A requirement has a pattern, a scope, and up to four propositional formula fields (`include/psp/requirement.hpp`). P is the trigger or forbidden proposition, S the target, Q the scope's opener and R its closer. Each field is present exactly where the pattern or the scope reads it. Q and R are formula fields with polarities of their own, not mode atoms as on FRETISH.

The nine patterns are Dwyer's absence, universality, existence, precedence and response, Dwyer's bounded existence ("P becomes true at most k times"), and Konrad and Cheng's bounded response, bounded invariance and bounded recurrence ("S holds at least every c ticks"). `Pattern` is a `std::variant`, as `timing::Timing` is. Each bounded pattern carries a count: k stretches of P for bounded existence, and c ticks for the other three. A count is at least 1 and at most 64 (`psp::k_max_count`). The lowering expands a count into an `X` chain, quadratically for bounded invariance under a boundary, so a larger count is refused at load. Count 0 is refused too, because there distinct patterns coincide (bounded response 0 and bounded invariance 0 both say `G(P -> S)`).

The five scopes are Dwyer's: globally, before R, after Q, between Q and R, and after Q until R (`PspScopeKind`). `psp::Requirement::to_string` prints a requirement in Konrad and Cheng's *structured English* ("After start until stop, it is always the case that if req holds, then grant holds after at most 1 time units.").

A requirement can instead be a `fixed` raw-LTL formula (`m_fixed`). A fixed requirement is never weakenable, and its LTL is its whole lowering. It exists so that the non-pattern lines of an imported SYNTECH17 specification survive the import (see "Corpus").

### What the language leaves out

Three groups of patterns are out, by the user's decision of 2026-11-20 (PLAN.md D3).

- **The chain patterns** (chain precedence and chain response). They have three or four propositions and nested eventualities, which do not factor into one body over a boundary, so the body-and-wrapper lowering and its orders do not apply to them.
- **Minimum and maximum duration.** They exist only in metric temporal logic (MTL), they read P on its rising edge, and no order was measured for them. The research report notes that they could bring joint steps (implications that need two fields to move at once), which the one-field search cannot reach.
- **The probabilistic patterns** of the Autili et al. catalogue. Their semantics needs probabilistic model checking, which no tool in the pipeline does.

## Lowering

`requirement_to_ltl` (`src/psp/lowering.cpp`) lowers a requirement as a body over an optional boundary B, inside a wrapper per scope. Globally and after Q read the body with no boundary. Before R, between Q and R, and after Q until R read it with B = R. The body table, with the unbounded form on the left:

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

The wrappers are those of the research report (`docs/research/psp-suitability.md` in the paper repository, section 3):

| Scope | Wrapper over body b |
|---|---|
| globally | `b(none)` |
| before R | `F R -> b(R)` |
| after Q | `!Q W (Q & b(none))` |
| between Q and R | `G((Q & !R & F R) -> b(R))` |
| after Q until R | `G((Q & !R) -> b(R))` |

Bounded operators are expanded into `X` chains, and every operand of an `X` is parenthesised, which black needs. Atoms are prefixed with `k_atom_prefix` (`iap_`) at load and stripped on output, as on FRETISH.

Three conventions fix the semantics where the literature leaves room.

- **Scopes are closed on the left and open on the right**, as Dwyer defines them. The point where B holds is outside the scope. Every bounded body follows this convention, so that the orders keep their edges. Bounded invariance under a boundary asks for S only up to the point before B, so universality (`S W B`) implies it.
- **After Q opens at the first Q.** The wrapper `!Q W (Q & b(none))` reads the body once, from the first point where Q holds. This follows Dwyer's own definition of the scope and departs from the published precedence-after-Q row (below).
- **Bounded response under a boundary is the strict form**, "S within n and before R" (`!R U[0,n] (S & !R)`), which is Konrad and Cheng's. The research prototype's `F[0,n](S & !B)` let R come between P and S, and so lost `bounded response n => response` at n of 2 or more.

Bounded existence follows the Dwyer web table's alternation of `!P` and `P` stretches, but takes the `Q & !R` guards of the other rows. Bounded recurrence follows "S holds at least every c ticks" ("Bounded recurrence: the body kept" under "Measured orders").

### Against the Dwyer table

`test_psp_lowering_matches_the_dwyer_table` and `test_psp_bounded_existence_matches_the_dwyer_table` (`test/psp/lowering_tests.cpp`) check the lowering against the 30 rows of the Dwyer web table that `docs/dev/psp-orders.json` records under `dwyer`. 29 are equivalent and 1 is pinned as strictly stronger. Of the 25 base rows, 24 are equivalent. The five published bounded-existence rows (k = 2) are each equivalent too, although they keep the old guards (`Q & F R`, or `Q` alone, with no `!R`). The missing `!R` makes no difference, because a body that starts where R holds is true at once.

### Precedence after Q

The published row for precedence after Q is `G(!q) | F(q & (!p W s))`. Its `F(q & ...)` lets *any* Q open the scope, but Dwyer defines after Q as the interval from the first Q. The row is therefore weaker than its own scope definition. The lowering, `!q W (q & (!p W s))`, opens at the first Q, and `ltlfilt` finds it strictly stronger than the row.

The trace `q; p; q; cycle{∅}` separates the two (`counter_trace` in `docs/dev/psp-orders.json`). P occurs after the first Q with no S before it, so the lowering rejects the trace. The row picks the second Q, after which P never occurs, and accepts it. `test_psp_lowering_matches_the_dwyer_table` pins the row as strictly weaker with this counter-trace. For the other four base patterns, reading the body from the first Q or from any Q gives equivalent formulas, so their after-Q rows agree with the lowering.

## File format

A `.psp` file is JSON with the top-level shape of the FRETISH format: `assumptions`, `guarantees`, `in_atoms` and `out_atoms`. `schemas/psp-specification.schema.json` describes it. Each requirement is either a pattern requirement or `{"fixed": "<LTL>"}`. A pattern requirement has these keys:

| Key | Value |
|---|---|
| `pattern` | `absence`, `universality`, `existence`, `precedence`, `response`, `bounded-response`, `bounded-invariance`, `bounded-existence` or `bounded-recurrence` |
| `count` | an integer from 1 to 64, present exactly on the four bounded patterns |
| `scope` | `globally` (the default when absent), `before`, `after`, `between` or `afteruntil` |
| `p` | read by absence, precedence, response, bounded response, bounded invariance and bounded existence |
| `s` | read by every pattern but absence and bounded existence |
| `q` | read by after, between and after-until |
| `r` | read by before, between and after-until |
| `weakenable` | `false` locks the requirement against every edit; absent means `true` |

A formula is propositional, over the declared atoms, with `! & | -> <->` and parentheses. A key that the pattern or scope does not read is an error, as is a missing one that it does. `examples/psp-arbiter/spec.psp` is a small example.

`parse_specification` and `load_specification` (`src/psp/serialisation.cpp`) enforce the schema. They also reject an atom declared in neither list, and screen atom names as every driver does (`src/runner/atom_names.hpp`). `write_specification` writes the format back with the prefix stripped. `SpecFormat<psp::Specification>` and `selects_psp` in `src/spec_format.hpp` route `ltl`, `compare`, `maximal` and `lint-ideals` to it for a `.psp` input. `realize` has its own `.psp` branch. `peredur` reads a `.psp` input by its extension or under `--format psp`, and writes `repair_N.psp` with `repair_N.fitness.json`. `mucs` does not read PSP.

## Measured orders

`scripts/psp_field_orders.py` measures every order below from the lowering and Spot 2.15.1, with black as a second solver, at counts 1 to 4. It writes `docs/dev/psp-orders.json` and exits 1 when a check fails. `src/psp/lowering.cpp` ports the script's lowering, and the `psp_lowering` suite checks the two equivalent at every pattern, scope and count from 1 to 4, reading the `lowering` table of `docs/dev/psp-orders.json` as its fixture; `scripts/test_psp_field_orders.py` checks that table still equals what the script lowers. Dwyer's single proposition is S in universality, existence and bounded recurrence, and P in absence and bounded existence.

The measured set has 21 patterns (the five unbounded ones, and the four bounded ones at counts 1 to 4) under 5 scopes, so 105 nodes.

**Pattern order.** The five scopes fall into three classes, each with its own pattern order (`pattern_order_classes`). The JSON lists every edge and the covering edges, which are the moves the pattern mutator makes.

| Class | Scopes | Edges | Covering edges |
|---|---|---|---|
| unbounded | globally, after | 99 | 24 |
| bounded | between, after-until | 81 | 20 |
| before | before | 80 | 19 |

**Scope order.** The 21 patterns fall into four cells, each with its own scope order (`scope_order_cells`). An edge `a => b` means the requirement under scope a implies the same requirement under scope b.

| Cell | Patterns | Edges |
|---|---|---|
| invariant | absence, universality, bounded invariance, bounded existence, bounded recurrence | globally ⇒ after ⇒ after-until ⇒ between, after ⇒ between, globally ⇒ before, globally ⇒ after-until, globally ⇒ between |
| existence | existence | after-until ⇒ between |
| precedence | precedence | after-until ⇒ between, globally ⇒ before |
| response | response, bounded response | after-until ⇒ between, globally ⇒ after |

**Polarity.** Each formula field is monotone (+, a weaker field gives a weaker requirement), antitone (−) or has no polarity (·) in each pattern and scope (`polarity`). A bounded pattern has the same polarities at every count from 1 to 4.

| Pattern | globally | before | after | between | after-until |
|---|---|---|---|---|---|
| absence | P− | P− R· | P− Q− | P− Q− R· | P− Q− R+ |
| universality | S+ | S+ R· | S+ Q− | S+ Q− R· | S+ Q− R+ |
| existence | S+ | S+ R− | S+ Q· | S+ Q− R· | S+ Q− R· |
| precedence | P− S+ | P− S+ R· | P− S+ Q· | P− S+ Q− R· | P− S+ Q− R+ |
| response | P− S+ | P− S+ R· | P− S+ Q− | P− S+ Q− R· | P− S+ Q− R· |
| bounded response | P− S+ | P− S+ R· | P− S+ Q− | P− S+ Q− R· | P− S+ Q− R· |
| bounded invariance | P− S+ | P− S+ R· | P− S+ Q− | P− S+ Q− R· | P− S+ Q− R+ |
| bounded existence | P· | P· R· | P· Q− | P· Q− R· | P· Q− R+ |
| bounded recurrence | S+ | S+ R· | S+ Q− | S+ Q− R· | S+ Q− R+ |

**The report reproduced.** With the prototype's bounded bodies (bounded response `F[0,n](S & !B)`, bounded invariance `G[0,n] S | (B R S)`) at counts 1 to 3, the script reproduces section 4 of the report: three pattern-order classes with 41, 36 and 35 edges, four scope-order cells, and 251 implications that change both fields, none of them a joint step.

**What the D4 bodies change.**

- *Pattern order, on the report's patterns.* There are two classes, not three: {globally, after, between, after-until} and {before}. The strict bounded response keeps `bounded response n => response` under a boundary, and the right-open bounded invariance keeps `universality => bounded invariance n`. Those are exactly the 5 edges (at counts 1 to 3) that the prototype lost under between and after-until. Before stays alone, because there `universality => existence` fails: R at the first point leaves the scope empty.
- *Pattern order, on all nine patterns.* There are three classes again, for a different reason: {globally, after} (99 edges), {between, after-until} (81) and {before} (80). Bounded recurrence alone makes the split. With no boundary, `bounded recurrence c` implies `bounded response n` for n ≥ c, `response` and `existence`. Under a boundary, B discharges the recurrence, so none of these 18 edges holds.
- *Scope order.* There are still four cells. Bounded existence and bounded recurrence join the cell of absence, universality and bounded invariance, and bounded response stays with response.
- *Polarity.* On the report's patterns, no cell differs from the report. In bounded existence, P has no polarity in any cell, since the count of P stretches is not monotone in P. Its Q and R behave as in absence: Q is antitone wherever it occurs, and R is monotone under after-until and has no polarity under before and between. In bounded recurrence, S is monotone in every cell, and Q and R behave as in bounded existence.
- *Joint steps.* There are none. Among the 105 nodes (pattern with count, scope) there are 534 implications that change both fields, and each one is reached through a single intermediate node.
- *Count chains.* Under every scope, each bounded pattern is a strict chain in its count from 1 to 4: bounded response, bounded existence and bounded recurrence weaken as the count grows, and bounded invariance strengthens. Under every scope, `bounded response n => response`, `universality => bounded invariance n`, `absence => bounded existence k` and `universality => bounded recurrence c` hold. `bounded recurrence c => existence` holds only under globally and after.

**Bounded recurrence: the body kept.** The script also measures a strict body, `(!B U[0,c] (S & !B)) W B`. It keeps the 18 edges under a boundary, so the pattern orders fall back to two classes, and its count chain is also strict. But the strict body forces S at the last point of every scope that B closes: at the point just before B, "S within c and before B" can only be met by S itself. The D4 body bounds every S-free stretch inside the scope by c, the final stretch included, which is what "S holds at least every c ticks" says. D4's body therefore stays, and its cost is the third class. The strict body is not lowered anywhere.

**Second solver.** black agrees with ltlfilt on all 16,612 queries: it proves 387, refutes 14,280, and finds no counter-model up to k = 10 for the other 1,945, which ltlfilt proves. That needs a 120 s time limit per query, the script's default. At the 10 s that `field_orders.py` uses, black left 56 bounded-existence queries at count 4 undecided, and it called two valid ones satisfiable: after-until ⇒ between for bounded existence 4, which holds because the two wrappers differ only by a guard, and the monotone polarity of R there. At 100 s, black finds no counter-model for the first of these.

## Implication rule

`psp::requirement_implies` (`src/psp/implication.cpp`) proves one requirement implies another with no solver, as the FRETISH `requirement_implies` does. It moves one of the six fields (the pattern with its count, the scope, P, S, Q and R) at a time, and every order of the steps is tried (64 states). The pattern moves along `pattern_implies` under the current scope's class, the scope along `scope_implies` in the current pattern's cell, and each formula along its polarity in the current cell (`field_polarity`), decided by `formula_implies`. No step moves two fields, since the measurement found no joint step. A field that one side does not read takes the other side's value, so it never needs a step of its own: each measured edge holds over atoms the two sides need not share, and so under any substitution of formulas for them. The rule extends each count relation from the measured counts 1 to 4 to every count, and the Lean proofs show that extension sound (see "Lean proofs").

A fixed raw-LTL requirement implies another only where the two lower identically. `formula_implies` decides only propositional formulas, so it cannot compare two LTL formulas, and an LTL check would launch a process. `spec_implies_by_rule` and `residual_destination` lift the rule slot by slot as on FRETISH: guarantees covariant, assumptions contravariant, an identical lowering anywhere in the pool or the rule on the entry in the same slot, and a tombstone read as `true`. `psp::spec_implies` (`include/psp/filter.hpp`) keeps the exact query and shortens it with `residual_destination`, as the FRETISH `spec_implies` does, so the rule changes its cost and never its verdict. `OrderedLanguage<psp::Specification>` forwards to these functions.

The `psp_implication` suite re-derives each table with `ltlfilt` at counts 1 to 4, non-edges included: the pattern order under every scope (2,100 pairs), the scope order for every pattern (420), and the polarity of each field in each cell, which must also equal `docs/dev/psp-orders.json`. A further test checks the pattern order at counts 5 and 7 under globally, before and between. `psp_pattern_order_table` and `psp_scope_order_table`, built with the tests, print the two orders for the Lean cross-check.

`test_psp_requirement_implies_is_sound` sends a sample of pairs up to two fields apart to SPOT, and fails on any unsound or missed pair. The opt-in suite `peredur_tests psp_requirement_implies_sweep` widens the pools: 17 patterns (the bounded ones at counts 1 to 3), every scope, and four values for each formula field, one of them an unrelated atom. It pairs every 41st requirement with every requirement up to three fields apart, a field that neither side reads not counting. Of 168,362 pairs SPOT finds 7,881 implications, and the rule proves all of them and no others: 957 of 957 at one field apart (3,654 pairs), 2,499 of 2,499 at two (30,718), and 4,425 of 4,425 at three (133,990). The suite asserts no unsound and no missed pair, and takes about a minute on the debug build with four ltlfilt runs at once. `PEREDUR_PSP_SWEEP_STRIDE=7` widens the sample to 997,675 pairs: SPOT finds 35,365 implications, 0 unsound and 0 missed, with 12,754 of 12,754 at two fields apart and 17,106 of 17,106 at three.

The unrelated atoms of Q and R must differ. With one atom `w` for both, `q & !r` never holds, so a between or after-until requirement with Q = R is vacuous, and SPOT finds implications into it from every requirement that no field order reaches. That first run of the sweep missed 6,062 such pairs and found no unsound one. The vacuous requirement is the result of the gutting move (see "Gutting").

## Operators

`OrderedLanguage<psp::Specification>` (`include/psp/ordered_language.hpp`) gives climb, two-way mutation, maximality removal, novelty and blame the same surface they read on FRETISH ("Ordered-language traits" in `docs/dev/operators.md`). A step moves one of six fields (`psp::RequirementField`: the pattern with its count, the scope, P, S, Q and R), and a bisected step crosses the count chain of one bounded pattern. `steps_towards` (`src/psp/order_moves.cpp`) moves a guarantee towards its join with the original's slot and an assumption towards its meet, restores a tombstoned guarantee and drops an appended assumption. The join and meet bound the pattern and the scope along their orders and combine each formula by its polarity (disjunction where the field is monotone and the bound is the join), and the result is kept only where `requirement_implies` proves it.

The per-slot mutator (`psp::mutate_requirement`, `src/psp/mutation.cpp`) has seven arms, each behind a `[psp]` key read before its draw, so an arm at 0 draws nothing: `p_pattern` moves the pattern one covering edge of the order of its scope's class (the bounded patterns at the slot's count and the counts donated from the specification), `p_scope_kind` moves the scope one covering edge in its pattern's cell, `p_count` halves or doubles a count or moves it to a donated one, and `p_p`, `p_s`, `p_q` and `p_r` rewrite a formula field. A rewrite follows the field's polarity under `mutation.p_monotone`, through `monotone_rewrite`, and is free (`mutate_formula`) where the field has none or `p_monotone` declines. A field that a pattern or scope move makes the requirement read takes a formula donated from the specification. Crossover (`psp::crossover_specifications`) crosses one field of one slot pair: the pattern with its count, the count alone, the scope, or a formula through the FRETISH subformula graft.

The structural actions (add an assumption, tombstone a guarantee) and the whole-specification mutation, with two-way mutation in every `gate_realizable` mode and blame narrowing, are one template (`mutate_specification_impl` in `src/genetic/mutation.cpp`) that FRETISH and PSP both instantiate. An added PSP assumption is a globally response over the inputs.

The objectives are those of FRETISH over the lowering: syntactic similarity is the mean of the keyword token similarity and the formula similarity of each field that either side reads, semantic similarity is the mean of the model-count similarity of each changed slot's lowering and `keyword_similarity` (pattern and scope downset Jaccard), and status is the MRS walk over the live guarantees, each subset checked by `ltlsynt` over `to_ltl()` with the well-separation test.

### Gutting

A rewrite of Q or R can empty a scope, after which the requirement holds vacuously. This is the *gutting move* of the research report (section 5). `psp::empties_scope` (`src/psp/order_moves.cpp`) detects four cases: R valid under before, Q unsatisfiable under after, and Q implying R under between and after-until. A gutted guarantee asks nothing of the system, so a search that may weaken freely can reach realisability by gutting every conflicting guarantee.

The search does not forbid the move, because emptying a scope is a weakening like any other and a repair may need it. It flags the move instead, as FRETISH flags its output modes. `repair_N.fitness.json` carries `gutted`, the guarantee slots that the repair emptied and the original did not (`gutted_guarantees`). The run prints a warning naming them, and `run.json` counts the gutted repairs under `psp.gutted_repairs`. `psp_empty_scope_is_detected` and `psp_gutted_repair_is_flagged_not_refused` (`test/psp/operators_tests.cpp`) cover both halves. The seeded corpus leaves out every fault that empties a scope (see "Corpus").

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

## Corpus

The PSP subjects come from three sources, and `scripts/run_experiments.py` registers each in its own table: `PSP_SPECS` (hand-written), `PSP_SYNTECH_SPECS` (SYNTECH17) and `PSP_SEEDED_SPECS` (seeded). All three join `SPECS`. The 5 hand-written, 9 SYNTECH17 and 70 seeded subjects make 84 unrealisable subjects. The campaign runs only the SYNTECH17 imports with 2 or more weakenable guarantee slots (`PSP_SYNTECH_EVAL`, from the `eval_candidate` column of `manifest.csv`), which is 1 of the 9, so it runs 76 subjects. An *event-delimited* slot below is one under before, after, between or after-until, the scopes FRETISH lacks.

### Hand-written subjects

Five specifications under `examples/psp-*`, each unrealisable with a known repair under `fixes/`. The four after `psp-arbiter` come from the case studies of the PSP literature (`docs/research/psp/psp-corpora.md` in the paper repository), and they use the event-delimited scopes on purpose.

| Subject | Subject matter | Event-delimited guarantees | Known repair |
|---|---|---|---|
| `psp-arbiter` | two bounded responses that conflict under one scope | 2 of 2 | `grant-follows-request.psp` |
| `psp-cruise` | automotive cruise control (Konrad and Cheng) | 3 of 4 | `close-on-fault.psp`: the scope closes on `brake \| fault` |
| `psp-crossing` | railway level crossing | 3 of 4 | `exclusion-before-fault.psp`: the exclusion holds only before the fault |
| `psp-patrol` | robot mission (Menghi et al.) | 2 of 4 | `base-until-next-mission.psp`: the base stay also ends at the next start |
| `psp-minepump` | mine pump | 2 of 3 | `respond-before-methane.psp`: the response holds only before methane |

`compare` finds the cruise, crossing and patrol specifications strictly stronger than their fixes. The arbiter's and the minepump's fixes are incomparable with the specification, so `lint-ideals` fails their weakening column by design.

### SYNTECH17

`scripts/import_spectra.py` converts the Spectra specifications of SYNTECH17 (`SpectraSynthesizer/spectra-specs`, read at commit `aa9ddb4`) into `.psp` files under `examples/psp-syntech/specs/`, with one row per file in `examples/psp-syntech/manifest.csv`. Its docstring gives the Spectra subset it reads. The essentials:

- SYNTECH17 calls three of the Dwyer library's patterns, and each becomes a weakenable PSP requirement: `pRespondsToS(p, s)` a globally response, `pHolds_afterQuntilR(p, q, r)` an after-until universality, and `pBecomesTrue_betweenQandR(p, q, r)` a between existence. Every other `asm` and `gar` line becomes a fixed entry, as does a pattern call whose arguments use `next`.
- Enumerations and bounded integers are bit-blasted with a log encoding, and a fixed entry restricts a code whose domain is not a power of two. `env` variables are inputs. `sys`, `aux` and monitor variables are outputs, and a monitor's constraints become fixed guarantees.
- Validation lowers each specification to `(A) -> (G)` and asks `ltlsynt --realizability --decompose=no` with a 300 s limit. `--decompose=no` avoids Spot issue #651, which answers unrealisable for some realisable specifications with a `G F` assumption. A specification is kept only when its verdict matches the Spectra folder it came from (`all_realizable` or `all_unrealizable`).

Of 149 files (123 realisable, 26 unrealisable), 60 are imported: 51 realisable and 9 unrealisable. The other 89 are excluded, and the manifest gives each reason.

| Reason | Files |
|---|---|
| `ltlsynt` timed out at 300 s | 78 |
| verdict differs from Spectra | 6 |
| uses `SINCE` | 4 |
| unknown predicate | 1 |

Of the 6 mismatches, 5 are unrealisable in Spectra and realisable here. Four of those have an assumption that reads a monitor (`monitor_in_assumptions`), so the system can break the monitor's guarantees and falsify the assumption. The fifth, a SimpleVehicle, has no reason found. The sixth, a realisable Elevator, comes out unrealisable, because its monitor constraints contradict each other between floors.

The imports are thin on weakenable material. Only 1 of the 9 unrealisable imports (`SimpleVehicle_VehicleSpec_31`) has 2 or more weakenable guarantee slots (`eval_candidate` in the manifest), and none has an event-delimited slot. All 9 are registered all the same, with no known repair. The seeded corpus therefore carries the evaluation of the scopes, and the 51 realisable imports serve as its bases.

### Seeded corpus

`psp::seeded_edits` (`src/psp/seeding.cpp`) lists the single-slot faults of a realisable specification, and `sample-mutants --psp-seeded` writes them. A fault is a step up the pattern, scope or count order, a literal added to a formula field against its polarity, or a sideways move: an incomparable pattern or scope, swapped delimiters, a flipped literal or a swapped signal. Edits that make a requirement vacuous are left out: an emptied scope, a donated delimiter that shares an atom with P or S, and a literal over a delimiter's atom. With these exclusions, SPOT finds 2 of the 116 faults on the cruise fix equivalent to their slot or weaker. Without them it found 34 of 176.

`experiments/2026-11-20-psp-seeded-corpus/scripts/seed.py` filters the faults as the FRETISH sideways corpus does and writes `examples/psp-seeded/`, which is tracked because `experiments/` keeps no data. A mutant is kept when `compare` finds it strictly stronger than its base or incomparable with it, it is unrealisable, each of its live guarantees is realisable alone, and the conjunction of its requirements is satisfiable. The base is then a known repair. The bases are the five hand-written fixes and the realisable SYNTECH17 imports (`examples/psp-seeded/bases.tsv`), with at most 40 candidates drawn from each SYNTECH17 base.

`mutants.csv` gives the stage at which each of 572 candidates stopped. 153 are kept (121 from the hand-written bases, 32 from SYNTECH17), and `selected.txt` holds at most 8 per base, 70 in all: 39 from the hand-written bases and 31 from SYNTECH17. Of the 70, 47 are sideways and 23 strictly stronger, and 42 edit a guarantee that the base states under an event-delimited scope. `examples/psp-seeded/subjects/` holds every kept mutant, each with its base under `fixes/` and the edit in `seed.json`.

Each subject differs from its base in one edit, so one mutation can revert it. The FRETISH corpus measures that with a trivial-reversion probe, and this corpus has none yet; PLAN.md moves the probe to P8.

## Lean proofs

`proofs/Psp/` (11 files, 1,972 lines) models the lowering and proves the tables, in core Lean 4 with no Mathlib. The PSP section of `proofs/README.md` lists every theorem, and `scripts/check_lean_psp_lowering.py` and `scripts/check_lean_psp_orders.py` tie the Lean transcriptions to the C++ and to `docs/dev/psp-orders.json`, each with a control that must fail.

What is proved, with P, S, Q and R arbitrary predicates on positions:

- The pattern order of each scope class and the scope order of each cell are exact at every count from 1 up (`pattern_exact`, `scope_exact`), and the C++ `pattern_implies` and `scope_implies` equal them (`pattern_implies_exact`, `scope_implies_exact`). The rule's extension of the count relations past 4 is therefore proved, not assumed.
- `field_polarity` is sound at every count (`polarity_sound`).
- The six-field chain search is sound, given correct answers from `formula_implies` (`reach_sound`).
- The lowering built as LTL syntax means what the model says (`eval_lowerSyn`), and `check_lean_psp_lowering.py` finds it equivalent to `ltl` on all 105 requirements at counts 1 to 4.

What is not proved:

- *No joint step.* The one-field search rests on the measurement of 534 cross implications at counts 1 to 4. A joint step would cost only completeness, since `reach_sound` holds whatever the search misses.
- *Exact polarity.* Where `field_polarity` answers no polarity, the proofs do not show that the field has none. The check script compares the table with SPOT at counts 1 to 4.
- The tie of `lowerSyn` to the C++ above count 4, which rests on the two expanding bounded operators with the same recursion.

## Out of scope

- The chain, duration and probabilistic patterns ("What the language leaves out").
- Temporal formulas in P, S, Q and R. The fields are propositional, and a SYNTECH17 pattern call that uses `next` becomes a fixed entry.
- Weakening a fixed entry, and comparing two fixed entries other than by identical lowering.
- `mucs` on a `.psp` input.
- The Spectra constructs the importer refuses (`SINCE`, `TRIGGERED`, `ONCE`, `HISTORICALLY`, counters, `trig`, `regexp`, local patterns and other library patterns), and SYNTECH17 specifications that `ltlsynt` does not decide in 300 s.
- A proof that no joint step exists, and exactness of the polarity table.
