import Psp.Exact

/-!
# The scope order is exact

`scope_exact`: for every pattern with counts of at least 1, one scope implies
another over every P, S, Q and R exactly when `scopeLe` of the pattern's cell
relates them. This holds for every count.

Most non-edges are refuted by one pattern-independent recipe: Q or R chosen
so that the stronger scope never opens (`vacuous_*`), and a trace on which the
pattern's body fails at the point where the weaker scope reads it
(`falsifier`). The rest are refuted by four small traces: `wx` for existence
and the responses, `wp` for precedence, and `ga_*` for globally into after.
-/

namespace Psp

open Fretish

set_option linter.unusedSimpArgs false

variable {a : Pat} {P S Q R : Pred}

/-- Every pattern's body fails at point `o` on some trace, whatever the
boundary does outside `[o, o + L]`. -/
theorem falsifier (a : Pat) (o : Nat) :
    ∃ P S : Pred, ∃ L : Nat, ∀ B : Pred, (∀ u, o ≤ u → u ≤ o + L → ¬ B u) →
      ¬ body a B P S o := by
  cases a with
  | absence => exact ⟨tt, tt, 0, fun B hB => not_W 0 (fun h => h trivial)
      (fun j hj => hB _ (by omega) (by omega))⟩
  | universality => exact ⟨tt, ff, 0, fun B hB => not_W 0 id
      (fun j hj => hB _ (by omega) (by omega))⟩
  | existence => exact ⟨tt, ff, 0, fun B _ ⟨_, ⟨h, _⟩, _⟩ => h⟩
  | precedence => exact ⟨tt, ff, 0, fun B hB => not_W 0 (fun h => h trivial)
      (fun j hj h => h.elim id (hB _ (by omega) (by omega)))⟩
  | response => exact ⟨tt, ff, 0, fun B hB => not_W 0 (fun h => (h trivial).elim
      (fun _ ⟨⟨h, _⟩, _⟩ => h)) (fun j hj => hB _ (by omega) (by omega))⟩
  | bresp n => exact ⟨tt, ff, 0, fun B hB => not_W 0 (fun h => (h trivial).elim
      (fun _ ⟨_, ⟨h, _⟩, _⟩ => h)) (fun j hj => hB _ (by omega) (by omega))⟩
  | binv n => exact ⟨tt, ff, 0, fun B hB => not_W 0 (fun h =>
      (h trivial 0 (Nat.zero_le _)).elim id (fun ⟨i, hi, hb⟩ => hB _ (by omega) (by omega) hb))
      (fun j hj => hB _ (by omega) (by omega))⟩
  | bexist k => exact ⟨alt o (k + 1), tt, 2 * (k + 1), fun B hB => by
      have := not_atMost_alt (B := B) (o := o) (N := k + 1) hB k 0 (by omega)
      rwa [Nat.mul_zero, Nat.add_zero] at this⟩
  | brec c => exact ⟨tt, ff, c, fun B hB => not_W 0
      (fun ⟨i, hi, h⟩ => h.elim id (hB _ (by omega) (by omega)))
      (fun j hj => hB _ (by omega) (by omega))⟩

/-! ## Scopes that never open -/

theorem vacuous_Qff {k : Scope} (hk : k = .after ∨ k = .between ∨ k = .afterUntil) :
    lower k a P S ff R 0 := by
  rcases hk with rfl | rfl | rfl
  · exact W_all (fun _ h => h)
  · exact fun _ ⟨h, _⟩ => h.elim
  · exact fun _ ⟨h, _⟩ => h.elim

theorem vacuous_Rff {k : Scope} (hk : k = .before ∨ k = .between) : lower k a P S Q ff 0 := by
  rcases hk with rfl | rfl
  · exact fun ⟨_, h⟩ => h.elim
  · exact fun _ ⟨_, _, ⟨_, h⟩⟩ => h.elim

theorem vacuous_Rtt : lower .afterUntil a P S Q tt 0 := fun _ ⟨_, h⟩ => absurd trivial h

