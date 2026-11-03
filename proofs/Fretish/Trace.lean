/-!
# Trace semantics

The LTL fragment that `requirement_to_ltl` emits, over infinite traces.

A propositional formula over a trace's atoms is a predicate on positions, so
every formula here is a `Pred`, and each operator evaluates at a position `t`
by looking forward from it. The names follow SPOT's syntax: `R` is release,
`U` strong until, `W` weak until. `Fle n` and `Gle n` are `F[0,n]` and
`G[0,n]`, which the lowering spells out as chains of `X`.

Every definition is an `abbrev`, so the anonymous constructor and `rcases`
see through it.
-/

namespace Fretish

abbrev Pred := Nat → Prop

/-- The constant `false`, which stands in for an absent scope boundary. -/
abbrev ff : Pred := fun _ => False

abbrev neg (p : Pred) : Pred := fun t => ¬ p t

/-- `X p` -/
abbrev X (p : Pred) : Pred := fun t => p (t + 1)

/-- `F[0,n] p` -/
abbrev Fle (n : Nat) (p : Pred) : Pred := fun t => ∃ k, k ≤ n ∧ p (t + k)

/-- `F[0,n-1] p`, false at `n = 0`. -/
abbrev Flt (n : Nat) (p : Pred) : Pred := fun t => ∃ k, k < n ∧ p (t + k)

/-- `G[0,n] p` -/
abbrev Gle (n : Nat) (p : Pred) : Pred := fun t => ∀ k, k ≤ n → p (t + k)

/-- `F p` -/
abbrev F (p : Pred) : Pred := fun t => ∃ j, p (t + j)

/-- `G p` -/
abbrev G (p : Pred) : Pred := fun t => ∀ j, p (t + j)

/-- `q U p`: `p` arrives, and `q` holds at every point before it. -/
abbrev U (q p : Pred) : Pred := fun t => ∃ j, p (t + j) ∧ ∀ i, i < j → q (t + i)

/-- `b R p`: `p` holds up to and including the first `b`, or for ever. -/
abbrev R (b p : Pred) : Pred := fun t => ∀ j, p (t + j) ∨ ∃ i, i < j ∧ b (t + i)

/-- `p W s` -/
abbrev W (p s : Pred) : Pred := fun t => U p s t ∨ G p t

/-- `p` implies `q` at every position of every trace. -/
abbrev Entails (p q : Pred) : Prop := ∀ t, p t → q t

variable {b c p q : Pred} {t : Nat}

theorem R_now (h : R b p t) : p t :=
  (h 0).resolve_right (fun ⟨_, hi, _⟩ => absurd hi (Nat.not_lt_zero _))

theorem R_one (h : R b p t) : b t ∨ p (t + 1) := by
  rcases h 1 with h | ⟨i, hi, hb⟩
  · exact Or.inr h
  · have : i = 0 := by omega
    subst this
    exact Or.inl hb

theorem R_mono_right (hpq : Entails p q) : Entails (R b p) (R b q) := by
  intro t h j
  rcases h j with h | h
  · exact Or.inl (hpq _ h)
  · exact Or.inr h

theorem R_mono_left (hbc : Entails b c) : Entails (R b p) (R c p) := by
  intro t h j
  rcases h j with h | ⟨i, hi, hb⟩
  · exact Or.inl h
  · exact Or.inr ⟨i, hi, hbc _ hb⟩

theorem R_ff : R ff p t ↔ G p t :=
  ⟨fun h j => (h j).resolve_right (fun ⟨_, _, hf⟩ => hf), fun h j => Or.inl (h j)⟩

end Fretish
