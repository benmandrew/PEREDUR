import Fretish.Only

/-!
# Scope wrappers

`lower` is `requirement_to_ltl` with the condition, response and mode as
arbitrary predicates. `inner` places the timing body under the condition, and
`wrap` is `scope_wrap`. Every one of them is monotone in the body, so an edge
of the timing order at the body carries to the whole requirement, for every
scope, condition type and mode.

The four theorems at the end give the four measured classes: global and after
take `implies0`; in, not in and before take `impliesB`; only in and only after
take `onlyImpliesB`; only before takes `onlyImplies0`.
-/

namespace Fretish

inductive ScopeKind where
  | global
  | inMode
  | notInMode
  | beforeMode
  | afterMode
  | onlyIn
  | onlyBefore
  | onlyAfter

inductive CondType where
  | continual
  | trigger

/-- `start_of_mode`: the point before the mode rises. -/
abbrev som (m : Pred) : Pred := fun t => ¬ m t ∧ m (t + 1)

/-- `end_of_mode`: the last point at which the mode holds. -/
abbrev eom (m : Pred) : Pred := fun t => m t ∧ ¬ m (t + 1)

/-- `scope_boundary`, with `none` for the empty string. -/
def scopeBoundary : ScopeKind → Pred → Option Pred
  | .global, _ => none
  | .afterMode, _ => none
  | .onlyBefore, _ => none
  | .inMode, m => some (eom m)
  | .onlyAfter, m => some (eom m)
  | .notInMode, m => some (som m)
  | .beforeMode, m => some (som m)
  | .onlyIn, m => some (som m)

def isOnly : ScopeKind → Bool
  | .onlyIn => true
  | .onlyBefore => true
  | .onlyAfter => true
  | _ => false

/-- The body `requirement_to_ltl` builds, the boundary taken as `ff` when absent. -/
def timingBody (k : ScopeKind) (T : Timing) (m r : Pred) : Pred :=
  match isOnly k with
  | true => onlyBody T ((scopeBoundary k m).getD ff) r
  | false => body T ((scopeBoundary k m).getD ff) r

/-! ## The condition -/

/-- Continual, unbounded: `G(c -> x)`. -/
abbrev innerContU (c x : Pred) : Pred := G (fun u => c u → x u)

/-- Continual, bounded: `b R (c -> x)`. -/
abbrev innerCont (b c x : Pred) : Pred := R b (fun u => c u → x u)

/-- Trigger, unbounded: `G((!c & Xc) -> X x) & (c -> x)`. -/
abbrev innerTrigU (c x : Pred) : Pred := fun t =>
  G (fun u => (¬ c u ∧ c (u + 1)) → x (u + 1)) t ∧ (c t → x t)

/-- Trigger, bounded: `(b R ((!c & (Xc & !b)) -> (X x & !b))) & (c -> x)`. -/
abbrev innerTrig (b c x : Pred) : Pred := fun t =>
  R b (fun u => (¬ c u ∧ c (u + 1) ∧ ¬ b u) → (x (u + 1) ∧ ¬ b u)) t ∧ (c t → x t)

/-- Trigger on the literal condition `true`: `c -> x`. -/
abbrev innerTrue (c x : Pred) : Pred := fun t => c t → x t

/-- The obligation under the condition. `ctrue` is the lowering's check that
the condition prints as `true`, which it makes for a trigger only. -/
def inner : Option Pred → CondType → Bool → Pred → Pred → Pred
  | _, .trigger, true, c, x => innerTrue c x
  | none, .continual, _, c, x => innerContU c x
  | some b, .continual, _, c, x => innerCont b c x
  | none, .trigger, false, c, x => innerTrigU c x
  | some b, .trigger, false, c, x => innerTrig b c x

/-! ## The wrapper -/

/-- `G(!e | X x)`: `x` at the point after every `e`. -/
abbrev reentry (e x : Pred) : Pred := G (fun u => ¬ e u ∨ x (u + 1))

/-- `((!e) U (e & X x)) | G(!e)`: `x` after the first `e`, if there is one. -/
abbrev firstEntry (e x : Pred) : Pred := fun t =>
  U (neg e) (fun u => e u ∧ x (u + 1)) t ∨ G (neg e) t

