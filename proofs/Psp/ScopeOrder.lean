import Psp.PatternOrder

/-!
# The PSP scope order

`scopeLe c k k'` is the scope order of cell `c`, the four cells of
`scope_order_cells` in `docs/dev/psp-orders.json`. Each edge holds for every
pattern of its cell at every count (`scope_sound`).

Two facts carry the edges. A body with no boundary in the invariant or
response cell holds again at every later point (`body_suffix`), so it holds
at the first Q and at every Q after. A body in the invariant cell or
precedence is monotone in its boundary (`body_mono_b`), so the body with no
boundary implies the body under R.
-/

namespace Psp

open Fretish

/-- The patterns that share a scope order: `PatternCell` in `include/psp/implication.hpp`. -/
inductive Cell where
  | invariant
  | existence
  | precedence
  | response
  deriving DecidableEq, Repr

/-- `pattern_cell` -/
def Pat.cell : Pat → Cell
  | .existence => .existence
  | .precedence => .precedence
  | .response | .bresp _ => .response
  | _ => .invariant

/-- The scope order of each cell. -/
def scopeLe : Cell → Scope → Scope → Prop
  | _, .globally, .globally | _, .before, .before | _, .after, .after
  | _, .between, .between | _, .afterUntil, .afterUntil => True
  | _, .afterUntil, .between => True
  | .invariant, .globally, _ => True
  | .invariant, .after, .afterUntil | .invariant, .after, .between => True
  | .precedence, .globally, .before => True
  | .response, .globally, .after => True
  | _, _, _ => False

variable {a : Pat} {B B' P S Q R : Pred} {t : Nat}

theorem atMost_mono_b {P : Pred} (hbb : ∀ u, B u → B' u) :
    ∀ {k t}, atMost B P k t → atMost B' P k t
  | 0, _, h => W_mono (fun _ => id) hbb h
  | k + 1, _, h => by
    simp only [atMost] at h ⊢
    exact W_mono (fun _ => id) (fun _ h => h.imp (hbb _)
      (W_mono (fun _ => id) (fun _ h => h.imp (hbb _) (atMost_mono_b hbb)))) h

/-- A body in the invariant cell, or precedence, is monotone in its boundary. -/
theorem body_mono_b (hbb : ∀ u, B u → B' u) (ha : a.cell = .invariant ∨ a = .precedence)
    (h : body a B P S t) : body a B' P S t := by
  cases a with
  | absence => exact W_mono (fun _ => id) hbb h
  | universality => exact W_mono (fun _ => id) hbb h
  | precedence => exact W_mono (fun _ => id) (fun _ h => h.imp id (hbb _)) h
  | binv n =>
    exact W_mono (fun _ h hP j hj => (h hP j hj).imp id (fun ⟨i, hi, hb⟩ => ⟨i, hi, hbb _ hb⟩))
      hbb h
  | bexist k => exact atMost_mono_b hbb h
  | brec c => exact W_mono (fun _ ⟨k, hk, h⟩ => ⟨k, hk, h.imp id (hbb _)⟩) hbb h
  | _ => simp [Pat.cell] at ha

theorem G_suffix {p : Pred} (h : G p t) (j : Nat) : G p (t + j) :=
  fun i => by rw [Nat.add_assoc]; exact h (j + i)

theorem W_ff_suffix {p : Pred} (h : W p ff t) (j : Nat) : W p ff (t + j) :=
  W_ff.mpr (G_suffix (W_ff.mp h) j)

/-- A body with no boundary in the invariant or response cell holds again later. -/
theorem body_suffix (ha : a.cell = .invariant ∨ a.cell = .response) (h : body a ff P S t)
    (j : Nat) : body a ff P S (t + j) := by
  cases a with
  | existence => simp [Pat.cell] at ha
  | precedence => simp [Pat.cell] at ha
  | bexist k =>
    induction j with
    | zero => exact h
    | succ j ih => exact atMost_step id ih
  | absence => exact W_ff_suffix h j
  | universality => exact W_ff_suffix h j
  | response => exact W_ff_suffix h j
  | bresp _ => exact W_ff_suffix h j
  | binv _ => exact W_ff_suffix h j
  | brec _ => exact W_ff_suffix h j

/-- The body with no boundary at every Q from the first one on. -/
theorem after_at (ha : a.cell = .invariant ∨ a.cell = .response)
    (h : W (neg Q) (fun u => Q u ∧ body a ff P S u) t) {j : Nat} (hq : Q (t + j)) :
    body a ff P S (t + j) := by
  rcases W_reach h j with h | ⟨i, hi, _, hb⟩
  · exact absurd hq h
  · have := body_suffix ha hb (j - i)
    rwa [show t + i + (j - i) = t + j by omega] at this

/-- Every edge of a pattern's cell holds between whole requirements. -/
theorem scope_sound {k k' : Scope} (h : scopeLe a.cell k k') : Implies k a k' a := by
  intro P S Q R t ht
  have inv : a.cell = .invariant → a.cell = .invariant ∨ a.cell = .response := Or.inl
  have binv : a.cell = .invariant → a.cell = .invariant ∨ a = .precedence := Or.inl
  have ff_le : ∀ u, ff u → R u := fun _ h => h.elim
  cases k <;> cases k' <;> revert h <;> cases hc : a.cell <;> simp only [scopeLe] <;> intro h <;>
    simp only [lower] at ht ⊢
  all_goals first
    | exact h.elim
    | exact ht
    | exact fun j ⟨hq, hr, _⟩ => ht j ⟨hq, hr⟩
    -- globally into before
    | exact fun _ => body_mono_b ff_le (binv hc) ht
    | exact fun _ => body_mono_b ff_le (Or.inr (by cases a <;> simp_all [Pat.cell])) ht
    -- globally into after
    | (by_cases hq : ∃ j, Q (t + j)
       · obtain ⟨j, hj, hmin⟩ := exists_first hq
         first
         | exact Or.inl ⟨j, ⟨hj, body_suffix (inv hc) ht j⟩, hmin⟩
         | exact Or.inl ⟨j, ⟨hj, body_suffix (Or.inr hc) ht j⟩, hmin⟩
       · exact Or.inr (fun j hj => hq ⟨j, hj⟩))
    -- globally into between and after-until
    | exact fun j _ => body_mono_b ff_le (binv hc) (body_suffix (inv hc) ht j)
    -- after into between and after-until
    | exact fun j ⟨hq, _⟩ => body_mono_b ff_le (binv hc) (after_at (inv hc) ht hq)

end Psp
