import Fretish.Cpp
import Fretish.Witness

/-!
# The scope order

`scopeImplies` transcribes `scope_implies` in `src/requirement.cpp`, with
`scopeEdges` for `scope_edges`, branch for branch. A scope's mode is reduced to
whether the two scopes share it, and a global scope carries none, so two global
scopes always share theirs.

`scope_implies_exact` proves the transcription exact for a condition that is
not the literal `true`: every pair it accepts implies at every mode, condition,
response and stop, and every pair it rejects has a trace on which the first
requirement holds and the second fails. `scope_implies_sound` proves the
accepted pairs also hold when the condition is the literal `true`.
-/

namespace Fretish

/-! ## The transcription -/

/-- `std::holds_alternative<timing::Eventually>` -/
def Kind.isEv : Kind → Bool
  | .basic .ev => true
  | _ => false

/-- `std::holds_alternative<timing::Always>` -/
def Kind.isAlw : Kind → Bool
  | .basic .alw => true
  | _ => false

/-- `scope_edges` -/
def scopeEdges (K : Kind) (ct : CondType) : List (ScopeKind × ScopeKind) :=
  let notinBefore : ScopeKind × ScopeKind := (.notInMode, .beforeMode)
  let fromGlobal : List (ScopeKind × ScopeKind) :=
    [(.global, .inMode), (.global, .notInMode), (.global, .beforeMode), (.global, .afterMode),
      notinBefore]
  if K.isEv then
    if ct == .continual then [(.global, .afterMode), notinBefore] else [notinBefore]
  else if ct == .continual || K.isAlw then fromGlobal
  else [(.global, .beforeMode), notinBefore]

