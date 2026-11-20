import Psp.Polarity

/-!
# The PSP chain search

`Search.reach` transcribes `RuleSearch` in `src/psp/implication.cpp`, which
`requirement_implies` runs when two lowerings differ. A state is the set of
fields already moved from `src`'s value to `dst`'s, over the six fields
pattern, scope, P, S, Q and R. A step moves one more field if `stepHolds`
accepts it in the context of the current state: the pattern along
`pattern_implies` at the state's scope class, the scope along `scope_implies`
in the state's pattern cell, and a formula field along `field_polarity` at the
state's pattern and scope.

A requirement here has all four formulas. Where the C++ requirement has no
value for a field, `RuleSearch` gives it the other side's value, or `true`,
and `Psp.Polarity` proves that a requirement which does not read a field does
not depend on it, so the choice changes no lowering.

The propositional checks are not transcribed. `formula_implies` decides them
with SPOT, and here they are eight answers that `Search.Sound` assumes
correct.

`RuleSearch` memoises visited states. Each step sets a new bit, so no state is
its own descendant, a visited state has already failed, and a search of depth
six reaches every state. `reach` leaves the memo out.

`reach_sound` proves that every accepted pair implies: each step is one of the
proved orders or polarities at the state's other fields, and implication is
transitive.
-/

namespace Psp

open Fretish

inductive Fld where
  | pattern
  | scope
  | P
  | S
  | Q
  | R
  deriving DecidableEq

def Fld.all : List Fld := [.pattern, .scope, .P, .S, .Q, .R]

/-- The fields already moved to `dst`'s value. -/
abbrev Moved := Fld → Bool

def Moved.set (mv : Moved) (f : Fld) : Moved := fun g => if g = f then true else mv g

/-- A requirement with a value for every formula field. -/
structure Req where
  pat : Pat
  scope : Scope
  P : Pred
  S : Pred
  Q : Pred
  R : Pred

def Req.sem (q : Req) : Pred := lower q.scope q.pat q.P q.S q.Q q.R

def Req.get (q : Req) : FField → Pred
  | .P => q.P
  | .S => q.S
  | .Q => q.Q
  | .R => q.R

