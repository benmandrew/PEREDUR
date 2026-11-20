import Psp.Cpp

/-!
# Polarity of each field

`fieldPolarity` (the C++ `field_polarity`) says how a requirement depends on
each of P, S, Q and R. `polarity_sound` proves every answer it gives:

* no answer: the requirement does not read the field;
* monotone: a weaker value of the field gives a weaker requirement;
* antitone: a stronger value of the field gives a weaker requirement;
* `Pol.none`: no claim, so the search must move the field both ways.

Soundness is all the search needs (`Psp.Chain`). That `Pol.none` is never
too cautious is not proved here; `scripts/check_lean_psp_orders.py` compares
the table with the polarities SPOT measured in `docs/dev/psp-orders.json`.
-/

namespace Psp

open Fretish

/-- The requirement with field `f` set to `X` and the other fields as given. -/
def lowerF (f : FField) (k : Scope) (a : Pat) (P S Q R X : Pred) : Pred :=
  match f with
  | .P => lower k a X S Q R
  | .S => lower k a P X Q R
  | .Q => lower k a P S X R
  | .R => lower k a P S Q X

/-- What each answer of `fieldPolarity` claims. -/
def PolSound (f : FField) (k : Scope) (a : Pat) : Option Pol → Prop
  | none => ∀ P S Q R X Y, lowerF f k a P S Q R X = lowerF f k a P S Q R Y
  | some .monotone => ∀ P S Q R X Y : Pred, (∀ u, X u → Y u) →
      Entails (lowerF f k a P S Q R X) (lowerF f k a P S Q R Y)
  | some .antitone => ∀ P S Q R X Y : Pred, (∀ u, Y u → X u) →
      Entails (lowerF f k a P S Q R X) (lowerF f k a P S Q R Y)
  | some .none => True

variable {a : Pat} {B P P' S S' : Pred} {t : Nat}

/-! ## The bodies -/

