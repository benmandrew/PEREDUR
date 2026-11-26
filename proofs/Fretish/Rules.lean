import Fretish.TimingLift
import Fretish.CondOrder

/-!
# The field rules

Each rule is a table over all pairs of values of one field, with the other
fields fixed. Each theorem here says that its table holds exactly when the
requirement with the first value implies the requirement with the second, at
every stop, mode, condition and response, for a condition other than the
literal `true`. At the literal `true` the tables are sound only.

The tables are the statements: reading them says what is proved. The trusted
part is the trace model they are proved against, `body` and `onlyBody` in
`Fretish.Timing` and `Fretish.Only`, and `lower` in `Fretish.Scope`.

The paper's figures draw these tables. They draw the covering pairs, and a path
in a figure is a pair a table relates.

| Paper | Table | Theorem |
|---|---|---|
| `⊑₀` (global, after) | `le0K` | `timing_rule_exact` |
| `⊑_B` (in, not in, before) | `leBK` | `timing_rule_exact` |
| dual `⊑_B` (only in, only after) | `leO leBK` | `timing_rule_exact` |
| dual `⊑₀` (only before) | `leO le0K` | `timing_rule_exact` |
| scope edges and cells | `scopeRule`, `scopeEdge`, `scopeCell` | `scope_rule_exact` |
| condition type | `condRule` | `cond_rule_exact` |

The C++ rules agree with these tables: `timingImplies`, `scopeImplies` and
`conditionTypeStep` transcribe them, and `timingTable_iff`, `scopeRule_iff` and
`condRule_iff` prove each transcription equal to its table.
-/

namespace Fretish

