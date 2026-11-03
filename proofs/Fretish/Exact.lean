import Fretish.Timing

/-!
# The exact timing orders

`exact0` and `exactB` characterise `implies0` and `impliesB` completely over
the timings with no stop: for every pair of kinds and every tick count, they
say whether one timing implies the other. The `→` direction is completeness,
since every implication that holds is one `le0` or `leB` lists, and each pair
they do not list is refuted by an explicit trace. The `←` direction is
soundness, and reuses the edge lemmas of `Fretish.Timing`.

Tick count 0 is included: `within 0`, `for 0` and `immediately` are then
equivalent, so the orders are preorders.
-/

namespace Fretish

/-- The timings with no stop. -/
inductive Basic where
  | imm
  | next
  | within (n : Nat)
  | forT (n : Nat)
  | after (n : Nat)
  | ev
  | alw
  deriving DecidableEq

def Basic.timing : Basic → Timing
  | .imm => .immediately
  | .next => .nextTick
  | .within n => .within n
  | .forT n => .forTicks n
  | .after n => .afterTicks n
  | .ev => .eventually
  | .alw => .always

/-- The order without a boundary. -/
def le0 : Basic → Basic → Prop
  | .imm, .imm => True
  | .imm, .within _ => True
  | .imm, .forT m => m = 0
  | .imm, .ev => True
  | .next, .next => True
  | .next, .within m => 1 ≤ m
  | .next, .ev => True
  | .within n, .imm => n = 0
  | .within n, .within m => n ≤ m
  | .within n, .forT m => n = 0 ∧ m = 0
  | .within _, .ev => True
  | .forT _, .imm => True
  | .forT n, .next => 1 ≤ n
  | .forT _, .within _ => True
  | .forT n, .forT m => m ≤ n
  | .forT _, .ev => True
  | .after n, .next => n = 0
  | .after n, .within m => n + 1 ≤ m
  | .after n, .after m => n = m
  | .after _, .ev => True
  | .ev, .ev => True
  | .alw, .after _ => False
  | .alw, _ => True
  | _, _ => False

/-- The order under a boundary: `le0` less the edges into `eventually` from
`next`, `within n` for `n ≥ 1`, and `after n`. -/
def leB : Basic → Basic → Prop
  | .next, .ev => False
  | .within n, .ev => n = 0
  | .after _, .ev => False
  | A, B => le0 A B

/-- The trace that holds at position `j` alone. -/
abbrev pt (j : Nat) : Pred := fun x => x = j

/-- The trace that holds up to position `j`. -/
abbrev upto (j : Nat) : Pred := fun x => x ≤ j

