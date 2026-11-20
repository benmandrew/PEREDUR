import Psp.Scopes

/-!
# The PSP pattern order

`patLe c a b` is the pattern order of scope class `c`, the three classes of
`pattern_order_classes` in `docs/dev/psp-orders.json`. Every edge holds for
every count, as an implication between bodies: with no boundary for the
unbounded class, and at a point inside the scope (where the boundary does not
hold) for the other two. `pattern_sound` lifts each edge through the scope
wrappers to whole requirements under every scope of the class.

Bounded existence's count chain (`atMost_mono`) is by induction over the
count, in `Psp.Bodies`.
-/

namespace Psp

open Fretish

/-- The scopes that share a pattern order: `ScopeClass` in `include/psp/implication.hpp`. -/
inductive Cls where
  | unbounded
  | bounded
  | before
  deriving DecidableEq, Repr

/-- `scope_class` -/
def Scope.cls : Scope → Cls
  | .globally | .after => .unbounded
  | .between | .afterUntil => .bounded
  | .before => .before

/-- The pattern order of each class. -/
def patLe (c : Cls) : Pat → Pat → Prop
  | .absence, .absence | .absence, .precedence | .absence, .response
  | .absence, .bresp _ | .absence, .binv _ | .absence, .bexist _ => True
  | .universality, .universality | .universality, .precedence
  | .universality, .response | .universality, .bresp _ | .universality, .binv _
  | .universality, .brec _ => True
  | .universality, .existence => c ≠ .before
  | .existence, .existence | .precedence, .precedence | .response, .response => True
  | .bresp _, .response => True
  | .bresp n, .bresp m => n ≤ m
  | .binv _, .precedence | .binv _, .response | .binv _, .bresp _ => True
  | .binv n, .binv m => m ≤ n
  | .bexist n, .bexist m => n ≤ m
  | .brec n, .brec m => n ≤ m
  | .brec n, .bresp m => c = .unbounded ∧ n ≤ m
  | .brec _, .existence | .brec _, .response => c = .unbounded
  | _, _ => False

/-! ## Edges between bodies -/

variable {B P S : Pred} {t : Nat}

theorem absence_to (b : Pat) (hb : b = .precedence ∨ b = .response ∨ (∃ n, b = .bresp n) ∨
    (∃ n, b = .binv n) ∨ ∃ n, b = .bexist n) (h : body .absence B P S t) : body b B P S t := by
  rcases hb with rfl | rfl | ⟨n, rfl⟩ | ⟨n, rfl⟩ | ⟨n, rfl⟩
  · exact W_mono (fun _ => id) (fun _ h => Or.inr h) h
  · exact W_mono (fun _ hp hP => absurd hP hp) (fun _ => id) h
  · exact W_mono (fun _ hp hP => absurd hP hp) (fun _ => id) h
  · exact W_mono (fun _ hp hP => absurd hP hp) (fun _ => id) h
  · exact atMost_mono (Nat.zero_le n) h

theorem universality_to (b : Pat) (hb : b = .precedence ∨ b = .response ∨ (∃ n, b = .bresp n) ∨
    (∃ n, b = .binv n) ∨ (∃ n, b = .brec n) ∨ (b = .existence ∧ ¬ B t))
    (h : body .universality B P S t) : body b B P S t := by
  rcases hb with rfl | rfl | ⟨n, rfl⟩ | ⟨n, rfl⟩ | ⟨n, rfl⟩ | ⟨rfl, hB⟩
  · rcases W_now h with h | h
    · exact W_of_now (Or.inr h)
    · exact W_of_now (Or.inl h)
  · exact W_mono (fun _ ⟨hs, hb⟩ _ => ⟨0, by simpa using ⟨hs, hb⟩, fun i hi => absurd hi
      (Nat.not_lt_zero i)⟩) (fun _ => id) (W_strict h)
  · exact W_mono (fun _ ⟨hs, hb⟩ _ => ⟨0, Nat.zero_le _, by simpa using ⟨hs, hb⟩,
      fun i hi => absurd hi (Nat.not_lt_zero i)⟩) (fun _ => id) (W_strict h)
  · exact W_mono (fun _ hW _ j _ => W_reach hW j) (fun _ => id) (W_suffix h)
  · exact W_mono (fun _ hs => ⟨0, Nat.zero_le _, Or.inl (by simpa using hs)⟩) (fun _ => id) h
  · rcases W_now h with h | h
    · exact absurd h hB
    · exact ⟨0, by simpa using ⟨h, hB⟩, fun i hi => absurd hi (Nat.not_lt_zero i)⟩

