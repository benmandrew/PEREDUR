import Fretish.ScopeOrder

/-!
# The timing order on whole requirements

`Fretish.Exact` and `Fretish.ExactOnly` refute each non-edge of the timing
orders with a trace on the timing *body*, under an arbitrary boundary. This
file lifts those refutations to whole requirements, at a condition other than
the literal `true`.

The refutations use only two boundaries: none (`ff`), and one at the point of
evaluation (`pt 0`). `body_sh` moves a refutation found at any position to
position 0. With the condition at position 0 alone (`pt 0`), and a mode that
puts position 0 inside the scope's interval, every scope but after reduces to
the body at position 0 (`req_*`). The mode also sets the boundary: a constant
mode gives none, and a mode that rises or falls just after position 0 gives one
there. In, only after: `upto 0`, which ends at 0. Not in, before, only in:
`from1`, which starts at 1. After enters its scope one point late, so its
refutation is moved to position 1 (`w1_of`) and its condition placed there.
-/

namespace Fretish

/-! ## Moving a trace -/

/-- `p` read from position `t`. -/
abbrev shd (t : Nat) (p : Pred) : Pred := fun x => p (t + x)

/-- `p` read one position late. -/
abbrev shu (p : Pred) : Pred := fun x => p (x - 1)

/-- The timing with its stop read from position `t`. -/
def Timing.sh (t : Nat) : Timing → Timing
  | .immediately => .immediately
  | .nextTick => .nextTick
  | .within n => .within n
  | .forTicks n => .forTicks n
  | .afterTicks n => .afterTicks n
  | .eventually => .eventually
  | .always => .always
  | .untilStop s => .untilStop (shd t s)
  | .beforeStop s => .beforeStop (shd t s)

theorem Kind.at_sh (K : Kind) (s : Pred) (t : Nat) : (K.at s).sh t = K.at (shd t s) := by
  rcases K with ⟨A⟩ | _ | _ <;> (try cases A) <;> rfl

theorem body_sh (T : Timing) (b p : Pred) (t u : Nat) :
    body (T.sh t) (shd t b) (shd t p) u ↔ body T b p (t + u) := by
  cases T <;>
    simp only [Timing.sh, body, withinB, forB, X, Fle, Flt, Gle, G, U, R, W, neg, shd,
      Nat.add_assoc]

theorem onlyBody_sh (T : Timing) (b p : Pred) (t u : Nat) :
    onlyBody (T.sh t) (shd t b) (shd t p) u ↔ onlyBody T b p (t + u) := by
  cases T <;>
    simp only [Timing.sh, onlyBody, dual, body, withinB, forB, X, Fle, Flt, Gle, G, U, R, W,
      neg, shd, Nat.add_assoc]

theorem shd_shu (p : Pred) : shd 1 (shu p) = p := funext fun x => by
  show p (1 + x - 1) = p x
  rw [Nat.add_sub_cancel_left]

/-! ## Refutations at position 0 -/

