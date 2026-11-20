# PSP

The PSP (Dwyer's Property Specification Patterns) front end. `PLAN.md` on `feat/psp-front-end` fixes the language (D3) and the lowering (D4). This file gains the rest of its sections in phase P9.

## Measured orders

`scripts/psp_field_orders.py` measures every order below from the lowering and Spot 2.15.1, with black as a second solver, at counts 1 to 4. It writes `docs/dev/psp-orders.json` and exits 1 when a check fails. The lowering lives in the script until P1 ports it to `src/psp/lowering.cpp`. Atoms: P is the trigger or forbidden proposition, S the target, Q the scope opener and R the scope closer. Dwyer's single proposition is S in universality, existence and bounded recurrence, and P in absence and bounded existence.

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
