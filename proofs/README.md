# FRETish order proofs

PEREDUR decides implication between FRETISH requirements by one rule per field. Each rule is a table over every pair of the field's values, and these proofs show that each table holds exactly when one requirement's LTL lowering implies the other's, at every tick count. Core Lean 4, no Mathlib.

## Results

In `Fretish/Rules.lean`. "Exact" means for a condition other than the literal `true`; at `true` the `_sound` theorems give soundness only.

| Theorem | Table |
|---|---|
| `timing_rule_exact`, `timing_rule_sound` | `timingTable`: `le0K`, `leBK`, `leO leBK` or `leO le0K` by scope class |
| `scope_rule_exact`, `scope_rule_sound` | `scopeRule`, from `scopeCell` and `scopeEdge` |
| `cond_rule_exact`, `cond_rule_sound` | `condRule` |

Further: `reach_sound` (`Fretish/Chain.lean`) proves the chain search that combines the rules sound, and `eval_lowerSyn` (`Fretish/Syntax.lean`) proves the LTL syntax PEREDUR builds means what the model says.

## What is trusted

Only the trace model: `body` (`Fretish/Timing.lean`), `onlyBody` (`Fretish/Only.lean`) and `lower` (`Fretish/Scope.lean`). The tables are what the theorems state. The paper's diagrams draw the tables and no proof depends on them.

## Building and checking

```sh
nix develop '.#proofs' --command sh -c 'cd proofs && lake build'
```

From the repo root, inside the same shell:

| Script | Checks |
|---|---|
| `scripts/lean_rule_diagrams.py` | Each timing table is a preorder on tick counts 0 to 4; prints the covering pairs the paper's figure draws. |
| `scripts/check_lean_lowering.py` | `lowerSyn` against the C++ `ltl` driver, through SPOT, at tick counts 0 to 3. |
| `scripts/check_lean_timing_order.py` | The transcription `timingImplies` against the C++ `timing_implies`. |
| `scripts/check_lean_scope_order.py` | The transcription `scopeImplies` against the C++ `scope_implies`. |

The last three need `cmake --build build --target ltl timing_order_table scope_order_table`. `timingTable_iff`, `scopeRule_iff` and `condRule_iff` prove each table equal to its transcription.

## What is not proved

- `conditionTypeStep` and `Search.reach` are tied to the C++ (`condition_type_step`, `RuleSearch`) by reading only.
- `reach_sound` assumes `formula_implies` answers correctly.
- The chain search is incomplete: an implication needing a field to pass through a third value reads as incomparable.
- `lowerSyn` matches the C++ by the differential check up to tick count 3, and above that by reading.
