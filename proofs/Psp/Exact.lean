import Psp.Witness

/-!
# The pattern order is exact

`pattern_exact`: under every scope, for patterns with counts of at least 1,
one pattern implies another over every P, S, Q and R exactly when `patLe`
of the scope's class relates them. This holds for every count, not only for
the counts 1 to 4 that `scripts/psp_field_orders.py` measures.

For each target pattern there is one trace (two for bounded response under a
boundary) on which the target fails and every pattern the order does not put
above it holds. The traces are bodies at position 0: with no boundary for the
unbounded class (`SepU`), and with a boundary that does not hold at 0 and
holds later for the others (`SepB`). The sources are shown to hold through
one or two strongest patterns and the edges of `Psp.PatternOrder`.
`not_implies_U` and `not_implies_B` turn a body trace into a refutation of
whole requirements, with Q true at position 0 alone.
-/

namespace Psp

open Fretish

set_option linter.unusedSimpArgs false

/-- Two bodies told apart with no boundary. -/
def SepU (a b : Pat) : Prop := ∃ P S : Pred, body a ff P S 0 ∧ ¬ body b ff P S 0

/-- Two bodies told apart under a boundary that closes the scope after 0. -/
def SepB (a b : Pat) : Prop :=
  ∃ B P S : Pred, ¬ B 0 ∧ F B 0 ∧ body a B P S 0 ∧ ¬ body b B P S 0

/-- Two bodies told apart under a boundary that holds somewhere. -/
def SepF (a b : Pat) : Prop := ∃ B P S : Pred, F B 0 ∧ body a B P S 0 ∧ ¬ body b B P S 0

/-- An edge read from a body that holds on the trace. -/
macro "srcU " h:term : tactic => `(tactic| (refine edge_unbounded ?_ $h; simp [patLe] <;> omega))

macro "srcB " hb:term:max h:term : tactic =>
  `(tactic| (refine edge_bounded ?_ $hb $h; simp [patLe] <;> omega))


variable {a b : Pat}

/-! ## No boundary -/

theorem sepU_absence (ha : a.Valid) (hna : ¬ patLe .unbounded a .absence) : SepU a .absence := by
  have hU : body .universality ff tt tt 0 := W_all (fun _ => trivial)
  have hE : body (.bexist 1) ff tt tt 0 := W_of_now (Or.inr (W_all (fun _ => trivial)))
  refine ⟨tt, tt, ?_, not_W 0 (fun h => h trivial) (fun _ _ h => h)⟩
  cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;>
    first | srcU hU | srcU hE

theorem sepU_universality (ha : a.Valid) (hna : ¬ patLe .unbounded a .universality) :
    SepU a .universality := by
  have hA : body .absence ff ff from1 0 := W_all (fun _ h => h)
  have hR : body (.brec 1) ff ff from1 0 := W_all (fun j => ⟨1, Nat.le_refl _, Or.inl (by ar)⟩)
  refine ⟨ff, from1, ?_, not_W 0 (by ar) (fun _ _ h => h)⟩
  cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;> first | srcU hA | srcU hR

theorem sepU_existence (hna : ¬ patLe .unbounded a .existence) : SepU a .existence := by
  have hA : body .absence ff ff ff 0 := W_all (fun _ h => h)
  refine ⟨ff, ff, ?_, fun ⟨_, ⟨h, _⟩, _⟩ => h⟩
  cases a <;> (try simp [patLe] at hna) <;> first | srcU hA

theorem sepU_precedence (ha : a.Valid) (hna : ¬ patLe .unbounded a .precedence) :
    SepU a .precedence := by
  have hR : body (.brec 1) ff (pt 0) from1 0 := W_all (fun j => ⟨1, Nat.le_refl _, Or.inl (by ar)⟩)
  have hE : body (.bexist 1) ff (pt 0) from1 0 := atMost_pt 0
  refine ⟨pt 0, from1, ?_, not_W 0 (by ar) (fun _ _ h => h.elim (by ar) id)⟩
  cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;> first | srcU hR | srcU hE

