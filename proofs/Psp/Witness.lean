import Psp.ScopeOrder

/-!
# Counterexample traces

The pieces the refutations of `Psp.Exact` and `Psp.ScopeExact` share.

* `W_at`, `W_all` and `not_W` decide a weak until on an explicit trace from
  the positions where its operands hold.
* `alt N` holds P at the even points below `2 N`: `N` stretches of P. It
  meets bounded existence `k` exactly when `N ≤ k` (`atMost_alt`,
  `not_atMost_alt`), by induction over the count, whatever the boundary
  does after `2 N`.
* `atMost_pt` gives bounded existence 1 on a trace where P holds at one
  point.
-/

namespace Psp

open Fretish

/-- The constant `true`. -/
abbrev tt : Pred := fun _ => True

/-- True from position 1 on. -/
abbrev from1 : Pred := fun t => 1 ≤ t

/-- True at 0 and from `m + 2` on: a gap of `m + 1` points after 0. -/
abbrev gap (m : Nat) : Pred := fun t => t = 0 ∨ m + 2 ≤ t

/-- True everywhere but at `m`. -/
abbrev skip (m : Nat) : Pred := fun t => t ≠ m

/-- True at 0 and at `m + 2`. -/
abbrev two (m : Nat) : Pred := fun t => t = 0 ∨ t = m + 2

/-- Closes an arithmetic goal about an explicit trace. -/
macro "ar" : tactic =>
  `(tactic| ((try simp only [pt, alt, tt, ff, neg, from1, gap, skip, two, Nat.zero_add,
    Nat.add_zero] at *) <;> omega))

variable {p s b P B : Pred} {t : Nat}

theorem W_at (j : Nat) (hs : s (t + j)) (hp : ∀ i, i < j → p (t + i)) : W p s t :=
  Or.inl ⟨j, hs, hp⟩

theorem W_all (hp : ∀ j, p (t + j)) : W p s t := Or.inr hp

/-- `p W s` fails where `p` fails before any `s`. -/
theorem not_W (i : Nat) (hp : ¬ p (t + i)) (hs : ∀ j, j ≤ i → ¬ s (t + j)) : ¬ W p s t := by
  intro h
  rcases W_reach h i with h | ⟨j, hj, h⟩
  · exact hp h
  · exact hs j hj h

/-! ## Bounded existence on explicit traces -/

/-- `N` stretches of P, one at each even point below `2 N`. -/
abbrev alt (N : Nat) : Pred := fun t => t < 2 * N ∧ t % 2 = 0

theorem atMost_cons {k : Nat} (hp : ¬ P t) (h : atMost B P k (t + 1)) : atMost B P k t := by
  cases k with
  | zero => exact W_cons hp h
  | succ k => exact W_cons hp h

theorem atMost_alt {N : Nat} : ∀ k i, N ≤ i + k → i ≤ N → atMost B (alt N) k (2 * i)
  | 0, i, h, _ => by
    have : i = N := by omega
    subst this
    exact W_all (fun j ⟨hj, _⟩ => by omega)
  | k + 1, i, h, hi => by
    by_cases he : i = N
    · subst he
      exact atMost_succ (atMost_alt k i (by omega) (Nat.le_refl _))
    · refine W_of_now (Or.inr (W_at 1 (Or.inr ?_) (fun j hj => ?_)))
      · refine atMost_cons (by omega) ?_
        have : atMost B (alt N) k (2 * (i + 1)) := atMost_alt k (i + 1) (by omega) (by omega)
        rwa [show 2 * (i + 1) = 2 * i + 1 + 1 by omega] at this
      · show 2 * i + j < 2 * N ∧ (2 * i + j) % 2 = 0
        omega

theorem not_atMost_alt {N : Nat} (hB : ∀ u, u ≤ 2 * N → ¬ B u) :
    ∀ k i, i + k < N → ¬ atMost B (alt N) k (2 * i)
  | 0, i, h, hm => by
    rcases W_now hm with hb | hp
    · exact hB _ (by omega) hb
    · exact hp ⟨by omega, by omega⟩
  | k + 1, i, h, hm => by
    have hb0 := hB (2 * i) (by omega)
    have hb1 := hB (2 * i + 1) (by omega)
    have ih := not_atMost_alt hB k (i + 1) (by omega)
    rw [show 2 * (i + 1) = 2 * i + 1 + 1 by omega] at ih
    simp only [atMost] at hm
    rcases W_now hm with hx | hp
    · rcases hx.resolve_left hb0 with hw
      rcases W_now hw with hy | _
      · exact ih (atMost_step hb1 (atMost_step hb0 (hy.resolve_left hb0)))
      · have hw1 := W_tail hw (fun h => h.elim hb0 (fun h' =>
          ih (atMost_step hb1 (atMost_step hb0 h'))))
        rcases W_now hw1 with hy | hp
        · exact ih (atMost_step hb1 (hy.resolve_left hb1))
        · exact absurd hp.2 (by omega)
    · exact hp ⟨by omega, by omega⟩

/-- P at one point alone is one stretch. -/
theorem atMost_pt (j : Nat) : atMost B (pt j) 1 0 := by
  refine W_at j (Or.inr (W_at 1 (Or.inr (W_all (fun i => ?_))) (fun i hi => ?_))) (fun i hi => ?_)
  · show ¬ (0 + j + 1 + i = j); omega
  · show 0 + j + i = j; omega
  · show ¬ (0 + i = j); omega

end Psp
