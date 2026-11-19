import Fretish.CondOrder

/-!
# Crossing into an only scope

An only scope forbids its timing's pattern where the matching plain scope
would demand it. When a plain timing's pattern rules out an only timing's at
the same point, the plain requirement implies the only one, although neither
middle state of the two-field move implies or is implied: the step has to move
the timing and the scope together.

`crossTiming` names the pairs. `after n` keeps the response false for `n + 1`
ticks and then makes it true; `immediately`, `for k` and `always` need it at
the first tick, `next` and `within k` within `n` ticks, and `after k` at a
different tick. In each pair one side is `after n`, so it reads both ways.

`crossScope` names the scope pairs the pattern clash lifts through:

* `not in m ⇒ only in m`, which share the wrapper and boundary;
* `global ⇒ only after m`, which reads the body only before the first exit, at
  every condition type;
* `global ⇒ only in m` and `global ⇒ only before m`, which read the body at
  re-entry points too, so under a continual condition or the timing `always`,
  as `global_suffix` requires.

`before m ⇒ only after m` is not in the family: at a point where `m` holds the
before scope holds vacuously and the only-after body does not.
-/

namespace Fretish

/-- The pairs with `after n`: a timing whose pattern clashes with `after n`'s. -/
def Basic.clashesAfter (A : Basic) (n : Nat) : Bool :=
  match A with
  | .imm => true
  | .next => decide (1 ≤ n)
  | .within k => decide (k ≤ n)
  | .forT _ => true
  | .after k => decide (k ≠ n)
  | .ev => false
  | .alw => true

/-- The timing pairs whose patterns cannot hold together. -/
def crossTiming : Kind → Kind → Bool
  | .basic (.after n), .basic B => B.clashesAfter n
  | .basic A, .basic (.after n) => A.clashesAfter n
  | _, _ => false