theorem sepU_response (ha : a.Valid) (hna : ¬ patLe .unbounded a .response) : SepU a .response := by
  have hE : body .existence ff (pt 1) (pt 0) 0 := ⟨0, ⟨rfl, id⟩, fun _ _ h => h⟩
  have hP : body .precedence ff (pt 1) (pt 0) 0 := W_of_now (Or.inl rfl)
  have hB : body (.bexist 1) ff (pt 1) (pt 0) 0 := atMost_pt 1
  refine ⟨pt 1, pt 0, ?_, not_W 1 (fun h => ?_) (fun _ _ h => h)⟩
  · cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;>
      first | srcU hE | srcU hP | srcU hB
  · obtain ⟨j, ⟨hs, _⟩, _⟩ := h rfl
    ar

theorem sepU_bresp {m : Nat} (ha : a.Valid) (hna : ¬ patLe .unbounded a (.bresp m)) :
    SepU a (.bresp m) := by
  have hR : body (.brec (m + 1)) ff (pt 1) (gap m) 0 := W_all (fun j => by
    by_cases hj : j = 0 ∨ m + 2 ≤ j
    · exact ⟨0, Nat.zero_le _, Or.inl (by ar)⟩
    · exact ⟨m + 2 - j, by omega, Or.inl (by ar)⟩)
  have hP : body .precedence ff (pt 1) (gap m) 0 := W_of_now (Or.inl (Or.inl rfl))
  have hB : body (.bexist 1) ff (pt 1) (gap m) 0 := atMost_pt 1
  refine ⟨pt 1, (gap m), ?_, not_W 1 (fun h => ?_) (fun _ _ h => h)⟩
  · cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;> first | srcU hR | srcU hP | srcU hB
  · obtain ⟨k, hk, ⟨hs, _⟩, _⟩ := h rfl
    ar

theorem sepU_binv {m : Nat} (hm : 0 < m) (ha : a.Valid) (hna : ¬ patLe .unbounded a (.binv m)) :
    SepU a (.binv m) := by
  have hI : ∀ n, n < m → body (.binv n) ff (pt 0) (skip m) 0 :=
    fun n hn => W_all (fun u hP j hj => Or.inl (by ar))
  have hR : body (.brec 1) ff (pt 0) (skip m) 0 := W_all (fun j => by
    by_cases hj : j = m
    · exact ⟨1, Nat.le_refl _, Or.inl (by ar)⟩
    · exact ⟨0, Nat.zero_le _, Or.inl (by ar)⟩)
  have hP : body .precedence ff (pt 0) (skip m) 0 := W_of_now (Or.inl (by ar))
  have hB : body (.bexist 1) ff (pt 0) (skip m) 0 := atMost_pt 0
  refine ⟨pt 0, (skip m), ?_, not_W 0 (fun h => ?_) (fun _ _ h => h)⟩
  · cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;> first | srcU hR | srcU hP | srcU hB | (refine hI _ ?_; omega)
  · rcases h rfl m (Nat.le_refl _) with h | ⟨_, _, h⟩
    · ar
    · exact h

theorem sepU_bexist {m : Nat} (ha : a.Valid) (hna : ¬ patLe .unbounded a (.bexist m)) :
    SepU a (.bexist m) := by
  have hU : body .universality ff (alt 0 (m + 1)) tt 0 := W_all (fun _ => trivial)
  have hB : body (.bexist (m + 1)) ff (alt 0 (m + 1)) tt 0 := by
    have := atMost_alt (B := ff) (o := 0) (N := m + 1) (m + 1) 0 (by omega) (by omega)
    rwa [Nat.mul_zero, Nat.add_zero] at this
  refine ⟨alt 0 (m + 1), tt, ?_, ?_⟩
  · cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;> first | srcU hU | srcU hB
  · have := not_atMost_alt (B := ff) (o := 0) (N := m + 1) (fun _ _ _ h => h) m 0 (by omega)
    rwa [Nat.mul_zero, Nat.add_zero] at this