/-- A refutation of `(k, a) ⇒ (k', a)` at position 0. -/
def Ref (k k' : Scope) (a : Pat) : Prop :=
  ∃ P S Q R : Pred, lower k a P S Q R 0 ∧ ¬ lower k' a P S Q R 0

/-! ## The pattern-independent refutations -/

theorem ref_globally {k : Scope} (hk : k ≠ .globally) : Ref k .globally a := by
  obtain ⟨P, S, L, h⟩ := falsifier a 0
  refine ⟨P, S, ff, ff, ?_, h ff (fun _ _ _ h => h)⟩
  cases k with
  | globally => exact absurd rfl hk
  | before => exact vacuous_Rff (.inl rfl)
  | after => exact vacuous_Qff (.inl rfl)
  | between => exact vacuous_Qff (.inr (.inl rfl))
  | afterUntil => exact vacuous_Qff (.inr (.inr rfl))

theorem ref_after {k : Scope} (hk : k = .before ∨ k = .between ∨ k = .afterUntil) :
    Ref k .after a := by
  obtain ⟨P, S, L, h⟩ := falsifier a 0
  have hn : ∀ R, ¬ lower .after a P S (pt 0) R 0 :=
    fun _ hl => h ff (fun _ _ _ h => h) (lower_after_pt0.mp hl)
  rcases hk with rfl | rfl | rfl
  · exact ⟨P, S, pt 0, ff, vacuous_Rff (.inl rfl), hn _⟩
  · exact ⟨P, S, pt 0, ff, vacuous_Rff (.inr rfl), hn _⟩
  · exact ⟨P, S, pt 0, tt, vacuous_Rtt, hn _⟩

theorem ref_before {k : Scope} (hk : k = .after ∨ k = .between ∨ k = .afterUntil) :
    Ref k .before a := by
  obtain ⟨P, S, L, h⟩ := falsifier a 0
  exact ⟨P, S, ff, pt (L + 1), vacuous_Qff hk,
    fun hl => h _ (fun _ _ _ => by ar) (hl ⟨L + 1, by ar⟩)⟩

theorem ref_afterUntil {k : Scope} (hk : k = .before ∨ k = .between) : Ref k .afterUntil a := by
  obtain ⟨P, S, L, h⟩ := falsifier a 0
  exact ⟨P, S, pt 0, ff, vacuous_Rff hk,
    fun hl => h ff (fun _ _ _ h => h) (lower_afterUntil_pt0.mp hl id)⟩

/-- Before reads its body with R at 0, where every body but existence holds;
between reads it again at 1. -/
theorem ref_before_between (ha : a ≠ .existence) : Ref .before .between a := by
  obtain ⟨P, S, L, h⟩ := falsifier a 1
  let R : Pred := fun t => t = 0 ∨ t = L + 2
  refine ⟨P, S, pt 1, R, fun _ => body_now (Or.inl rfl) ha, fun hl => ?_⟩
  exact h R (fun u _ _ => by simp only [R]; omega) (hl 1 ⟨rfl, by simp only [R]; omega,
    ⟨L + 1, by simp only [R]; omega⟩⟩)

/-! ## The pattern-specific refutations -/

theorem ref_before_between_existence : Ref .before .between .existence :=
  ⟨ff, pt 0, pt 1, pt 2, fun _ => ⟨0, ⟨rfl, by ar⟩, fun _ _ h => by ar⟩,
    fun hl => by
      obtain ⟨j, ⟨hs, _⟩, _⟩ := hl 1 ⟨rfl, by ar, ⟨1, rfl⟩⟩
      ar⟩

/-- `wx`: P at 0, S and R at 1, Q at 0. Existence and the responses hold with
no boundary, and fail with R as the boundary. -/
theorem ref_wx (ha : a = .existence ∨ a = .response ∨ ∃ n, a = .bresp n ∧ 0 < n)
    {k k' : Scope} (hk : k = .globally ∨ k = .after)
    (hk' : k' = .before ∨ k' = .between ∨ k' = .afterUntil) : Ref k k' a := by
  have h0 : body a ff (pt 0) (pt 1) 0 := by
    rcases ha with rfl | rfl | ⟨n, rfl, hn⟩
    · exact ⟨1, ⟨rfl, id⟩, fun _ _ h => h⟩
    · exact W_all (fun u hP => ⟨1 - u, ⟨by ar, id⟩, fun _ _ h => h⟩)
    · exact W_all (fun u hP => ⟨1 - u, by ar, ⟨by ar, id⟩, fun _ _ h => h⟩)
  have h1 : ¬ body a (pt 1) (pt 0) (pt 1) 0 := by
    rcases ha with rfl | rfl | ⟨n, rfl, hn⟩
    · exact fun ⟨j, ⟨hs, hr⟩, _⟩ => by ar
    · exact not_W 0 (fun h => let ⟨j, ⟨hs, hr⟩, _⟩ := h rfl; by ar) (fun _ _ => by ar)
    · exact not_W 0 (fun h => let ⟨j, _, ⟨hs, hr⟩, _⟩ := h rfl; by ar) (fun _ _ => by ar)
  refine ⟨pt 0, pt 1, pt 0, pt 1, ?_, ?_⟩
  · rcases hk with rfl | rfl
    · exact h0
    · exact lower_after_pt0.mpr h0
  · rcases hk' with rfl | rfl | rfl
    · exact fun hl => h1 (hl ⟨1, rfl⟩)
    · exact fun hl => h1 (lower_between_pt0.mp hl (by ar) ⟨1, rfl⟩)
    · exact fun hl => h1 (lower_afterUntil_pt0.mp hl (by ar))

/-- `wp`: Q at 0 and 2, S at 1, P at 3, R at 4. Precedence holds from the
first Q, and fails from the second, where P comes before S or R. -/
theorem ref_wp {k k' : Scope} (hk : k = .globally ∨ k = .after)
    (hk' : k' = .between ∨ k' = .afterUntil) : Ref k k' .precedence := by
  let Q : Pred := fun t => t = 0 ∨ t = 2
  have h0 : body .precedence ff (pt 3) (pt 1) 0 := W_at 1 (Or.inl rfl) (fun _ _ => by ar)
  have h2 : ¬ body .precedence (pt 4) (pt 3) (pt 1) 2 :=
    not_W 1 (fun h => h rfl) (fun _ _ h => h.elim (by ar) (by ar))
  refine ⟨pt 3, pt 1, Q, pt 4, ?_, ?_⟩
  · rcases hk with rfl | rfl
    · exact h0
    · exact W_of_now ⟨Or.inl rfl, h0⟩
  · rcases hk' with rfl | rfl
    · exact fun hl => h2 (hl 2 ⟨Or.inr rfl, by ar, ⟨2, rfl⟩⟩)
    · exact fun hl => h2 (hl 2 ⟨Or.inr rfl, by ar⟩)

/-- After reads its body from the first Q, at 1; globally reads it at 0. -/
theorem ref_globally_after (ha : a = .existence ∨ a = .precedence) : Ref .globally .after a := by
  have hq : ∀ {x : Pred}, W (neg (pt 1)) (fun u => pt 1 u ∧ x u) 0 → x 1 := fun h => by
    rcases W_reach h 1 with h | ⟨i, hi, hq, hx⟩
    · exact absurd rfl h
    · have : i = 1 := by ar
      subst this
      exact hx
  rcases ha with rfl | rfl
  · exact ⟨ff, pt 0, pt 1, ff, ⟨0, ⟨rfl, id⟩, fun _ _ h => h⟩,
      fun hl => let ⟨j, ⟨hs, _⟩, _⟩ := hq hl; by ar⟩
  · exact ⟨pt 2, pt 0, pt 1, ff, W_of_now (Or.inl rfl),
      fun hl => not_W 1 (fun h => h rfl) (fun _ _ h => h.elim (by ar) id) (hq hl)⟩

/-- Every non-edge of a cell has a refutation. -/
theorem refute {k k' : Scope} (ha : a.Valid) (hn : ¬ scopeLe a.cell k k') : Ref k k' a := by
  cases a <;> cases k <;> cases k' <;> simp [scopeLe, Pat.cell, Pat.Valid] at hn ha <;>
  first
  | (refine ref_globally ?_; decide)
  | (refine ref_after ?_; decide)
  | (refine ref_before ?_; decide)
  | (refine ref_afterUntil ?_; decide)
  | exact ref_before_between_existence
  | (refine ref_before_between ?_; intro h; cases h; done)
  | (refine ref_globally_after ?_; decide)
  | (refine ref_wp ?_ ?_ <;> decide)
  | (refine ref_wx (.inl rfl) ?_ ?_ <;> decide)
  | (refine ref_wx (.inr (.inl rfl)) ?_ ?_ <;> decide)
  | (refine ref_wx (.inr (.inr ⟨_, rfl, ha⟩)) ?_ ?_ <;> decide)

/-- For every pattern the C++ accepts, one scope implies another over every
P, S, Q and R exactly when the pattern's cell says so. -/
theorem scope_exact (ha : a.Valid) (k k' : Scope) : Implies k a k' a ↔ scopeLe a.cell k k' := by
  refine ⟨fun h => ?_, scope_sound⟩
  apply Classical.byContradiction
  intro hn
  obtain ⟨P, S, Q, R, h1, h2⟩ := refute ha hn
  exact h2 (h P S Q R 0 h1)

end Psp