theorem body_anti_P (ha : ∀ n, a ≠ .bexist n) (hP : ∀ u, P' u → P u)
    (h : body a B P S t) : body a B P' S t := by
  cases a with
  | absence => exact W_mono (fun u h hp => h (hP u hp)) (fun _ => id) h
  | universality => exact h
  | existence => exact h
  | precedence => exact W_mono (fun u h hp => h (hP u hp)) (fun _ => id) h
  | response => exact W_mono (fun u h hp => h (hP u hp)) (fun _ => id) h
  | bresp _ => exact W_mono (fun u h hp => h (hP u hp)) (fun _ => id) h
  | binv _ => exact W_mono (fun u h hp => h (hP u hp)) (fun _ => id) h
  | bexist n => exact absurd rfl (ha n)
  | brec _ => exact h

theorem body_mono_S (hS : ∀ u, S u → S' u) (h : body a B P S t) : body a B P S' t := by
  cases a with
  | absence => exact h
  | bexist _ => exact h
  | universality => exact W_mono hS (fun _ => id) h
  | existence =>
    obtain ⟨j, ⟨hs, hb⟩, hi⟩ := h
    exact ⟨j, ⟨hS _ hs, hb⟩, hi⟩
  | precedence => exact W_mono (fun _ => id) (fun u h => h.imp (hS u) id) h
  | response =>
    exact W_mono (fun _ h hp => let ⟨j, ⟨hs, hb⟩, hi⟩ := h hp; ⟨j, ⟨hS _ hs, hb⟩, hi⟩)
      (fun _ => id) h
  | bresp _ =>
    exact W_mono (fun _ h hp => let ⟨k, hk, ⟨hs, hb⟩, hi⟩ := h hp; ⟨k, hk, ⟨hS _ hs, hb⟩, hi⟩)
      (fun _ => id) h
  | binv _ => exact W_mono (fun _ h hp j hj => (h hp j hj).imp (hS _) id) (fun _ => id) h
  | brec _ => exact W_mono (fun _ ⟨k, hk, h⟩ => ⟨k, hk, h.imp (hS _) id⟩) (fun _ => id) h

/-! ## Each kind of answer -/

theorem unread_P (h : a.usesP = false) (k : Scope) : PolSound .P k a none := by
  intro P S Q R X Y
  cases a <;> simp [Pat.usesP] at h <;> cases k <;> rfl

theorem unread_S (h : a.usesS = false) (k : Scope) : PolSound .S k a none := by
  intro P S Q R X Y
  cases a <;> simp [Pat.usesS] at h <;> cases k <;> rfl

theorem unread_Q {k : Scope} (h : k.usesQ = false) : PolSound .Q k a none := by
  intro P S Q R X Y
  cases k <;> simp [Scope.usesQ] at h <;> rfl

theorem unread_R {k : Scope} (h : k.usesR = false) : PolSound .R k a none := by
  intro P S Q R X Y
  cases k <;> simp [Scope.usesR] at h <;> rfl

theorem anti_P (ha : ∀ n, a ≠ .bexist n) (k : Scope) : PolSound .P k a (some .antitone) :=
  fun _ _ _ _ _ _ hXY => lower_mono k (fun _ => body_anti_P ha hXY)
    (fun _ _ => body_anti_P ha hXY) (fun _ _ => body_anti_P ha hXY)

theorem mono_S (k : Scope) : PolSound .S k a (some .monotone) :=
  fun _ _ _ _ _ _ hXY => lower_mono k (fun _ => body_mono_S hXY)
    (fun _ _ => body_mono_S hXY) (fun _ _ => body_mono_S hXY)

theorem anti_Q_between : PolSound .Q .between a (some .antitone) :=
  fun _ _ _ _ _ _ hYX _ h j ⟨hq, hr, hf⟩ => h j ⟨hYX _ hq, hr, hf⟩

theorem anti_Q_afterUntil : PolSound .Q .afterUntil a (some .antitone) :=
  fun _ _ _ _ _ _ hYX _ h j ⟨hq, hr⟩ => h j ⟨hYX _ hq, hr⟩

/-- After opens at the first Q. A body that holds again at every later point
holds at the first point of a stronger Q, which comes no earlier. -/
theorem anti_Q_after (ha : a.cell = .invariant ∨ a.cell = .response) :
    PolSound .Q .after a (some .antitone) := by
  intro P S Q R X Y hYX t h
  by_cases hy : ∃ j, Y (t + j)
  · obtain ⟨j, hj, hmin⟩ := exists_first hy
    exact Or.inl ⟨j, ⟨hj, after_at ha h (hYX _ hj)⟩, hmin⟩
  · exact Or.inr (fun j hj => hy ⟨j, hj⟩)

/-- Under after-until a weaker R closes the scope no earlier, and the bodies
of the invariant cell and precedence are monotone in their boundary. -/
theorem mono_R_afterUntil (ha : a.cell = .invariant ∨ a = .precedence) :
    PolSound .R .afterUntil a (some .monotone) :=
  fun _ _ _ _ _ _ hXY _ h j ⟨hq, hr⟩ =>
    body_mono_b hXY ha (h j ⟨hq, fun hx => hr (hXY _ hx)⟩)

/-- Before with existence: a stronger R still arrives whenever a weaker one
does, and S then comes before the stronger R. -/
theorem anti_R_before : PolSound .R .before .existence (some .antitone) := by
  intro P S Q R X Y hYX t h hf
  obtain ⟨j, hy⟩ := hf
  obtain ⟨i, ⟨hs, hx⟩, hb⟩ := h ⟨j, hYX _ hy⟩
  exact ⟨i, ⟨hs, fun hy => hx (hYX _ hy)⟩, fun m hm hy => hb m hm (hYX _ hy)⟩

/-- Every answer of `field_polarity` holds, at every count. -/
theorem polarity_sound (f : FField) (a : Pat) (k : Scope) :
    PolSound f k a (fieldPolarity f a k) := by
  cases f <;> cases a <;> cases k <;>
    simp only [fieldPolarity, Pat.usesP, Pat.usesS, Scope.usesQ, Scope.usesR, Pat.cell',
      Bool.not_true, Bool.not_false, Bool.and_true, Bool.and_false, Bool.or_true, Bool.or_false,
      decide_true, decide_false, reduceCtorEq, if_true, if_false, Bool.false_eq_true]
  all_goals first
    | trivial
    | exact unread_P rfl _
    | exact unread_S rfl _
    | exact unread_Q rfl
    | exact unread_R rfl
    | exact anti_P (fun _ h => by cases h) _
    | exact mono_S _
    | exact anti_Q_between
    | exact anti_Q_afterUntil
    | exact anti_Q_after (by simp [Pat.cell])
    | exact mono_R_afterUntil (by simp [Pat.cell])
    | exact anti_R_before

end Psp