/-- `scope_implies`. `sameMode` is the comparison of the two modes. -/
def scopeImplies (k k' : ScopeKind) (sameMode : Bool) (K : Kind) (ct : CondType) : Bool :=
  if k == k' && sameMode then true
  else if k != .global && !sameMode then false
  else (scopeEdges K ct).any (· == (k, k'))

theorem Kind.cls_cases (K : Kind) :
    (K = .basic .ev ∧ K.isEv = true ∧ K.isAlw = false) ∨
    (K = .basic .alw ∧ K.isEv = false ∧ K.isAlw = true) ∨
    (K ≠ .basic .ev ∧ K ≠ .basic .alw ∧ K.isEv = false ∧ K.isAlw = false) := by
  rcases K with ⟨A⟩ | _ | _ <;> (try cases A) <;> simp [Kind.isEv, Kind.isAlw]

theorem Kind.at_ne_ev {K : Kind} {s : Pred} (h : K ≠ .basic .ev) : K.at s ≠ .eventually := by
  rcases K with ⟨A⟩ | _ | _ <;> (try cases A) <;> simp_all [Kind.at, Basic.timing]

variable {ct : CondType} {ctrue : Bool} {T : Timing} {K : Kind} {s m m' c r b x : Pred} {t : Nat}

/-! ## Each scope, unfolded -/

theorem lower_global : lower .global ct ctrue T m c r t ↔ inner none ct ctrue c (body T ff r) t :=
  Iff.rfl

theorem lower_in : lower .inMode ct ctrue T m c r t ↔
    reentry (som m) (inner (some (eom m)) ct ctrue c (body T (eom m) r)) t ∧
      (m t → inner (some (eom m)) ct ctrue c (body T (eom m) r) t) := Iff.rfl

theorem lower_notIn : lower .notInMode ct ctrue T m c r t ↔
    reentry (eom m) (inner (some (som m)) ct ctrue c (body T (som m) r)) t ∧
      (¬ m t → inner (some (som m)) ct ctrue c (body T (som m) r) t) := Iff.rfl

theorem lower_before : lower .beforeMode ct ctrue T m c r t ↔
    inner (some (som m)) ct ctrue c (body T (som m) r) t ∨ m t := Iff.rfl

theorem lower_after : lower .afterMode ct ctrue T m c r t ↔
    firstEntry (eom m) (inner none ct ctrue c (body T ff r)) t := Iff.rfl

theorem lower_onlyIn : lower .onlyIn ct ctrue T m c r t ↔
    reentry (eom m) (inner (some (som m)) ct ctrue c (onlyBody T (som m) r)) t ∧
      (¬ m t → inner (some (som m)) ct ctrue c (onlyBody T (som m) r) t) := Iff.rfl

theorem lower_onlyBefore : lower .onlyBefore ct ctrue T m c r t ↔
    (¬ m t → firstEntry (som m) (inner none ct ctrue c (onlyBody T ff r)) t) ∧
      (m t → inner none ct ctrue c (onlyBody T ff r) t) := Iff.rfl

theorem lower_onlyAfter : lower .onlyAfter ct ctrue T m c r t ↔
    inner (some (eom m)) ct ctrue c (onlyBody T (eom m) r) t := Iff.rfl

/-! ## Soundness -/

/-- A boundary only weakens a plain body, except under `eventually`. -/
theorem body_bmono (T : Timing) (hT : T ≠ .eventually) {b b' p : Pred} (hb : Entails b b') :
    Entails (body T b p) (body T b' p) := by
  intro t h
  cases T with
  | immediately => exact h
  | nextTick => exact Or.imp (hb t) id h
  | within n => exact Or.imp id (fun ⟨k, hk, h⟩ => ⟨k, hk, hb _ h⟩) h
  | forTicks n => exact Or.imp id (R_mono_left hb t) h
  | afterTicks n =>
    exact ⟨Or.imp id (R_mono_left hb t) h.1, Or.imp id (fun ⟨k, hk, h⟩ => ⟨k, hk, hb _ h⟩) h.2⟩
  | eventually => exact absurd rfl hT
  | always => exact R_mono_left hb t h
  | untilStop s => exact Or.imp id (R_mono_left hb t) h
  | beforeStop s => exact R_mono_left (fun u => Or.imp id (hb u)) t h

/-- The unbounded condition implies the bounded one over a weaker body. -/
theorem inner_relax {x' : Pred} (hx : ∀ u, x u → x' u) (h : inner none ct ctrue c x t) :
    inner (some b) ct ctrue c x' t := by
  cases ct <;> cases ctrue
  · exact fun j => Or.inl (fun hc => hx _ (h j hc))
  · exact fun j => Or.inl (fun hc => hx _ (h j hc))
  · exact ⟨fun j => Or.inl (fun ⟨h1, h2, h3⟩ => ⟨hx _ (h.1 j ⟨h1, h2⟩), h3⟩),
      fun hc => hx _ (h.2 hc)⟩
  · exact fun hc => hx _ (h hc)

theorem G_shift {u : Nat} (h : G r t) (hu : t ≤ u) : G r u := fun l => by
  have := h (u - t + l)
  rwa [show t + (u - t + l) = u + l by omega] at this

/-- A trigger over `always` holds at every point of the condition. -/
theorem trig_alw (h : innerTrigU c (R ff r) t) : ∀ j, c (t + j) → G r (t + j) := by
  intro j hcj
  obtain ⟨j0, hc0, hmin⟩ := exists_first (p := fun j => c (t + j)) ⟨j, hcj⟩
  have hj : j0 ≤ j := Nat.le_of_not_lt (fun hlt => hmin j hlt hcj)
  have g0 : G r (t + j0) := by
    cases j0 with
    | zero => exact R_ff.mp (h.2 hc0)
    | succ i => exact R_ff.mp (h.1 i ⟨hmin i (Nat.lt_succ_self i), hc0⟩)
  exact G_shift g0 (by omega)

/-- The global requirement holds at every later point, under a continual
condition or the timing `always`. -/
theorem global_suffix (hs : ct = .continual ∨ K = .basic .alw) (hc : ctrue = false ∨ c = tt)
    (h : inner none ct ctrue c (body (K.at s) ff r) t) :
    ∀ k, inner none ct ctrue c (body (K.at s) ff r) (t + k) := by
  intro k
  cases ct
  · cases ctrue <;>
    exact fun j hcj => by
      have := h (k + j)
      rw [← Nat.add_assoc] at this
      exact this hcj
  · rcases hs with hs | rfl
    · cases hs
    cases ctrue
    · change innerTrigU c (R ff r) t at h
      change innerTrigU c (R ff r) (t + k)
      have TA := trig_alw h
      refine ⟨fun j he => R_ff.mpr ?_, fun hck => R_ff.mpr (TA k hck)⟩
      have e : t + k + j + 1 = t + (k + j + 1) := by omega
      have := TA (k + j + 1) (e ▸ he.2)
      rwa [← e] at this
    · rcases hc with hc | rfl
      · cases hc
      change True → R ff r t at h
      change True → R ff r (t + k)
      exact fun _ => R_ff.mpr (G_shift (R_ff.mp (h trivial)) (Nat.le_add_right t k))

theorem sound_nb : Entails (lower .notInMode ct ctrue T m c r) (lower .beforeMode ct ctrue T m c r) := by
  intro t h
  by_cases hm : m t
  · exact lower_before.mpr (Or.inr hm)
  · exact lower_before.mpr (Or.inl ((lower_notIn.mp h).2 hm))

theorem sound_GB (hK : K ≠ .basic .ev) :
    Entails (lower .global ct ctrue (K.at s) m c r) (lower .beforeMode ct ctrue (K.at s) m c r) :=
  fun _ h => lower_before.mpr (Or.inl (inner_relax
    (body_bmono _ (Kind.at_ne_ev hK) (fun _ h => h.elim)) (lower_global.mp h)))

theorem sound_GI (hs : ct = .continual ∨ K = .basic .alw) (hc : ctrue = false ∨ c = tt)
    (hK : K ≠ .basic .ev) :
    Entails (lower .global ct ctrue (K.at s) m c r) (lower .inMode ct ctrue (K.at s) m c r) := by
  intro t h
  have hx : ∀ k, inner (some (eom m)) ct ctrue c (body (K.at s) (eom m) r) (t + k) := fun k =>
    inner_relax (body_bmono _ (Kind.at_ne_ev hK) (fun _ h => h.elim)) (global_suffix hs hc h k)
  refine lower_in.mpr ⟨fun j => Or.inr ?_, fun _ => hx 0⟩
  have := hx (j + 1)
  rwa [← Nat.add_assoc] at this

theorem sound_GNI (hs : ct = .continual ∨ K = .basic .alw) (hc : ctrue = false ∨ c = tt)
    (hK : K ≠ .basic .ev) :
    Entails (lower .global ct ctrue (K.at s) m c r) (lower .notInMode ct ctrue (K.at s) m c r) := by
  intro t h
  have hx : ∀ k, inner (some (som m)) ct ctrue c (body (K.at s) (som m) r) (t + k) := fun k =>
    inner_relax (body_bmono _ (Kind.at_ne_ev hK) (fun _ h => h.elim)) (global_suffix hs hc h k)
  refine lower_notIn.mpr ⟨fun j => Or.inr ?_, fun _ => hx 0⟩
  have := hx (j + 1)
  rwa [← Nat.add_assoc] at this

theorem sound_GA (hs : ct = .continual ∨ K = .basic .alw) (hc : ctrue = false ∨ c = tt) :
    Entails (lower .global ct ctrue (K.at s) m c r) (lower .afterMode ct ctrue (K.at s) m c r) := by
  intro t h
  refine lower_after.mpr (firstEntry_of_all (fun j => ?_))
  have := global_suffix hs hc h (j + 1)
  rwa [← Nat.add_assoc] at this

/-- Every edge `scope_edges` lists holds at one mode. -/
theorem edge_sound (hc : ctrue = false ∨ c = tt)
    {k k' : ScopeKind} (he : (scopeEdges K ct).any (· == (k, k')) = true) :
    Entails (lower k ct ctrue (K.at s) m c r) (lower k' ct ctrue (K.at s) m c r) := by
  rcases K.cls_cases with ⟨rfl, hev, hal⟩ | ⟨rfl, hev, hal⟩ | ⟨hne, hna, hev, hal⟩ <;>
  cases ct <;> cases k <;> cases k' <;>
    simp [scopeEdges, Kind.isEv, Kind.isAlw] at he <;>
    first
    | exact sound_nb
    | exact sound_GB (by first | assumption | decide)
    | exact sound_GA (by first | exact Or.inl rfl | exact Or.inr rfl) hc
    | exact sound_GI (by first | exact Or.inl rfl | exact Or.inr rfl) hc
        (by first | assumption | decide)
    | exact sound_GNI (by first | exact Or.inl rfl | exact Or.inr rfl) hc
        (by first | assumption | decide)

/-! ## Refutations -/

/-- A trace on which the first requirement holds at 0 and the second fails,
both at one mode. -/
def NotOrder (ct : CondType) (K : Kind) (k k' : ScopeKind) : Prop :=
  ∃ s m c r, lower k ct false (K.at s) m c r 0 ∧ ¬ lower k' ct false (K.at s) m c r 0

/-- The same at two modes. -/
def NotOrderD (ct : CondType) (K : Kind) (k k' : ScopeKind) : Prop :=
  ∃ s m m' c r, lower k ct false (K.at s) m c r 0 ∧ ¬ lower k' ct false (K.at s) m' c r 0

/-- The pairs of constant-mode values a trace can separate. -/
def sepOK : V → V → Bool
  | .T, .P | .T, .N | .P, .N | .N, .P => true
  | _, _ => false

theorem sepW {a a' : V} (h : sepOK a a' = true) (ct : CondType) (K : Kind) :
    ∃ s r, a.eval (Pz ct false (pt 0) (K.at s) r) (Nz ct false (pt 0) (K.at s) r) ∧
      ¬ a'.eval (Pz ct false (pt 0) (K.at s) r) (Nz ct false (pt 0) (K.at s) r) := by
  have hP : ∀ s r, Pz ct false (pt 0) (K.at s) r ↔ bodyU (K.at s) r 0 :=
    fun s r => (inner_pt0 none).trans (body_ff _ _ _)
  have hN : ∀ s r, Nz ct false (pt 0) (K.at s) r ↔ onlyBody (K.at s) ff r 0 :=
    fun s r => inner_pt0 none
  cases a <;> cases a' <;> (first | exact absurd h (by decide) | skip)
  · obtain ⟨s, hb, -⟩ := wBot K
    exact ⟨s, ff, trivial, fun h => hb 0 ((hP s ff).mp h)⟩
  · obtain ⟨s, r, -, hn⟩ := wTop 0 K
    exact ⟨s, r, trivial, fun h => hn ((hN s r).mp h)⟩
  · obtain ⟨s, r, hp, hn⟩ := wTop 0 K
    exact ⟨s, r, (hP s r).mpr hp, fun h => hn ((hN s r).mp h)⟩
  · obtain ⟨s, hb, ho⟩ := wBot K
    exact ⟨s, ff, (hN s ff).mpr (ho ff 0), fun h => hb 0 ((hP s ff).mp h)⟩

theorem ref_ff (k k' : ScopeKind) (h : sepOK (vff k) (vff k') = true) : NotOrder ct K k k' := by
  obtain ⟨s, r, h1, h2⟩ := sepW h ct K
  exact ⟨s, ff, pt 0, r, (lower_ff k).mpr h1, fun h => h2 ((lower_ff k').mp h)⟩

theorem ref_tt (k k' : ScopeKind) (h : sepOK (vtt k) (vtt k') = true) : NotOrder ct K k k' := by
  obtain ⟨s, r, h1, h2⟩ := sepW h ct K
  exact ⟨s, tt, pt 0, r, (lower_tt k).mpr h1, fun h => h2 ((lower_tt k').mp h)⟩

theorem refD_ff_tt (k k' : ScopeKind) (h : sepOK (vff k) (vtt k') = true) :
    NotOrderD ct K k k' := by
  obtain ⟨s, r, h1, h2⟩ := sepW h ct K
  exact ⟨s, ff, tt, pt 0, r, (lower_ff k).mpr h1, fun h => h2 ((lower_tt k').mp h)⟩

theorem refD_tt_ff (k k' : ScopeKind) (h : sepOK (vtt k) (vff k') = true) :
    NotOrderD ct K k k' := by
  obtain ⟨s, r, h1, h2⟩ := sepW h ct K
  exact ⟨s, tt, ff, pt 0, r, (lower_tt k).mpr h1, fun h => h2 ((lower_ff k').mp h)⟩

/-! ### Modes that change -/

/-- Holds at 0 alone. -/
theorem eom_upto0 : eom (upto 0) 0 := ⟨Nat.le_refl 0, by omega⟩
theorem som_upto0 : som (upto 0) = ff :=
  funext fun u => propext ⟨fun ⟨_, (h : u + 1 ≤ 0)⟩ => by omega, False.elim⟩

/-- Rises at 1 and never falls. -/
abbrev from1 : Pred := fun x => 1 ≤ x
theorem som_from1 : som from1 0 := ⟨by omega, by omega⟩
theorem eom_from1 : eom from1 = ff :=
  funext fun u => propext ⟨fun ⟨(h1 : 1 ≤ u), (h2 : ¬ 1 ≤ u + 1)⟩ => by omega, False.elim⟩

/-- False at 1 alone. -/
abbrev mid : Pred := fun x => x ≠ 1

/-- A response that never holds fails after the first exit, under a condition
that always holds. -/
theorem after_bot (hb : ∀ t, ¬ bodyU (K.at s) ff t) :
    ¬ lower .afterMode ct false (K.at s) (upto 0) tt ff 0 :=
  fun h => hb 1 ((body_ff _ _ _).mp (inner_tt_now (firstEntry_now eom_upto0 (lower_after.mp h))))

/-- A boundary at the next point leaves a condition that always holds over a
response that never holds unmet there. -/
theorem notIn_bot (hb : ∀ t, ¬ bodyU (K.at s) ff t) :
    ¬ lower .notInMode ct false (K.at s) (upto 0) tt ff 0 := by
  intro h
  have hx := ((lower_notIn.mp h).1 0).resolve_left (not_not_intro eom_upto0)
  rw [som_upto0] at hx
  exact hb 1 ((body_ff _ _ _).mp (inner_tt_now (inner_some_ff.mp hx)))

/-- The only scopes hold over a response that never holds. -/
theorem only_bot (hb : ∀ b t, onlyBody (K.at s) b ff t) (k : ScopeKind) (hk : isOnly k = true) :
    lower k ct false (K.at s) m c ff 0 := by
  have hx : ∀ b' u, inner b' ct false c (onlyBody (K.at s) (b'.getD ff) ff) u :=
    fun _ _ => inner_of_all _ (hb _)
  cases k <;> (first | exact absurd hk (by decide) | skip)
  · exact lower_onlyIn.mpr ⟨fun _ => Or.inr (hx (some (som m)) _), fun _ => hx (some (som m)) _⟩
  · exact lower_onlyBefore.mpr ⟨fun _ => firstEntry_of_all (fun _ => hx none _), fun _ => hx none _⟩
  · exact lower_onlyAfter.mpr (hx (some (eom m)) _)

/-- In ⇏ after. -/
theorem ref_in_after : NotOrder ct K .inMode .afterMode := by
  obtain ⟨s, hb, -⟩ := wBot K
  refine ⟨s, upto 0, pt 2, ff, lower_in.mpr ⟨?_, fun _ => inner_vac eom_upto0 (by omega) (by omega)⟩,
    fun h => ?_⟩
  · rw [som_upto0]; exact fun _ => Or.inl id
  · have hx := firstEntry_now eom_upto0 (lower_after.mp h)
    exact hb 2 ((body_ff _ _ _).mp (inner_pt_at hx (by omega)))

/-- Not in ⇏ after. -/
theorem ref_notIn_after : NotOrder ct K .notInMode .afterMode := by
  obtain ⟨s, hb, -⟩ := wBot K
  refine ⟨s, mid, pt 3, ff, lower_notIn.mpr ⟨fun j => ?_, fun h => absurd (by omega) h⟩, fun h => ?_⟩
  · by_cases hj : j = 0
    · subst hj
      exact Or.inr (inner_vac ⟨by omega, by omega⟩ (by omega) (by omega))
    · exact Or.inl (fun ⟨_, (h : ¬ 0 + j + 1 ≠ 1)⟩ => hj (by omega))
  · have hx := firstEntry_now (e := eom mid) (t := 0) ⟨by omega, by omega⟩ (lower_after.mp h)
    exact hb 3 ((body_ff _ _ _).mp (inner_pt_at hx (by omega)))

/-- Before ⇏ not in. -/
theorem ref_before_notIn : NotOrder ct K .beforeMode .notInMode := by
  obtain ⟨s, hb, -⟩ := wBot K
  exact ⟨s, upto 0, tt, ff, lower_before.mpr (Or.inr (Nat.le_refl 0)), notIn_bot hb⟩

/-- Before ⇏ after. -/
theorem ref_before_after : NotOrder ct K .beforeMode .afterMode := by
  obtain ⟨s, hb, -⟩ := wBot K
  exact ⟨s, upto 0, tt, ff, lower_before.mpr (Or.inr (Nat.le_refl 0)), after_bot hb⟩

/-- Each only scope ⇏ after. -/
theorem ref_only_after (k : ScopeKind) (hk : isOnly k = true) : NotOrder ct K k .afterMode := by
  obtain ⟨s, hb, ho⟩ := wBot K
  exact ⟨s, upto 0, tt, ff, only_bot ho k hk, after_bot hb⟩

/-- Only after, vacuous at an exit before the condition. -/
theorem onlyAfter_vac : lower .onlyAfter ct false T (upto 0) (pt 2) r 0 :=
  lower_onlyAfter.mpr (inner_vac eom_upto0 (by omega) (by omega))

/-- Only after ⇏ only in. -/
theorem ref_onlyAfter_onlyIn : NotOrder ct K .onlyAfter .onlyIn := by
  obtain ⟨s, r, -, hn⟩ := wTop 2 K
  refine ⟨s, upto 0, pt 2, r, onlyAfter_vac, fun h => ?_⟩
  have hx := ((lower_onlyIn.mp h).1 0).resolve_left (not_not_intro eom_upto0)
  rw [som_upto0] at hx
  exact hn (inner_pt_at (inner_some_ff.mp hx) (by omega))

/-- Only after ⇏ only before. -/
theorem ref_onlyAfter_onlyBefore : NotOrder ct K .onlyAfter .onlyBefore := by
  obtain ⟨s, r, -, hn⟩ := wTop 2 K
  exact ⟨s, upto 0, pt 2, r, onlyAfter_vac,
    fun h => hn (inner_pt_at ((lower_onlyBefore.mp h).2 (Nat.le_refl 0)) (by omega))⟩

/-! ### Global under `eventually` -/

theorem global_ev : lower .global ct false (Kind.at (.basic .ev) s) m (pt 0) (pt 1) 0 :=
  lower_global.mpr ((inner_pt0 none).mpr ⟨1, rfl, fun _ _ h => h⟩)

theorem ref_ev_in : NotOrder ct (.basic .ev) .global .inMode :=
  ⟨ff, upto 0, pt 0, pt 1, global_ev, fun h =>
    let ⟨_, (hj : 0 + _ = 1), hpre⟩ := (inner_pt0 _).mp ((lower_in.mp h).2 (Nat.le_refl 0))
    hpre 0 (by omega) eom_upto0⟩

theorem ref_ev_notIn : NotOrder ct (.basic .ev) .global .notInMode :=
  ⟨ff, from1, pt 0, pt 1, global_ev, fun h =>
    let ⟨_, (hj : 0 + _ = 1), hpre⟩ := (inner_pt0 _).mp ((lower_notIn.mp h).2 (by omega))
    hpre 0 (by omega) som_from1⟩

theorem ref_ev_before : NotOrder ct (.basic .ev) .global .beforeMode :=
  ⟨ff, from1, pt 0, pt 1, global_ev, fun h =>
    let ⟨_, (hj : 0 + _ = 1), hpre⟩ :=
      (inner_pt0 _).mp ((lower_before.mp h).resolve_right (by omega))
    hpre 0 (by omega) som_from1⟩

/-! ### Global under a trigger -/

theorem ref_trig (hK : K ≠ .basic .alw) {k' : ScopeKind}
    (h : ∀ s r, ¬ bodyU (K.at s) r 1 →
      ∃ m, ¬ lower k' .trigger false (K.at s) m tt r 0) :
    NotOrder .trigger K .global k' := by
  obtain ⟨s, r, h0, h1⟩ := wD 0 K hK
  obtain ⟨m, hm⟩ := h s r h1
  exact ⟨s, m, tt, r, lower_global.mpr ((inner_tt_trig none).mpr ((body_ff _ _ _).mpr h0)), hm⟩

theorem ref_trig_in (hK : K ≠ .basic .alw) : NotOrder .trigger K .global .inMode :=
  ref_trig hK fun _ _ h1 => ⟨from1, fun h => by
    have hx := ((lower_in.mp h).1 0).resolve_left (not_not_intro som_from1)
    rw [eom_from1] at hx
    exact h1 ((body_ff _ _ _).mp (inner_tt_now (inner_some_ff.mp hx)))⟩

theorem ref_trig_notIn (hK : K ≠ .basic .alw) : NotOrder .trigger K .global .notInMode :=
  ref_trig hK fun _ _ h1 => ⟨upto 0, fun h => by
    have hx := ((lower_notIn.mp h).1 0).resolve_left (not_not_intro eom_upto0)
    rw [som_upto0] at hx
    exact h1 ((body_ff _ _ _).mp (inner_tt_now (inner_some_ff.mp hx)))⟩

theorem ref_trig_after (hK : K ≠ .basic .alw) : NotOrder .trigger K .global .afterMode :=
  ref_trig hK fun _ _ h1 => ⟨upto 0, fun h =>
    h1 ((body_ff _ _ _).mp (inner_tt_now (firstEntry_now eom_upto0 (lower_after.mp h))))⟩

/-- Every pair `scope_edges` leaves out, other than a scope with itself, fails
at one mode. -/
theorem not_edge {k k' : ScopeKind} (hne : k ≠ k')
    (he : (scopeEdges K ct).any (· == (k, k')) = false) : NotOrder ct K k k' := by
  rcases K.cls_cases with ⟨rfl, hev, hal⟩ | ⟨rfl, hev, hal⟩ | ⟨hne', hna, hev, hal⟩ <;>
  cases ct <;> cases k <;> cases k' <;>
    simp [scopeEdges, Kind.isEv, Kind.isAlw] at he hne <;>
    first
    | exact ref_ff _ _ rfl
    | exact ref_tt _ _ rfl
    | exact ref_in_after
    | exact ref_notIn_after
    | exact ref_before_notIn
    | exact ref_before_after
    | exact ref_only_after _ rfl
    | exact ref_onlyAfter_onlyIn
    | exact ref_onlyAfter_onlyBefore
    | exact ref_ev_in
    | exact ref_ev_notIn
    | exact ref_ev_before
    | exact ref_trig_in (by first | assumption | decide)
    | exact ref_trig_notIn (by first | assumption | decide)
    | exact ref_trig_after (by first | assumption | decide)

/-! ### Two modes -/

theorem refD_after : NotOrderD ct K .afterMode .afterMode := by
  obtain ⟨s, hb, -⟩ := wBot K
  exact ⟨s, ff, upto 0, tt, ff, (lower_ff .afterMode).mpr trivial, after_bot hb⟩

theorem refD_onlyAfter : NotOrderD ct K .onlyAfter .onlyAfter := by
  obtain ⟨s, r, -, hn⟩ := wTop 2 K
  refine ⟨s, upto 0, ff, pt 2, r, onlyAfter_vac, fun h => ?_⟩
  have hx := lower_onlyAfter.mp h
  rw [eom_ff] at hx
  exact hn (inner_pt_at (inner_some_ff.mp hx) (by omega))

/-- A scoped requirement implies neither itself nor `before` at another mode. -/
theorem diff_refute {k k' : ScopeKind} (hk : k ≠ .global)
    (h : k = k' ∨ (k = .notInMode ∧ k' = .beforeMode)) : NotOrderD ct K k k' := by
  rcases h with rfl | ⟨rfl, rfl⟩
  · cases k
    · exact absurd rfl hk
    · exact refD_ff_tt _ _ rfl
    · exact refD_tt_ff _ _ rfl
    · exact refD_tt_ff _ _ rfl
    · exact refD_after
    · exact refD_tt_ff _ _ rfl
    · exact refD_ff_tt _ _ rfl
    · exact refD_onlyAfter
  · exact refD_tt_ff _ _ rfl

/-- The only edge `scope_edges` lists from a scope other than global. -/
theorem edge_nonglobal {k k' : ScopeKind} (he : (scopeEdges K ct).any (· == (k, k')) = true) :
    k = .global ∨ (k = .notInMode ∧ k' = .beforeMode) := by
  rcases K.cls_cases with ⟨rfl, hev, hal⟩ | ⟨rfl, hev, hal⟩ | ⟨_, _, hev, hal⟩ <;>
  cases ct <;> cases k <;> cases k' <;>
    simp [scopeEdges, Kind.isEv, Kind.isAlw] at he ⊢

/-! ## The theorems -/

/-- The scope order at a condition other than the literal `true`. `same` says
whether the two scopes share a mode. -/
def ScopeOrder (ct : CondType) (K : Kind) (k k' : ScopeKind) (same : Bool) : Prop :=
  ∀ s m m' c r, (same = true → m' = m) →
    Entails (lower k ct false (K.at s) m c r) (lower k' ct false (K.at s) m' c r)

/-- Every pair `scope_implies` accepts holds, at any condition other than the
literal `true` and at the literal `true` itself. -/
theorem scope_implies_sound {k k' : ScopeKind} {same : Bool} (hc : ctrue = false ∨ c = tt)
    (hm : same = true → m' = m) (h : scopeImplies k k' same K ct = true) :
    Entails (lower k ct ctrue (K.at s) m c r) (lower k' ct ctrue (K.at s) m' c r) := by
  simp only [scopeImplies] at h
  split at h
  · rename_i h1
    simp only [Bool.and_eq_true, beq_iff_eq] at h1
    obtain ⟨rfl, hs⟩ := h1
    rw [hm hs]
    exact fun _ => id
  · split at h
    · cases h
    · rename_i h2
      have : k = .global ∨ same = true := by
        cases same <;> simp_all
      rcases this with rfl | hs
      · exact edge_sound (m := m') hc h
      · rw [hm hs]
        exact edge_sound hc h

/-- `scope_implies` is exact: a pair holds at every stop, mode, condition and
response exactly when it accepts the pair. Two global scopes share their mode,
which is empty. -/
theorem scope_implies_exact (ct : CondType) (K : Kind) (k k' : ScopeKind) (same : Bool)
    (hg : k = .global → k' = .global → same = true) :
    ScopeOrder ct K k k' same ↔ scopeImplies k k' same K ct = true := by
  constructor
  · intro h
    have toNot : NotOrder ct K k k' → False := fun ⟨s, m, c, r, h1, h2⟩ =>
      h2 (h s m m c r (fun _ => rfl) 0 h1)
    cases hs : scopeImplies k k' same K ct
    · exfalso
      simp only [scopeImplies] at hs
      split at hs
      · cases hs
      · rename_i h1
        split at hs
        · rename_i h2
          simp only [Bool.and_eq_true, bne_iff_ne, ne_eq, Bool.not_eq_true'] at h2
          obtain ⟨hk, rfl⟩ := h2
          by_cases hd : k = k' ∨ (k = .notInMode ∧ k' = .beforeMode)
          · obtain ⟨s, m, m', c, r, h1, h2⟩ := diff_refute (ct := ct) (K := K) hk hd
            exact h2 (h s m m' c r (fun h => by cases h) 0 h1)
          · refine toNot (not_edge (fun he => hd (.inl he)) ?_)
            cases he : (scopeEdges K ct).any (· == (k, k'))
            · rfl
            · rcases edge_nonglobal he with hg' | hnb
              · exact absurd hg' hk
              · exact absurd (.inr hnb) hd
        · rename_i h2
          refine toNot (not_edge (fun he => ?_) hs)
          subst he
          cases same
          · simp only [Bool.and_eq_true, bne_iff_ne, ne_eq, Bool.not_eq_true',
              and_true, Decidable.not_not] at h2
            exact absurd (hg h2 h2) (by decide)
          · simp at h1
    · rfl
  · intro h s m m' c r hm
    exact scope_implies_sound (Or.inl rfl) hm h

end Fretish
