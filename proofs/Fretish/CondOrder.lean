import Fretish.ScopeOrder

/-!
# The condition-type order

`conditionTypeStep` transcribes `condition_type_step` in
`src/filter/implication_check.cpp`. A continual requirement implies its trigger
form under every scope and timing. A trigger implies its continual form exactly
where the body is a release: `always` under a plain scope, `eventually` under an
only scope, whose negated body is `always` over the negated response.

`condition_type_step_exact` proves the transcription exact for a condition that
is not the literal `true`, and `condition_type_step_sound` proves its accepted
steps also hold at the literal `true`.
-/

namespace Fretish

/-- `condition_type_step`, for a step from `src` to the other condition type. -/
def conditionTypeStep (src : CondType) (k : ScopeKind) (K : Kind) : Bool :=
  if src == .continual then true
  else if isOnly k then K.isEv else K.isAlw

variable {ct ct' : CondType} {ctrue : Bool} {T : Timing} {K : Kind} {s m c r b p x : Pred} {t : Nat}

/-! ## Soundness -/

/-- A continual condition implies the trigger at the same body. -/
theorem inner_cont_trig (ob : Option Pred) :
    inner ob .continual ctrue c x t → inner ob .trigger ctrue c x t := by
  cases ob <;> cases ctrue
  · exact fun h => ⟨fun j ⟨_, hc⟩ => h (j + 1) hc, fun hc => h 0 hc⟩
  · exact fun h hc => h 0 hc
  · intro h
    rename_i b
    change R b (fun u => c u → x u) t at h
    refine ⟨fun j => ?_, fun hc => R_now h hc⟩
    rcases h (j + 1) with h1 | ⟨i, hi, hb⟩
    · exact Or.inl (fun ⟨_, hc, hnb⟩ => ⟨h1 hc, hnb⟩)
    · by_cases hij : i < j
      · exact Or.inr ⟨i, hij, hb⟩
      · have : i = j := by omega
        subst this
        exact Or.inl (fun ⟨_, _, hnb⟩ => absurd hb hnb)
  · exact fun h hc => R_now (b := _) (p := fun u => c u → x u) h hc

/-- A release with no boundary in a window still holds at its end. -/
theorem R_shift {k : Nat} (h : R b p t) (hnb : ∀ i, i < k → ¬ b (t + i)) : R b p (t + k) := by
  intro l
  rcases h (k + l) with hp | ⟨i, hi, hb⟩
  · exact Or.inl (by rwa [← Nat.add_assoc] at hp)
  · by_cases hik : i < k
    · exact absurd hb (hnb i hik)
    · refine Or.inr ⟨i - k, by omega, ?_⟩
      rwa [show t + k + (i - k) = t + i by omega]

/-- A trigger over a release implies the continual condition over it. -/
theorem inner_trig_cont_R (ob : Option Pred) (hc : ctrue = false ∨ c = tt) :
    inner ob .trigger ctrue c (R (ob.getD ff) p) t →
      inner ob .continual ctrue c (R (ob.getD ff) p) t := by
  cases ob <;> cases ctrue
  · intro h j hcj
    exact R_ff.mpr (trig_alw h j hcj)
  · rcases hc with hc | rfl
    · cases hc
    intro h j _
    exact R_ff.mpr (G_shift (R_ff.mp (h trivial)) (Nat.le_add_right t j))
  · rename_i b
    intro h j
    by_cases hw : ∃ i, i < j ∧ b (t + i)
    · exact Or.inr hw
    refine Or.inl (fun hcj => ?_)
    have hw' : ∀ i, i < j → ¬ b (t + i) := fun i hi hb => hw ⟨i, hi, hb⟩
    obtain ⟨j0, hc0, hmin⟩ := exists_first (p := fun j => c (t + j)) ⟨j, hcj⟩
    have hj : j0 ≤ j := Nat.le_of_not_lt (fun hlt => hmin j hlt hcj)
    have r0 : R b p (t + j0) := by
      cases j0 with
      | zero => exact h.2 hc0
      | succ i =>
        rcases h.1 i with h1 | ⟨i', hi', hb⟩
        · exact (h1 ⟨hmin i (Nat.lt_succ_self i), hc0, hw' i (by omega)⟩).1
        · exact absurd hb (hw' i' (by omega))
    have := R_shift (k := j - j0) r0 (fun i hi hb => hw' (j0 + i) (by omega)
      (by rwa [← Nat.add_assoc]))
    rwa [show t + j0 + (j - j0) = t + j by omega] at this
  · rcases hc with hc | rfl
    · cases hc
    rename_i b
    intro h j
    by_cases hw : ∃ i, i < j ∧ b (t + i)
    · exact Or.inr hw
    exact Or.inl (fun _ => R_shift (h trivial) (fun i hi hb => hw ⟨i, hi, hb⟩))

theorem sound_cont_trig (k : ScopeKind) :
    Entails (lower k .continual ctrue (K.at s) m c r) (lower k .trigger ctrue (K.at s) m c r) :=
  wrap_mono k m (fun _ => inner_cont_trig _)

theorem sound_trig_cont (k : ScopeKind) (hc : ctrue = false ∨ c = tt)
    (hk : isOnly k = true ∧ K = .basic .ev ∨ isOnly k = false ∧ K = .basic .alw) :
    Entails (lower k .trigger ctrue (K.at s) m c r) (lower k .continual ctrue (K.at s) m c r) := by
  rcases hk with ⟨hk, rfl⟩ | ⟨hk, rfl⟩ <;> cases k <;>
    (first | exact absurd hk (by decide) | skip) <;>
    exact wrap_mono _ m (fun _ => inner_trig_cont_R _ hc)

/-- Every step `condition_type_step` accepts holds, at any condition other than
the literal `true` and at the literal `true` itself. -/
theorem condition_type_step_sound {k : ScopeKind} (hc : ctrue = false ∨ c = tt) (hne : ct ≠ ct')
    (h : conditionTypeStep ct k K = true) :
    Entails (lower k ct ctrue (K.at s) m c r) (lower k ct' ctrue (K.at s) m c r) := by
  cases ct <;> cases ct' <;> (first | exact absurd rfl hne | skip)
  · exact sound_cont_trig k
  · refine sound_trig_cont k hc ?_
    simp only [conditionTypeStep] at h
    rcases K.cls_cases with ⟨rfl, -, -⟩ | ⟨rfl, -, -⟩ | ⟨_, _, hev, hal⟩ <;>
      cases hk : isOnly k <;> simp_all [Kind.isEv, Kind.isAlw]

/-! ## Refutations -/

/-- A trigger that holds at 0 where its continual form fails. -/
def NotStep (k : ScopeKind) (K : Kind) : Prop :=
  ∃ s m c r, lower k .trigger false (K.at s) m c r 0 ∧ ¬ lower k .continual false (K.at s) m c r 0

/-- A plain scope that reads the plain body at a constant mode. -/
theorem refcP {k : ScopeKind} {M : Pred} (hK : K ≠ .basic .alw)
    (hl : ∀ ct s r, lower k ct false (K.at s) M tt r 0 ↔ Pz ct false tt (K.at s) r) :
    NotStep k K := by
  obtain ⟨s, r, h0, h1⟩ := wD 0 K hK
  exact ⟨s, M, tt, r, (hl _ s r).mpr ((inner_tt_trig none).mpr ((body_ff _ _ _).mpr h0)),
    fun h => h1 ((body_ff _ _ _).mp (((hl _ s r).mp h) 1 trivial))⟩

/-- An only scope that reads the only body at a constant mode. -/
theorem refcN {k : ScopeKind} {M : Pred} (hK : K ≠ .basic .ev)
    (hl : ∀ ct s r, lower k ct false (K.at s) M tt r 0 ↔ Nz ct false tt (K.at s) r) :
    NotStep k K := by
  obtain ⟨s, r, h0, h1⟩ := wDN K hK
  exact ⟨s, M, tt, r, (hl _ s r).mpr ((inner_tt_trig none).mpr h0),
    fun h => h1 (((hl _ s r).mp h) 1 trivial)⟩

theorem lowerP_ff {k : ScopeKind} (hv : vff k = .P) :
    lower k ct false T ff tt r 0 ↔ Pz ct false tt T r := by
  rw [lower_ff, hv]; exact Iff.rfl

theorem lowerP_tt {k : ScopeKind} (hv : vtt k = .P) :
    lower k ct false T tt tt r 0 ↔ Pz ct false tt T r := by
  rw [lower_tt, hv]; exact Iff.rfl

theorem lowerN_ff {k : ScopeKind} (hv : vff k = .N) :
    lower k ct false T ff tt r 0 ↔ Nz ct false tt T r := by
  rw [lower_ff, hv]; exact Iff.rfl

theorem lowerN_tt {k : ScopeKind} (hv : vtt k = .N) :
    lower k ct false T tt tt r 0 ↔ Nz ct false tt T r := by
  rw [lower_tt, hv]; exact Iff.rfl

/-- After: the first exit at 0 reads the body at 1. -/
theorem refc_after (hK : K ≠ .basic .alw) : NotStep .afterMode K := by
  obtain ⟨s, r, h1, h2⟩ := wD 1 K hK
  refine ⟨s, upto 0, tt, r, lower_after.mpr (Or.inl ⟨0, ⟨eom_upto0, ?_⟩, fun _ hi => absurd hi
    (Nat.not_lt_zero _)⟩), fun h => ?_⟩
  · exact (inner_tt_trig none).mpr ((body_ff _ _ _).mpr h1)
  · exact h2 ((body_ff _ _ _).mp ((firstEntry_now eom_upto0 (lower_after.mp h)) 1 trivial))

theorem not_step {k : ScopeKind} (h : conditionTypeStep .trigger k K = false) : NotStep k K := by
  simp only [conditionTypeStep] at h
  rcases K.cls_cases with ⟨rfl, -, -⟩ | ⟨rfl, -, -⟩ | ⟨hne, hna, hev, hal⟩ <;>
  cases k <;> simp_all [isOnly, Kind.isEv, Kind.isAlw] <;>
    first
    | exact refc_after (by first | assumption | decide)
    | exact refcP (by first | assumption | decide) (fun _ _ _ => lowerP_ff rfl)
    | exact refcP (by first | assumption | decide) (fun _ _ _ => lowerP_tt rfl)
    | exact refcN (by first | assumption | decide) (fun _ _ _ => lowerN_ff rfl)
    | exact refcN (by first | assumption | decide) (fun _ _ _ => lowerN_tt rfl)

/-! ## The theorem -/

/-- The condition-type order at a condition other than the literal `true`. -/
def CondOrder (ct ct' : CondType) (k : ScopeKind) (K : Kind) : Prop :=
  ∀ s m c r, Entails (lower k ct false (K.at s) m c r) (lower k ct' false (K.at s) m c r)

/-- `condition_type_step` is exact: a change of condition type holds at every
stop, mode, condition and response exactly when it accepts the step. -/
theorem condition_type_step_exact (ct ct' : CondType) (hne : ct ≠ ct') (k : ScopeKind) (K : Kind) :
    CondOrder ct ct' k K ↔ conditionTypeStep ct k K = true := by
  constructor
  · intro h
    cases hs : conditionTypeStep ct k K
    · cases ct <;> cases ct' <;> (first | exact absurd rfl hne | skip)
      · simp [conditionTypeStep] at hs
      · obtain ⟨s, m, c, r, h1, h2⟩ := not_step hs
        exact absurd (h s m c r 0 h1) h2
    · rfl
  · exact fun h s m c r => condition_type_step_sound (Or.inl rfl) hne h

end Fretish