/-- The scope pairs a clash lifts through. -/
def crossScope (k k' : ScopeKind) (sameMode : Bool) (K : Kind) (ct : CondType) : Bool :=
  match k, k' with
  | .global, .onlyAfter => true
  | .global, .onlyIn | .global, .onlyBefore => ct == .continual || K.isAlw
  | .notInMode, .onlyIn => sameMode
  | _, _ => false

variable {n : Nat} {b₀ b p : Pred}

/-! ## The bodies -/

/-- A plain body that clashes with `after n` implies the only body of `after n`,
at any boundary the plain one implies. -/
theorem plain_only_after (A : Basic) (h : A.clashesAfter n = true) (hb : Entails b₀ b) :
    Entails (body A.timing b₀ p) (onlyBody (.afterTicks n) b p) := by
  intro t hp
  change withinB n b p t ∨ forB (n + 1) b (neg p) t
  cases A with
  | imm => exact Or.inl (Or.inl ⟨0, Nat.zero_le _, hp⟩)
  | next =>
    have h : 1 ≤ n := by simpa [Basic.clashesAfter] using h
    rcases hp with hb0 | hp
    · exact Or.inl (Or.inr ⟨0, by omega, hb t hb0⟩)
    · exact Or.inl (Or.inl ⟨1, h, hp⟩)
  | within k =>
    have h : k ≤ n := by simpa [Basic.clashesAfter] using h
    rcases hp with ⟨j, hj, hp⟩ | ⟨j, hj, hbj⟩
    · exact Or.inl (Or.inl ⟨j, by omega, hp⟩)
    · exact Or.inl (Or.inr ⟨j, by omega, hb _ hbj⟩)
  | forT k =>
    rcases hp with hp | hp
    · exact Or.inl (Or.inl ⟨0, Nat.zero_le _, hp 0 (Nat.zero_le _)⟩)
    · exact Or.inl (Or.inl ⟨0, Nat.zero_le _, R_now hp⟩)
  | after k =>
    have h : k ≠ n := by simpa [Basic.clashesAfter] using h
    rcases Nat.lt_or_gt_of_ne h with hlt | hgt
    · rcases hp.2 with ⟨j, hj, hp⟩ | ⟨j, hj, hbj⟩
      · exact Or.inl (Or.inl ⟨j, by omega, hp⟩)
      · exact Or.inl (Or.inr ⟨j, by omega, hb _ hbj⟩)
    · rcases hp.1 with hp | hp
      · exact Or.inr (Or.inl (fun j hj => hp j (by omega)))
      · exact Or.inr (Or.inr (R_mono_left hb t hp))
  | ev => simp [Basic.clashesAfter] at h
  | alw => exact Or.inl (Or.inl ⟨0, Nat.zero_le _, R_now (b := b₀) hp⟩)

/-- `after n` holds its response false at the first tick. -/
theorem after_now {t : Nat} (hp : forB n b₀ (neg p) t) : ¬ p t := by
  rcases hp with hp | hp
  · exact hp 0 (Nat.zero_le _)
  · exact R_now hp

/-- `after n` implies the only body of any timing that clashes with it, at any
boundary its own implies. -/
theorem after_plain_only (B : Basic) (h : B.clashesAfter n = true) (hb : Entails b₀ b) :
    Entails (body (.afterTicks n) b₀ p) (onlyBody B.timing b p) := by
  intro t hp
  change forB n b₀ (neg p) t ∧ withinB (n + 1) b₀ p t at hp
  cases B with
  | imm => exact after_now hp.1
  | next =>
    have h : 1 ≤ n := by simpa [Basic.clashesAfter] using h
    change b t ∨ ¬ p (t + 1)
    rcases hp.1 with hq | hq
    · exact Or.inr (hq 1 h)
    · exact (R_one hq).imp (hb t) id
  | within k =>
    have h : k ≤ n := by simpa [Basic.clashesAfter] using h
    change forB k b (neg p) t
    rcases hp.1 with hq | hq
    · exact Or.inl (fun j hj => hq j (by omega))
    · exact Or.inr (R_mono_left hb t hq)
  | forT k =>
    change withinB k b (neg p) t
    exact Or.inl ⟨0, Nat.zero_le _, after_now hp.1⟩
  | after k =>
    have h : k ≠ n := by simpa [Basic.clashesAfter] using h
    change withinB k b p t ∨ forB (k + 1) b (neg p) t
    rcases Nat.lt_or_gt_of_ne h with hlt | hgt
    · rcases hp.1 with hq | hq
      · exact Or.inr (Or.inl (fun j hj => hq j (by omega)))
      · exact Or.inr (Or.inr (R_mono_left hb t hq))
    · rcases hp.2 with ⟨j, hj, hq⟩ | ⟨j, hj, hbj⟩
      · exact Or.inl (Or.inl ⟨j, by omega, hq⟩)
      · exact Or.inl (Or.inr ⟨j, by omega, hb _ hbj⟩)
  | ev => simp [Basic.clashesAfter] at h
  | alw =>
    change U (neg b) (neg p) t
    exact ⟨0, after_now hp.1, fun i hi => absurd hi (Nat.not_lt_zero _)⟩

/-- Every pair `crossTiming` accepts: the plain body implies the only body. -/
theorem cross_body {K K' : Kind} (h : crossTiming K K' = true) (s s' : Pred)
    (hb : Entails b₀ b) : Entails (body (K.at s) b₀ p) (onlyBody (K'.at s') b p) := by
  rcases K with ⟨A⟩ | _ | _ <;> rcases K' with ⟨B⟩ | _ | _ <;>
    (first | (simp [crossTiming] at h; done) | skip) <;>
  cases A <;> cases B <;> simp only [crossTiming] at h <;>
    (try exact absurd h (by decide)) <;>
    first
    | exact after_plain_only _ h hb
    | exact plain_only_after _ h hb

/-! ## The scopes -/

variable {ct : CondType} {ctrue : Bool} {T T' : Timing} {K : Kind} {s m m₀ c r : Pred}

theorem cross_notIn_onlyIn (hx : ∀ b, Entails (body T b r) (onlyBody T' b r)) :
    Entails (lower .notInMode ct ctrue T m c r) (lower .onlyIn ct ctrue T' m c r) :=
  wrap_mono .onlyIn m (inner_mono _ ct ctrue c (hx (som m)))

theorem cross_global_onlyAfter (hx : Entails (body T ff r) (onlyBody T' (eom m) r)) :
    Entails (lower .global ct ctrue T m₀ c r) (lower .onlyAfter ct ctrue T' m c r) :=
  fun _ h => inner_relax hx h

theorem cross_global_onlyIn (hs : ct = .continual ∨ K = .basic .alw)
    (hc : ctrue = false ∨ c = tt) (hx : Entails (body (K.at s) ff r) (onlyBody T' (som m) r)) :
    Entails (lower .global ct ctrue (K.at s) m₀ c r) (lower .onlyIn ct ctrue T' m c r) := by
  intro t h
  have hx' : ∀ k, inner (some (som m)) ct ctrue c (onlyBody T' (som m) r) (t + k) := fun k =>
    inner_relax hx (global_suffix hs hc h k)
  refine lower_onlyIn.mpr ⟨fun j => Or.inr ?_, fun _ => hx' 0⟩
  have := hx' (j + 1)
  rwa [← Nat.add_assoc] at this

theorem cross_global_onlyBefore (hs : ct = .continual ∨ K = .basic .alw)
    (hc : ctrue = false ∨ c = tt) (hx : Entails (body (K.at s) ff r) (onlyBody T' ff r)) :
    Entails (lower .global ct ctrue (K.at s) m₀ c r) (lower .onlyBefore ct ctrue T' m c r) := by
  intro t h
  have hx' : ∀ k, inner none ct ctrue c (onlyBody T' ff r) (t + k) := fun k =>
    inner_mono none ct ctrue c hx _ (global_suffix hs hc h k)
  refine lower_onlyBefore.mpr ⟨fun _ => firstEntry_of_all (fun j => ?_), fun _ => hx' 0⟩
  have := hx' (j + 1)
  rwa [← Nat.add_assoc] at this

theorem Kind.eq_alw {K : Kind} (h : K.isAlw = true) : K = .basic .alw := by
  rcases K with ⟨A⟩ | _ | _ <;> (try cases A) <;> simp_all [Kind.isAlw]

/-- Every two-field move the cross step accepts implies, at any condition,
response and stop, given the two scopes' modes agree where `sameMode` says. -/
theorem cross_sound {k k' : ScopeKind} {K' : Kind} {s' m' : Pred} {sameMode : Bool}
    (hmode : sameMode = true → m' = m) (hc : ctrue = false ∨ c = tt)
    (ht : crossTiming K K' = true) (hk : crossScope k k' sameMode K ct = true) :
    Entails (lower k ct ctrue (K.at s) m c r) (lower k' ct ctrue (K'.at s') m' c r) := by
  cases k <;> cases k' <;> simp only [crossScope] at hk <;> (try exact absurd hk (by decide))
  · have hs : ct = .continual ∨ K = .basic .alw := by
      simp only [Bool.or_eq_true, beq_iff_eq] at hk
      exact hk.imp id Kind.eq_alw
    exact cross_global_onlyIn hs hc (cross_body ht s s' (fun _ h => h.elim))
  · have hs : ct = .continual ∨ K = .basic .alw := by
      simp only [Bool.or_eq_true, beq_iff_eq] at hk
      exact hk.imp id Kind.eq_alw
    exact cross_global_onlyBefore hs hc (cross_body ht s s' (fun _ h => h.elim))
  · exact cross_global_onlyAfter (cross_body ht s s' (fun _ h => h.elim))
  · rw [hmode hk]
    exact cross_notIn_onlyIn (fun _ => cross_body ht s s' (fun _ h => h))

end Fretish