theorem sepU_brec {m : Nat} (ha : a.Valid) (hna : ¬ patLe .unbounded a (.brec m)) :
    SepU a (.brec m) := by
  have hA : body .absence ff ff (gap m) 0 := W_all (fun _ h => h)
  have hE : body .existence ff ff (gap m) 0 := ⟨0, ⟨Or.inl rfl, id⟩, fun _ _ h => h⟩
  have hR : body (.brec (m + 1)) ff ff (gap m) 0 := W_all (fun j => by
    by_cases hj : j = 0 ∨ m + 2 ≤ j
    · exact ⟨0, Nat.zero_le _, Or.inl (by ar)⟩
    · exact ⟨m + 2 - j, by omega, Or.inl (by ar)⟩)
  refine ⟨ff, (gap m), ?_, not_W 1 (fun ⟨k, hk, h⟩ => h.elim (by ar) id) (fun _ _ h => h)⟩
  cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;> first | srcU hA | srcU hE | srcU hR

theorem sepU (ha : a.Valid) (hb : b.Valid) (hna : ¬ patLe .unbounded a b) : SepU a b := by
  cases b with
  | absence => exact sepU_absence ha hna
  | universality => exact sepU_universality ha hna
  | existence => exact sepU_existence hna
  | precedence => exact sepU_precedence ha hna
  | response => exact sepU_response ha hna
  | bresp m => exact sepU_bresp ha hna
  | binv m => exact sepU_binv hb ha hna
  | bexist m => exact sepU_bexist ha hna
  | brec m => exact sepU_brec ha hna

/-! ## Under a boundary -/

theorem sepB_absence (ha : a.Valid) (hna : ¬ patLe .bounded a .absence) : SepB a .absence := by
  have hB0 : ¬ pt 1 0 := by ar
  have hU : body .universality (pt 1) tt tt 0 := W_all (fun _ => trivial)
  have hE : body (.bexist 1) (pt 1) tt tt 0 := W_of_now (Or.inr (W_all (fun _ => trivial)))
  refine ⟨pt 1, tt, tt, hB0, ⟨1, rfl⟩, ?_, not_W 0 (fun h => h trivial) (fun _ _ h => by ar)⟩
  cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;>
    first | srcB hB0 hU | srcB hB0 hE

theorem sepB_universality (ha : a.Valid) (hna : ¬ patLe .bounded a .universality) :
    SepB a .universality := by
  have hB0 : ¬ pt 2 0 := by ar
  have hA : body .absence (pt 2) ff from1 0 := W_all (fun _ h => h)
  have hE : body .existence (pt 2) ff from1 0 :=
    ⟨1, ⟨by ar, by ar⟩, fun _ _ h => by ar⟩
  have hR : body (.brec 1) (pt 2) ff from1 0 :=
    W_all (fun j => ⟨1, Nat.le_refl _, Or.inl (by ar)⟩)
  refine ⟨pt 2, ff, from1, hB0, ⟨2, rfl⟩, ?_, not_W 0 (by ar) (fun _ _ h => by ar)⟩
  cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;>
    first | exact hE | srcB hB0 hA | srcB hB0 hR

theorem sepB_existence (ha : a.Valid) (hna : ¬ patLe .bounded a .existence) :
    SepB a .existence := by
  have hB0 : ¬ pt 1 0 := by ar
  have hA : body .absence (pt 1) ff ff 0 := W_all (fun _ h => h)
  have hR : body (.brec 1) (pt 1) ff ff 0 :=
    W_at 1 rfl (fun i hi => ⟨1, Nat.le_refl _, Or.inr (by ar)⟩)
  refine ⟨pt 1, ff, ff, hB0, ⟨1, rfl⟩, ?_, fun ⟨_, ⟨h, _⟩, _⟩ => h⟩
  cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;>
    first | srcB hB0 hA | srcB hB0 hR

theorem sepB_precedence (ha : a.Valid) (hna : ¬ patLe .bounded a .precedence) :
    SepB a .precedence := by
  have hB0 : ¬ pt 2 0 := by ar
  have hR : body (.brec 1) (pt 2) (pt 0) from1 0 :=
    W_all (fun j => ⟨1, Nat.le_refl _, Or.inl (by ar)⟩)
  have hB : body (.bexist 1) (pt 2) (pt 0) from1 0 := atMost_pt 0
  have hS : body (.bresp 1) (pt 2) (pt 0) from1 0 :=
    W_all (fun u hP => ⟨1, Nat.le_refl _, ⟨by ar, by ar⟩, fun _ _ => by ar⟩)
  have hE : body .existence (pt 2) (pt 0) from1 0 :=
    ⟨1, ⟨by ar, by ar⟩, fun _ _ h => by ar⟩
  refine ⟨pt 2, pt 0, from1, hB0, ⟨2, rfl⟩, ?_, not_W 0 (by ar) (fun _ _ h => by ar)⟩
  cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;>
    first | exact hE | srcB hB0 hR | srcB hB0 hB | srcB hB0 hS