macro "unf" : tactic =>
  `(tactic| (try simp only [Basic.timing, bodyU, body, withinB, forB, X, Fle, Flt, Gle, F, G,
    U, R, neg, pt, upto, ff] at *))

variable {T T' : Timing}

theorem implies0_iff_U : implies0 T T' ↔ ∀ p t, bodyU T p t → bodyU T' p t :=
  ⟨fun h p t hT => (body_ff T' p t).mp (h p t ((body_ff T p t).mpr hT)),
   fun h p t hT => (body_ff T' p t).mpr (h p t ((body_ff T p t).mp hT))⟩

theorem yes0 {Q : Prop} (hq : Q) (h : ∀ p t, bodyU T p t → bodyU T' p t) :
    implies0 T T' ↔ Q :=
  ⟨fun _ => hq, fun _ => implies0_iff_U.mpr h⟩

theorem no0 {Q : Prop} (hq : ¬ Q) (p : Pred) (hT : bodyU T p 0) (hT' : ¬ bodyU T' p 0) :
    implies0 T T' ↔ Q :=
  ⟨fun h => absurd (implies0_iff_U.mp h p 0 hT) hT', fun q => absurd q hq⟩

/-- Every pair of stop-free timings, without a boundary. -/
theorem exact0 : ∀ A B : Basic, implies0 A.timing B.timing ↔ le0 A B
  -- immediately
  | .imm, .imm => yes0 trivial (fun _ _ h => h)
  | .imm, .next => no0 id (pt 0) (by unf) (by unf <;> omega)
  | .imm, .within _ => yes0 trivial (by intro p t h; unf <;> exact ⟨0, Nat.zero_le _, h⟩)
  | .imm, .forT m => by
    by_cases hm : m = 0
    · exact yes0 hm (by
        intro p t h; unf <;> intro k hk
        have : k = 0 := by omega
        subst this; exact h)
    · exact no0 hm (pt 0) (by unf) (by unf <;> (intro h; have := h 1 (by omega); omega))
  | .imm, .after _ =>
    no0 id (pt 0) (by unf) (by unf <;> (intro ⟨h, _⟩; exact h 0 (Nat.zero_le _) (by omega)))
  | .imm, .ev => yes0 trivial (by intro p t h; unf <;> exact ⟨0, h⟩)
  | .imm, .alw => no0 id (pt 0) (by unf) (by unf <;> (intro h; have := h 1; omega))
  -- next
  | .next, .imm => no0 id (pt 1) (by unf) (by unf <;> omega)
  | .next, .next => yes0 trivial (fun _ _ h => h)
  | .next, .within m => by
    by_cases hm : 1 ≤ m
    · exact yes0 hm (by intro p t h; unf <;> exact ⟨1, hm, h⟩)
    · exact no0 hm (pt 1) (by unf) (by unf <;> (intro ⟨k, hk, he⟩; omega))
  | .next, .forT _ =>
    no0 id (pt 1) (by unf) (by unf <;> (intro h; have := h 0 (Nat.zero_le _); omega))
  | .next, .after _ =>
    no0 id (upto 1) (by unf <;> omega)
      (by unf <;> (intro ⟨h, _⟩; exact h 0 (Nat.zero_le _) (by omega)))
  | .next, .ev => yes0 trivial (by intro p t h; unf <;> exact ⟨1, h⟩)
  | .next, .alw => no0 id (pt 1) (by unf) (by unf <;> (intro h; have := h 0; omega))
  -- within
  | .within n, .imm => by
    by_cases hn : n = 0
    · exact yes0 hn (by
        intro p t h; unf <;> obtain ⟨k, hk, hp⟩ := h
        have : k = 0 := by omega
        subst this; exact hp)
    · exact no0 hn (pt 1) (by unf <;> exact ⟨1, by omega, by omega⟩) (by unf <;> omega)
  | .within _, .next =>
    no0 id (pt 0) (by unf <;> exact ⟨0, Nat.zero_le _, by omega⟩) (by unf <;> omega)
  | .within n, .within m => by
    by_cases h : n ≤ m
    · exact yes0 h (by
        intro p t hp; unf <;> obtain ⟨k, hk, hk'⟩ := hp
        exact ⟨k, by omega, hk'⟩)
    · exact no0 h (pt n) (by unf <;> exact ⟨n, Nat.le_refl _, by omega⟩)
        (by unf <;> (intro ⟨k, hk, he⟩; omega))
  | .within n, .forT m => by
    by_cases h : n = 0 ∧ m = 0
    · exact yes0 h (by
        obtain ⟨rfl, rfl⟩ := h
        intro p t hp; unf <;> obtain ⟨k, hk, hk'⟩ := hp
        intro j hj
        have : k = 0 := by omega
        have : j = 0 := by omega
        subst_vars; exact hk')
    · by_cases hn : n = 0
      · exact no0 h (pt 0) (by unf <;> exact ⟨0, Nat.zero_le _, by omega⟩)
          (by unf <;> (intro hf; have := hf 1 (by omega); omega))
      · exact no0 h (pt 1) (by unf <;> exact ⟨1, by omega, by omega⟩)
          (by unf <;> (intro hf; have := hf 0 (Nat.zero_le _); omega))
  | .within _, .after _ =>
    no0 id (pt 0) (by unf <;> exact ⟨0, Nat.zero_le _, by omega⟩)
      (by unf <;> (intro ⟨h, _⟩; exact h 0 (Nat.zero_le _) (by omega)))
  | .within _, .ev => yes0 trivial (by
      intro p t hp; unf <;> obtain ⟨k, _, hk⟩ := hp
      exact ⟨k, hk⟩)
  | .within _, .alw =>
    no0 id (pt 0) (by unf <;> exact ⟨0, Nat.zero_le _, by omega⟩)
      (by unf <;> (intro h; have := h 1; omega))
  -- for
  | .forT _, .imm => yes0 trivial (by intro p t hp; unf <;> exact hp 0 (Nat.zero_le _))
  | .forT n, .next => by
    by_cases h : 1 ≤ n
    · exact yes0 h (by intro p t hp; unf <;> exact hp 1 h)
    · exact no0 h (pt 0) (by unf <;> (intro k hk; omega)) (by unf <;> omega)
  | .forT _, .within _ => yes0 trivial (by
      intro p t hp; unf <;> exact ⟨0, Nat.zero_le _, hp 0 (Nat.zero_le _)⟩)
  | .forT n, .forT m => by
    by_cases h : m ≤ n
    · exact yes0 h (by intro p t hp; unf <;> (intro k hk; exact hp k (by omega)))
    · exact no0 h (upto n) (by unf <;> (intro k hk; omega))
        (by unf <;> (intro hf; have := hf (n + 1) (by omega); omega))
  | .forT n, .after _ =>
    no0 id (upto n) (by unf <;> (intro k hk; omega))
      (by unf <;> (intro ⟨hf, _⟩; exact hf 0 (Nat.zero_le _) (by omega)))
  | .forT _, .ev => yes0 trivial (by intro p t hp; unf <;> exact ⟨0, hp 0 (Nat.zero_le _)⟩)
  | .forT n, .alw =>
    no0 id (upto n) (by unf <;> (intro k hk; omega))
      (by unf <;> (intro hf; have := hf (n + 1); omega))
  -- after
  | .after n, .imm =>
    no0 id (pt (n + 1)) (by unf <;> exact ⟨fun k hk he => by omega, by omega⟩)
      (by unf <;> omega)
  | .after n, .next => by
    by_cases h : n = 0
    · exact yes0 h (by subst h; intro p t hp; unf <;> exact hp.2)
    · exact no0 h (pt (n + 1)) (by unf <;> exact ⟨fun k hk he => by omega, by omega⟩)
        (by unf <;> omega)
  | .after n, .within m => by
    by_cases h : n + 1 ≤ m
    · exact yes0 h (by intro p t hp; unf <;> exact ⟨n + 1, h, hp.2⟩)
    · exact no0 h (pt (n + 1)) (by unf <;> exact ⟨fun k hk he => by omega, by omega⟩)
        (by unf <;> (intro ⟨k, hk, he⟩; omega))
  | .after n, .forT _ =>
    no0 id (pt (n + 1)) (by unf <;> exact ⟨fun k hk he => by omega, by omega⟩)
      (by unf <;> (intro hf; have := hf 0 (Nat.zero_le _); omega))
  | .after n, .after m => by
    by_cases h : n = m
    · exact yes0 h (by subst h; exact fun _ _ hp => hp)
    · exact no0 h (pt (n + 1)) (by unf <;> exact ⟨fun k hk he => by omega, by omega⟩)
        (by unf <;> (intro ⟨_, he⟩; omega))
  | .after n, .ev => yes0 trivial (by intro p t hp; unf <;> exact ⟨n + 1, hp.2⟩)
  | .after n, .alw =>
    no0 id (pt (n + 1)) (by unf <;> exact ⟨fun k hk he => by omega, by omega⟩)
      (by unf <;> (intro hf; have := hf 0; omega))
  -- eventually
  | .ev, .imm => no0 id (pt 1) (by unf <;> exact ⟨1, by omega⟩) (by unf <;> omega)
  | .ev, .next => no0 id (pt 0) (by unf <;> exact ⟨0, by omega⟩) (by unf <;> omega)
  | .ev, .within m =>
    no0 id (pt (m + 1)) (by unf <;> exact ⟨m + 1, by omega⟩)
      (by unf <;> (intro ⟨k, hk, he⟩; omega))
  | .ev, .forT _ =>
    no0 id (pt 1) (by unf <;> exact ⟨1, by omega⟩)
      (by unf <;> (intro hf; have := hf 0 (Nat.zero_le _); omega))
  | .ev, .after _ =>
    no0 id (pt 0) (by unf <;> exact ⟨0, by omega⟩)
      (by unf <;> (intro ⟨hf, _⟩; exact hf 0 (Nat.zero_le _) (by omega)))
  | .ev, .ev => yes0 trivial (fun _ _ h => h)
  | .ev, .alw =>
    no0 id (pt 0) (by unf <;> exact ⟨0, by omega⟩) (by unf <;> (intro h; have := h 1; omega))
  -- always
  | .alw, .imm => yes0 trivial (by intro p t hp; unf <;> exact hp 0)
  | .alw, .next => yes0 trivial (by intro p t hp; unf <;> exact hp 1)
  | .alw, .within _ => yes0 trivial (by intro p t hp; unf <;> exact ⟨0, Nat.zero_le _, hp 0⟩)
  | .alw, .forT _ => yes0 trivial (by intro p t hp; unf <;> (intro k _; exact hp k))
  | .alw, .after _ =>
    no0 id (fun x => 0 ≤ x) (by unf <;> (intro j; omega))
      (by unf <;> (intro ⟨hf, _⟩; exact hf 0 (Nat.zero_le _) (by omega)))
  | .alw, .ev => yes0 trivial (by intro p t hp; unf <;> exact ⟨0, hp 0⟩)
  | .alw, .alw => yes0 trivial (fun _ _ h => h)

/-! ## Under a boundary -/

theorem noB {Q : Prop} (hq : ¬ Q) (b p : Pred) (hT : body T b p 0) (hT' : ¬ body T' b p 0) :
    impliesB T T' ↔ Q :=
  ⟨fun h => absurd (h b p 0 hT) hT', fun q => absurd q hq⟩

/-- A pair `leB` keeps from `le0`: completeness comes from `exact0`, since
an implication under every boundary holds in particular under none. -/
theorem viaB {A B : Basic} {Q : Prop} (hQ : le0 A B → Q) (hB : Q → impliesB A.timing B.timing) :
    impliesB A.timing B.timing ↔ Q :=
  ⟨fun h => hQ ((exact0 A B).mp (implies0_of_impliesB h)), hB⟩

theorem reflB : impliesB T T := fun _ _ _ h => h

theorem imm_for0 : impliesB .immediately (.forTicks 0) := by
  intro b p t h
  unf
  refine Or.inl (fun k hk => ?_)
  have : k = 0 := by omega
  subst this; exact h

theorem within0_imm : impliesB (.within 0) .immediately := by
  intro b p t h
  unf
  rcases h with ⟨k, hk, hp⟩ | ⟨_, hk, _⟩
  · have : k = 0 := by omega
    subst this; exact hp
  · omega

theorem after0_next : impliesB (.afterTicks 0) .nextTick := by
  intro b p t h
  unf
  obtain ⟨hf, hw⟩ := h
  have h0 : ¬ p t := by
    rcases hf with hf | hf
    · exact hf 0 (Nat.le_refl _)
    · exact (R_now (b := b) (p := neg p) hf)
  rcases hw with ⟨k, hk, hp⟩ | ⟨k, hk, hb⟩
  · rcases Nat.lt_or_ge k 1 with h1 | h1
    · have : k = 0 := by omega
      subst this; exact absurd hp h0
    · have : k = 1 := by omega
      subst this; exact Or.inr hp
  · have : k = 0 := by omega
    subst this; exact Or.inl hb

theorem always_imm : impliesB .always .immediately :=
  impliesB_trans (always_for (n := 0)) for_imm

/-- Every pair of stop-free timings, under a boundary. -/
theorem exactB : ∀ A B : Basic, impliesB A.timing B.timing ↔ leB A B
  -- the edges into `eventually` that a boundary removes
  | .next, .ev =>
    noB id (pt 0) ff (by unf <;> exact Or.inl trivial) (by unf <;> (intro ⟨_, hf, _⟩; exact hf))
  | .within n, .ev => by
    by_cases hn : n = 0
    · exact ⟨fun _ => hn, fun _ => by
        subst hn; exact impliesB_trans within0_imm imm_eventually⟩
    · exact noB hn (pt 0) ff (by unf <;> exact Or.inr ⟨0, by omega, rfl⟩)
        (by unf <;> (intro ⟨_, hf, _⟩; exact hf))
  | .after _, .ev =>
    noB id (pt 0) ff
      (by unf <;> exact ⟨Or.inl (fun _ _ h => h), Or.inr ⟨0, by omega, rfl⟩⟩)
      (by unf <;> (intro ⟨_, hf, _⟩; exact hf))
  -- every other pair is as in `le0`
  | .imm, .imm => viaB id (fun _ => reflB)
  | .imm, .next => viaB id False.elim
  | .imm, .within _ => viaB id (fun _ => imm_within)
  | .imm, .forT _ => viaB id (fun h => by subst h; exact imm_for0)
  | .imm, .after _ => viaB id False.elim
  | .imm, .ev => viaB id (fun _ => imm_eventually)
  | .imm, .alw => viaB id False.elim
  | .next, .imm => viaB id False.elim
  | .next, .next => viaB id (fun _ => reflB)
  | .next, .within _ => viaB id next_within
  | .next, .forT _ => viaB id False.elim
  | .next, .after _ => viaB id False.elim
  | .next, .alw => viaB id False.elim
  | .within _, .imm => viaB id (fun h => by subst h; exact within0_imm)
  | .within _, .next => viaB id False.elim
  | .within _, .within _ => viaB id within_within
  | .within _, .forT _ => viaB id (fun ⟨h1, h2⟩ => by
      subst h1 h2; exact impliesB_trans within0_imm imm_for0)
  | .within _, .after _ => viaB id False.elim
  | .within _, .alw => viaB id False.elim
  | .forT _, .imm => viaB id (fun _ => for_imm)
  | .forT _, .next => viaB id for_next
  | .forT _, .within _ => viaB id (fun _ => impliesB_trans for_imm imm_within)
  | .forT _, .forT _ => viaB id for_for
  | .forT _, .after _ => viaB id False.elim
  | .forT _, .ev => viaB id (fun _ => impliesB_trans for_imm imm_eventually)
  | .forT _, .alw => viaB id False.elim
  | .after _, .imm => viaB id False.elim
  | .after _, .next => viaB id (fun h => by subst h; exact after0_next)
  | .after n, .within m => viaB id (fun (h : n + 1 ≤ m) => after_within (by omega))
  | .after _, .forT _ => viaB id False.elim
  | .after _, .after _ => viaB id (fun h => by subst h; exact reflB)
  | .after _, .alw => viaB id False.elim
  | .ev, .imm => viaB id False.elim
  | .ev, .next => viaB id False.elim
  | .ev, .within _ => viaB id False.elim
  | .ev, .forT _ => viaB id False.elim
  | .ev, .after _ => viaB id False.elim
  | .ev, .ev => viaB id (fun _ => reflB)
  | .ev, .alw => viaB id False.elim
  | .alw, .imm => viaB id (fun _ => always_imm)
  | .alw, .next => viaB id (fun _ => impliesB_trans (always_for (n := 1)) (for_next (Nat.le_refl _)))
  | .alw, .within _ => viaB id (fun _ => impliesB_trans always_imm imm_within)
  | .alw, .forT _ => viaB id (fun _ => always_for)
  | .alw, .after _ => viaB id False.elim
  | .alw, .ev => viaB id (fun _ => impliesB_trans always_imm imm_eventually)
  | .alw, .alw => viaB id (fun _ => reflB)

/-! ## Timings with a stop

The measured orders treat the stop as a free atom, so a stop kind is compared
with the same stop on both sides and the order quantifies over it as well as
over the response. `Kind.at s` places stop `s`; a stop-free kind ignores it.
-/

inductive Kind where
  | basic (A : Basic)
  | untilK
  | beforeK

def Kind.at : Kind → Pred → Timing
  | .basic A, _ => A.timing
  | .untilK, s => .untilStop s
  | .beforeK, s => .beforeStop s

def le0K : Kind → Kind → Prop
  | .basic A, .basic B => le0 A B
  | .basic A, .untilK => A = .alw
  | .untilK, .untilK => True
  | .beforeK, .beforeK => True
  | _, _ => False

def leBK : Kind → Kind → Prop
  | .basic A, .basic B => leB A B
  | K, K' => le0K K K'

abbrev implies0K (K K' : Kind) : Prop := ∀ s, implies0 (K.at s) (K'.at s)

abbrev impliesBK (K K' : Kind) : Prop := ∀ s, impliesB (K.at s) (K'.at s)

/-- No stop-free timing holds of a response that never holds. -/
theorem bodyU_ff_false : ∀ B : Basic, ¬ bodyU B.timing ff 0
  | .imm => fun h => h
  | .next => fun h => h
  | .within n => by show ¬ ∃ k, k ≤ n ∧ False; exact fun ⟨_, _, h⟩ => h
  | .forT n => by show ¬ ∀ k, k ≤ n → False; exact fun h => h 0 (Nat.zero_le _)
  | .after n => by show ¬ ((∀ k, k ≤ n → ¬ False) ∧ False); exact fun ⟨_, h⟩ => h
  | .ev => by show ¬ ∃ j, False; exact fun ⟨_, h⟩ => h
  | .alw => by show ¬ ∀ j : Nat, False; exact fun h => h 0

/-- Every stop-free timing holds of some response. -/
theorem basic_sat : ∀ A : Basic, ∃ p, bodyU A.timing p 0
  | .imm => ⟨pt 0, rfl⟩
  | .next => ⟨pt 1, rfl⟩
  | .within _ => ⟨pt 0, by unf <;> exact ⟨0, Nat.zero_le _, by omega⟩⟩
  | .forT n => ⟨upto n, by unf <;> (intro k hk; omega)⟩
  | .after n => ⟨pt (n + 1), by unf <;> exact ⟨fun k hk he => by omega, by omega⟩⟩
  | .ev => ⟨pt 0, ⟨0, rfl⟩⟩
  | .alw => ⟨fun x => 0 ≤ x, fun j => Nat.zero_le _⟩

/-- A stop at the first position satisfies `until` whatever the response. -/
theorem until_pt0 (p : Pred) : bodyU (.untilStop (pt 0)) p 0 :=
  Or.inl ⟨0, rfl, fun _ hi => absurd hi (Nat.not_lt_zero _)⟩

/-- A stop at the first position falsifies `before` whatever the response. -/
theorem before_pt0 (p : Pred) : ¬ bodyU (.beforeStop (pt 0)) p 0 := by
  intro h
  rcases h 0 with h | ⟨_, hi, _⟩
  · exact h rfl
  · exact absurd hi (Nat.not_lt_zero _)

/-- With no stop, `until` is `always`. -/
theorem until_ff (h : implies0 T (.untilStop ff)) : implies0 T .always := by
  intro p t hT
  have hW : W p ff t := (body_ff (.untilStop ff) p t).mp (h p t hT)
  refine (body_ff .always p t).mpr ?_
  rcases hW with ⟨_, hj, _⟩ | hg
  · exact hj.elim
  · exact hg

theorem untilNo {A : Basic} (hA : ¬ A = .alw) (h0 : ¬ le0 A .alw) :
    implies0K (.basic A) .untilK ↔ A = .alw :=
  ⟨fun h => absurd ((exact0 A .alw).mp (until_ff (h ff))) h0, fun e => absurd e hA⟩

/-- Every pair of kinds, without a boundary. -/
theorem exact0K : ∀ K K' : Kind, implies0K K K' ↔ le0K K K'
  | .basic A, .basic B => ⟨fun h => (exact0 A B).mp (h ff), fun h _ => (exact0 A B).mpr h⟩
  | .basic .alw, .untilK =>
    ⟨fun _ => rfl, fun _ _ => implies0_of_impliesB always_until⟩
  | .basic .imm, .untilK => untilNo (by intro h; cases h) id
  | .basic .next, .untilK => untilNo (by intro h; cases h) id
  | .basic (.within _), .untilK => untilNo (by intro h; cases h) id
  | .basic (.forT _), .untilK => untilNo (by intro h; cases h) id
  | .basic (.after _), .untilK => untilNo (by intro h; cases h) id
  | .basic .ev, .untilK => untilNo (by intro h; cases h) id
  | .basic A, .beforeK =>
    ⟨fun h =>
      let ⟨p, hp⟩ := basic_sat A
      absurd (implies0_iff_U.mp (h (pt 0)) p 0 hp) (before_pt0 p), False.elim⟩
  | .untilK, .basic B =>
    ⟨fun h => absurd (implies0_iff_U.mp (h (pt 0)) ff 0 (until_pt0 ff)) (bodyU_ff_false B),
     False.elim⟩
  | .untilK, .untilK => ⟨fun _ => trivial, fun _ _ _ _ h => h⟩
  | .untilK, .beforeK =>
    ⟨fun h => absurd (implies0_iff_U.mp (h (pt 0)) ff 0 (until_pt0 ff)) (before_pt0 ff),
     False.elim⟩
  | .beforeK, .basic B =>
    ⟨fun h => absurd (implies0_iff_U.mp (h ff) ff 0 (fun _ => Or.inl id)) (bodyU_ff_false B),
     False.elim⟩
  | .beforeK, .untilK =>
    ⟨fun h => by
      have hW := implies0_iff_U.mp (h ff) ff 0 (fun _ => Or.inl id)
      rcases hW with ⟨_, hj, _⟩ | hg
      · exact hj
      · exact hg 0,
     False.elim⟩
  | .beforeK, .beforeK => ⟨fun _ => trivial, fun _ _ _ _ h => h⟩

theorem implies0K_of_impliesBK {K K' : Kind} (h : impliesBK K K') : implies0K K K' :=
  fun s => implies0_of_impliesB (h s)

/-- Every pair of kinds, under a boundary. -/
theorem exactBK : ∀ K K' : Kind, impliesBK K K' ↔ leBK K K'
  | .basic A, .basic B => ⟨fun h => (exactB A B).mp (h ff), fun h _ => (exactB A B).mpr h⟩
  | .basic A, .untilK =>
    ⟨fun h => (exact0K (.basic A) .untilK).mp (implies0K_of_impliesBK h),
     fun (h : A = .alw) _ => by subst h; exact always_until⟩
  | .basic A, .beforeK =>
    ⟨fun h => (exact0K (.basic A) .beforeK).mp (implies0K_of_impliesBK h), False.elim⟩
  | .untilK, .basic B =>
    ⟨fun h => (exact0K .untilK (.basic B)).mp (implies0K_of_impliesBK h), False.elim⟩
  | .untilK, .untilK => ⟨fun _ => trivial, fun _ _ => reflB⟩
  | .untilK, .beforeK =>
    ⟨fun h => (exact0K .untilK .beforeK).mp (implies0K_of_impliesBK h), False.elim⟩
  | .beforeK, .basic B =>
    ⟨fun h => (exact0K .beforeK (.basic B)).mp (implies0K_of_impliesBK h), False.elim⟩
  | .beforeK, .untilK =>
    ⟨fun h => (exact0K .beforeK .untilK).mp (implies0K_of_impliesBK h), False.elim⟩
  | .beforeK, .beforeK => ⟨fun _ => trivial, fun _ _ => reflB⟩

/-! ## Stop polarity, exactly -/

theorem until_stop_exact {s s' : Pred} :
    implies0 (.untilStop s) (.untilStop s') ↔ Entails s s' := by
  constructor
  · intro h t hs
    have hW := implies0_iff_U.mp h ff t (Or.inl ⟨0, hs, fun _ hi => absurd hi (Nat.not_lt_zero _)⟩)
    rcases hW with ⟨j, hj, hq⟩ | hg
    · rcases Nat.eq_zero_or_pos j with h0 | hpos
      · subst h0; exact hj
      · exact (hq 0 hpos).elim
    · exact (hg 0).elim
  · intro hs
    exact implies0_of_impliesB (fun b p t h => until_stop_mono hs t h)

theorem before_stop_exact {s s' : Pred} :
    implies0 (.beforeStop s) (.beforeStop s') ↔ Entails s' s := by
  constructor
  · intro h t hs'
    apply Classical.byContradiction
    intro hns
    -- A response at `t` alone satisfies `before s` at `t`, since `s` fails
    -- there and every later stop comes after the response, while `s'` holds
    -- at `t` and so falsifies `before s'`.
    have hb : bodyU (.beforeStop s) (pt t) t := by
      intro j
      rcases Nat.eq_zero_or_pos j with h0 | hpos
      · subst h0; exact Or.inl hns
      · exact Or.inr ⟨0, hpos, by omega⟩
    rcases implies0_iff_U.mp h _ t hb 0 with h0 | ⟨_, hi, _⟩
    · exact h0 hs'
    · exact absurd hi (Nat.not_lt_zero _)
  · intro hs
    exact implies0_of_impliesB (fun b p t h => before_stop_anti hs t h)

end Fretish