variable {K K' : Kind}

/-- A plain pair that fails somewhere without a boundary fails at position 0. -/
theorem w0_of (h : ¬ implies0K K K') :
    ∃ s p, body (K.at s) ff p 0 ∧ ¬ body (K'.at s) ff p 0 := by
  apply Classical.byContradiction
  intro hn
  apply h
  intro s p t hK
  apply Classical.byContradiction
  intro hK'
  apply hn
  refine ⟨shd t s, shd t p, ?_, ?_⟩
  · rw [← Kind.at_sh]
    exact (body_sh _ ff p t 0).mpr hK
  · rw [← Kind.at_sh]
    exact fun h' => hK' ((body_sh _ ff p t 0).mp h')

/-- An only pair that fails somewhere without a boundary fails at position 0. -/
theorem wO0_of (h : ¬ onlyImplies0K K K') :
    ∃ s p, onlyBody (K.at s) ff p 0 ∧ ¬ onlyBody (K'.at s) ff p 0 := by
  apply Classical.byContradiction
  intro hn
  apply h
  intro s p t hK
  apply Classical.byContradiction
  intro hK'
  apply hn
  refine ⟨shd t s, shd t p, ?_, ?_⟩
  · rw [← Kind.at_sh]
    exact (onlyBody_sh _ ff p t 0).mpr hK
  · rw [← Kind.at_sh]
    exact fun h' => hK' ((onlyBody_sh _ ff p t 0).mp h')

/-- A plain refutation at position 0 moves to position 1. -/
theorem w1_of (h : ∃ s p, body (K.at s) ff p 0 ∧ ¬ body (K'.at s) ff p 0) :
    ∃ s p, body (K.at s) ff p 1 ∧ ¬ body (K'.at s) ff p 1 := by
  obtain ⟨s, p, h1, h2⟩ := h
  have e : ∀ K : Kind, body (K.at s) ff p 0 ↔ body (K.at (shu s)) ff (shu p) 1 := fun K => by
    have := body_sh (K.at (shu s)) ff (shu p) 1 0
    rw [Kind.at_sh, shd_shu, shd_shu] at this
    exact this
  exact ⟨shu s, shu p, (e K).mp h1, fun h => h2 ((e K').mpr h)⟩

/-! ## The pairs a boundary removes

A boundary at the point of evaluation discharges `next`, `within n` for
`n ≥ 1` and `after n`, but not `eventually`. Under an only scope the duals
are `next` and `for n` for `n ≥ 1` against `always`. -/

theorem wit_next_ev : body .nextTick (pt 0) ff 0 ∧ ¬ body .eventually (pt 0) ff 0 :=
  ⟨Or.inl rfl, fun ⟨_, h, _⟩ => h⟩

theorem wit_within_ev {n : Nat} (hn : 1 ≤ n) :
    body (.within n) (pt 0) ff 0 ∧ ¬ body .eventually (pt 0) ff 0 :=
  ⟨Or.inr ⟨0, hn, rfl⟩, fun ⟨_, h, _⟩ => h⟩

theorem wit_after_ev {n : Nat} :
    body (.afterTicks n) (pt 0) ff 0 ∧ ¬ body .eventually (pt 0) ff 0 :=
  ⟨⟨Or.inl (fun _ _ h => h), Or.inr ⟨0, by omega, rfl⟩⟩, fun ⟨_, h, _⟩ => h⟩

theorem wit_next_alw : onlyBody .nextTick (pt 0) tt 0 ∧ ¬ onlyBody .always (pt 0) tt 0 :=
  ⟨Or.inl rfl, fun ⟨_, h, _⟩ => h trivial⟩

theorem wit_for_alw {n : Nat} (hn : 1 ≤ n) :
    onlyBody (.forTicks n) (pt 0) tt 0 ∧ ¬ onlyBody .always (pt 0) tt 0 :=
  ⟨Or.inr ⟨0, hn, rfl⟩, fun ⟨_, h, _⟩ => h trivial⟩

/-- A pair the bounded order rejects is rejected by the unbounded order, or is
one a boundary at position 0 refutes. -/
theorem splitB (h : ¬ leBK K K') :
    ¬ le0K K K' ∨ ∃ s p, body (K.at s) (pt 0) p 0 ∧ ¬ body (K'.at s) (pt 0) p 0 := by
  by_cases h0 : le0K K K'
  · refine .inr ?_
    rcases K with ⟨A⟩ | _ | _ <;> rcases K' with ⟨B⟩ | _ | _ <;>
      (try cases A) <;> (try cases B) <;>
      simp_all [leBK, leB, le0K, le0] <;>
      first
      | (exfalso; omega)
      | exact ⟨ff, ff, wit_next_ev⟩
      | exact ⟨ff, ff, wit_within_ev (by omega)⟩
      | exact ⟨ff, ff, wit_after_ev⟩
  · exact .inl h0

/-- The same under an only scope. -/
theorem splitO (h : ¬ leO leBK K K') :
    ¬ leO le0K K K' ∨ ∃ s p, onlyBody (K.at s) (pt 0) p 0 ∧ ¬ onlyBody (K'.at s) (pt 0) p 0 := by
  by_cases h0 : leO le0K K K'
  · refine .inr ?_
    rcases K with ⟨A⟩ | _ | _ <;> rcases K' with ⟨B⟩ | _ | _ <;>
      (try cases A) <;> (try cases B) <;>
      simp_all [leO, Kind.notAfter, Kind.dual, Kind.ticks, intoAfter, leBK, leB, le0K, le0] <;>
      first
      | (exfalso; omega)
      | exact ⟨ff, tt, wit_next_alw⟩
      | exact ⟨ff, tt, wit_for_alw (by omega)⟩
  · exact .inl h0

/-! ## Each scope at position 0

With the condition at position 0 alone, each requirement asks for the body
there. -/

variable {ct : CondType} {T : Timing} {r : Pred}

theorem eom_upto0_eq : eom (upto 0) = pt 0 :=
  funext fun u => propext ⟨fun ⟨(h1 : u ≤ 0), _⟩ => by omega,
    fun (h : u = 0) => show u ≤ 0 ∧ ¬ u + 1 ≤ 0 by omega⟩

theorem som_from1_eq : som from1 = pt 0 :=
  funext fun u => propext ⟨fun ⟨(h1 : ¬ 1 ≤ u), _⟩ => by omega,
    fun (h : u = 0) => show ¬ 1 ≤ u ∧ 1 ≤ u + 1 by omega⟩

theorem req_P_ff {k : ScopeKind} (hk : vff k = .P) :
    lower k ct false T ff (pt 0) r 0 ↔ body T ff r 0 := by
  rw [lower_ff, hk]
  exact inner_pt0 none

theorem req_P_tt {k : ScopeKind} (hk : vtt k = .P) :
    lower k ct false T tt (pt 0) r 0 ↔ body T ff r 0 := by
  rw [lower_tt, hk]
  exact inner_pt0 none

theorem req_N_ff {k : ScopeKind} (hk : vff k = .N) :
    lower k ct false T ff (pt 0) r 0 ↔ onlyBody T ff r 0 := by
  rw [lower_ff, hk]
  exact inner_pt0 none

theorem req_N_tt {k : ScopeKind} (hk : vtt k = .N) :
    lower k ct false T tt (pt 0) r 0 ↔ onlyBody T ff r 0 := by
  rw [lower_tt, hk]
  exact inner_pt0 none

theorem req_in_pt : lower .inMode ct false T (upto 0) (pt 0) r 0 ↔ body T (pt 0) r 0 := by
  rw [lower_in, som_upto0, eom_upto0_eq, reentry_ff_iff, true_and]
  exact ⟨fun h => (inner_pt0 _).mp (h (Nat.le_refl 0)), fun h _ => (inner_pt0 _).mpr h⟩

theorem req_notIn_pt : lower .notInMode ct false T from1 (pt 0) r 0 ↔ body T (pt 0) r 0 := by
  rw [lower_notIn, eom_from1, som_from1_eq, reentry_ff_iff, true_and]
  exact ⟨fun h => (inner_pt0 _).mp (h (by omega)), fun h _ => (inner_pt0 _).mpr h⟩

theorem req_before_pt : lower .beforeMode ct false T from1 (pt 0) r 0 ↔ body T (pt 0) r 0 := by
  rw [lower_before, som_from1_eq]
  exact ⟨fun h => (inner_pt0 _).mp (h.resolve_right (by omega)),
    fun h => Or.inl ((inner_pt0 _).mpr h)⟩

theorem req_onlyIn_pt : lower .onlyIn ct false T from1 (pt 0) r 0 ↔ onlyBody T (pt 0) r 0 := by
  rw [lower_onlyIn, eom_from1, som_from1_eq, reentry_ff_iff, true_and]
  exact ⟨fun h => (inner_pt0 _).mp (h (by omega)), fun h _ => (inner_pt0 _).mpr h⟩

theorem req_onlyAfter_pt :
    lower .onlyAfter ct false T (upto 0) (pt 0) r 0 ↔ onlyBody T (pt 0) r 0 := by
  rw [lower_onlyAfter, eom_upto0_eq]
  exact inner_pt0 _

/-- After enters its scope at position 1, after the mode ends at 0. -/
theorem req_after : lower .afterMode ct false T (upto 0) (pt 1) r 0 ↔ body T ff r 1 := by
  have hc : ∀ x : Pred, inner none ct false (pt 1) x 1 ↔ x 1 := fun x => by
    cases ct
    · exact ⟨fun h => h 0 rfl, fun hx j (hj : 1 + j = 1) => by rw [hj]; exact hx⟩
    · exact ⟨fun h => h.2 rfl, fun hx => ⟨fun j ⟨_, (h : 1 + j + 1 = 1)⟩ => by omega,
        fun _ => hx⟩⟩
  rw [lower_after]
  refine ⟨fun h => (hc _).mp (firstEntry_now eom_upto0 h), fun h => ?_⟩
  exact Or.inl ⟨0, ⟨eom_upto0, (hc _).mpr h⟩, fun _ hi => absurd hi (Nat.not_lt_zero _)⟩

/-! ## Refutations of whole requirements -/

/-- A trace on which the requirement with the first timing holds at 0 and the
one with the second fails, the other fields equal. -/
def TimingNotReq (k : ScopeKind) (ct : CondType) (K K' : Kind) : Prop :=
  ∃ s m c r, lower k ct false (K.at s) m c r 0 ∧ ¬ lower k ct false (K'.at s) m c r 0

theorem lift {k : ScopeKind} {m c : Pred} (f : Timing → Pred → Prop)
    (hf : ∀ T r, lower k ct false T m c r 0 ↔ f T r)
    (h : ∃ s r, f (K.at s) r ∧ ¬ f (K'.at s) r) : TimingNotReq k ct K K' := by
  obtain ⟨s, r, h1, h2⟩ := h
  exact ⟨s, m, c, r, (hf _ _).mpr h1, fun h => h2 ((hf _ _).mp h)⟩

/-- Global and after: a pair outside the unbounded order. -/
theorem notReq_unbounded {k : ScopeKind} (hk : k = .global ∨ k = .afterMode)
    (h : ¬ implies0K K K') : TimingNotReq k ct K K' := by
  rcases hk with rfl | rfl
  · exact lift (fun T r => body T ff r 0) (fun _ _ => req_P_ff rfl) (w0_of h)
  · exact lift (fun T r => body T ff r 1) (fun _ _ => req_after) (w1_of (w0_of h))

/-- In, not in and before: a pair outside the bounded order. -/
theorem notReq_bounded {k : ScopeKind} (hk : k = .inMode ∨ k = .notInMode ∨ k = .beforeMode)
    (h : ¬ impliesBK K K') : TimingNotReq k ct K K' := by
  rcases splitB (fun h' => h ((exactBK K K').mpr h')) with h0 | hb
  · have w := w0_of (fun h' => h0 ((exact0K K K').mp h'))
    rcases hk with rfl | rfl | rfl
    · exact lift (fun T r => body T ff r 0) (fun _ _ => req_P_tt rfl) w
    · exact lift (fun T r => body T ff r 0) (fun _ _ => req_P_ff rfl) w
    · exact lift (fun T r => body T ff r 0) (fun _ _ => req_P_ff rfl) w
  · rcases hk with rfl | rfl | rfl
    · exact lift (fun T r => body T (pt 0) r 0) (fun _ _ => req_in_pt) hb
    · exact lift (fun T r => body T (pt 0) r 0) (fun _ _ => req_notIn_pt) hb
    · exact lift (fun T r => body T (pt 0) r 0) (fun _ _ => req_before_pt) hb

/-- Only in and only after: a pair outside the bounded only order. -/
theorem notReq_only_bounded {k : ScopeKind} (hk : k = .onlyIn ∨ k = .onlyAfter)
    (h : ¬ onlyImpliesBK K K') : TimingNotReq k ct K K' := by
  rcases splitO (fun h' => h ((exactOB K K').mpr h')) with h0 | hb
  · have w := wO0_of (fun h' => h0 ((exactO0 K K').mp h'))
    rcases hk with rfl | rfl
    · exact lift (fun T r => onlyBody T ff r 0) (fun _ _ => req_N_ff rfl) w
    · exact lift (fun T r => onlyBody T ff r 0) (fun _ _ => req_N_ff rfl) w
  · rcases hk with rfl | rfl
    · exact lift (fun T r => onlyBody T (pt 0) r 0) (fun _ _ => req_onlyIn_pt) hb
    · exact lift (fun T r => onlyBody T (pt 0) r 0) (fun _ _ => req_onlyAfter_pt) hb

/-- Only before: a pair outside the unbounded only order. -/
theorem notReq_only_unbounded (h : ¬ onlyImplies0K K K') : TimingNotReq .onlyBefore ct K K' :=
  lift (fun T r => onlyBody T ff r 0) (fun _ _ => req_N_tt rfl) (wO0_of h)

/-! ## The theorems -/

/-- With the other fields equal, the requirement with timing `K` implies the
one with `K'` at every stop, mode, condition and response. -/
def TimingReq (k : ScopeKind) (ct : CondType) (ctrue : Bool) (K K' : Kind) : Prop :=
  ∀ s m c r, Entails (lower k ct ctrue (K.at s) m c r) (lower k ct ctrue (K'.at s) m c r)

/-- Every edge of the body order lifts to whole requirements, at any condition. -/
theorem timingOrder_req (k : ScopeKind) (ct : CondType) (ctrue : Bool)
    (h : timingOrder k K K') : TimingReq k ct ctrue K K' := by
  intro s m c r
  cases k <;> simp only [timingOrder] at h
  · exact lower_unbounded .global (.inl rfl) (h s)
  · exact lower_bounded .inMode (.inl rfl) (h s)
  · exact lower_bounded .notInMode (.inr (.inl rfl)) (h s)
  · exact lower_bounded .beforeMode (.inr (.inr rfl)) (h s)
  · exact lower_unbounded .afterMode (.inr rfl) (h s)
  · exact lower_only_bounded .onlyIn (.inl rfl) (h s)
  · exact lower_only_unbounded (h s)
  · exact lower_only_bounded .onlyAfter (.inr rfl) (h s)

/-- Every pair outside the body order fails between whole requirements. -/
theorem notReq_of (k : ScopeKind) (ct : CondType) (h : ¬ timingOrder k K K') :
    TimingNotReq k ct K K' := by
  cases k <;> simp only [timingOrder] at h
  · exact notReq_unbounded (.inl rfl) h
  · exact notReq_bounded (.inl rfl) h
  · exact notReq_bounded (.inr (.inl rfl)) h
  · exact notReq_bounded (.inr (.inr rfl)) h
  · exact notReq_unbounded (.inr rfl) h
  · exact notReq_only_bounded (.inl rfl) h
  · exact notReq_only_unbounded h
  · exact notReq_only_bounded (.inr rfl) h

/-- At a condition other than the literal `true`, the body order is the order
between whole requirements, under every scope and condition type. -/
theorem timingOrder_exact_req (k : ScopeKind) (ct : CondType) (K K' : Kind) :
    TimingReq k ct false K K' ↔ timingOrder k K K' := by
  refine ⟨fun h => Classical.byContradiction fun hn => ?_, timingOrder_req k ct false⟩
  obtain ⟨s, m, c, r, h1, h2⟩ := notReq_of k ct hn
  exact h2 (h s m c r 0 h1)

/-- `timing_implies` is exact between whole requirements: for timings it
accepts and a condition other than the literal `true`, with the other fields
equal, the requirement with `K` implies the one with `K'` at every stop, mode,
condition and response exactly when `timing_implies` says so. -/
theorem timing_implies_exact_req (k : ScopeKind) (ct : CondType) (hK : K.cppValid)
    (hK' : K'.cppValid) : TimingReq k ct false K K' ↔ timingImplies K K' k = true :=
  (timingOrder_exact_req k ct K K').trans (timing_implies_exact k hK hK')

/-- Every edge `timing_implies` claims holds between whole requirements, at the
literal `true` too. There it is not exact: under a continual condition both
`for 1` and `always` lower to `G r` under the global scope. -/
theorem timing_implies_sound_req (k : ScopeKind) (ct : CondType) (ctrue : Bool)
    (hK : K.cppValid) (hK' : K'.cppValid) (h : timingImplies K K' k = true) :
    TimingReq k ct ctrue K K' :=
  timingOrder_req k ct ctrue (timing_implies_sound k hK hK' h)

end Fretish