def wrap : ScopeKind → Pred → Pred → Pred
  | .global, _, x => x
  | .onlyAfter, _, x => x
  | .inMode, m, x => fun t => reentry (som m) x t ∧ (m t → x t)
  | .notInMode, m, x => fun t => reentry (eom m) x t ∧ (¬ m t → x t)
  | .onlyIn, m, x => fun t => reentry (eom m) x t ∧ (¬ m t → x t)
  | .beforeMode, m, x => fun t => x t ∨ m t
  | .afterMode, m, x => firstEntry (eom m) x
  | .onlyBefore, m, x => fun t => (¬ m t → firstEntry (som m) x t) ∧ (m t → x t)

def lower (k : ScopeKind) (ct : CondType) (ctrue : Bool) (T : Timing) (m c r : Pred) :
    Pred :=
  wrap k m (inner (scopeBoundary k m) ct ctrue c (timingBody k T m r))

variable {b c c' e x y : Pred}

/-! ## Monotonicity in the body -/

theorem innerContU_mono (hxy : Entails x y) : Entails (innerContU c x) (innerContU c y) :=
  fun _ h j hc => hxy _ (h j hc)

theorem innerCont_mono (hxy : Entails x y) : Entails (innerCont b c x) (innerCont b c y) := by
  intro t h j
  rcases h j with h | h
  · exact Or.inl (fun hc => hxy _ (h hc))
  · exact Or.inr h

theorem innerTrigU_mono (hxy : Entails x y) : Entails (innerTrigU c x) (innerTrigU c y) := by
  intro t h
  exact ⟨fun j he => hxy _ (h.1 j he), fun hc => hxy t (h.2 hc)⟩

theorem innerTrig_mono (hxy : Entails x y) : Entails (innerTrig b c x) (innerTrig b c y) := by
  intro t h
  refine ⟨fun j => ?_, fun hc => hxy t (h.2 hc)⟩
  rcases h.1 j with h1 | h1
  · exact Or.inl (fun he => ⟨hxy _ (h1 he).1, (h1 he).2⟩)
  · exact Or.inr h1

theorem innerTrue_mono (hxy : Entails x y) : Entails (innerTrue c x) (innerTrue c y) :=
  fun t h hc => hxy t (h hc)

theorem inner_mono (ob : Option Pred) (ct : CondType) (ctrue : Bool) (c : Pred)
    (hxy : Entails x y) : Entails (inner ob ct ctrue c x) (inner ob ct ctrue c y) := by
  cases ob <;> cases ct <;> cases ctrue <;>
    first
    | exact innerContU_mono hxy
    | exact innerCont_mono hxy
    | exact innerTrigU_mono hxy
    | exact innerTrig_mono hxy
    | exact innerTrue_mono hxy

theorem reentry_mono (hxy : Entails x y) : Entails (reentry e x) (reentry e y) :=
  fun _ h j => Or.imp id (hxy _) (h j)

theorem firstEntry_mono (hxy : Entails x y) : Entails (firstEntry e x) (firstEntry e y) := by
  intro t h
  rcases h with ⟨j, ⟨he, hx⟩, hpre⟩ | h
  · exact Or.inl ⟨j, ⟨he, hxy _ hx⟩, hpre⟩
  · exact Or.inr h

theorem wrap_mono (k : ScopeKind) (m : Pred) (hxy : Entails x y) :
    Entails (wrap k m x) (wrap k m y) := by
  cases k with
  | global => exact hxy
  | onlyAfter => exact hxy
  | inMode =>
    intro t h
    change reentry (som m) x t ∧ (m t → x t) at h
    change reentry (som m) y t ∧ (m t → y t)
    exact ⟨reentry_mono hxy t h.1, fun hm => hxy t (h.2 hm)⟩
  | notInMode =>
    intro t h
    change reentry (eom m) x t ∧ (¬ m t → x t) at h
    change reentry (eom m) y t ∧ (¬ m t → y t)
    exact ⟨reentry_mono hxy t h.1, fun hm => hxy t (h.2 hm)⟩
  | onlyIn =>
    intro t h
    change reentry (eom m) x t ∧ (¬ m t → x t) at h
    change reentry (eom m) y t ∧ (¬ m t → y t)
    exact ⟨reentry_mono hxy t h.1, fun hm => hxy t (h.2 hm)⟩
  | beforeMode =>
    intro t h
    change x t ∨ m t at h
    change y t ∨ m t
    exact Or.imp (hxy t) id h
  | afterMode =>
    intro t h
    change firstEntry (eom m) x t at h
    change firstEntry (eom m) y t
    exact firstEntry_mono hxy t h
  | onlyBefore =>
    intro t h
    change (¬ m t → firstEntry (som m) x t) ∧ (m t → x t) at h
    change (¬ m t → firstEntry (som m) y t) ∧ (m t → y t)
    exact ⟨fun hm => firstEntry_mono hxy t (h.1 hm), fun hm => hxy t (h.2 hm)⟩

/-! ## The timing order on whole requirements -/

variable {T T' : Timing} {ct : CondType} {ctrue : Bool} {m r : Pred}

theorem lower_mono (k : ScopeKind) (hb : Entails (timingBody k T m r) (timingBody k T' m r)) :
    Entails (lower k ct ctrue T m c r) (lower k ct ctrue T' m c r) :=
  wrap_mono k m (inner_mono _ ct ctrue c hb)

/-- Global and after: no boundary. -/
theorem lower_unbounded (k : ScopeKind) (hk : k = .global ∨ k = .afterMode)
    (h : implies0 T T') : Entails (lower k ct ctrue T m c r) (lower k ct ctrue T' m c r) := by
  rcases hk with rfl | rfl
  · exact lower_mono .global (h r)
  · exact lower_mono .afterMode (h r)

/-- In, not in and before: a boundary. -/
theorem lower_bounded (k : ScopeKind)
    (hk : k = .inMode ∨ k = .notInMode ∨ k = .beforeMode) (h : impliesB T T') :
    Entails (lower k ct ctrue T m c r) (lower k ct ctrue T' m c r) := by
  rcases hk with rfl | rfl | rfl
  · exact lower_mono .inMode (h _ r)
  · exact lower_mono .notInMode (h _ r)
  · exact lower_mono .beforeMode (h _ r)

/-- Only in and only after: a boundary, under the negated body. -/
theorem lower_only_bounded (k : ScopeKind) (hk : k = .onlyIn ∨ k = .onlyAfter)
    (h : onlyImpliesB T T') :
    Entails (lower k ct ctrue T m c r) (lower k ct ctrue T' m c r) := by
  rcases hk with rfl | rfl
  · exact lower_mono .onlyIn (h _ r)
  · exact lower_mono .onlyAfter (h _ r)

/-- Only before: no boundary, under the negated body. -/
theorem lower_only_unbounded (h : onlyImplies0 T T') :
    Entails (lower .onlyBefore ct ctrue T m c r) (lower .onlyBefore ct ctrue T' m c r) :=
  lower_mono .onlyBefore (h r)

/-! ## The condition under a continual requirement -/

theorem innerContU_condition_anti (hc : Entails c' c) :
    Entails (innerContU c x) (innerContU c' x) :=
  fun _ h j hc' => h j (hc _ hc')

theorem innerCont_condition_anti (hc : Entails c' c) :
    Entails (innerCont b c x) (innerCont b c' x) := by
  intro t h j
  rcases h j with h | h
  · exact Or.inl (fun hc' => h (hc _ hc'))
  · exact Or.inr h

theorem inner_condition_anti (ob : Option Pred) (ctrue : Bool) (hc : Entails c' c) :
    Entails (inner ob .continual ctrue c x) (inner ob .continual ctrue c' x) := by
  cases ob <;> cases ctrue <;>
    first
    | exact innerContU_condition_anti hc
    | exact innerCont_condition_anti hc

theorem lower_condition_anti (k : ScopeKind) (hc : Entails c' c) :
    Entails (lower k .continual ctrue T m c r) (lower k .continual ctrue T m c' r) :=
  wrap_mono k m (inner_condition_anti _ ctrue hc)

end Fretish