variable {K K' : Kind}

/-! ## Timing -/

/-- The timing rule under each scope: one of four tables, by whether the scope
supplies a boundary and whether it is an only scope. -/
def timingTable : ScopeKind → Kind → Kind → Prop
  | .global, K, K' | .afterMode, K, K' => le0K K K'
  | .inMode, K, K' | .notInMode, K, K' | .beforeMode, K, K' => leBK K K'
  | .onlyIn, K, K' | .onlyAfter, K, K' => leO leBK K K'
  | .onlyBefore, K, K' => leO le0K K K'

/-- On timing bodies, the table is the order. -/
theorem timing_table_exact_body (k : ScopeKind) (K K' : Kind) :
    timingOrder k K K' ↔ timingTable k K K' := by
  cases k <;> simp only [timingOrder, timingTable]
  · exact exact0K K K'
  · exact exactBK K K'
  · exact exactBK K K'
  · exact exactBK K K'
  · exact exact0K K K'
  · exact exactOB K K'
  · exact exactO0 K K'
  · exact exactOB K K'

/-- The timing rule is exact: under every scope and condition type, for a
condition other than the literal `true`, the requirement with `K` implies the
requirement with `K'` exactly when the table relates them. Every tick count is
covered, `within 0` and `for 0` included. -/
theorem timing_rule_exact (k : ScopeKind) (ct : CondType) (K K' : Kind) :
    TimingReq k ct false K K' ↔ timingTable k K K' :=
  (timingOrder_exact_req k ct K K').trans (timing_table_exact_body k K K')

/-- The timing rule is sound at the literal `true` too. -/
theorem timing_rule_sound (k : ScopeKind) (ct : CondType) (ctrue : Bool)
    (h : timingTable k K K') : TimingReq k ct ctrue K K' :=
  timingOrder_req k ct ctrue ((timing_table_exact_body k K K').mpr h)

/-! ## Scope -/

/-- The cells of `fig:scope-cells`: condition type against `always`,
`eventually` or any other timing. -/
inductive ScopeCell where
  | A
  | B
  | C
  | D
  deriving DecidableEq

def scopeCell : CondType → Kind → ScopeCell
  | .continual, .basic .ev => .C
  | .trigger, .basic .ev => .D
  | _, .basic .alw => .A
  | .continual, _ => .A
  | .trigger, _ => .B

/-- The edges of `fig:scope-edges`, each at the cells where it holds. The set
is closed under composition, so it is the whole order between distinct
scopes. -/
def scopeEdge : ScopeCell → ScopeKind → ScopeKind → Prop
  | .A, .global, .inMode => True
  | .A, .global, .notInMode => True
  | .A, .global, .beforeMode | .B, .global, .beforeMode => True
  | .A, .global, .afterMode | .C, .global, .afterMode => True
  | _, .notInMode, .beforeMode => True
  | _, _, _ => False

/-- The scope rule. `same` says whether the two scopes have the same mode. A
scope implies itself at the same mode; a global scope has no mode, so its edges
hold whatever the other scope's mode is, and the edge from not in to before
holds at the same mode only. -/
def scopeRule (ct : CondType) (K : Kind) (k k' : ScopeKind) (same : Bool) : Prop :=
  (k = k' ∧ same = true) ∨ ((k = .global ∨ same = true) ∧ scopeEdge (scopeCell ct K) k k')

/-- The edge list of `scope_edges` for each cell. -/
def cellEdges : ScopeCell → List (ScopeKind × ScopeKind)
  | .A => [(.global, .inMode), (.global, .notInMode), (.global, .beforeMode),
      (.global, .afterMode), (.notInMode, .beforeMode)]
  | .B => [(.global, .beforeMode), (.notInMode, .beforeMode)]
  | .C => [(.global, .afterMode), (.notInMode, .beforeMode)]
  | .D => [(.notInMode, .beforeMode)]

theorem scopeEdges_cell (ct : CondType) (K : Kind) :
    scopeEdges K ct = cellEdges (scopeCell ct K) := by
  rcases K with ⟨A⟩ | _ | _ <;> (try cases A) <;> cases ct <;> rfl

theorem scopeRule_iff (ct : CondType) (K : Kind) (k k' : ScopeKind) (same : Bool) :
    scopeRule ct K k k' same ↔ scopeImplies k k' same K ct = true := by
  simp only [scopeRule, scopeImplies, scopeEdges_cell]
  generalize scopeCell ct K = c
  cases c <;> cases k <;> cases k' <;> cases same <;> simp [scopeEdge, cellEdges]

/-- The scope rule is exact: under every condition type and timing, for a
condition other than the literal `true`, one scope implies another at every
stop, mode, condition and response exactly when the rule relates them. Two
global scopes share their mode, which is empty. -/
theorem scope_rule_exact (ct : CondType) (K : Kind) (k k' : ScopeKind) (same : Bool)
    (hg : k = .global → k' = .global → same = true) :
    ScopeOrder ct K k k' same ↔ scopeRule ct K k k' same :=
  (scope_implies_exact ct K k k' same hg).trans (scopeRule_iff ct K k k' same).symm

/-- The scope rule is sound at the literal `true` too. -/
theorem scope_rule_sound {ct : CondType} {ctrue : Bool} {K : Kind} {k k' : ScopeKind}
    {same : Bool} {s m m' c r : Pred} (hc : ctrue = false ∨ c = tt) (hm : same = true → m' = m)
    (h : scopeRule ct K k k' same) :
    Entails (lower k ct ctrue (K.at s) m c r) (lower k' ct ctrue (K.at s) m' c r) :=
  scope_implies_sound hc hm ((scopeRule_iff ct K k k' same).mp h)

/-! ## Condition type -/

/-- The condition-type rule: continual implies trigger always, and trigger
implies continual where the two coincide, at `always` under a plain scope and
`eventually` under an only scope. -/
def condRule : CondType → CondType → ScopeKind → Kind → Prop
  | .continual, _, _, _ => True
  | .trigger, .trigger, _, _ => True
  | .trigger, .continual, k, K =>
    (isOnly k = false ∧ K = .basic .alw) ∨ (isOnly k = true ∧ K = .basic .ev)

theorem condRule_iff (ct ct' : CondType) (hne : ct ≠ ct') (k : ScopeKind) (K : Kind) :
    condRule ct ct' k K ↔ conditionTypeStep ct k K = true := by
  cases ct <;> cases ct' <;> (first | exact absurd rfl hne | skip) <;>
    rcases K with ⟨A⟩ | _ | _ <;> (try cases A) <;> cases k <;>
      simp [condRule, conditionTypeStep, isOnly, Kind.isEv, Kind.isAlw]

/-- The condition-type rule is exact: under every scope and timing, for a
condition other than the literal `true`, one condition type implies the other
at every stop, mode, condition and response exactly when the rule relates
them. -/
theorem cond_rule_exact (ct ct' : CondType) (k : ScopeKind) (K : Kind) :
    CondOrder ct ct' k K ↔ condRule ct ct' k K := by
  by_cases hne : ct = ct'
  · subst hne
    cases ct <;> exact ⟨fun _ => trivial, fun _ _ _ _ _ _ h => h⟩
  · exact (condition_type_step_exact ct ct' hne k K).trans (condRule_iff ct ct' hne k K).symm

/-- The condition-type rule is sound at the literal `true` too. -/
theorem cond_rule_sound {ct ct' : CondType} {ctrue : Bool} {k : ScopeKind} {K : Kind}
    {s m c r : Pred} (hc : ctrue = false ∨ c = tt) (h : condRule ct ct' k K) :
    Entails (lower k ct ctrue (K.at s) m c r) (lower k ct' ctrue (K.at s) m c r) := by
  by_cases hne : ct = ct'
  · subst hne; exact fun _ h => h
  · exact condition_type_step_sound hc hne ((condRule_iff ct ct' hne k K).mp h)

/-! ## The C++ transcription of the timing rule -/

/-- `timingImplies` is the timing table, for the timings `timing_implies`
accepts (not `within 0` or `for 0`). -/
theorem timingTable_iff (k : ScopeKind) (hK : K.cppValid) (hK' : K'.cppValid) :
    timingTable k K K' ↔ timingImplies K K' k = true :=
  (timing_table_exact_body k K K').symm.trans (timing_implies_exact k hK hK')

/-! ## Deciding the tables, for printing -/

instance (A B : Basic) : Decidable (le0 A B) := by
  cases A <;> cases B <;> simp only [le0] <;> infer_instance

instance (A B : Basic) : Decidable (leB A B) := by
  cases A <;> cases B <;> simp only [leB, le0] <;> infer_instance

instance (K K' : Kind) : Decidable (le0K K K') := by
  cases K <;> cases K' <;> simp only [le0K] <;> infer_instance

instance (K K' : Kind) : Decidable (leBK K K') := by
  cases K <;> cases K' <;> simp only [leBK, le0K] <;> infer_instance

instance (K K' : Kind) : Decidable (intoAfter K (Kind.ticks K')) := by
  unfold intoAfter
  split <;> infer_instance

instance {base : Kind → Kind → Prop} [∀ K K', Decidable (base K K')] (K K' : Kind) :
    Decidable (leO base K K') := by
  unfold leO
  split <;> split <;> infer_instance

instance (k : ScopeKind) (K K' : Kind) : Decidable (timingTable k K K') := by
  cases k <;> simp only [timingTable] <;> infer_instance

end Fretish
