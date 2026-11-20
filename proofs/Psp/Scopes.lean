import Psp.Bodies

/-!
# PSP scopes

The model of the scope wrappers in `requirement_to_ltl` (`src/psp/lowering.cpp`).
Q and R are arbitrary predicates. Before reads the body with boundary R from
the first point, after reads it with no boundary from the first Q, and between
and after-until read it with boundary R at every Q where R does not hold.

`Implies k a k' b` is the implication between two requirements over the same
P, S, Q and R, at every position of every trace: what `scripts/psp_field_orders.py`
asks SPOT over free atoms.
-/

namespace Psp

open Fretish

inductive Scope where
  | globally
  | before
  | after
  | between
  | afterUntil
  deriving DecidableEq, Repr

/-- `requirement_to_ltl` for a pattern requirement. -/
def lower (k : Scope) (a : Pat) (P S Q R : Pred) : Pred :=
  match k with
  | .globally => body a ff P S
  | .before => fun t => F R t → body a R P S t
  | .after => W (neg Q) (fun t => Q t ∧ body a ff P S t)
  | .between => G (fun t => Q t ∧ ¬ R t ∧ F R t → body a R P S t)
  | .afterUntil => G (fun t => Q t ∧ ¬ R t → body a R P S t)

/-- Requirement `(k, a)` implies `(k', b)` over every P, S, Q and R. -/
def Implies (k : Scope) (a : Pat) (k' : Scope) (b : Pat) : Prop :=
  ∀ P S Q R, Entails (lower k a P S Q R) (lower k' b P S Q R)

/-! ## The wrappers are monotone in the body -/

variable {a a' : Pat} {P S Q R P' S' Q' R' : Pred}

/-- A body implication under the boundary each scope gives lifts to the scope. -/
theorem lower_mono (k : Scope)
    (h0 : ∀ t, body a ff P S t → body a' ff P' S' t)
    (hR : ∀ t, ¬ R t → body a R P S t → body a' R P' S' t)
    (hF : ∀ t, F R t → body a R P S t → body a' R P' S' t) :
    Entails (lower k a P S Q R) (lower k a' P' S' Q R) := by
  intro t h
  cases k with
  | globally => exact h0 t h
  | before => exact fun hf => hF t hf (h hf)
  | after => exact W_mono (fun _ => id) (fun u ⟨hq, hb⟩ => ⟨hq, h0 u hb⟩) h
  | between => exact fun j ⟨hq, hr, hf⟩ => hR _ hr (h j ⟨hq, hr, hf⟩)
  | afterUntil => exact fun j ⟨hq, hr⟩ => hR _ hr (h j ⟨hq, hr⟩)

/-! ## Reading a requirement at position 0

With Q true at position 0 alone, each scope reads one body there. The
counterexamples of `Psp.Exact` and `Psp.ScopeExact` are built from these. -/

abbrev pt (j : Nat) : Pred := fun x => x = j

theorem lower_globally {t : Nat} : lower .globally a P S Q R t ↔ body a ff P S t := Iff.rfl

theorem lower_after_pt0 : lower .after a P S (pt 0) R 0 ↔ body a ff P S 0 := by
  constructor
  · intro h
    rcases W_now h with ⟨_, h⟩ | h
    · exact h
    · exact absurd rfl h
  · intro h
    exact W_of_now ⟨rfl, h⟩

theorem lower_between_pt0 :
    lower .between a P S (pt 0) R 0 ↔ (¬ R 0 → F R 0 → body a R P S 0) := by
  constructor
  · intro h hr hf
    exact h 0 ⟨rfl, hr, hf⟩
  · intro h j ⟨hq, hr, hf⟩
    simp only [pt] at hq
    have : j = 0 := by omega
    subst this
    exact h hr hf

theorem lower_afterUntil_pt0 :
    lower .afterUntil a P S (pt 0) R 0 ↔ (¬ R 0 → body a R P S 0) := by
  constructor
  · intro h hr
    exact h 0 ⟨rfl, hr⟩
  · intro h j ⟨hq, hr⟩
    simp only [pt] at hq
    have : j = 0 := by omega
    subst this
    exact h hr

end Psp
