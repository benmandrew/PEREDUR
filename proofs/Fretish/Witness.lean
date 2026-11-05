import Fretish.Exact
import Fretish.Scope

/-!
# Counterexamples to whole requirements

The non-edges of the scope and condition-type orders have to be refuted at
the level of `lower`, since a trace that falsifies a timing body can still
satisfy the requirement once the condition and `scope_wrap` are applied. This
file gathers the pieces those refutations share.

* With the mode constant (`ff` or `tt`), every scope lowers to one of three
  things at position 0: nothing (`V.T`), the condition over the plain body
  (`V.P`), or the condition over the only body (`V.N`). `lower_ff` and
  `lower_tt` give the table, and `sep` turns a pair of distinct values into a
  counterexample.
* The `w*` lemmas give, for each kind of timing, a response and a stop that
  make a body hold or fail where a counterexample needs it.
* The `inner_*` and `firstEntry_*` lemmas evaluate the condition and the
  scope wrapper on the traces the counterexamples use.
-/

namespace Fretish

/-- The constant `true`. -/
abbrev tt : Pred := fun _ => True

variable {b c e x : Pred} {t : Nat} {ct : CondType} {ctrue : Bool} {T : Timing} {r : Pred}

/-! ## First positions -/

theorem exists_first {p : Nat → Prop} : (∃ j, p j) → ∃ j, p j ∧ ∀ i, i < j → ¬ p i := by
  intro ⟨j, hj⟩
  induction j using Nat.strongRecOn with
  | ind j ih =>
    by_cases h : ∃ i, i < j ∧ p i
    · rcases h with ⟨i, hi, hpi⟩
      exact ih i hi hpi
    · exact ⟨j, hj, fun i hi hpi => h ⟨i, hi, hpi⟩⟩

/-- `x` after every position satisfies the first-entry wrapper. -/
theorem firstEntry_of_all (h : ∀ j, x (t + j + 1)) : firstEntry e x t := by
  by_cases he : ∃ j, e (t + j)
  · rcases exists_first he with ⟨j, hj, hmin⟩
    exact Or.inl ⟨j, ⟨hj, h j⟩, fun i hi => hmin i hi⟩
  · exact Or.inr (fun j hj => he ⟨j, hj⟩)

/-- An entry at the first position makes the wrapper demand `x` at the next. -/
theorem firstEntry_now (he : e t) (h : firstEntry e x t) : x (t + 1) := by
  rcases h with ⟨j, ⟨_, hx⟩, hpre⟩ | h
  · cases j with
    | zero => exact hx
    | succ j => exact absurd he (hpre 0 (Nat.zero_lt_succ _))
  · exact absurd he (h 0)

theorem reentry_ff_iff : reentry ff x t ↔ True := ⟨fun _ => trivial, fun _ _ => Or.inl id⟩

theorem firstEntry_ff_iff : firstEntry ff x t ↔ True :=
  ⟨fun _ => trivial, fun _ => Or.inr (fun _ => id)⟩

/-! ## Constant modes -/

theorem som_ff : som ff = ff := funext fun _ => propext ⟨fun h => h.2, False.elim⟩
theorem som_tt : som tt = ff := funext fun _ => propext ⟨fun h => h.1 trivial, False.elim⟩
theorem eom_ff : eom ff = ff := funext fun _ => propext ⟨fun h => h.1, False.elim⟩
theorem eom_tt : eom tt = ff := funext fun _ => propext ⟨fun h => h.2 trivial, False.elim⟩

/-- A boundary that never holds is no boundary. -/
theorem inner_some_ff : inner (some ff) ct ctrue c x t ↔ inner none ct ctrue c x t := by
  cases ct <;> cases ctrue
  · exact (R_ff : R ff (fun u => c u → x u) t ↔ _)
  · exact (R_ff : R ff (fun u => c u → x u) t ↔ _)
  · show innerTrig ff c x t ↔ innerTrigU c x t
    constructor
    · intro ⟨h1, h2⟩
      exact ⟨fun j he => ((R_ff.mp h1) j ⟨he.1, he.2, id⟩).1, h2⟩
    · intro ⟨h1, h2⟩
      exact ⟨R_ff.mpr (fun j he => ⟨h1 j ⟨he.1, he.2.1⟩, he.2.2⟩), h2⟩
  · exact Iff.rfl

