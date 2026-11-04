import Fretish.Scope
import Fretish.ExactOnly

/-!
# `timing_implies`, transcribed

`timingImplies` transcribes `timing_implies` in `src/requirement.cpp`, branch
for branch, with `globalTimingImplies` for its helper `global_timing_implies`.
It works over `Kind`, so a stop is a free atom and both sides carry the same
one, as in `scripts/field_orders.py`. `scripts/check_lean_timing_order.py`
compares its table with the C++ function's.
-/

namespace Fretish

deriving instance DecidableEq for Kind

/-- `k_carries_stop` -/
def Kind.carriesStop : Kind → Bool
  | .untilK | .beforeK => true
  | _ => false

/-- `global_timing_implies` -/
def globalTimingImplies (upper lower : Kind) : Bool :=
  if upper = lower then true else
  match upper, lower with
  | .basic .alw, w => !((w matches .basic (.after _)) || (w matches .beforeK))
  | s, .basic .ev => !s.carriesStop
  | .basic (.forT n), .basic (.forT m) => decide (n > m)
  | .basic (.forT _), w => (w matches .basic .imm | .basic .next | .basic (.within _))
  | .basic .imm, w | .basic .next, w => (w matches .basic (.within _))
  | .basic (.within n), .basic (.within m) | .basic (.after n), .basic (.within m) =>
    decide (m > n)
  | .basic (.after n), .basic .next => decide (n = 0)
  | .basic (.within _), _ | .basic (.after _), _ => false
  | .basic .ev, _ | .untilK, _ | .beforeK, _ => false

/-- `timing_implies` -/
def timingImplies (stronger weaker : Kind) (scope : ScopeKind) : Bool :=
  match scope with
  | .global | .afterMode => globalTimingImplies stronger weaker
  | .inMode | .notInMode | .beforeMode =>
    let dischargedAtBoundary :=
      (stronger matches .basic .next | .basic (.within _) | .basic (.after _))
    if dischargedAtBoundary && (weaker matches .basic .ev) then false
    else globalTimingImplies stronger weaker
  | .onlyIn | .onlyBefore | .onlyAfter =>
    if (stronger matches .basic .ev) && (weaker matches .beforeK) then true
    else if weaker matches .basic .alw then
      if stronger matches .untilK then false
      else
        let forOrNext := (stronger matches .basic (.forT _) | .basic .next)
        if !(scope matches .onlyBefore) && forOrNext then false
        else globalTimingImplies weaker stronger
    else globalTimingImplies weaker stronger

/-! ## Against the proved orders -/

/-- The order the lowering gives the timing under each scope. -/
def timingOrder : ScopeKind → Kind → Kind → Prop
  | .global, K, K' | .afterMode, K, K' => implies0K K K'
  | .inMode, K, K' | .notInMode, K, K' | .beforeMode, K, K' => impliesBK K K'
  | .onlyIn, K, K' | .onlyAfter, K, K' => onlyImpliesBK K K'
  | .onlyBefore, K, K' => onlyImplies0K K K'

/-- What `timing_implies` asserts of each argument: not `within 0` or `for 0`. -/
def Kind.cppValid : Kind → Prop
  | .basic (.within n) | .basic (.forT n) => 0 < n
  | _ => True

variable {K K' : Kind}

theorem table0 (hK : K.cppValid) (hK' : K'.cppValid) :
    le0K K K' ↔ globalTimingImplies K K' = true := by
  rcases K with ⟨A⟩ | _ | _ <;> rcases K' with ⟨B⟩ | _ | _ <;>
    (try cases A) <;> (try cases B) <;>
    simp_all [le0K, le0, globalTimingImplies, Kind.cppValid, Kind.carriesStop] <;> omega

theorem tableB {k : ScopeKind} (hk : k = .inMode ∨ k = .notInMode ∨ k = .beforeMode)
    (hK : K.cppValid) (hK' : K'.cppValid) :
    leBK K K' ↔ timingImplies K K' k = true := by
  rcases hk with rfl | rfl | rfl <;>
  rcases K with ⟨A⟩ | _ | _ <;> rcases K' with ⟨B⟩ | _ | _ <;>
    (try cases A) <;> (try cases B) <;>
    simp_all [leBK, leB, le0K, le0, timingImplies, globalTimingImplies, Kind.cppValid,
      Kind.carriesStop] <;> omega

theorem tableO {k : ScopeKind} {base : Kind → Kind → Prop}
    (hk : (k = .onlyIn ∨ k = .onlyAfter) ∧ base = leBK ∨ k = .onlyBefore ∧ base = le0K)
    (hK : K.cppValid) (hK' : K'.cppValid) :
    leO base K K' ↔ timingImplies K K' k = true := by
  rcases hk with ⟨rfl | rfl, rfl⟩ | ⟨rfl, rfl⟩ <;>
  rcases K with ⟨A⟩ | _ | _ <;> rcases K' with ⟨B⟩ | _ | _ <;>
    (try cases A) <;> (try cases B) <;>
    simp_all [leO, Kind.notAfter, Kind.dual, Kind.ticks, intoAfter, leBK, leB, le0K, le0,
      timingImplies, globalTimingImplies, Kind.cppValid, Kind.carriesStop] <;> omega

/-- Under every scope, for timings `timing_implies` accepts, a timing implies
another exactly when `timing_implies` says so. -/
theorem timing_implies_exact (k : ScopeKind) (hK : K.cppValid) (hK' : K'.cppValid) :
    timingOrder k K K' ↔ timingImplies K K' k = true := by
  cases k <;> simp only [timingOrder]
  · exact (exact0K K K').trans (table0 hK hK')
  · exact (exactBK K K').trans (tableB (.inl rfl) hK hK')
  · exact (exactBK K K').trans (tableB (.inr (.inl rfl)) hK hK')
  · exact (exactBK K K').trans (tableB (.inr (.inr rfl)) hK hK')
  · exact (exact0K K K').trans (table0 hK hK')
  · exact (exactOB K K').trans (tableO (.inl ⟨.inl rfl, rfl⟩) hK hK')
  · exact (exactO0 K K').trans (tableO (.inr ⟨rfl, rfl⟩) hK hK')
  · exact (exactOB K K').trans (tableO (.inl ⟨.inr rfl, rfl⟩) hK hK')

/-- Every edge `timing_implies` claims holds. -/
theorem timing_implies_sound (k : ScopeKind) (hK : K.cppValid) (hK' : K'.cppValid)
    (h : timingImplies K K' k = true) : timingOrder k K K' :=
  (timing_implies_exact k hK hK').mpr h

end Fretish
