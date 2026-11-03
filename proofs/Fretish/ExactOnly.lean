import Fretish.Only
import Fretish.Exact

/-!
# The exact timing orders under the only scopes

An only scope reads every timing but `after n` as its dual over the negated
response, and negation is a bijection on responses, so between two such kinds
the only order is the base order between their duals (`dual0`, `dualB`). The
`after n` rows and columns are characterised directly: `after n` implies only
itself, and is implied by `next` at `n = 0`, by `within m` for `m > n`, and by
`eventually`. `exactO0` covers only before, which has no boundary, and
`exactOB` covers only in and only after.
-/

namespace Fretish

def Kind.dual : Kind → Kind
  | .basic (.within n) => .basic (.forT n)
  | .basic (.forT n) => .basic (.within n)
  | .basic .ev => .basic .alw
  | .basic .alw => .basic .ev
  | .untilK => .beforeK
  | .beforeK => .untilK
  | K => K

def Kind.notAfter : Kind → Bool
  | .basic (.after _) => false
  | _ => true

def Kind.ticks : Kind → Nat
  | .basic (.after n) => n
  | _ => 0

/-- The kinds that imply `after n` under an only scope. -/
def intoAfter : Kind → Nat → Prop
  | .basic .next, n => n = 0
  | .basic (.within m), n => n + 1 ≤ m
  | .basic .ev, _ => True
  | _, _ => False

