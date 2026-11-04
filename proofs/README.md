# FRETish timing proofs

Lean 4 proofs that the timing order `RuleSearch` uses holds for every tick count. `scripts/field_orders.py` checks the same order against SPOT, but only for tick counts up to 3. The proofs now give the exact timing orders for every tick count, every edge and every non-edge, and they use core Lean with `omega` and no Mathlib.

## Building

```sh
nix develop '.#proofs'
cd proofs
lake build
```

The `proofs` shell is separate from the default one because `lean4` is a 650 MB download on x86_64-linux, and on aarch64-darwin it has no binary cache entry and builds from source. `lean-toolchain` pins the same release as the flake's nixpkgs (4.30.0), for anyone using elan instead.

## What is proved

The model is `requirement_to_ltl` over infinite traces, with the condition, response, mode and stop as arbitrary predicates on positions.

- **`Fretish/Trace.lean`.** The operators the lowering emits: `X`, `F[0,n]`, `G[0,n]`, `U`, `R` and `W`.
- **`Fretish/Timing.lean`.** `body` transcribes `timing_body` for an arbitrary boundary predicate. `body_ff` shows that with the boundary set to false it equals the unbounded branch the lowering takes for global, after and only before. The edge lemmas prove every covering edge of the bounded order (`impliesB`) and of the unbounded one (`implies0`) for all tick counts. The file also proves the response and stop polarities.
- **`Fretish/Only.lean`.** `onlyBody` transcribes `negated_timing_body`. Its edges are the edges above read at the negated response, plus the edges into `after n`. The response is antitone and the stops swap polarity.
- **`Fretish/Scope.lean`.** `inner` and `wrap` transcribe the condition and `scope_wrap`, and both are monotone in the body. The four `lower_*` theorems carry an edge of the right order to whole requirements, under either condition type and any mode. `lower_condition_anti` proves that the condition is antitone under a continual requirement.
- **`Fretish/Exact.lean`.** `exact0` and `exactB` state `implies0 T T' ↔ le0 T T'` and `impliesB T T' ↔ leB T T'` for every pair of stop-free timings (`Basic`) at every tick count, so each order is characterised exactly. The `→` direction refutes every pair the relation leaves out with an explicit trace, and the `←` direction reuses the edge lemmas. `exact0K` and `exactBK` extend this to `until` and `before`, with the stop quantified like the response, as `field_orders.py` treats it as a free atom. `until_stop_exact` and `before_stop_exact` show that `until s` implies `until s'` exactly when `s` implies `s'`, and `before` reverses it. Tick count 0 is included, where `within 0`, `for 0` and `immediately` coincide, so the orders are *preorders*.
- **`Fretish/ExactOnly.lean`.** `exactO0` (only before) and `exactOB` (only in, only after) give the only orders exactly. Between two kinds other than `after n`, the only order is the base order between their duals, since negating the response is a bijection. `after n` implies only itself, and is implied by `next` at `n = 0`, by `within m` for `m > n`, and by `eventually`.
- **`Fretish/Cpp.lean`.** `timingImplies` transcribes `timing_implies` branch for branch, over the same `Kind`s, so a stop is one free atom on both sides. `timing_implies_exact` states the result for every scope and every pair of timings the C++ accepts (neither is `within 0` or `for 0`): the lowering's timing order holds exactly when `timing_implies` returns true. Until 2026-11-04 `timing_implies` missed `after 0 ⇒ next`, and `next ⇒ after 0` under the only scopes, and the first version of this theorem carried them as an exception.
- **`Fretish/Syntax.lean`.** An `LTL` syntax with the textbook semantics `evalL`, and `lowerSyn`, which builds the formula `requirement_to_ltl` builds, branch for branch, expanding bounded operators into `X` chains as the C++ does. `eval_lowerSyn` proves that `evalL σ (lowerSyn k ct T m c r)` equals `lower` at the same arguments, so the theorems above hold of the formulas `lowerSyn` prints. `LTL.render` prints a formula in SPOT syntax.

## Checking the transcription

Before the proofs were written, the four relations were checked by script against the orders `field_orders.py` measures. They agree on all 210 ordered pairs of the 15 measured timings in each of the four scope classes.

`Lowering.lean` prints `lowerSyn` for every scope (8), condition type (2), condition (`c` or `true`) and timing with tick counts 0 to 3 (18), 576 requirements in all, each as `ltl` driver JSON and a formula. `scripts/check_lean_lowering.py` runs it (`lake env lean --run Lowering.lean`, so it needs the proofs shell), lowers the same requirements with `build/ltl`, and asks `ltlfilt` whether each pair is equivalent. As a control it pairs each Lean formula with the next requirement's C++ formula, and those must differ. At the time of writing, all 576 agree and 544 of the 576 shifted pairs differ, in about 10 seconds. Run it from the repo root, after building `build/ltl`:

```sh
nix develop '.#proofs' --command python3 scripts/check_lean_lowering.py
```

`TimingOrder.lean` prints `timingImplies` for every scope and every ordered pair of 19 timings: tick counts 0 to 4, less `within 0` and `for 0`. The test helper `timing_order_table` prints `timing_implies` over the same grid, and `scripts/check_lean_timing_order.py` compares the two tables cell by cell. Both functions compare tick counts only with each other and with 0 or 1, so the grid reaches every branch of either. As a control it reads the C++ table with the two timings swapped, and those cells must differ. At the time of writing, all 2,888 cells agree, 768 of them edges, and 1,232 swapped cells differ, in about 4 seconds. Build the helper first:

```sh
cmake --build build --target timing_order_table
nix develop '.#proofs' --command python3 scripts/check_lean_timing_order.py
```

## What is not proved

The scope order and the condition-type order have no Lean theorems yet, and rest on `field_orders.py` at tick counts up to 3. `timing_implies` itself is tied to the proofs through `timingImplies` and the table check above. How `requirement_implies` chains its edges across fields is checked only by the `implication_oracle` stage of `field_orders.py`, at 1 and 2 ticks. `lowerSyn` is still written by reading `src/requirement.cpp`, so the link to the C++ is the differential check above, which covers tick counts up to 3; above that it rests on the expansions in `lowerSyn` following the same recursion as the C++ loops. `eval_lowerSyn` and the exact-order theorems depend on `propext`, `Classical.choice` and `Quot.sound`; the edge theorems depend on `propext` alone.
