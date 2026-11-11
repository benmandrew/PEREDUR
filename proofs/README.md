# FRETish order proofs

PEREDUR decides *implication* between FRETISH requirements by rule, without calling a solver. The rules live in three C++ tables: `timing_implies` and `scope_implies` (with its helper `scope_edges`) in `src/requirement.cpp`, and `condition_type_step` in `src/filter/implication_check.cpp`. The proofs ask whether each table returns true exactly when the *lowering* of one requirement to linear temporal logic (LTL), `requirement_to_ltl`, implies the other's, at every tick count. `scripts/field_orders.py` checks this with SPOT, but only up to tick count 3. The answer is yes. Each table matches the lowering exactly, for every tick count, within the limits under [What is not proved](#what-is-not-proved). The proofs use core Lean 4 with `omega` and no Mathlib.

## Main results

Each theorem is stated about a Lean *transcription* of a C++ function (`timingImplies`, `scopeImplies` or `conditionTypeStep`). [The checks below](#checking-the-transcription-against-the-c) tie each transcription to the C++.

| Theorem | C++ it covers | What it says |
|---|---|---|
| `timing_implies_exact` (`Fretish/Cpp.lean`) | `timing_implies` | Under every scope, for every pair of timings the C++ accepts (neither is `within 0` or `for 0`), one timing implies the other exactly when `timing_implies` returns true. |
| `scope_implies_exact` (`Fretish/ScopeOrder.lean`) | `scope_implies`, `scope_edges` | For a condition other than the literal `true`, one scope implies another at every stop, mode, condition and response exactly when `scope_implies` returns true. |
| `scope_implies_sound` (`Fretish/ScopeOrder.lean`) | `scope_implies`, `scope_edges` | Every pair `scope_implies` accepts also holds at the literal condition `true`. |
| `condition_type_step_exact` (`Fretish/CondOrder.lean`) | `condition_type_step` | For a condition other than the literal `true`, a change of condition type holds exactly when `condition_type_step` accepts it. |
| `condition_type_step_sound` (`Fretish/CondOrder.lean`) | `condition_type_step` | Every change `condition_type_step` accepts also holds at the literal condition `true`. |
| `eval_lowerSyn` (`Fretish/Syntax.lean`) | `requirement_to_ltl` | The formula built as LTL syntax, branch for branch like the C++, means what the model of the lowering says, so the theorems above hold of real formulas. |

The proofs also establish five facts that a reader of the C++ needs.

- The timing order depends on the scope class. Global and after scopes use an unbounded order. In, not in and before scopes use a bounded one, because the mode boundary discharges `next`, `within n` and `after n` but not `eventually`, so those three no longer imply it. The three only scopes negate the response, so their order is the base order between the timings' duals, plus extra edges into `after n`.
- Until 2026-11-04, `timing_implies` missed `after 0 ⇒ next`, and `next ⇒ after 0` under the only scopes. The first version of `timing_implies_exact` had to carry both as an exception, which is how they were found.
- A continual requirement implies its trigger form under every scope and timing. A trigger implies its continual form only where the body is a release: `always` under a plain scope, and `eventually` under an only scope.
- At tick count 0, `within 0`, `for 0` and `immediately` coincide. Distinct timings then imply each other, so the orders are *preorders*.
- At the literal condition `true`, the scope and condition-type theorems give *soundness* only, because the tables reject some implications that hold there.

## How the proof goes

Each file named below is `Fretish/<Name>.lean`, and its module header explains its own lemmas.

1. **Model.** The lowering becomes a function on infinite *traces*, with the condition, response, mode and stop as arbitrary predicates on positions (`Trace`, `Timing`, `Only`, `Scope`).
2. **Edges.** An *edge* is a pair of timings that an order relates. Every edge of each order holds for all tick counts, and the scope wrappers are monotone in the body, so each edge lifts to whole requirements under any condition type and mode (`Timing`, `Only`, `Scope`).
3. **Non-edges.** An explicit trace refutes every pair an order leaves out, which makes each order exact (`Exact`, `ExactOnly`, `Witness`).
4. **Transcriptions.** The C++ functions are transcribed branch for branch, and each transcription is proved equal to the exact order (`Cpp`, `ScopeOrder`, `CondOrder`).
5. **Syntax.** `lowerSyn` builds the formula `requirement_to_ltl` builds as LTL syntax, expanding bounded operators into `X` chains as the C++ does. `eval_lowerSyn` proves that its meaning is the model from step 1 (`Syntax`).

## Building

```sh
nix develop '.#proofs'
cd proofs
lake build
```

The `proofs` shell is separate from the default one because `lean4` is a 650 MB download on x86_64-linux, and on aarch64-darwin it has no binary cache entry and builds from source. `lean-toolchain` pins the same release as the flake's nixpkgs (4.30.0), for anyone using elan instead.

## Checking the transcription against the C++

The Lean model and the transcriptions were written by reading the C++, so three scripts compare them with the C++ output. Each script also runs a control, a deliberately mismatched pairing that must differ, which shows that the comparison can fail. The results below are from the time of writing.

| Script | What it compares | Grid | Control | Result |
|---|---|---|---|---|
| `check_lean_lowering.py` | `lowerSyn`, printed by `Lowering.lean`, against `build/ltl`, with `ltlfilt` deciding equivalence | 8 scopes × 2 condition types × 2 conditions (`c` or `true`) × 18 timings at tick counts 0 to 3: 576 requirements | Each Lean formula against the next requirement's C++ formula | 576 of 576 agree; 544 of 576 shifted pairs differ |
| `check_lean_timing_order.py` | `timingImplies`, printed by `TimingOrder.lean`, against `timing_implies`, printed by `timing_order_table` | Every scope × every ordered pair of 19 timings (tick counts 0 to 4, less `within 0` and `for 0`) | The C++ table with the two timings swapped | 2,888 cells agree, 768 of them edges; 1,232 swapped cells differ |
| `check_lean_scope_order.py` | `scopeImplies`, printed by `ScopeOrderTable.lean`, against `scope_implies`, printed by `scope_order_table` | 2 condition types × the same 19 timings × every ordered pair of scopes, with the weaker scope at the stronger's mode or another one | The C++ table with the two scopes swapped | 4,864 cells agree, 568 of them edges; 452 swapped cells differ |

Both timing functions compare tick counts only with each other and with 0 or 1, so the 19-timing grid reaches every branch of either. `scope_edges` branches only on the condition type and on whether the timing is `eventually` or `always`, so the scope grid reaches every branch too.

Run the scripts from the repo root, after building the C++ helpers. `check_lean_lowering.py` runs `lake env lean --run Lowering.lean`, so all three run inside the proofs shell.

```sh
cmake --build build --target ltl timing_order_table scope_order_table
nix develop '.#proofs' --command python3 scripts/check_lean_lowering.py
nix develop '.#proofs' --command python3 scripts/check_lean_timing_order.py
nix develop '.#proofs' --command python3 scripts/check_lean_scope_order.py
```

Before the proofs were written, a script checked the four relations against the orders `field_orders.py` measures. They agreed on all 210 ordered pairs of the 15 measured timings in each of the four scope classes.

## What is not proved

- `condition_type_step` is tied to `conditionTypeStep` only by reading. It is a private member of a class in `src/filter/implication_check.cpp`, so a helper cannot call it as it stands.
- At the literal condition `true`, the theorems give soundness only. `field_orders.py` finds further true implications there that `scope_implies` rejects, such as global implying each only scope under a continual `after n`. Rejecting them costs only *completeness*.
- How `requirement_implies` chains edges across fields is checked only by the `implication_oracle` stage of `field_orders.py`, at 1 and 2 ticks.
- `lowerSyn` is tied to the C++ by the differential check above, which covers tick counts up to 3. Above that, the link rests on `lowerSyn` expanding bounded operators with the same recursion as the C++ loops.
- `eval_lowerSyn` and the exact and sound theorems depend on `propext`, `Classical.choice` and `Quot.sound`. The edge theorems depend on `propext` alone.
