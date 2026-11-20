import Fretish.Trace

/-!
# PSP pattern bodies

The model of `bounded_body` and `unbounded_body` in `src/psp/lowering.cpp`.
Each pattern has a body over an optional boundary `b`, the point where the
scope closes, which is outside the scope. The body with no boundary is the
body at `b = ff`: `Psp.Syntax` proves that the C++ formula for each meets it.
The propositions P and S are arbitrary predicates on positions, and the
counts are natural numbers.

The lemmas at the end are about weak until, which every body but existence
is built from. `W_suffix` and `W_strict` say what holds at each point before
the first boundary, and the edges of `Psp.PatternOrder` are read from them
point by point.
-/

namespace Psp

open Fretish

/-- The patterns of PLAN.md D3. The four bounded patterns carry their count. -/
inductive Pat where
  | absence
  | universality
  | existence
  | precedence
  | response
  | bresp (n : Nat)
  | binv (n : Nat)
  | bexist (n : Nat)
  | brec (n : Nat)
  deriving DecidableEq, Repr

/-- What the C++ accepts: every count is at least 1 (`src/psp/serialisation.cpp`). -/
def Pat.Valid : Pat → Prop
  | .bresp n | .binv n | .bexist n | .brec n => 0 < n
  | _ => True

/-- `within_before`: `!b U[0,n] (s & !b)`. -/
abbrev withinB (n : Nat) (b s : Pred) : Pred := fun t =>
  ∃ k, k ≤ n ∧ (s (t + k) ∧ ¬ b (t + k)) ∧ ∀ i, i < k → ¬ b (t + i)

/-- `held_until`: `s` at each of the next `n` points, up to the first `b`. -/
abbrev heldB (n : Nat) (b s : Pred) : Pred := fun t =>
  ∀ j, j ≤ n → s (t + j) ∨ ∃ i, i ≤ j ∧ b (t + i)

/-- `at_most`: at most `k` stretches of `p` before `b`. -/
def atMost (b p : Pred) : Nat → Pred
  | 0 => W (neg p) b
  | k + 1 => W (neg p) (fun t => b t ∨ W p (fun u => b u ∨ atMost b p k u) t)

/-- `bounded_body`, and `unbounded_body` at `b = ff`. -/
def body (a : Pat) (b P S : Pred) : Pred :=
  match a with
  | .absence => W (neg P) b
  | .universality => W S b
  | .existence => U (neg b) (fun t => S t ∧ ¬ b t)
  | .precedence => W (neg P) (fun t => S t ∨ b t)
  | .response => W (fun t => P t → U (neg b) (fun u => S u ∧ ¬ b u) t) b
  | .bresp n => W (fun t => P t → withinB n b S t) b
  | .binv n => W (fun t => P t → heldB n b S t) b
  | .bexist k => atMost b P k
  | .brec c => W (Fle c (fun t => S t ∨ b t)) b

/-! ## Weak until -/

variable {p p' q s s' b : Pred} {t : Nat}

theorem W_mono (hp : ∀ u, p u → p' u) (hs : ∀ u, s u → s' u) (h : W p s t) : W p' s' t := by
  rcases h with ⟨j, hj, hi⟩ | h
  · exact Or.inl ⟨j, hs _ hj, fun i hi' => hp _ (hi i hi')⟩
  · exact Or.inr (fun j => hp _ (h j))

theorem W_of_now (h : s t) : W p s t :=
  Or.inl ⟨0, by simpa using h, fun i hi => absurd hi (Nat.not_lt_zero i)⟩

theorem W_now (h : W p s t) : s t ∨ p t := by
  rcases h with ⟨j, hj, hi⟩ | h
  · cases j with
    | zero => exact Or.inl (by simpa using hj)
    | succ j => exact Or.inr (by simpa using hi 0 (Nat.zero_lt_succ j))
  · exact Or.inr (by simpa using h 0)

theorem W_cons (hp : p t) (h : W p s (t + 1)) : W p s t := by
  rcases h with ⟨j, hj, hi⟩ | h
  · refine Or.inl ⟨j + 1, by simpa [Nat.add_assoc, Nat.add_comm 1] using hj, fun i hi' => ?_⟩
    cases i with
    | zero => simpa using hp
    | succ i =>
      have := hi i (by omega)
      simpa [Nat.add_assoc, Nat.add_comm 1] using this
  · refine Or.inr (fun j => ?_)
    cases j with
    | zero => simpa using hp
    | succ j => simpa [Nat.add_assoc, Nat.add_comm 1] using h j

theorem W_tail (h : W p s t) (hs : ¬ s t) : W p s (t + 1) := by
  rcases h with ⟨j, hj, hi⟩ | h
  · cases j with
    | zero => exact absurd (by simpa using hj) hs
    | succ j =>
      exact Or.inl ⟨j, by simpa [Nat.add_assoc, Nat.add_comm 1] using hj,
        fun i hi' => by simpa [Nat.add_assoc, Nat.add_comm 1] using hi (i + 1) (by omega)⟩
  · exact Or.inr (fun j => by simpa [Nat.add_assoc, Nat.add_comm 1] using h (j + 1))