/-- What a scope demands at position 0 when its mode is constant. -/
inductive V where
  | T
  | P
  | N

def V.eval : V → Prop → Prop → Prop
  | .T, _, _ => True
  | .P, p, _ => p
  | .N, _, n => n

/-- The value of each scope with the mode never holding. -/
def vff : ScopeKind → V
  | .global => .P
  | .inMode => .T
  | .notInMode => .P
  | .beforeMode => .P
  | .afterMode => .T
  | .onlyIn => .N
  | .onlyBefore => .T
  | .onlyAfter => .N

/-- The value of each scope with the mode always holding. -/
def vtt : ScopeKind → V
  | .global => .P
  | .inMode => .P
  | .notInMode => .T
  | .beforeMode => .T
  | .afterMode => .T
  | .onlyIn => .T
  | .onlyBefore => .N
  | .onlyAfter => .N

/-- The condition over the plain body, with no boundary, at position 0. -/
abbrev Pz (ct : CondType) (ctrue : Bool) (c : Pred) (T : Timing) (r : Pred) : Prop :=
  inner none ct ctrue c (body T ff r) 0

/-- The condition over the only body, with no boundary, at position 0. -/
abbrev Nz (ct : CondType) (ctrue : Bool) (c : Pred) (T : Timing) (r : Pred) : Prop :=
  inner none ct ctrue c (onlyBody T ff r) 0

theorem lower_ff (k : ScopeKind) :
    lower k ct ctrue T ff c r 0 ↔ (vff k).eval (Pz ct ctrue c T r) (Nz ct ctrue c T r) := by
  cases k <;>
    simp only [lower, wrap, scopeBoundary, timingBody, isOnly, Option.getD, som_ff, eom_ff,
      vff, V.eval, inner_some_ff, reentry_ff_iff, firstEntry_ff_iff, ff, not_false_eq_true,
      true_implies, false_implies, true_and, and_true, or_false]

theorem lower_tt (k : ScopeKind) :
    lower k ct ctrue T tt c r 0 ↔ (vtt k).eval (Pz ct ctrue c T r) (Nz ct ctrue c T r) := by
  cases k <;>
    simp only [lower, wrap, scopeBoundary, timingBody, isOnly, Option.getD, som_tt, eom_tt,
      vtt, V.eval, inner_some_ff, reentry_ff_iff, firstEntry_ff_iff, tt, not_true_eq_false,
      true_implies, false_implies, true_and, and_true, or_true]

/-! ## The condition on chosen traces -/

theorem pred_eq_ff (hb : ∀ t, ¬ b t) : b = ff := funext fun t => propext ⟨hb t, False.elim⟩

/-- A condition that holds at position 0 alone asks for the body there. -/
theorem inner_pt0 (ob : Option Pred) : inner ob ct false (pt 0) x 0 ↔ x 0 := by
  cases ob <;> cases ct
  · exact ⟨fun h => h 0 rfl, fun hx j hj => by rw [show j = 0 by omega]; exact hx⟩
  · exact ⟨fun h => h.2 rfl,
      fun hx => ⟨fun j ⟨_, h⟩ => absurd h (by omega), fun _ => hx⟩⟩
  · exact ⟨fun h => R_now (b := _) (p := fun u => pt 0 u → x u) h rfl,
      fun hx j => Or.inl (fun hj => by rw [show j = 0 by omega]; exact hx)⟩
  · exact ⟨fun h => h.2 rfl,
      fun hx => ⟨fun j => Or.inl (fun ⟨_, h, _⟩ => absurd h (by omega)), fun _ => hx⟩⟩

/-- A trigger on a condition that always holds asks for the body at the start. -/
theorem inner_tt_trig (ob : Option Pred) : inner ob .trigger false tt x t ↔ x t := by
  cases ob
  · exact ⟨fun h => h.2 trivial, fun hx => ⟨fun _ ⟨h, _⟩ => absurd trivial h, fun _ => hx⟩⟩
  · exact ⟨fun h => h.2 trivial,
      fun hx => ⟨fun _ => Or.inl (fun ⟨h, _, _⟩ => absurd trivial h), fun _ => hx⟩⟩

/-- A continual requirement on a condition that always holds asks for the body
everywhere. -/
theorem inner_tt_cont : inner none .continual ctrue tt x t ↔ ∀ j, x (t + j) := by
  cases ctrue <;> exact ⟨fun h j => h j trivial, fun h j _ => h j⟩