theorem sepB_response (ha : a.Valid) (hna : ¬ patLe .bounded a .response) :
    SepB a .response := by
  have hB0 : ¬ pt 2 0 := by ar
  have hE : body .existence (pt 2) (pt 1) (pt 0) 0 := ⟨0, ⟨rfl, by ar⟩, fun _ _ h => by ar⟩
  have hP : body .precedence (pt 2) (pt 1) (pt 0) 0 := W_of_now (Or.inl rfl)
  have hB : body (.bexist 1) (pt 2) (pt 1) (pt 0) 0 := atMost_pt 1
  have hR : body (.brec 1) (pt 2) (pt 1) (pt 0) 0 := W_at 2 rfl (fun i hi => by
    by_cases h : i = 0
    · exact ⟨0, Nat.zero_le _, Or.inl (by ar)⟩
    · exact ⟨1, Nat.le_refl _, Or.inr (by ar)⟩)
  refine ⟨pt 2, pt 1, pt 0, hB0, ⟨2, rfl⟩, ?_, not_W 1 (fun h => ?_) (fun _ _ h => by ar)⟩
  · cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;>
      first | exact hE | srcB hB0 hP | srcB hB0 hB | srcB hB0 hR
  · obtain ⟨j, ⟨hs, _⟩, _⟩ := h rfl
    ar

theorem sepB_bresp {m : Nat} (ha : a.Valid) (hna : ¬ patLe .bounded a (.bresp m)) :
    SepB a (.bresp m) := by
  by_cases hc : ∃ c, a = .brec c
  · obtain ⟨c, rfl⟩ := hc
    have hB0 : ¬ pt 1 0 := by ar
    have hR : body (.brec 1) (pt 1) (pt 0) ff 0 :=
      W_at 1 rfl (fun i hi => ⟨1, Nat.le_refl _, Or.inr (by ar)⟩)
    refine ⟨pt 1, pt 0, ff, hB0, ⟨1, rfl⟩, ?_, not_W 0 (fun h => ?_) (fun _ _ h => by ar)⟩
    · simp [Pat.Valid] at ha
      srcB hB0 hR
    · obtain ⟨_, _, ⟨hs, _⟩, _⟩ := h rfl
      exact hs
  · have hB0 : ¬ pt (m + 3) 0 := by ar
    have hE : body .existence (pt (m + 3)) (pt 1) (two m) 0 :=
      ⟨0, ⟨Or.inl rfl, by ar⟩, fun _ _ h => by ar⟩
    have hP : body .precedence (pt (m + 3)) (pt 1) (two m) 0 := W_of_now (Or.inl (Or.inl rfl))
    have hB : body (.bexist 1) (pt (m + 3)) (pt 1) (two m) 0 := atMost_pt 1
    have hS : body (.bresp (m + 1)) (pt (m + 3)) (pt 1) (two m) 0 :=
      W_all (fun u hP => ⟨m + 1, Nat.le_refl _, ⟨by ar, by ar⟩, fun _ _ => by ar⟩)
    refine ⟨pt (m + 3), pt 1, two m, hB0, ⟨m + 3, by ar⟩, ?_,
      not_W 1 (fun h => ?_) (fun _ _ h => by ar)⟩
    · cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;>
        first | exact hE | srcB hB0 hP | srcB hB0 hB | srcB hB0 hS | exact absurd ⟨_, rfl⟩ hc
    · obtain ⟨k, hk, ⟨hs, _⟩, _⟩ := h rfl
      ar