/-- `p W b` reaches every point: `p` there, or `b` no later. -/
theorem W_reach (h : W p b t) (j : Nat) : p (t + j) ∨ ∃ i, i ≤ j ∧ b (t + i) := by
  rcases h with ⟨m, hm, hi⟩ | h
  · by_cases hj : j < m
    · exact Or.inl (hi j hj)
    · exact Or.inr ⟨m, by omega, hm⟩
  · exact Or.inl (h j)

/-- `p W b` holds again at every point before the boundary. -/
theorem W_suffix (h : W p b t) : W (fun u => W p b u) b t := by
  rcases h with ⟨m, hm, hi⟩ | h
  · refine Or.inl ⟨m, hm, fun i hi' => Or.inl ⟨m - i, ?_, fun k hk => ?_⟩⟩
    · have : t + i + (m - i) = t + m := by omega
      rw [this]; exact hm
    · have := hi (i + k) (by omega)
      rwa [Nat.add_assoc]
  · exact Or.inr (fun j => Or.inr (fun k => by rw [Nat.add_assoc]; exact h (j + k)))

theorem exists_first {r : Nat → Prop} : (∃ j, r j) → ∃ j, r j ∧ ∀ i, i < j → ¬ r i := by
  intro ⟨j, hj⟩
  induction j using Nat.strongRecOn with
  | ind j ih =>
    by_cases h : ∃ i, i < j ∧ r i
    · rcases h with ⟨i, hi, hri⟩
      exact ih i hi hri
    · exact ⟨j, hj, fun i hi hri => h ⟨i, hi, hri⟩⟩

/-- Before the first boundary the boundary does not hold. -/
theorem W_strict (h : W p b t) : W (fun u => p u ∧ ¬ b u) b t := by
  by_cases hb : ∃ j, b (t + j)
  · obtain ⟨j, hj, hmin⟩ := exists_first hb
    refine Or.inl ⟨j, hj, fun i hi => ⟨?_, hmin i hi⟩⟩
    rcases W_reach h i with h' | ⟨k, hk, hb'⟩
    · exact h'
    · exact absurd hb' (hmin k (by omega))
  · refine Or.inr (fun j => ⟨?_, fun h' => hb ⟨j, h'⟩⟩)
    rcases W_reach h j with h' | ⟨k, _, hb'⟩
    · exact h'
    · exact absurd ⟨k, hb'⟩ hb

/-- A rule read point by point implies the precedence body. -/
theorem W_prec {P S : Pred} (h : W (fun u => P u → S u ∨ b u) b t) :
    W (neg P) (fun u => S u ∨ b u) t := by
  by_cases hx : ∃ j, S (t + j) ∨ b (t + j)
  · obtain ⟨j, hj, hmin⟩ := exists_first hx
    refine Or.inl ⟨j, hj, fun i hi hP => ?_⟩
    rcases W_reach h i with h' | ⟨k, hk, hb'⟩
    · exact hmin i hi (h' hP)
    · exact hmin k (by omega) (Or.inr hb')
  · refine Or.inr (fun i hP => ?_)
    rcases W_reach h i with h' | ⟨k, _, hb'⟩
    · exact hx ⟨i, h' hP⟩
    · exact hx ⟨k, Or.inr hb'⟩

theorem W_ff : W p ff t ↔ G p t :=
  ⟨fun h => fun j => (W_reach h j).resolve_right (fun ⟨_, _, h⟩ => h), Or.inr⟩

/-! ## Bounded existence -/

theorem atMost_succ {P : Pred} {k : Nat} (h : atMost b P k t) : atMost b P (k + 1) t :=
  W_of_now (Or.inr (W_of_now (Or.inr h)))

theorem atMost_mono {P : Pred} {n m : Nat} (hnm : n ≤ m) (h : atMost b P n t) :
    atMost b P m t := by
  induction m with
  | zero =>
    have : n = 0 := by omega
    subst this; exact h
  | succ m ih =>
    by_cases he : n = m + 1
    · subst he; exact h
    · exact atMost_succ (ih (by omega))

/-- Bounded existence holds again one point later, inside the scope. -/
theorem atMost_step {P : Pred} : ∀ {k t}, ¬ b t → atMost b P k t → atMost b P k (t + 1)
  | 0, _, hb, h => W_tail h hb
  | k + 1, t, hb, h => by
    simp only [atMost] at h ⊢
    by_cases hX : b t ∨ W P (fun u => b u ∨ atMost b P k u) t
    · have hW := hX.resolve_left hb
      by_cases hY : b t ∨ atMost b P k t
      · exact W_of_now (Or.inr (W_of_now (Or.inr (atMost_step hb (hY.resolve_left hb)))))
      · exact W_of_now (Or.inr (W_tail hW hY))
    · exact W_tail h hX

end Psp