/-- A condition that holds at one later position asks for the body there. -/
theorem inner_pt_succ (k : Nat) (h : inner none ct false (pt (t + k + 1)) x t) :
    x (t + k + 1) := by
  cases ct
  · exact h (k + 1) (by omega)
  · exact h.1 k ⟨by omega, rfl⟩

/-- A boundary at the start, with the condition false there and at the next
position, leaves nothing to check. -/
theorem inner_vac (hb : b t) (hc0 : ¬ c t) (hc1 : ¬ c (t + 1)) :
    inner (some b) ct false c x t := by
  cases ct
  · intro j
    cases j with
    | zero => exact Or.inl (fun h => absurd h hc0)
    | succ j => exact Or.inr ⟨0, Nat.zero_lt_succ _, hb⟩
  · refine ⟨fun j => ?_, fun h => absurd h hc0⟩
    cases j with
    | zero => exact Or.inl (fun ⟨_, h, _⟩ => absurd h hc1)
    | succ j => exact Or.inr ⟨0, Nat.zero_lt_succ _, hb⟩

/-- A body true everywhere satisfies the condition everywhere. -/
theorem inner_of_all (ob : Option Pred) (h : ∀ u, x u) : inner ob ct ctrue c x t := by
  cases ob <;> cases ct <;> cases ctrue
  · exact fun j _ => h _
  · exact fun j _ => h _
  · exact ⟨fun j _ => h _, fun _ => h _⟩
  · exact fun _ => h _
  · exact fun j => Or.inl (fun _ => h _)
  · exact fun j => Or.inl (fun _ => h _)
  · exact ⟨fun j => Or.inl (fun ⟨_, _, hb⟩ => ⟨h _, hb⟩), fun _ => h _⟩
  · exact fun _ => h _

/-! ## Responses and stops for each kind -/