theorem sepB_binv {m : Nat} (hm : 0 < m) (ha : a.Valid) (hna : ¬ patLe .bounded a (.binv m)) :
    SepB a (.binv m) := by
  have hB0 : ¬ pt (m + 1) 0 := by ar
  have hI : ∀ n, n < m → body (.binv n) (pt (m + 1)) (pt 0) (skip m) 0 :=
    fun n hn => W_all (fun u hP j hj => Or.inl (by ar))
  have hR : body (.brec 1) (pt (m + 1)) (pt 0) (skip m) 0 := W_all (fun j => by
    by_cases hj : j = m
    · exact ⟨1, Nat.le_refl _, Or.inl (by ar)⟩
    · exact ⟨0, Nat.zero_le _, Or.inl (by ar)⟩)
  have hE : body .existence (pt (m + 1)) (pt 0) (skip m) 0 :=
    ⟨0, ⟨by ar, by ar⟩, fun _ _ h => by ar⟩
  have hP : body .precedence (pt (m + 1)) (pt 0) (skip m) 0 := W_of_now (Or.inl (by ar))
  have hS : body (.bresp 1) (pt (m + 1)) (pt 0) (skip m) 0 :=
    W_all (fun u hP => ⟨0, Nat.zero_le _, ⟨by ar, by ar⟩, fun _ _ => by ar⟩)
  have hB : body (.bexist 1) (pt (m + 1)) (pt 0) (skip m) 0 := atMost_pt 0
  refine ⟨pt (m + 1), pt 0, skip m, hB0, ⟨m + 1, by ar⟩, ?_,
    not_W 0 (fun h => ?_) (fun _ _ h => by ar)⟩
  · cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;>
      first | exact hE | srcB hB0 hR | srcB hB0 hP | srcB hB0 hB | srcB hB0 hS | (refine hI _ ?_; omega)
  · rcases h rfl m (Nat.le_refl _) with h | ⟨_, _, h⟩ <;> ar

theorem sepB_bexist {m : Nat} (ha : a.Valid) (hna : ¬ patLe .bounded a (.bexist m)) :
    SepB a (.bexist m) := by
  have hB0 : ¬ pt (2 * m + 3) 0 := by ar
  have hU : body .universality (pt (2 * m + 3)) (alt 0 (m + 1)) tt 0 := W_all (fun _ => trivial)
  have hE : body .existence (pt (2 * m + 3)) (alt 0 (m + 1)) tt 0 :=
    ⟨0, ⟨trivial, by ar⟩, fun _ _ h => by ar⟩
  have hB : body (.bexist (m + 1)) (pt (2 * m + 3)) (alt 0 (m + 1)) tt 0 := by
    have := atMost_alt (B := pt (2 * m + 3)) (o := 0) (N := m + 1) (m + 1) 0 (by omega) (by omega)
    rwa [Nat.mul_zero, Nat.add_zero] at this
  refine ⟨pt (2 * m + 3), alt 0 (m + 1), tt, hB0, ⟨2 * m + 3, by ar⟩, ?_, ?_⟩
  · cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;>
      first | exact hE | srcB hB0 hU | srcB hB0 hB
  · have := not_atMost_alt (B := pt (2 * m + 3)) (o := 0) (N := m + 1) (fun _ _ _ h => by ar) m 0
      (by omega)
    rwa [Nat.mul_zero, Nat.add_zero] at this

theorem sepB_brec {m : Nat} (ha : a.Valid) (hna : ¬ patLe .bounded a (.brec m)) :
    SepB a (.brec m) := by
  have hB0 : ¬ pt (m + 3) 0 := by ar
  have hA : body .absence (pt (m + 3)) ff (gap m) 0 := W_all (fun _ h => h)
  have hE : body .existence (pt (m + 3)) ff (gap m) 0 :=
    ⟨0, ⟨Or.inl rfl, by ar⟩, fun _ _ h => by ar⟩
  have hR : body (.brec (m + 1)) (pt (m + 3)) ff (gap m) 0 := W_all (fun j => by
    by_cases hj : j = 0 ∨ m + 2 ≤ j
    · exact ⟨0, Nat.zero_le _, Or.inl (by ar)⟩
    · exact ⟨m + 2 - j, by omega, Or.inl (by ar)⟩)
  refine ⟨pt (m + 3), ff, gap m, hB0, ⟨m + 3, by ar⟩, ?_,
    not_W 1 (fun ⟨k, hk, h⟩ => h.elim (by ar) (by ar)) (fun _ _ h => by ar)⟩
  cases a <;> (try simp [patLe] at hna) <;> (try simp [Pat.Valid] at ha) <;>
    first | exact hE | srcB hB0 hA | srcB hB0 hR