/-- The only order built from a base order over kinds. -/
def leO (base : Kind → Kind → Prop) (K K' : Kind) : Prop :=
  if K.notAfter then (if K'.notAfter then base K.dual K'.dual else intoAfter K K'.ticks)
  else (if K'.notAfter then False else K.ticks = K'.ticks)

abbrev onlyImplies0K (K K' : Kind) : Prop := ∀ s, onlyImplies0 (K.at s) (K'.at s)

abbrev onlyImpliesBK (K K' : Kind) : Prop := ∀ s, onlyImpliesB (K.at s) (K'.at s)

theorem after_of {K : Kind} (h : K.notAfter = false) : ∃ n, K = .basic (.after n) := by
  rcases K with A | _ | _
  · cases A
    case after n => exact ⟨n, rfl⟩
    all_goals simp [Kind.notAfter] at h
  all_goals simp [Kind.notAfter] at h

/-! ## Between duals -/

theorem onlyBody_eq : ∀ K : Kind, K.notAfter = true →
    ∀ s b p, onlyBody (K.at s) b p = body (K.dual.at s) b (neg p)
  | .basic .imm, _, _, _, _ => rfl
  | .basic .next, _, _, _, _ => rfl
  | .basic (.within _), _, _, _, _ => rfl
  | .basic (.forT _), _, _, _, _ => rfl
  | .basic (.after _), h, _, _, _ => by simp [Kind.notAfter] at h
  | .basic .ev, _, _, _, _ => rfl
  | .basic .alw, _, _, _, _ => rfl
  | .untilK, _, _, _, _ => rfl
  | .beforeK, _, _, _, _ => rfl

theorem neg_neg (p : Pred) : neg (neg p) = p :=
  funext fun _ => propext ⟨fun h => Classical.byContradiction h, fun h hn => hn h⟩

theorem dualIff0 {K K' : Kind} (hK : K.notAfter = true) (hK' : K'.notAfter = true) :
    onlyImplies0K K K' ↔ implies0K K.dual K'.dual := by
  constructor
  · intro h s p t hp
    have := h s (neg p) t (by rw [onlyBody_eq K hK, neg_neg]; exact hp)
    rw [onlyBody_eq K' hK', neg_neg] at this
    exact this
  · intro h s p t hp
    rw [onlyBody_eq K hK] at hp
    rw [onlyBody_eq K' hK']
    exact h s (neg p) t hp

theorem dualIffB {K K' : Kind} (hK : K.notAfter = true) (hK' : K'.notAfter = true) :
    onlyImpliesBK K K' ↔ impliesBK K.dual K'.dual := by
  constructor
  · intro h s b p t hp
    have := h s b (neg p) t (by rw [onlyBody_eq K hK, neg_neg]; exact hp)
    rw [onlyBody_eq K' hK', neg_neg] at this
    exact this
  · intro h s b p t hp
    rw [onlyBody_eq K hK] at hp
    rw [onlyBody_eq K' hK']
    exact h s b (neg p) t hp

theorem dual0 {K K' : Kind} (hK : K.notAfter = true) (hK' : K'.notAfter = true) :
    onlyImplies0K K K' ↔ le0K K.dual K'.dual :=
  (dualIff0 hK hK').trans (exact0K _ _)

theorem dualB {K K' : Kind} (hK : K.notAfter = true) (hK' : K'.notAfter = true) :
    onlyImpliesBK K K' ↔ leBK K.dual K'.dual :=
  (dualIffB hK hK').trans (exactBK _ _)

theorem onlyImplies0K_of_onlyImpliesBK {K K' : Kind} (h : onlyImpliesBK K K') :
    onlyImplies0K K K' :=
  fun s => onlyImplies0_of_onlyImpliesB (h s)

/-! ## Into `after n` -/

/-- A response that first holds at `n + 1` falsifies `after n`. -/
theorem after_fails (n : Nat) : ¬ onlyBody (.afterTicks n) ff (pt (n + 1)) 0 := by
  show ¬ (((∃ k, k ≤ n ∧ 0 + k = n + 1) ∨ (∃ k, k < n ∧ False)) ∨
    ((∀ k, k ≤ n + 1 → ¬ (0 + k = n + 1)) ∨ (∀ j, ¬ (0 + j = n + 1) ∨ ∃ i, i < j ∧ False)))
  intro h
  rcases h with (⟨k, hk, he⟩ | ⟨_, _, hf⟩) | (hg | hr)
  · omega
  · exact hf
  · exact hg (n + 1) (Nat.le_refl _) (by omega)
  · rcases hr (n + 1) with h | ⟨_, _, hf⟩
    · exact h (by omega)
    · exact hf

theorem next_after0B : onlyImpliesB .nextTick (.afterTicks 0) := by
  intro b p t h
  change b t ∨ ¬ p (t + 1) at h
  change ((∃ k, k ≤ 0 ∧ p (t + k)) ∨ (∃ k, k < 0 ∧ b (t + k))) ∨
    ((∀ k, k ≤ 0 + 1 → ¬ p (t + k)) ∨ R b (neg p) t)
  by_cases hp : p t
  · exact Or.inl (Or.inl ⟨0, Nat.le_refl _, hp⟩)
  · rcases h with hb | hn
    · refine Or.inr (Or.inr fun j => ?_)
      rcases Nat.eq_zero_or_pos j with h0 | hpos
      · subst h0
        exact Or.inl hp
      · exact Or.inr ⟨0, hpos, hb⟩
    · refine Or.inr (Or.inl fun k hk => ?_)
      rcases Nat.lt_or_ge k 1 with h1 | h1
      · have : k = 0 := by omega
        subst this
        exact hp
      · have : k = 1 := by omega
        subst this
        exact hn

/-- The edges into `after n`, under a boundary. -/
theorem intoAfterEdgeB {n : Nat} : ∀ K : Kind, intoAfter K n → onlyImpliesBK K (.basic (.after n))
  | .basic .next, (h : n = 0) => fun _ => by subst h; exact next_after0B
  | .basic (.within _), (h : n + 1 ≤ _) => fun _ => only_within_after (by omega)
  | .basic .ev, _ => fun _ => only_eventually_after
  | .basic .imm, h => False.elim h
  | .basic (.forT _), h => False.elim h
  | .basic (.after _), h => False.elim h
  | .basic .alw, h => False.elim h
  | .untilK, h => False.elim h
  | .beforeK, h => False.elim h

theorem noInto {K : Kind} {n : Nat} {Q : Prop} (hq : ¬ Q) (s : Pred)
    (hK : onlyBody (K.at s) ff (pt (n + 1)) 0) : onlyImplies0K K (.basic (.after n)) ↔ Q :=
  ⟨fun h => absurd (h s (pt (n + 1)) 0 hK) (after_fails n), fun q => absurd q hq⟩

theorem yesInto {K : Kind} {n : Nat} (h : intoAfter K n) :
    onlyImplies0K K (.basic (.after n)) ↔ intoAfter K n :=
  ⟨fun _ => h, fun _ => onlyImplies0K_of_onlyImpliesBK (intoAfterEdgeB K h)⟩

theorem intoAfter0 : ∀ K : Kind, K.notAfter = true → ∀ n,
    (onlyImplies0K K (.basic (.after n)) ↔ intoAfter K n)
  | .basic .imm, _, n => noInto id ff (show ¬ (0 = n + 1) by omega)
  | .basic .next, _, n => by
    by_cases h : n = 0
    · exact yesInto (K := .basic .next) h
    · exact noInto h ff (show False ∨ ¬ (0 + 1 = n + 1) from Or.inr (by omega))
  | .basic (.within m), _, n => by
    by_cases h : n + 1 ≤ m
    · exact yesInto (K := .basic (.within m)) h
    · exact noInto h ff (show (∀ k, k ≤ m → ¬ (0 + k = n + 1)) ∨ _ from
        Or.inl fun k hk he => by omega)
  | .basic (.forT m), _, n =>
    noInto id ff (show (∃ k, k ≤ m ∧ ¬ (0 + k = n + 1)) ∨ _ from
      Or.inl ⟨0, Nat.zero_le _, by omega⟩)
  | .basic (.after _), h, _ => by simp [Kind.notAfter] at h
  | .basic .ev, _, _ => yesInto (K := .basic .ev) trivial
  | .basic .alw, _, n =>
    noInto id ff (show ∃ j, ¬ (0 + j = n + 1) ∧ ∀ i, i < j → ¬ False from
      ⟨0, by omega, fun _ hi => absurd hi (Nat.not_lt_zero _)⟩)
  | .untilK, _, _ =>
    noInto id ff (show ∀ j, ¬ False ∨ _ from fun _ => Or.inl id)
  | .beforeK, _, n =>
    noInto id (pt 0) (show ((∃ j, 0 + j = 0 ∧ ∀ i, i < j → ¬ (0 + i = n + 1)) ∨ _) ∨ _ from
      Or.inl (Or.inl ⟨0, rfl, fun _ hi => absurd hi (Nat.not_lt_zero _)⟩))

/-! ## Out of `after n` -/

theorem noOut {K' : Kind} {n : Nat} (s p : Pred) (h0 : p 0)
    (hK' : ¬ onlyBody (K'.at s) ff p 0) : onlyImplies0K (.basic (.after n)) K' ↔ False :=
  ⟨fun h => hK' (h s p 0 (Or.inl (Or.inl ⟨0, Nat.zero_le _, h0⟩))), False.elim⟩

theorem afterOut0 : ∀ K' : Kind, K'.notAfter = true → ∀ n,
    (onlyImplies0K (.basic (.after n)) K' ↔ False)
  | .basic .imm, _, _ => noOut ff (pt 0) rfl (show ¬ ¬ (0 = 0) from fun h => h rfl)
  | .basic .next, _, _ =>
    noOut ff (upto 1) (Nat.zero_le _) (show ¬ (False ∨ ¬ (0 + 1 ≤ 1)) by
      intro h
      rcases h with h | h
      · exact h
      · exact h (by omega))
  | .basic (.within m), _, _ =>
    noOut ff (pt 0) rfl
      (show ¬ ((∀ k, k ≤ m → ¬ (0 + k = 0)) ∨ (∀ j, ¬ (0 + j = 0) ∨ ∃ i, i < j ∧ False)) by
        intro h
        rcases h with h | h
        · exact h 0 (Nat.zero_le _) rfl
        · rcases h 0 with h | ⟨_, _, h⟩
          · exact h rfl
          · exact h)
  | .basic (.forT m), _, _ =>
    noOut ff (upto m) (Nat.zero_le _)
      (show ¬ ((∃ k, k ≤ m ∧ ¬ (0 + k ≤ m)) ∨ (∃ k, k < m ∧ False)) by
        intro h
        rcases h with ⟨k, hk, h⟩ | ⟨_, _, h⟩
        · exact h (by omega)
        · exact h)
  | .basic (.after _), h, _ => by simp [Kind.notAfter] at h
  | .basic .ev, _, _ =>
    noOut ff (pt 0) rfl (show ¬ (∀ j, ¬ (0 + j = 0) ∨ ∃ i, i < j ∧ False) by
      intro h
      rcases h 0 with h | ⟨_, _, h⟩
      · exact h rfl
      · exact h)
  | .basic .alw, _, _ =>
    noOut ff (fun x => 0 ≤ x) (Nat.zero_le _)
      (show ¬ (∃ j, ¬ (0 ≤ 0 + j) ∧ ∀ i, i < j → ¬ False) from
        fun ⟨_, h, _⟩ => h (Nat.zero_le _))
  | .untilK, _, _ =>
    noOut (pt 0) (pt 0) rfl
      (show ¬ (∀ j, ¬ (0 + j = 0) ∨ ∃ i, i < j ∧ (¬ (0 + i = 0) ∨ False)) by
        intro h
        rcases h 0 with h | ⟨_, hi, _⟩
        · exact h rfl
        · exact absurd hi (Nat.not_lt_zero _))
  | .beforeK, _, _ =>
    noOut ff (pt 0) rfl
      (show ¬ (((∃ j, False ∧ ∀ i, i < j → ¬ (0 + i = 0)) ∨ (∀ j, ¬ (0 + j = 0))) ∨
          (∀ j, ¬ (0 + j = 0) ∨ ∃ i, i < j ∧ False)) by
        intro h
        rcases h with (⟨_, h, _⟩ | h) | h
        · exact h
        · exact h 0 rfl
        · rcases h 0 with h | ⟨_, _, h⟩
          · exact h rfl
          · exact h)

theorem afterAfter0 (n m : Nat) :
    onlyImplies0K (.basic (.after n)) (.basic (.after m)) ↔ n = m := by
  by_cases h : n = m
  · subst h
    exact ⟨fun _ => rfl, fun _ _ _ _ h => h⟩
  · refine ⟨fun hi => absurd (hi ff (pt (m + 1)) 0 ?_) (after_fails m), fun e => absurd e h⟩
    show ((∃ k, k ≤ n ∧ 0 + k = m + 1) ∨ (∃ k, k < n ∧ False)) ∨
      ((∀ k, k ≤ n + 1 → ¬ (0 + k = m + 1)) ∨ _)
    rcases Nat.lt_or_gt_of_ne h with hlt | hgt
    · exact Or.inr (Or.inl fun k hk he => by omega)
    · exact Or.inl (Or.inl ⟨m + 1, by omega, by omega⟩)

/-! ## The exact only orders -/

/-- Every pair of kinds under only before, which has no boundary. -/
theorem exactO0 (K K' : Kind) : onlyImplies0K K K' ↔ leO le0K K K' := by
  unfold leO
  cases hK : K.notAfter <;> cases hK' : K'.notAfter <;>
    simp only [Bool.false_eq_true, ↓reduceIte]
  · obtain ⟨n, rfl⟩ := after_of hK
    obtain ⟨m, rfl⟩ := after_of hK'
    exact afterAfter0 n m
  · obtain ⟨n, rfl⟩ := after_of hK
    exact afterOut0 K' hK' n
  · obtain ⟨m, rfl⟩ := after_of hK'
    exact intoAfter0 K hK m
  · exact dual0 hK hK'

/-- Every pair of kinds under only in and only after, which have a boundary. -/
theorem exactOB (K K' : Kind) : onlyImpliesBK K K' ↔ leO leBK K K' := by
  unfold leO
  cases hK : K.notAfter <;> cases hK' : K'.notAfter <;>
    simp only [Bool.false_eq_true, ↓reduceIte]
  · obtain ⟨n, rfl⟩ := after_of hK
    obtain ⟨m, rfl⟩ := after_of hK'
    exact ⟨fun h => (afterAfter0 n m).mp (onlyImplies0K_of_onlyImpliesBK h),
      fun (e : n = m) => by subst e; exact fun _ _ _ _ h => h⟩
  · obtain ⟨n, rfl⟩ := after_of hK
    exact ⟨fun h => (afterOut0 K' hK' n).mp (onlyImplies0K_of_onlyImpliesBK h), False.elim⟩
  · obtain ⟨m, rfl⟩ := after_of hK'
    exact ⟨fun h => (intoAfter0 K hK m).mp (onlyImplies0K_of_onlyImpliesBK h),
      intoAfterEdgeB K⟩
  · exact dualB hK hK'

end Fretish
