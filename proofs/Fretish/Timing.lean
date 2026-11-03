import Fretish.Trace

/-!
# Timing bodies and the timing order

`body T b r` is `timing_body` in `src/requirement.cpp`: the obligation timing
`T` places on response `r`, discharged early at boundary `b`. The scopes with
no boundary (global, after, only before) take `b = ff`, and `body_ff` shows the
result equals the unbounded branch the lowering emits for them.

The edge lemmas prove the covering edges of the two orders that
`scripts/field_orders.py` measures for tick counts up to 3, here for every
tick count:

* `impliesB`, the order under a scope boundary (in, not in, before), holding
  for every boundary predicate;
* `implies0`, the order without one (global, after), which adds the edges
  into `eventually` from `within n` and `after n`.

Both orders are closed under transitivity, so the covering edges give every
pair the script finds. `Fretish.Exact` proves the non-edges.
-/

namespace Fretish

inductive Timing where
  | immediately
  | nextTick
  | within (n : Nat)
  | forTicks (n : Nat)
  | afterTicks (n : Nat)
  | eventually
  | always
  | untilStop (s : Pred)
  | beforeStop (s : Pred)

/-- `within_body`: `F[0,n] p`, or the boundary in `[0, n-1]`. -/
abbrev withinB (n : Nat) (b p : Pred) : Pred := fun t => Fle n p t ∨ Flt n b t

/-- `for_body`: `G[0,n] p`, or `p` up to the boundary. -/
abbrev forB (n : Nat) (b p : Pred) : Pred := fun t => Gle n p t ∨ R b p t

def body : Timing → Pred → Pred → Pred
  | .immediately, _, p => p
  | .nextTick, b, p => fun t => b t ∨ X p t
  | .within n, b, p => withinB n b p
  | .forTicks n, b, p => forB n b p
  | .afterTicks n, b, p => fun t => forB n b (neg p) t ∧ withinB (n + 1) b p t
  | .eventually, b, p => U (neg b) p
  | .always, b, p => R b p
  | .untilStop s, b, p => fun t => W p s t ∨ R b p t
  | .beforeStop s, b, p => R (fun t => p t ∨ b t) (neg s)

/-- The unbounded branch of `timing_body`, taken when the boundary is empty. -/
def bodyU : Timing → Pred → Pred
  | .immediately, p => p
  | .nextTick, p => X p
  | .within n, p => Fle n p
  | .forTicks n, p => Gle n p
  | .afterTicks n, p => fun t => Gle n (neg p) t ∧ p (t + (n + 1))
  | .eventually, p => F p
  | .always, p => G p
  | .untilStop s, p => W p s
  | .beforeStop s, p => R p (neg s)