theorem sepB (ha : a.Valid) (hb : b.Valid) (hna : ¬ patLe .bounded a b) : SepB a b := by
  cases b with
  | absence => exact sepB_absence ha hna
  | universality => exact sepB_universality ha hna
  | existence => exact sepB_existence ha hna
  | precedence => exact sepB_precedence ha hna
  | response => exact sepB_response ha hna
  | bresp m => exact sepB_bresp ha hna
  | binv m => exact sepB_binv hb ha hna
  | bexist m => exact sepB_bexist ha hna
  | brec m => exact sepB_brec ha hna

/-- Under before, R at the first point empties the scope: universality holds
and existence fails. -/
theorem sepF_universality_existence : SepF .universality .existence :=
  ⟨pt 0, ff, tt, ⟨0, rfl⟩, W_all (fun _ => trivial), fun ⟨j, ⟨_, hb⟩, hi⟩ => by
    cases j with
    | zero => exact hb rfl
    | succ j => exact hi 0 (by omega) rfl⟩

/-! ## Whole requirements -/

theorem not_implies_U {k : Scope} (hk : k = .globally ∨ k = .after) (h : SepU a b) :
    ¬ Implies k a k b := by
  obtain ⟨P, S, ha, hb⟩ := h
  intro hi
  rcases hk with rfl | rfl
  · exact hb (hi P S ff ff 0 ha)
  · exact hb (lower_after_pt0.mp (hi P S (pt 0) ff 0 (lower_after_pt0.mpr ha)))

theorem not_implies_F (h : SepF a b) : ¬ Implies .before a .before b := by
  obtain ⟨B, P, S, hf, ha, hb⟩ := h
  exact fun hi => hb (hi P S ff B 0 (fun _ => ha) hf)

theorem not_implies_B {k : Scope} (hk : k = .before ∨ k = .between ∨ k = .afterUntil)
    (h : SepB a b) : ¬ Implies k a k b := by
  obtain ⟨B, P, S, h0, hf, ha, hb⟩ := h
  intro hi
  rcases hk with rfl | rfl | rfl
  · exact not_implies_F ⟨B, P, S, hf, ha, hb⟩ hi
  · exact hb (lower_between_pt0.mp (hi P S (pt 0) B 0 (lower_between_pt0.mpr (fun _ _ => ha)))
      h0 hf)
  · exact hb (lower_afterUntil_pt0.mp (hi P S (pt 0) B 0 (lower_afterUntil_pt0.mpr (fun _ => ha)))
      h0)

/-- Under every scope, for patterns the C++ accepts, one pattern implies
another over every P, S, Q and R exactly when the class's order says so. -/
theorem pattern_exact (k : Scope) {a b : Pat} (ha : a.Valid) (hb : b.Valid) :
    Implies k a k b ↔ patLe k.cls a b := by
  refine ⟨fun h => ?_, pattern_sound k⟩
  apply Classical.byContradiction
  intro hn
  cases k with
  | globally => exact not_implies_U (.inl rfl) (sepU ha hb hn) h
  | after => exact not_implies_U (.inr rfl) (sepU ha hb hn) h
  | between => exact not_implies_B (.inr (.inl rfl)) (sepB ha hb hn) h
  | afterUntil => exact not_implies_B (.inr (.inr rfl)) (sepB ha hb hn) h
  | before =>
    by_cases hbd : patLe .bounded a b
    · have : a = .universality ∧ b = .existence := by
        cases a <;> cases b <;> simp_all [patLe, Scope.cls] <;> omega
      obtain ⟨rfl, rfl⟩ := this
      exact not_implies_F sepF_universality_existence h
    · exact not_implies_B (.inl rfl) (sepB ha hb hbd) h

end Psp