macro "unfw" : tactic =>
  `(tactic| (simp only [Kind.at, Basic.timing, bodyU, onlyBody, dual, body, withinB, forB, X,
    Fle, Flt, Gle, F, G, U, R, W, neg, pt, upto, ff, tt] at *))

/-- A response that never holds falsifies every plain body and satisfies every
only body, whatever the boundary. -/
theorem wBot : ∀ K : Kind, ∃ s, (∀ t, ¬ bodyU (K.at s) ff t) ∧ ∀ b t, onlyBody (K.at s) b ff t
  | .basic .imm => ⟨ff, by unfw; exact ⟨fun _ h => h, fun _ _ h => h⟩⟩
  | .basic .next => ⟨ff, by unfw; exact ⟨fun _ h => h, fun _ _ => Or.inr id⟩⟩
  | .basic (.within _) => ⟨ff, by
      unfw; exact ⟨fun _ ⟨_, _, h⟩ => h, fun _ _ => Or.inl (fun _ _ h => h)⟩⟩
  | .basic (.forT _) => ⟨ff, by
      unfw; exact ⟨fun _ h => h 0 (Nat.zero_le _), fun _ _ => Or.inl ⟨0, Nat.zero_le _, id⟩⟩⟩
  | .basic (.after _) => ⟨ff, by
      unfw; exact ⟨fun _ ⟨_, h⟩ => h, fun _ _ => Or.inr (Or.inl (fun _ _ h => h))⟩⟩
  | .basic .ev => ⟨ff, by unfw; exact ⟨fun _ ⟨_, h⟩ => h, fun _ _ _ => Or.inl id⟩⟩
  | .basic .alw => ⟨ff, by
      unfw; exact ⟨fun _ h => h 0, fun _ _ => ⟨0, id, fun _ hi => absurd hi (Nat.not_lt_zero _)⟩⟩⟩
  | .untilK => ⟨ff, by
      unfw
      refine ⟨fun _ h => ?_, fun _ _ _ => Or.inl id⟩
      rcases h with ⟨_, h, _⟩ | h
      · exact h
      · exact h 0⟩
  | .beforeK => ⟨tt, by
      unfw
      refine ⟨fun _ h => ?_, fun _ _ => Or.inr (fun _ => Or.inl id)⟩
      rcases h 0 with h | ⟨_, hi, _⟩
      · exact h trivial
      · exact absurd hi (Nat.not_lt_zero _)⟩

/-- A response that makes the plain body hold at `j` and the only body fail
there. -/
theorem wTop (j : Nat) : ∀ K : Kind, ∃ s r, bodyU (K.at s) r j ∧ ¬ onlyBody (K.at s) ff r j
  | .basic .imm => ⟨ff, tt, by unfw; exact ⟨trivial, fun h => h trivial⟩⟩
  | .basic .next => ⟨ff, tt, by
      unfw
      refine ⟨trivial, fun h => ?_⟩
      rcases h with h | h
      · exact h
      · exact h trivial⟩
  | .basic (.within n) => ⟨ff, tt, by
      unfw
      refine ⟨⟨0, Nat.zero_le _, trivial⟩, fun h => ?_⟩
      rcases h with h | h
      · exact h 0 (Nat.zero_le _) trivial
      · rcases h 0 with h | ⟨_, _, h⟩
        · exact h trivial
        · exact h⟩
  | .basic (.forT n) => ⟨ff, tt, by
      unfw
      refine ⟨fun _ _ => trivial, fun h => ?_⟩
      rcases h with ⟨_, _, h⟩ | ⟨_, _, h⟩
      · exact h trivial
      · exact h⟩
  | .basic (.after n) => ⟨ff, pt (j + n + 1), by
      unfw
      refine ⟨⟨fun k hk h => by omega, by omega⟩, fun h => ?_⟩
      rcases h with (⟨k, hk, h⟩ | ⟨_, _, h⟩) | (h | h)
      · omega
      · exact h
      · exact h (n + 1) (Nat.le_refl _) (by omega)
      · rcases h (n + 1) with h | ⟨_, _, h⟩
        · exact h (by omega)
        · exact h⟩
  | .basic .ev => ⟨ff, tt, by
      unfw
      refine ⟨⟨0, trivial⟩, fun h => ?_⟩
      rcases h 0 with h | ⟨_, _, h⟩
      · exact h trivial
      · exact h⟩
  | .basic .alw => ⟨ff, tt, by
      unfw
      exact ⟨fun _ => trivial, fun ⟨_, h, _⟩ => h trivial⟩⟩
  | .untilK => ⟨tt, tt, by
      unfw
      refine ⟨Or.inr (fun _ => trivial), fun h => ?_⟩
      rcases h 0 with h | ⟨_, hi, _⟩
      · exact h trivial
      · exact absurd hi (Nat.not_lt_zero _)⟩
  | .beforeK => ⟨ff, tt, by
      unfw
      refine ⟨fun _ => Or.inl id, fun h => ?_⟩
      rcases h with (⟨_, h, _⟩ | h) | h
      · exact h
      · exact h 0 trivial
      · rcases h 0 with h | ⟨_, hi, _⟩
        · exact h trivial
        · exact absurd hi (Nat.not_lt_zero _)⟩

/-- Every plain body but `always` can hold at `j` and fail at `j + 1`. -/
theorem wD (j : Nat) : ∀ K : Kind, K ≠ .basic .alw →
    ∃ s r, bodyU (K.at s) r j ∧ ¬ bodyU (K.at s) r (j + 1)
  | .basic .imm, _ => ⟨ff, pt j, by unfw; exact ⟨trivial, by omega⟩⟩
  | .basic .next, _ => ⟨ff, pt (j + 1), by unfw; exact ⟨trivial, by omega⟩⟩
  | .basic (.within n), _ => ⟨ff, pt j, by
      unfw
      exact ⟨⟨0, Nat.zero_le _, by omega⟩, fun ⟨k, _, h⟩ => by omega⟩⟩
  | .basic (.forT n), _ => ⟨ff, upto (j + n), by
      unfw
      exact ⟨fun k hk => by omega, fun h => by have := h n (Nat.le_refl _); omega⟩⟩
  | .basic (.after n), _ => ⟨ff, pt (j + n + 1), by
      unfw
      exact ⟨⟨fun k hk h => by omega, by omega⟩, fun ⟨h, _⟩ => h n (Nat.le_refl _) (by omega)⟩⟩
  | .basic .ev, _ => ⟨ff, pt j, by
      unfw
      exact ⟨⟨0, by omega⟩, fun ⟨k, h⟩ => by omega⟩⟩
  | .basic .alw, h => absurd rfl h
  | .untilK, _ => ⟨pt j, ff, by
      unfw
      refine ⟨Or.inl ⟨0, by omega, fun _ hi => absurd hi (Nat.not_lt_zero _)⟩, fun h => ?_⟩
      rcases h with ⟨k, hk, hpre⟩ | h
      · cases k with
        | zero => omega
        | succ k => exact hpre 0 (Nat.zero_lt_succ _)
      · exact h 0⟩
  | .beforeK, _ => ⟨pt (j + 1), pt j, by
      unfw
      refine ⟨fun k => ?_, fun h => ?_⟩
      · cases k with
        | zero => exact Or.inl (by omega)
        | succ k => exact Or.inr ⟨0, Nat.zero_lt_succ _, by omega⟩
      · rcases h 0 with h | ⟨_, hi, _⟩
        · exact h (by omega)
        · exact absurd hi (Nat.not_lt_zero _)⟩

/-- Every only body but that of `eventually` can hold at 0 and fail at 1. -/
theorem wDN : ∀ K : Kind, K ≠ .basic .ev →
    ∃ s r, onlyBody (K.at s) ff r 0 ∧ ¬ onlyBody (K.at s) ff r 1
  | .basic .imm, _ => ⟨ff, pt 1, by unfw; decide⟩
  | .basic .next, _ => ⟨ff, pt 2, by unfw; decide⟩
  | .basic (.within n), _ => ⟨ff, fun x => n < x, by
      unfw
      refine ⟨Or.inl (fun k hk h => by omega), fun h => ?_⟩
      rcases h with h | h
      · exact h n (Nat.le_refl _) (by omega)
      · rcases h n with h | ⟨_, _, h⟩
        · exact h (by omega)
        · exact h⟩
  | .basic (.forT n), _ => ⟨ff, fun x => x ≠ 0, by
      unfw
      refine ⟨Or.inl ⟨0, Nat.zero_le _, by omega⟩, fun h => ?_⟩
      rcases h with ⟨k, _, h⟩ | ⟨_, _, h⟩
      · exact h (by omega)
      · exact h⟩
  | .basic (.after n), _ => ⟨ff, pt (n + 2), by
      unfw
      refine ⟨Or.inr (Or.inl (fun k hk h => by omega)), fun h => ?_⟩
      rcases h with (⟨k, hk, h⟩ | ⟨_, _, h⟩) | (h | h)
      · omega
      · exact h
      · exact h (n + 1) (Nat.le_refl _) (by omega)
      · rcases h (n + 1) with h | ⟨_, _, h⟩
        · exact h (by omega)
        · exact h⟩
  | .basic .ev, h => absurd rfl h
  | .basic .alw, _ => ⟨ff, fun x => x ≠ 0, by
      unfw
      exact ⟨⟨0, by omega, fun _ hi => absurd hi (Nat.not_lt_zero _)⟩, fun ⟨_, h, _⟩ => h (by omega)⟩⟩
  | .untilK, _ => ⟨pt 1, ff, by
      unfw
      refine ⟨fun k => ?_, fun h => ?_⟩
      · cases k with
        | zero => exact Or.inl (by omega)
        | succ k => exact Or.inr ⟨0, Nat.zero_lt_succ _, Or.inl id⟩
      · rcases h 0 with h | ⟨_, hi, _⟩
        · exact h (by omega)
        · exact absurd hi (Nat.not_lt_zero _)⟩
  | .beforeK, _ => ⟨pt 0, tt, by
      unfw
      refine ⟨Or.inl (Or.inl ⟨0, by omega, fun _ hi => absurd hi (Nat.not_lt_zero _)⟩), fun h => ?_⟩
      rcases h with (⟨k, hk, hpre⟩ | h) | h
      · cases k with
        | zero => omega
        | succ k => exact hpre 0 (Nat.zero_lt_succ _) trivial
      · exact h 0 trivial
      · rcases h 0 with h | ⟨_, hi, _⟩
        · exact h trivial
        · exact absurd hi (Nat.not_lt_zero _)⟩

/-- A condition that holds at one later position asks for the body there. -/
theorem inner_pt_at {u : Nat} (h : inner none ct false (pt u) x t) (hu : t < u) : x u := by
  obtain ⟨k, rfl⟩ : ∃ k, u = t + k + 1 := ⟨u - t - 1, by omega⟩
  exact inner_pt_succ k h

/-- A condition that always holds asks for the body at the start. -/
theorem inner_tt_now : inner none ct false tt x t → x t := by
  cases ct
  · exact fun h => h 0 trivial
  · exact fun h => h.2 trivial

end Fretish