/-- The order under a boundary: `T` implies `T'` whatever the boundary. -/
abbrev impliesB (T T' : Timing) : Prop :=
  ∀ b p : Pred, Entails (body T b p) (body T' b p)

/-- The order without a boundary. -/
abbrev implies0 (T T' : Timing) : Prop :=
  ∀ p : Pred, Entails (body T ff p) (body T' ff p)

variable {n m : Nat} {b p q s s' : Pred} {t : Nat} {T T' T'' : Timing}

/-! ## The boundary-free bodies match the lowering -/

theorem body_ff (T : Timing) (p : Pred) (t : Nat) : body T ff p t ↔ bodyU T p t := by
  cases T with
  | immediately => exact Iff.rfl
  | nextTick =>
    show (False ∨ p (t + 1)) ↔ p (t + 1)
    exact ⟨fun h => h.resolve_left id, Or.inr⟩
  | within n =>
    show ((∃ k, k ≤ n ∧ p (t + k)) ∨ (∃ k, k < n ∧ False)) ↔ ∃ k, k ≤ n ∧ p (t + k)
    constructor
    · intro h
      rcases h with h | ⟨_, _, hf⟩
      · exact h
      · exact False.elim hf
    · exact Or.inl
  | forTicks n =>
    show ((∀ k, k ≤ n → p (t + k)) ∨ (∀ j, p (t + j) ∨ ∃ i, i < j ∧ False)) ↔
      ∀ k, k ≤ n → p (t + k)
    constructor
    · intro h
      rcases h with h | h
      · exact h
      · exact fun k _ => (h k).resolve_right (fun ⟨_, _, hf⟩ => hf)
    · exact Or.inl
  | afterTicks n =>
    show (((∀ k, k ≤ n → ¬ p (t + k)) ∨ (∀ j, ¬ p (t + j) ∨ ∃ i, i < j ∧ False)) ∧
        ((∃ k, k ≤ n + 1 ∧ p (t + k)) ∨ (∃ k, k < n + 1 ∧ False))) ↔
      (∀ k, k ≤ n → ¬ p (t + k)) ∧ p (t + (n + 1))
    constructor
    · intro ⟨hf, hw⟩
      have hg : ∀ k, k ≤ n → ¬ p (t + k) := by
        rcases hf with hf | hf
        · exact hf
        · exact fun k _ => (hf k).resolve_right (fun ⟨_, _, h⟩ => h)
      refine ⟨hg, ?_⟩
      rcases hw with ⟨k, hk, hpk⟩ | ⟨_, _, h⟩
      · rcases Nat.lt_or_ge n k with hlt | hge
        · have : k = n + 1 := by omega
          subst this
          exact hpk
        · exact absurd hpk (hg k hge)
      · exact False.elim h
    · intro ⟨hg, hp⟩
      exact ⟨Or.inl hg, Or.inl ⟨n + 1, Nat.le_refl _, hp⟩⟩
  | eventually =>
    show (∃ j, p (t + j) ∧ ∀ i, i < j → ¬ False) ↔ ∃ j, p (t + j)
    constructor
    · intro ⟨j, hj, _⟩
      exact ⟨j, hj⟩
    · intro ⟨j, hj⟩
      exact ⟨j, hj, fun _ _ h => h⟩
  | always =>
    show R ff p t ↔ G p t
    exact R_ff (p := p) (t := t)
  | untilStop s =>
    show (W p s t ∨ R ff p t) ↔ W p s t
    constructor
    · intro h
      rcases h with h | h
      · exact h
      · exact Or.inr (R_ff.mp h)
    · exact Or.inl
  | beforeStop s =>
    show (∀ j, ¬ s (t + j) ∨ ∃ i, i < j ∧ (p (t + i) ∨ False)) ↔
      ∀ j, ¬ s (t + j) ∨ ∃ i, i < j ∧ p (t + i)
    constructor
    · intro h j
      rcases h j with h | ⟨i, hi, h⟩
      · exact Or.inl h
      · exact Or.inr ⟨i, hi, h.resolve_right id⟩
    · intro h j
      rcases h j with h | ⟨i, hi, h⟩
      · exact Or.inl h
      · exact Or.inr ⟨i, hi, Or.inl h⟩

/-! ## Tick monotonicity of the bounded bodies -/

theorem withinB_mono (h : n ≤ m) (hp : withinB n b p t) : withinB m b p t := by
  rcases hp with ⟨k, hk, hpk⟩ | ⟨k, hk, hbk⟩
  · exact Or.inl ⟨k, by omega, hpk⟩
  · exact Or.inr ⟨k, by omega, hbk⟩

theorem forB_mono (h : m ≤ n) (hp : forB n b p t) : forB m b p t := by
  rcases hp with hp | hp
  · exact Or.inl (fun k hk => hp k (by omega))
  · exact Or.inr hp

/-! ## Covering edges under a boundary -/

theorem within_within (h : n ≤ m) : impliesB (.within n) (.within m) := by
  intro b p t hp
  change withinB n b p t at hp
  exact withinB_mono h hp

theorem for_for (h : m ≤ n) : impliesB (.forTicks n) (.forTicks m) := by
  intro b p t hp
  change forB n b p t at hp
  exact forB_mono h hp

theorem for_imm : impliesB (.forTicks n) .immediately := by
  intro b p t hp
  change forB n b p t at hp
  change p t
  rcases hp with hp | hp
  · exact hp 0 (Nat.zero_le n)
  · exact R_now hp

theorem for_next (h : 1 ≤ n) : impliesB (.forTicks n) .nextTick := by
  intro b p t hp
  change forB n b p t at hp
  change b t ∨ p (t + 1)
  rcases hp with hp | hp
  · exact Or.inr (hp 1 h)
  · exact R_one hp

theorem imm_within : impliesB .immediately (.within m) := by
  intro b p t hp
  change p t at hp
  change withinB m b p t
  exact Or.inl ⟨0, Nat.zero_le m, hp⟩

theorem next_within (h : 1 ≤ m) : impliesB .nextTick (.within m) := by
  intro b p t hp
  change b t ∨ p (t + 1) at hp
  change withinB m b p t
  rcases hp with hb | hp
  · exact Or.inr ⟨0, by omega, hb⟩
  · exact Or.inl ⟨1, h, hp⟩

theorem after_within (h : n < m) : impliesB (.afterTicks n) (.within m) := by
  intro b p t hp
  change forB n b (neg p) t ∧ withinB (n + 1) b p t at hp
  change withinB m b p t
  exact withinB_mono (by omega) hp.2

theorem always_for : impliesB .always (.forTicks n) := by
  intro b p t hp
  change R b p t at hp
  change forB n b p t
  exact Or.inr hp

theorem always_until : impliesB .always (.untilStop s) := by
  intro b p t hp
  change R b p t at hp
  change W p s t ∨ R b p t
  exact Or.inr hp

theorem imm_eventually : impliesB .immediately .eventually := by
  intro b p t hp
  change p t at hp
  change U (neg b) p t
  exact ⟨0, hp, fun _ hi => absurd hi (Nat.not_lt_zero _)⟩

theorem impliesB_trans (h1 : impliesB T T') (h2 : impliesB T' T'') : impliesB T T'' :=
  fun b p t h => h2 b p t (h1 b p t h)

/-! ## Covering edges without a boundary -/

theorem implies0_of_impliesB (h : impliesB T T') : implies0 T T' :=
  fun p => h ff p

theorem within_eventually0 : implies0 (.within n) .eventually := by
  intro p t hp
  change withinB n ff p t at hp
  change U (neg ff) p t
  rcases hp with ⟨k, _, hk⟩ | ⟨_, _, hf⟩
  · exact ⟨k, hk, fun _ _ h => h⟩
  · exact False.elim hf

theorem after_eventually0 : implies0 (.afterTicks n) .eventually := by
  intro p t hp
  change forB n ff (neg p) t ∧ withinB (n + 1) ff p t at hp
  exact within_eventually0 (n := n + 1) p t hp.2

theorem implies0_trans (h1 : implies0 T T') (h2 : implies0 T' T'') : implies0 T T'' :=
  fun p t h => h2 p t (h1 p t h)

/-! ## Polarity in the response and the stop

`RuleSearch` treats the response as monotone except under `after n`, `until`'s
stop as monotone and `before`'s stop as antitone. -/

theorem body_response_mono (T : Timing) (hT : ∀ n, T ≠ .afterTicks n)
    (hpq : Entails p q) : Entails (body T b p) (body T b q) := by
  cases T with
  | immediately => exact hpq
  | nextTick =>
    intro t h
    change b t ∨ p (t + 1) at h
    change b t ∨ q (t + 1)
    exact Or.imp id (hpq _) h
  | within n =>
    intro t h
    change withinB n b p t at h
    change withinB n b q t
    rcases h with ⟨k, hk, h⟩ | h
    · exact Or.inl ⟨k, hk, hpq _ h⟩
    · exact Or.inr h
  | forTicks n =>
    intro t h
    change forB n b p t at h
    change forB n b q t
    rcases h with h | h
    · exact Or.inl (fun k hk => hpq _ (h k hk))
    · exact Or.inr (R_mono_right hpq t h)
  | afterTicks n => exact (hT n rfl).elim
  | eventually =>
    intro t h
    change U (neg b) p t at h
    change U (neg b) q t
    obtain ⟨j, hj, hpre⟩ := h
    exact ⟨j, hpq _ hj, hpre⟩
  | always =>
    intro t h
    change R b p t at h
    exact R_mono_right hpq t h
  | untilStop s =>
    intro t h
    change W p s t ∨ R b p t at h
    change W q s t ∨ R b q t
    rcases h with (⟨j, hs, hpre⟩ | hg) | h
    · exact Or.inl (Or.inl ⟨j, hs, fun i hi => hpq _ (hpre i hi)⟩)
    · exact Or.inl (Or.inr (fun j => hpq _ (hg j)))
    · exact Or.inr (R_mono_right hpq t h)
  | beforeStop s =>
    intro t h
    change R (fun u => p u ∨ b u) (neg s) t at h
    change R (fun u => q u ∨ b u) (neg s) t
    exact R_mono_left (b := fun u => p u ∨ b u) (c := fun u => q u ∨ b u)
      (fun u hu => Or.imp (hpq u) id hu) t h

theorem until_stop_mono (hs : Entails s s') :
    Entails (body (.untilStop s) b p) (body (.untilStop s') b p) := by
  intro t h
  change W p s t ∨ R b p t at h
  change W p s' t ∨ R b p t
  rcases h with (⟨j, hj, hpre⟩ | hg) | h
  · exact Or.inl (Or.inl ⟨j, hs _ hj, hpre⟩)
  · exact Or.inl (Or.inr hg)
  · exact Or.inr h

theorem before_stop_anti (hs : Entails s s') :
    Entails (body (.beforeStop s') b p) (body (.beforeStop s) b p) := by
  intro t h
  change R (fun u => p u ∨ b u) (neg s') t at h
  change R (fun u => p u ∨ b u) (neg s) t
  exact R_mono_right (p := neg s') (q := neg s) (fun u hu hsu => hu (hs u hsu)) t h

end Fretish