theorem bresp_to {n : Nat} (b : Pat) (hb : b = .response ∨ ∃ m, b = .bresp m ∧ n ≤ m)
    (h : body (.bresp n) B P S t) : body b B P S t := by
  rcases hb with rfl | ⟨m, rfl, hm⟩
  · exact W_mono (fun _ h hP => let ⟨k, _, hk, hi⟩ := h hP; ⟨k, hk, hi⟩) (fun _ => id) h
  · exact W_mono (fun _ h hP => let ⟨k, hk, hk', hi⟩ := h hP; ⟨k, by omega, hk', hi⟩)
      (fun _ => id) h

/-- The trigger point of a bounded invariance holds S. -/
theorem heldB_now {n : Nat} {u : Nat} (h : heldB n B S u) (hb : ¬ B u) : S u := by
  rcases h 0 (Nat.zero_le _) with h | ⟨i, hi, hb'⟩
  · simpa using h
  · have : i = 0 := by omega
    subst this
    exact absurd (by simpa using hb') hb

theorem binv_to {n : Nat} (b : Pat) (hb : b = .precedence ∨ b = .response ∨ (∃ m, b = .bresp m) ∨
    ∃ m, b = .binv m ∧ m ≤ n) (h : body (.binv n) B P S t) : body b B P S t := by
  rcases hb with rfl | rfl | ⟨m, rfl⟩ | ⟨m, rfl, hm⟩
  · exact W_prec (W_mono (fun _ ⟨h, hb⟩ hP => Or.inl (heldB_now (h hP) hb)) (fun _ => id)
      (W_strict h))
  · exact W_mono (fun _ ⟨h, hb⟩ hP => ⟨0, by simpa using ⟨heldB_now (h hP) hb, hb⟩,
      fun i hi => absurd hi (Nat.not_lt_zero i)⟩) (fun _ => id) (W_strict h)
  · exact W_mono (fun _ ⟨h, hb⟩ hP => ⟨0, Nat.zero_le _, by simpa using ⟨heldB_now (h hP) hb, hb⟩,
      fun i hi => absurd hi (Nat.not_lt_zero i)⟩) (fun _ => id) (W_strict h)
  · exact W_mono (fun _ h hP j hj => h hP j (by omega)) (fun _ => id) h

theorem brec_to {n m : Nat} (hm : n ≤ m) (h : body (.brec n) B P S t) : body (.brec m) B P S t :=
  W_mono (fun _ ⟨k, hk, h⟩ => ⟨k, by omega, h⟩) (fun _ => id) h

/-- With no boundary, bounded recurrence implies the responses and existence. -/
theorem brec_to_ff {n : Nat} (b : Pat) (hb : b = .existence ∨ b = .response ∨
    ∃ m, b = .bresp m ∧ n ≤ m) (h : body (.brec n) ff P S t) : body b ff P S t := by
  rcases hb with rfl | rfl | ⟨m, rfl, hm⟩
  · rcases W_now h with h | ⟨k, _, h⟩
    · exact h.elim
    · exact ⟨k, ⟨h.resolve_right id, id⟩, fun _ _ => id⟩
  · exact W_mono (fun _ ⟨k, _, h⟩ _ => ⟨k, ⟨h.resolve_right id, id⟩, fun _ _ => id⟩)
      (fun _ => id) h
  · exact W_mono (fun _ ⟨k, hk, h⟩ _ => ⟨k, by omega, ⟨h.resolve_right id, id⟩, fun _ _ => id⟩)
      (fun _ => id) h

/-- A body whose boundary holds now holds, except existence. -/
theorem body_now {a : Pat} (hb : B t) (ha : a ≠ .existence) : body a B P S t := by
  cases a with
  | existence => exact absurd rfl ha
  | precedence => exact W_of_now (Or.inr hb)
  | bexist k =>
    cases k with
    | zero => exact W_of_now hb
    | succ k => exact W_of_now (Or.inl hb)
  | _ => exact W_of_now hb

/-- Every edge of the bounded order holds inside the scope. -/
theorem edge_bounded {a b : Pat} (h : patLe .bounded a b) (hB : ¬ B t)
    (ha : body a B P S t) : body b B P S t := by
  cases a <;> cases b <;> simp only [patLe, reduceCtorEq, false_and] at h <;>
  first
  | exact ha
  | exact absence_to _ (by simp) ha
  | exact universality_to _ (Or.inr (Or.inr (Or.inr (Or.inr (Or.inr ⟨rfl, hB⟩))))) ha
  | exact universality_to _ (by simp) ha
  | exact bresp_to _ (by simp [h]) ha
  | exact binv_to _ (by simp [h]) ha
  | exact atMost_mono h ha
  | exact brec_to h ha
  | (subst_vars; exact ha)

theorem patLe_before {a b : Pat} (h : patLe .before a b) :
    patLe .bounded a b ∧ (b = .existence → a = .existence) := by
  cases a <;> cases b <;> simp_all [patLe]

theorem patLe_bounded_unbounded {a b : Pat} (h : patLe .bounded a b) : patLe .unbounded a b := by
  cases a <;> cases b <;> simp_all [patLe]

/-- Every edge of the unbounded order holds with no boundary. -/
theorem edge_unbounded {a b : Pat} (h : patLe .unbounded a b) (ha : body a ff P S t) :
    body b ff P S t := by
  cases a <;> cases b <;> simp only [patLe] at h <;>
  first
  | exact ha
  | exact absence_to _ (by simp) ha
  | exact universality_to _ (Or.inr (Or.inr (Or.inr (Or.inr (Or.inr ⟨rfl, id⟩))))) ha
  | exact universality_to _ (by simp) ha
  | exact bresp_to _ (by simp [h]) ha
  | exact binv_to _ (by simp [h]) ha
  | exact atMost_mono h ha
  | exact brec_to h ha
  | exact brec_to_ff _ (by simp [h]) ha
  | exact brec_to_ff _ (by simp [h.2]) ha

/-- Every edge of a scope's class holds between whole requirements. -/
theorem pattern_sound (k : Scope) {a b : Pat} (h : patLe k.cls a b) : Implies k a k b := by
  intro P S Q R t ht
  cases k with
  | globally => exact edge_unbounded h ht
  | after => exact W_mono (fun _ => id) (fun u ⟨hq, hb⟩ => ⟨hq, edge_unbounded h hb⟩) ht
  | between => exact fun j ⟨hq, hr, hf⟩ => edge_bounded h hr (ht j ⟨hq, hr, hf⟩)
  | afterUntil => exact fun j ⟨hq, hr⟩ => edge_bounded h hr (ht j ⟨hq, hr⟩)
  | before =>
    intro hf
    obtain ⟨hb, he⟩ := patLe_before h
    by_cases hr : R t
    · by_cases hbe : b = .existence
      · rw [he hbe] at ht; rw [hbe]; exact ht hf
      · exact body_now hr hbe
    · exact edge_bounded hb hr (ht hf)

end Psp