/-- The state: each field from `r'` if moved, else from `r`. -/
def mix (mv : Moved) (r r' : Req) : Req where
  pat := if mv .pattern then r'.pat else r.pat
  scope := if mv .scope then r'.scope else r.scope
  P := if mv .P then r'.P else r.P
  S := if mv .S then r'.S else r.S
  Q := if mv .Q then r'.Q else r.Q
  R := if mv .R then r'.R else r.R

/-- A search, with `formula_implies`'s answers for each formula field: `fwd`
for `src`'s value implying `dst`'s (`onward`), `back` for the converse. -/
structure Search where
  src : Req
  dst : Req
  fwd : FField → Bool
  back : FField → Bool

/-- The answers are correct. -/
structure Search.Sound (Sr : Search) : Prop where
  fwd : ∀ f, Sr.fwd f = true → Entails (Sr.src.get f) (Sr.dst.get f)
  back : ∀ f, Sr.back f = true → Entails (Sr.dst.get f) (Sr.src.get f)

abbrev Search.state (Sr : Search) (mv : Moved) : Req := mix mv Sr.src Sr.dst

/-- `formula_step` -/
def Search.formulaStep (Sr : Search) (f : FField) (a : Pat) (k : Scope) : Bool :=
  match fieldPolarity f a k with
  | none => true
  | some .monotone => Sr.fwd f
  | some .antitone => Sr.back f
  | some .none => Sr.fwd f && Sr.back f

/-- `step_holds` -/
def Search.stepHolds (Sr : Search) (mv : Moved) : Fld → Bool
  | .pattern => patternImplies Sr.src.pat Sr.dst.pat (Sr.state mv).scope.cls'
  | .scope => scopeImplies Sr.src.scope Sr.dst.scope (Sr.state mv).pat.cell'
  | .P => Sr.formulaStep .P (Sr.state mv).pat (Sr.state mv).scope
  | .S => Sr.formulaStep .S (Sr.state mv).pat (Sr.state mv).scope
  | .Q => Sr.formulaStep .Q (Sr.state mv).pat (Sr.state mv).scope
  | .R => Sr.formulaStep .R (Sr.state mv).pat (Sr.state mv).scope

/-- `reach`, to depth `n`. -/
def Search.reachN (Sr : Search) : Nat → Moved → Bool
  | 0, mv => Fld.all.all mv
  | n + 1, mv =>
    Fld.all.all mv ||
      Fld.all.any (fun f => !mv f && Sr.stepHolds mv f && Sr.reachN n (mv.set f))

/-- `reaches_dest`, from the fields `src` and `dst` already share. -/
def Search.reach (Sr : Search) (start : Moved) : Bool := Sr.reachN 6 start

/-! ## Soundness -/

variable {Sr : Search} {mv : Moved}

theorem eq_of_entails {p q : Pred} (h : Entails p q) (h' : Entails q p) : p = q :=
  funext fun t => propext ⟨h t, h' t⟩

/-- A formula step, at any values of the other fields. -/
theorem formula_sound (hS : Sr.Sound) {f : FField} {a : Pat} {k : Scope}
    (h : Sr.formulaStep f a k = true) (P S Q R : Pred) :
    Entails (lowerF f k a P S Q R (Sr.src.get f)) (lowerF f k a P S Q R (Sr.dst.get f)) := by
  have hp := polarity_sound f a k
  unfold Search.formulaStep at h
  revert hp h
  cases fieldPolarity f a k with
  | none =>
    intro _ hp
    rw [hp P S Q R (Sr.src.get f) (Sr.dst.get f)]
    exact fun _ => id
  | some p =>
    cases p with
    | monotone => exact fun h hp => hp P S Q R _ _ (hS.fwd f h)
    | antitone => exact fun h hp => hp P S Q R _ _ (hS.back f h)
    | none =>
      intro h _
      simp only [Bool.and_eq_true] at h
      rw [eq_of_entails (hS.fwd f h.1) (hS.back f h.2)]
      exact fun _ => id

theorem step_sound (hS : Sr.Sound) (f : Fld) (hf : mv f = false) (h : Sr.stepHolds mv f = true) :
    Entails (Sr.state mv).sem (Sr.state (mv.set f)).sem := by
  cases f <;>
    simp only [Search.stepHolds, Search.state, mix, Req.sem, Moved.set, hf, reduceCtorEq,
      ↓reduceIte, Bool.false_eq_true] at h ⊢
  · rw [patternImplies_iff, cls_eq] at h
    exact pattern_sound _ h _ _ _ _
  · rw [scopeImplies_iff, cell_eq] at h
    exact scope_sound h _ _ _ _
  · exact formula_sound hS h ff _ _ _
  · exact formula_sound hS h _ ff _ _
  · exact formula_sound hS h _ _ ff _
  · exact formula_sound hS h _ _ _ ff

theorem state_all (h : Fld.all.all mv = true) : Sr.state mv = Sr.dst := by
  simp only [Fld.all, List.all_cons, List.all_nil, Bool.and_true, Bool.and_eq_true] at h
  simp [Search.state, mix, h]

theorem reachN_sound (hS : Sr.Sound) : ∀ n mv, Sr.reachN n mv = true →
    Entails (Sr.state mv).sem Sr.dst.sem
  | 0, mv, h => by rw [state_all h]; exact fun _ => id
  | n + 1, mv, h => by
    simp only [Search.reachN, Bool.or_eq_true, List.any_eq_true, Bool.and_eq_true,
      Bool.not_eq_true'] at h
    rcases h with h | ⟨f, -, ⟨hf, hs⟩, hr⟩
    · rw [state_all h]; exact fun _ => id
    · exact fun t ht => reachN_sound hS n _ hr t (step_sound hS f hf hs t ht)

/-- Every pair `reach` accepts implies, from a start that marks only fields
the two requirements share. Patterns need not be valid: the orders' edges
hold at every count. -/
theorem reach_sound (hS : Sr.Sound) {start : Moved} (hstart : Sr.state start = Sr.src)
    (h : Sr.reach start = true) : Entails Sr.src.sem Sr.dst.sem :=
  hstart ▸ reachN_sound hS 6 start h

end Psp
