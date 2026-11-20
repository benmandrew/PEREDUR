# FRETish order proofs

PEREDUR decides *implication* between FRETISH requirements by rule, without calling a solver. The rules live in three C++ tables: `timing_implies` and `scope_implies` (with its helper `scope_edges`) in `src/requirement.cpp`, and `condition_type_step` in `src/filter/implication_check.cpp`. The proofs ask whether each table returns true exactly when the *lowering* of one requirement to linear temporal logic (LTL), `requirement_to_ltl`, implies the other's, at every tick count. `scripts/field_orders.py` checks this with SPOT, but only up to tick count 3. The answer is yes. Each table matches the lowering exactly, for every tick count, within the limits under [What is not proved](#what-is-not-proved). The proofs use core Lean 4 with `omega` and no Mathlib.

## Main results

Each theorem is stated about a Lean *transcription* of a C++ function (`timingImplies`, `scopeImplies` or `conditionTypeStep`). [The checks below](#checking-the-transcription-against-the-c) tie each transcription to the C++.

| Theorem | C++ it covers | What it says |
|---|---|---|
| `timing_implies_exact` (`Fretish/Cpp.lean`) | `timing_implies` | Under every scope, for every pair of timings the C++ accepts (neither is `within 0` or `for 0`), one timing *body* implies the other at every boundary exactly when `timing_implies` returns true. |
| `timing_implies_exact_req` (`Fretish/TimingLift.lean`) | `timing_implies` | The same between whole requirements. Under every scope and condition type, for a condition other than the literal `true` and with the other fields equal, the requirement with one timing implies the requirement with the other at every stop, mode, condition and response exactly when `timing_implies` returns true. |
| `timing_implies_sound_req` (`Fretish/TimingLift.lean`) | `timing_implies` | Every pair `timing_implies` accepts also holds between whole requirements at the literal condition `true`. It is not exact there: under a continual condition and the global scope, `for 1` and `always` both lower to `G r`. |
| `scope_implies_exact` (`Fretish/ScopeOrder.lean`) | `scope_implies`, `scope_edges` | For a condition other than the literal `true`, one scope implies another at every stop, mode, condition and response exactly when `scope_implies` returns true. |
| `scope_implies_sound` (`Fretish/ScopeOrder.lean`) | `scope_implies`, `scope_edges` | Every pair `scope_implies` accepts also holds at the literal condition `true`. |
| `condition_type_step_exact` (`Fretish/CondOrder.lean`) | `condition_type_step` | For a condition other than the literal `true`, a change of condition type holds exactly when `condition_type_step` accepts it. |
| `condition_type_step_sound` (`Fretish/CondOrder.lean`) | `condition_type_step` | Every change `condition_type_step` accepts also holds at the literal condition `true`. |
| `condition_coincide_sound` (`Fretish/Chain.lean`) | the condition step of `RuleSearch` | Under a trigger where the two condition types coincide (`always` under a plain scope, `eventually` under an only scope), the condition is antitone, as under continual. |
| `cross_sound` (`Fretish/Cross.lean`) | `cross_step_holds` in `RuleSearch` | A plain timing whose pattern clashes with an only timing's (one is `after n`, the other `immediately`, `for k`, `always`, `next` for `n ≥ 1`, `within k` for `k ≤ n`, or `after k` for `k ≠ n`) implies it from not in to only in at one mode, from global to only after, and from global to only in or only before under a continual condition or `always`. Neither one-field middle state implies, so the step moves the timing and the scope together. |
| `reach_sound` (`Fretish/Chain.lean`) | `RuleSearch` in `requirement_implies` | Every pair the chain search accepts implies, given correct answers from `formula_implies` and a start that marks only fields the two requirements share. |
| `eval_lowerSyn` (`Fretish/Syntax.lean`) | `requirement_to_ltl` | The formula built as LTL syntax, branch for branch like the C++, means what the model of the lowering says, so the theorems above hold of real formulas. |

The proofs also establish five facts that a reader of the C++ needs.

- The timing order depends on the scope class. Global and after scopes use an unbounded order. In, not in and before scopes use a bounded one, because the mode boundary discharges `next`, `within n` and `after n` but not `eventually`, so those three no longer imply it. The three only scopes negate the response, so their order is the base order between the timings' duals, plus extra edges into `after n`.
- Until 2026-11-04, `timing_implies` missed `after 0 ⇒ next`, and `next ⇒ after 0` under the only scopes. The first version of `timing_implies_exact` had to carry both as an exception, which is how they were found.
- A continual requirement implies its trigger form under every scope and timing. A trigger implies its continual form only where the body is a release: `always` under a plain scope, and `eventually` under an only scope.
- At tick count 0, `within 0`, `for 0` and `immediately` coincide. Distinct timings then imply each other, so the orders are *preorders*.
- At the literal condition `true`, the timing, scope and condition-type theorems on whole requirements give *soundness* only, because the tables reject some implications that hold there.

## How the proof goes

Each file named below is `Fretish/<Name>.lean`, and its module header explains its own lemmas.

1. **Model.** The lowering becomes a function on infinite *traces*, with the condition, response, mode and stop as arbitrary predicates on positions (`Trace`, `Timing`, `Only`, `Scope`).
2. **Edges.** An *edge* is a pair of timings that an order relates. Every edge of each order holds for all tick counts, and the scope wrappers are monotone in the body, so each edge lifts to whole requirements under any condition type and mode (`Timing`, `Only`, `Scope`).
3. **Non-edges.** An explicit trace refutes every pair an order leaves out, which makes each order exact (`Exact`, `ExactOnly`, `Witness`). The timing refutations are traces on the body, and they need only two boundaries: none, and one at the point of evaluation. Each moves to position 0, and a condition true there alone, with a mode that puts that point in the scope and sets the boundary, turns it into a refutation between whole requirements. After enters its scope one point late, so its refutation moves to position 1 (`TimingLift`).
4. **Transcriptions.** The C++ functions are transcribed branch for branch, and each transcription is proved equal to the exact order (`Cpp`, `ScopeOrder`, `CondOrder`).
5. **Chains.** `Search.reach` transcribes `RuleSearch`, which chains one-field steps. Each step is one of the theorems above, or a response, stop or condition polarity, read at the other fields of the current state. A trigger's condition takes the continual polarity where the two condition types coincide, by a chain through the continual form. One step moves two fields at once, the timing and the scope into an only scope, by `cross_sound` (`Cross`). Soundness follows by induction on the chain (`Chain`).
6. **Syntax.** `lowerSyn` builds the formula `requirement_to_ltl` builds as LTL syntax, expanding bounded operators into `X` chains as the C++ does. `eval_lowerSyn` proves that its meaning is the model from step 1 (`Syntax`).

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
- `Search.reach` is tied to `RuleSearch` only by reading, and it leaves out the memo of failed states, which can only cut a branch that already failed. The `implication_oracle` stage of `field_orders.py` checks the C++ search against SPOT at 1 and 2 ticks.
- `reach_sound` assumes that `formula_implies` answers correctly. It decides each propositional step by exhaustive model counting, which the proofs do not model.
- The chain search is incomplete, and nothing here bounds what it misses. An implication that needs a field to pass through a third value, or more than one field to move at once outside the cross step, reads as incomparable. `before m ⇒ only after m` is not a cross step: where `m` holds the before scope holds vacuously and the only-after body need not.
- `lowerSyn` is tied to the C++ by the differential check above, which covers tick counts up to 3. Above that, the link rests on `lowerSyn` expanding bounded operators with the same recursion as the C++ loops.
- `eval_lowerSyn`, `reach_sound` and the exact and sound theorems depend on `propext`, `Classical.choice` and `Quot.sound`. The edge theorems depend on `propext` alone.

# PSP order proofs

PEREDUR decides implication between PSP requirements by rule too (`psp::requirement_implies` in `src/psp/implication.cpp`, described in `docs/dev/psp.md`). Its tables are `pattern_implies`, `scope_implies` and `field_polarity`, measured with SPOT at counts 1 to 4 (`docs/dev/psp-orders.json`). The proofs in `Psp/` ask the same question as for FRETISH: does each table return true exactly when the lowering (PLAN.md D4: a body per pattern over an optional boundary, inside a wrapper per scope) of one requirement implies the other's, at every count? The answer is yes for the pattern and scope orders, at every count from 1 up, and the polarities are sound. P, S, Q and R are arbitrary predicates on positions and counts are natural numbers. The proofs use core Lean 4 with `omega` and no Mathlib.

## Main results

| Theorem | C++ it covers | What it says |
|---|---|---|
| `pattern_sound` (`Psp/PatternOrder.lean`) | `pattern_implies` | Every edge of the pattern order of a scope's class holds between whole requirements under that scope, at every count. |
| `pattern_exact` (`Psp/Exact.lean`) | `pattern_implies` | Under every scope, for counts of at least 1, one pattern implies another over every P, S, Q and R exactly when the order of the scope's class relates them. |
| `scope_sound` (`Psp/ScopeOrder.lean`) | `scope_implies` | Every edge of a pattern's cell holds between whole requirements, at every count. |
| `scope_exact` (`Psp/ScopeExact.lean`) | `scope_implies` | For counts of at least 1, one scope implies another for a pattern exactly when the order of the pattern's cell relates them. |
| `pattern_implies_exact`, `scope_implies_exact` (`Psp/Cpp.lean`) | `pattern_implies`, `scope_implies`, `scope_class`, `pattern_cell` | The transcribed C++ functions return true exactly when the implication holds, at every count the C++ accepts. |
| `polarity_sound` (`Psp/Polarity.lean`) | `field_polarity` | Every answer holds at every count: a field the requirement does not read changes nothing, and a monotone or antitone field moves the requirement the way the table says. |
| `reach_sound` (`Psp/Chain.lean`) | `RuleSearch` in `requirement_implies` | Every pair the six-field chain search accepts implies, given correct answers from `formula_implies` and a start that marks only fields the two requirements share. |
| `eval_lowerSyn` (`Psp/Syntax.lean`) | `requirement_to_ltl` | The formula built as LTL syntax, branch for branch like the C++, means what the model of the lowering says. |

The proofs also establish three facts that a reader of the C++ needs.

- The pattern order has three scope classes: unbounded (globally, after), bounded (between, after-until) and before. Universality implies existence except under before, and bounded recurrence implies existence, response and bounded response at a count no lower only in the unbounded class, where no boundary can cut the recurrence short.
- At count 0 distinct patterns coincide (bounded response 0 and bounded invariance 0 both say G(P → S), and bounded recurrence 0 is universality), so exactness needs counts of at least 1. The C++ loader rejects count 0.
- The exactness theorems hold at every count, so the rule's extension of each count relation from the measured counts 1 to 4 to every count is proved, not assumed.

## How the proof goes

Each file named below is `Psp/<Name>.lean`.

1. **Model.** `body` gives each pattern's body over a boundary B, with the unbounded body at B = false, and `lower` wraps it per scope (`Bodies`, `Scopes`).
2. **Edges.** Each edge holds point by point before the first boundary, and bounded existence by induction over its count of alternations (`PatternOrder`). Scope edges rest on two facts: invariant and response bodies with no boundary hold again on every suffix, and invariant and precedence bodies are monotone in their boundary (`ScopeOrder`).
3. **Non-edges.** An explicit trace refutes every pair an order leaves out, at every count (`Witness`, `Exact`, `ScopeExact`). Bounded existence uses an alternating trace with N stretches of P; the others use points and gaps placed relative to the count.
4. **Transcriptions.** `pattern_implies`, `scope_implies` and `field_polarity` are transcribed branch for branch, and the first two are proved equal to the exact orders (`Cpp`). The polarities are proved sound (`Polarity`).
5. **Chains.** `Search.reach` transcribes `RuleSearch` over pattern, scope, P, S, Q and R. Each step is one of the theorems above at the other fields of the current state, and soundness follows by induction (`Chain`).
6. **Syntax.** `lowerSyn` builds the C++ formula, with the unbounded bodies in `G` and `F` and the bounded operators as `X` chains, and `eval_lowerSyn` proves it means the model (`Syntax`).

## Checking the transcription against the C++

| Script | What it compares | Grid | Control | Result |
|---|---|---|---|---|
| `check_lean_psp_lowering.py` | `lowerSyn`, printed by `PspLowering.lean`, against `build/ltl` on a `.psp` file, with `ltlfilt` deciding equivalence | 21 patterns (the bounded ones at counts 1 to 4) × 5 scopes: 105 requirements | Each Lean formula against the next requirement's C++ formula | 105 of 105 agree; 105 of 105 shifted pairs differ |
| `check_lean_psp_orders.py` | `patternImplies`, `scopeImplies` and `fieldPolarity`, printed by `PspOrders.lean`, against `psp_pattern_order_table`, `psp_scope_order_table` and the SPOT measurements in `docs/dev/psp-orders.json` | 5 scopes × 21 × 21 patterns; 21 patterns × 5 × 5 scopes; 105 polarity cells | Each table read with stronger and weaker swapped; the polarities with P and S swapped | Patterns: 2,205 cells agree with both, 545 of them edges; 880 swapped cells differ. Scopes: 525 cells agree with both, 216 edges; 222 swapped cells differ. Polarities: 105 of 105 cells agree with SPOT; all 105 differ with P and S swapped |

```sh
cmake --build build --target ltl psp_pattern_order_table psp_scope_order_table
nix develop '.#proofs' --command python3 scripts/check_lean_psp_lowering.py
nix develop '.#proofs' --command python3 scripts/check_lean_psp_orders.py
```

## What is not proved for PSP

- **No joint step.** The C++ search moves one field at a time because SPOT found that each of the 534 implications between requirements that differ in both pattern and scope, at counts 1 to 4, passes through one of its two middle requirements (`joint_steps` in `psp-orders.json`). This is not proved. It needs a refutation of every such pair whose two middle steps both fail, a joint exactness theorem over every pattern pair and every scope pair at every count, and the one-field witnesses of `Exact` and `ScopeExact` do not compose into those. A joint step would cost only completeness: `reach_sound` holds whatever the search misses.
- **Polarity is sound, not exact.** Where `field_polarity` answers `None`, the proofs do not show that the field has no polarity. `check_lean_psp_orders.py` checks the table against SPOT at counts 1 to 4.
- `Search.reach` is tied to `RuleSearch` by reading, and leaves out its memo of visited states, which can only cut a branch that already failed. `reach_sound` assumes `formula_implies` answers correctly.
- `lowerSyn` is tied to the C++ by the differential check above at counts 1 to 4. Above that, the link rests on `lowerSyn` expanding bounded operators with the same recursion as the C++ loops.
- `eval_lowerSyn` depends on `propext` and `Quot.sound`; the other theorems also on `Classical.choice`.
